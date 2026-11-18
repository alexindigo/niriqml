#!/usr/bin/env python3
"""Compile the pinned schemars subset. Python stdlib only; never run at build time."""

import argparse
import copy
import difflib
import hashlib
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCHEMA = "schema/niri-ipc-v26.04.schema.json"
BINDINGS = "schema/qml-bindings.json"
EXTENSIONS = "schema/extensions.json"
NEWER_SCHEMA = "schema/niri-ipc-main-689c1e9.schema.json"
ANNOTATIONS = {"description", "title", "$schema", "$id", "x-niri-source", "default", "examples", "deprecated", "introducedAfter"}
KEYWORDS = {"$defs", "$ref", "type", "const", "enum", "oneOf", "anyOf", "properties", "required", "additionalProperties", "items", "prefixItems", "minItems", "maxItems", "format", "minimum", "maximum"}
FORMATS = {"uint8", "uint16", "uint32", "uint64", "uint", "int8", "int16", "int32", "int64", "int", "double", "float"}
CPP_KEYWORDS = set("alignas alignof and asm auto bitand bitor bool break case catch char class compl const constexpr const_cast continue co_await co_return co_yield decltype default delete do double dynamic_cast else enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept not nullptr operator or private protected public register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while xor".split())
ROOT_GROUPS = {"Request": 16, "Action": 141, "OutputAction": 9, "Response": 14, "Event": 19}


class SchemaError(ValueError):
    pass


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise SchemaError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def load(path):
    return json.loads(path.read_text(), object_pairs_hook=unique_object)


def camel(name):
    parts = name.split("_")
    return parts[0] + "".join(p[:1].upper() + p[1:] for p in parts[1:])


def identifier(name, path):
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) or name in CPP_KEYWORDS:
        raise SchemaError(f"{path}: invalid C++ identifier {name!r}; add a naming binding")
    return name


def merge_extensions(base, newer, manifest):
    """Only explicitly reviewed additive nodes enter the released contract.

    Shared wire constraints must match. The separately reviewed newer serde
    defaults may relax requiredness upstream, but never in our released input.
    """
    if set(manifest) != {"formatVersion", "baseline", "schema", "commit", "additions", "ignoredDefaults"} or manifest["formatVersion"] != 1:
        raise SchemaError("unsupported extension manifest")
    if manifest["baseline"] != base["x-niri-source"]["release"] or manifest["commit"] != newer["x-niri-source"]["commit"] or manifest["schema"] != NEWER_SCHEMA:
        raise SchemaError("extension provenance mismatch")
    selections = {}
    for entry in manifest["additions"]:
        if set(entry) != {"path", "introducedAfter", "decision"} or entry["introducedAfter"] != manifest["baseline"] or not entry["decision"]:
            raise SchemaError("invalid extension selection")
        if entry["path"] in selections:
            raise SchemaError("duplicate extension selection")
        selections[entry["path"]] = entry
    selected, defaults = set(), set()

    def addition(node, path):
        if path not in selections:
            raise SchemaError(f"{path}: unselected newer addition")
        selected.add(path)
        return {**copy.deepcopy(node), "introducedAfter": manifest["baseline"]}

    def tag(branch):
        if "const" in branch:
            return branch["const"]
        props = branch.get("properties", {})
        if len(props) != 1:
            raise SchemaError("extension comparison requires single-tag branches")
        return next(iter(props))

    def variants(branches, path):
        result = {tag(branch): branch for branch in branches}
        if len(result) != len(branches):
            raise SchemaError(f"{path}: colliding variant tags")
        return result

    def merge(old, new, path):
        if not isinstance(old, dict) or not isinstance(new, dict):
            if old != new:
                raise SchemaError(f"{path}: shared field type/constraint changed")
            return copy.deepcopy(old)
        result = copy.deepcopy(old)
        keys = (set(old) | set(new)) - ANNOTATIONS
        for key in sorted(keys):
            a, b = old.get(key), new.get(key)
            child = path + "/" + key
            if key in {"$defs", "properties"}:
                if not isinstance(a, dict) or not isinstance(b, dict) or set(a) - set(b):
                    raise SchemaError(f"{child}: shared fields removed")
                values = {}
                for name in sorted(set(a) | set(b)):
                    location = path if key == "properties" and path.endswith("/variants/" + name) else child + "/" + name
                    if name not in a:
                        values[name] = addition(b[name], location)
                        # New received fields must represent absence honestly.
                        value = b[name]
                        nullable = "null" in value.get("type", []) or {"type": "null"} in value.get("anyOf", [])
                        if key == "properties" and not nullable:
                            raise SchemaError(f"{location}: newer record field must be nullable")
                    else:
                        values[name] = merge(a[name], b[name], location)
                result[key] = values
            elif key == "oneOf":
                before, after = variants(a, path), variants(b, path)
                if set(before) - set(after):
                    raise SchemaError(f"{path}: shared variants removed")
                result[key] = [merge(before[t], after[t], path + "/variants/" + t) if t in before else addition(after[t], path + "/variants/" + t) for t in sorted(after)]
            elif key == "required":
                before, after = set(a or []), set(b or [])
                if (after - before) & set(old.get("properties", {})):
                    raise SchemaError(f"{child}: shared requiredness tightened")
                for name in before - after:
                    location = path + "/properties/" + name
                    if "default" not in new.get("properties", {}).get(name, {}):
                        raise SchemaError(f"{location}: shared requiredness changed without serde default")
                    defaults.add(location)
                # Baseline requiredness is deliberately retained, even for new
                # fields required by the newer source.
            elif a != b:
                raise SchemaError(f"{child}: shared field type/constraint changed")
        return result

    merged = merge(base, newer, "")
    if selected != set(selections):
        raise SchemaError(f"stale extension selections: {sorted(set(selections) - selected)}")
    if len(manifest["ignoredDefaults"]) != len(set(manifest["ignoredDefaults"])) or defaults != set(manifest["ignoredDefaults"]):
        raise SchemaError(f"unreviewed newer defaults: {sorted(defaults)}")
    return merged


class Compiler:
    def __init__(self, schema, bindings, extensions=None):
        self.schema = schema
        self.bindings = bindings
        self.extensions = extensions
        if set(bindings) - {"formatVersion", "types", "tuples", "actions", "requests", "events"} or bindings.get("formatVersion") != 1:
            raise SchemaError("unsupported binding manifest")
        self.defs = schema.get("$defs", {})
        self.ir = {}
        for name in sorted(self.defs):
            self.ir[name] = self.normalize(self.defs[name], f"#/$defs/{name}")
            if "introducedAfter" in self.defs[name]:
                self.ir[name]["introducedAfter"] = self.defs[name]["introducedAfter"]
        self.root = self.normalize(schema, "#")
        self.validate_bindings()
        self.tuples = {}
        tuple_cpp_names = {self.name(name) for name in self.ir} | {"NiriArgs", "NiriInvalidArgument"}
        tuple_qml_names = {bindings["types"].get(name, {}).get("qmlName", self.name(name)[:1].lower() + self.name(name)[1:]) for name in self.ir}
        for path, binding in bindings.get("tuples", {}).items():
            if set(binding) != {"cppName", "qmlName", "members"}:
                raise SchemaError(f"{path}: tuple binding contains more than names")
            parts = path.split("/")
            if len(parts) != 4 or parts[-1] != "items":
                raise SchemaError(f"{path}: invalid tuple binding path")
            try:
                variant = next(v for v in self.ir[parts[0]]["variants"] if v["tag"] == parts[1])
                array = next(f["type"] for f in variant["payload"]["fields"] if f["wire"] == parts[2])
                item = array["item"]
            except (KeyError, StopIteration, TypeError) as error:
                raise SchemaError(f"{path}: unresolved tuple binding") from error
            if array["kind"] != "array" or item["kind"] != "tuple" or len(item["items"]) != len(binding["members"]):
                raise SchemaError(f"{path}: tuple binding shape mismatch")
            names = [binding["cppName"], binding["qmlName"], *binding["members"]]
            for name in names:
                identifier(name, path)
            member_names = binding["members"] + ["qml" + name[:1].upper() + name[1:] for name in binding["members"]]
            if len(member_names) != len(set(member_names)) or binding["cppName"] in tuple_cpp_names or binding["qmlName"] in tuple_qml_names:
                raise SchemaError(f"{path}: tuple name collision")
            tuple_cpp_names.add(binding["cppName"])
            tuple_qml_names.add(binding["qmlName"])
            self.tuples[path] = {**binding, "type": item}
        for name, ir in self.ir.items():
            binding = self.bindings["types"].get(name, {})
            for field in ir.get("fields", []):
                field["name"] = binding.get("fields", {}).get(field["wire"], field["name"])

    def ref(self, pointer):
        if not pointer.startswith("#/"):
            raise SchemaError(f"external or invalid ref: {pointer}")
        node = self.schema
        try:
            for part in pointer[2:].split("/"):
                node = node[part.replace("~1", "/").replace("~0", "~")]
        except (KeyError, TypeError) as error:
            raise SchemaError(f"unresolved ref: {pointer}") from error
        if pointer.startswith("#/$defs/") and pointer.count("/") == 2:
            return pointer[len("#/$defs/"):].replace("~1", "/").replace("~0", "~")
        raise SchemaError(f"reference without named type identity: {pointer}")

    def normalize(self, node, path):
        if not isinstance(node, dict):
            raise SchemaError(f"{path}: expected schema object")
        unknown = set(node) - KEYWORDS - ANNOTATIONS
        if unknown:
            raise SchemaError(f"{path}: unsupported structural keywords {sorted(unknown)}")
        structural = set(node) - ANNOTATIONS - {"$defs"}
        if "$ref" in node:
            if structural != {"$ref"}:
                raise SchemaError(f"{path}: unsupported ref siblings")
            return {"kind": "ref", "name": self.ref(node["$ref"])}
        if isinstance(node.get("type"), list):
            types = node["type"]
            if len(types) != 2 or "null" not in types:
                raise SchemaError(f"{path}: unsupported type union")
            inner = dict(node, type=next(t for t in types if t != "null"))
            return {"kind": "optional", "item": self.normalize(inner, path + "/nonNull")}
        if "anyOf" in node:
            branches = node["anyOf"]
            nonnull = [b for b in branches if b != {"type": "null"}]
            if structural != {"anyOf"} or len(branches) != 2 or len(nonnull) != 1:
                raise SchemaError(f"{path}: ambiguous anyOf")
            return {"kind": "optional", "item": self.normalize(nonnull[0], path + "/nonNull")}
        if "oneOf" in node:
            if structural != {"oneOf"}:
                raise SchemaError(f"{path}: unsupported oneOf siblings")
            variants = []
            for i, branch in enumerate(node["oneOf"]):
                b = self.normalize(branch, f"{path}/oneOf/{i}")
                if b["kind"] == "literal":
                    variants.append({"tag": b["value"], "payload": None})
                elif b["kind"] == "enum":
                    variants.extend({"tag": tag, "payload": None} for tag in b["values"])
                elif b["kind"] == "record" and len(b["fields"]) == 1 and b["fields"][0]["required"]:
                    field = b["fields"][0]
                    variants.append({"tag": field["wire"], "payload": field["type"]})
                else:
                    raise SchemaError(f"{path}: not a single-key externally tagged union")
                if "introducedAfter" in branch:
                    variants[-1]["introducedAfter"] = branch["introducedAfter"]
            tags = [v["tag"] for v in variants]
            if len(tags) != len(set(tags)):
                raise SchemaError(f"{path}: colliding variant tags")
            return {"kind": "union", "variants": sorted(variants, key=lambda v: v["tag"])}
        if "const" in node or "enum" in node:
            if "const" in node and "enum" in node:
                raise SchemaError(f"{path}: ambiguous const/enum")
            values = [node["const"]] if "const" in node else node["enum"]
            if node.get("type") != "string" or not values or any(not isinstance(v, str) for v in values) or len(values) != len(set(values)):
                raise SchemaError(f"{path}: unsupported enum")
            if structural - {"const", "enum", "type"}:
                raise SchemaError(f"{path}: unsupported enum constraints")
            return {"kind": "literal", "value": values[0]} if "const" in node else {"kind": "enum", "values": sorted(values)}
        kind = node.get("type")
        allowed = {"type"}
        if kind == "object":
            allowed |= {"properties", "required", "additionalProperties"}
            props = node.get("properties", {})
            extra = node.get("additionalProperties", True)
            required = node.get("required", [])
            if len(required) != len(set(required)) or set(required) - set(props):
                raise SchemaError(f"{path}: invalid required fields")
            if isinstance(extra, dict):
                if props or required:
                    raise SchemaError(f"{path}: mixed map and record")
                result = {"kind": "map", "item": self.normalize(extra, path + "/additionalProperties")}
            elif isinstance(extra, bool):
                fields = [{"wire": key, "name": camel(key), "required": key in required,
                           "type": self.normalize(value, path + "/properties/" + key),
                           **({"introducedAfter": value["introducedAfter"]} if "introducedAfter" in value else {})} for key, value in sorted(props.items())]
                result = {"kind": "record", "fields": fields, "open": extra}
            else:
                raise SchemaError(f"{path}: invalid additionalProperties")
        elif kind == "array":
            allowed |= {"prefixItems", "items", "minItems", "maxItems"}
            minimum, maximum = node.get("minItems", 0), node.get("maxItems")
            if minimum < 0 or (maximum is not None and maximum < minimum):
                raise SchemaError(f"{path}: invalid array bounds")
            if "prefixItems" in node:
                items = [self.normalize(v, f"{path}/prefixItems/{i}") for i, v in enumerate(node["prefixItems"])]
                if "items" in node or minimum != len(items) or maximum != len(items):
                    raise SchemaError(f"{path}: tuple without exact length")
                result = {"kind": "tuple", "items": items}
            else:
                if minimum != 0 and minimum != maximum or maximum is not None and minimum != maximum:
                    raise SchemaError(f"{path}: unsupported variable array bounds")
                result = {"kind": "array", "item": self.normalize(node["items"], path + "/items"), "min": minimum, "max": maximum}
        elif kind in {"integer", "number"}:
            allowed |= {"format", "minimum", "maximum"}
            fmt = node.get("format")
            if fmt not in FORMATS or (kind == "number") != (fmt in {"double", "float"}):
                raise SchemaError(f"{path}: unsupported numeric format {fmt}")
            result = {"kind": kind, "format": fmt}
            # Bounds not represented by the native Rust width need new emission
            # support rather than being silently accepted and then ignored.
            bits = int(re.search(r"\d+$", fmt).group()) if re.search(r"\d+$", fmt) else 64
            lower = 0 if fmt.startswith("uint") else -(1 << (bits - 1))
            upper = (1 << bits) - 1 if fmt.startswith("uint") else (1 << (bits - 1)) - 1
            for key in ("minimum", "maximum"):
                if key in node:
                    if kind == "number" or node[key] != (lower if key == "minimum" else upper):
                        raise SchemaError(f"{path}: unsupported numeric bound {key}={node[key]}")
                    result[key] = node[key]
        elif kind in {"string", "boolean", "null"}:
            result = {"kind": kind}
        else:
            raise SchemaError(f"{path}: unsupported shape")
        if structural - allowed:
            raise SchemaError(f"{path}: unsupported constraints {sorted(structural - allowed)}")
        return result

    def validate_bindings(self):
        events = self.bindings.get("events", {})
        if set(events) != {v["tag"] for v in self.ir["Event"]["variants"]}:
            raise SchemaError("event bindings must cover exactly the event inventory")
        names = set()
        for variant in self.ir["Event"]["variants"]:
            tag = variant["tag"]
            binding = events[tag]
            if set(binding) - {"name", "arguments", "aliases"}:
                raise SchemaError(f"{tag}: event binding contains more than names/order")
            fields = variant["payload"]["fields"]
            keys = {f["wire"] for f in fields}
            order = binding.get("arguments", [f["wire"] for f in fields])
            if len(order) != len(keys) or set(order) != keys or set(binding.get("aliases", {})) - keys:
                raise SchemaError(f"{tag}: event arguments are not a field permutation")
            name = identifier(binding.get("name", tag[:1].lower() + tag[1:]), tag)
            aliases = [identifier(binding.get("aliases", {}).get(f["wire"], f["name"]), tag) for f in fields]
            if name in names or len(aliases) != len(set(aliases)):
                raise SchemaError(f"{tag}: colliding event name")
            names.add(name)
        requests = self.bindings.get("requests", {})
        request_tags = {v["tag"] for v in self.ir["Request"]["variants"]}
        response_tags = {v["tag"] for v in self.ir["Response"]["variants"]} | {"Err"}
        if set(requests) != request_tags:
            raise SchemaError("request bindings must cover exactly the request inventory")
        request_methods = set()
        for variant in self.ir["Request"]["variants"]:
            tag, payload = variant["tag"], variant["payload"]
            binding = requests[tag]
            if set(binding) - {"name", "arguments", "argument", "response", "source"} or binding.get("response") not in response_tags or not re.fullmatch(r"v26\.04:src/ipc/server\.rs:[0-9,\-]+", binding.get("source", "")):
                raise SchemaError(f"{tag}: invalid request/response name or source binding")
            name = identifier(binding.get("name", tag[:1].lower() + tag[1:]), tag)
            if name in request_methods:
                raise SchemaError(f"{tag}: colliding request method name")
            request_methods.add(name)
            if payload and payload["kind"] == "record":
                fields = {f["wire"] for f in payload["fields"]}
                order = binding.get("arguments", sorted(fields))
                if len(order) != len(fields) or set(order) != fields or "argument" in binding:
                    raise SchemaError(f"{tag}: request argument binding is not a field permutation")
            elif "arguments" in binding or (not payload and "argument" in binding):
                raise SchemaError(f"{tag}: request binding names nonexistent arguments")
            elif payload:
                identifier(binding.get("argument", "value"), tag)
        variants = {v["tag"]: v for v in self.ir["Action"]["variants"]}
        methods = set()
        for tag, variant in variants.items():
            binding = self.bindings.get("actions", {}).get(tag, {})
            if set(binding) - {"name", "arguments", "aliases"}:
                raise SchemaError(f"{tag}: action binding contains more than names/order")
            fields = variant["payload"]["fields"]
            keys = {f["wire"] for f in fields}
            args = binding.get("arguments", [f["wire"] for f in fields])
            if len(args) != len(keys) or set(args) != keys or set(binding.get("aliases", {})) - keys:
                raise SchemaError(f"{tag}: action argument binding is not a field permutation")
            method = binding.get("name", tag[:1].lower() + tag[1:])
            identifier(method, tag)
            aliases = [binding.get("aliases", {}).get(f["wire"], f["name"]) for f in fields]
            for alias in aliases:
                identifier(alias, tag)
            if method in methods or len(aliases) != len(set(aliases)):
                raise SchemaError(f"{tag}: colliding action name")
            methods.add(method)
        if set(self.bindings.get("actions", {})) - set(variants):
            raise SchemaError("action binding names unknown variant")
        for name, binding in self.bindings["types"].items():
            if name not in self.ir or set(binding) - {"cppName", "qmlName", "variants", "fields"}:
                raise SchemaError(f"invalid type binding: {name}")
            ir = self.ir[name]
            tags = [v["tag"] for v in ir.get("variants", [])] + ir.get("values", [])
            if set(binding.get("variants", {})) - set(tags):
                raise SchemaError(f"{name}: binding names unknown variants")
            if set(binding.get("fields", {})) - {f["wire"] for f in ir.get("fields", [])}:
                raise SchemaError(f"{name}: binding names unknown fields")
        cpp_names, qml_names = {"NiriArgs", "NiriInvalidArgument"}, {"NiriArgs", "niriInvalidArgument"}
        for name, ir in self.ir.items():
            cpp = self.name(name)
            qml = self.bindings["types"].get(name, {}).get("qmlName", cpp[:1].lower() + cpp[1:])
            identifier(cpp, name)
            identifier(qml, name)
            if cpp in cpp_names or qml in qml_names:
                raise SchemaError(f"{name}: colliding public type names")
            cpp_names.add(cpp)
            qml_names.add(qml)
            if ir["kind"] in {"union", "enum"}:
                if cpp in qml_names:
                    raise SchemaError(f"{name}: colliding enum namespace")
                qml_names.add(cpp)
            used = {"extensions", "qmlExtensions"} if ir["kind"] == "record" else {"Unknown", "Invalid", "kind", "known", "valid", "tag", "wireName", "raw", "error"}
            entries = [(f["wire"], f["name"], "fields") for f in ir.get("fields", [])]
            entries += [(v["tag"], v["tag"], "variants") for v in ir.get("variants", [])]
            entries += [(v, v, "variants") for v in ir.get("values", [])]
            for wire, default, group in entries:
                alias = self.bindings["types"].get(name, {}).get(group, {}).get(wire, default)
                identifier(alias, name + "/" + wire)
                if alias in used:
                    raise SchemaError(f"{name}: colliding member {alias}")
                used.add(alias)

    def name(self, name):
        return self.bindings["types"].get(name, {}).get("cppName", "Niri" + name)

    def inventory(self):
        types = {}
        for name, ir in self.ir.items():
            types[name] = {"cppName": self.name(name), **ir}
        roots = {f["wire"]: f["type"]["name"] for f in self.root["fields"]}
        return {"formatVersion": 1, "source": self.schema["x-niri-source"], "extensions": self.extensions, "roots": roots, "types": types, "tupleTypes": self.tuples}


class SourceBuckets:
    """Pack complete semantic definitions, never slices of C++ source text."""
    def __init__(self, emitter, family, prefix, suffix=(), limit=12000):
        self.emitter, self.family = emitter, family
        self.prefix, self.suffix, self.limit = prefix, list(suffix), limit
        self.buckets = []

    def add(self, lines):
        if not lines:
            return
        size = sum(len(line) + 1 for line in lines)
        if not self.buckets or self.buckets[-1][0] + size > self.limit:
            self.buckets.append([0, []])
        self.buckets[-1][0] += size
        self.buckets[-1][1].extend(lines)

    def take(self, lines, start):
        self.add(lines[start:])
        del lines[start:]

    def outputs(self):
        return {f"generated/{self.family}_{i:03d}_generated.cpp":
                "\n".join([self.emitter.header(), "// clang-format off", *self.prefix, *lines,
                            *self.suffix, "// clang-format on", ""])
                for i, (_, lines) in enumerate(self.buckets, 1)}


class CppEmitter:
    """Supporting value types. Callable wrappers are emitted separately."""

    def __init__(self, compiler, hashes):
        self.c = compiler
        self.types = {n: ir for n, ir in compiler.ir.items() if (n not in ROOT_GROUPS or n in {"Action", "OutputAction", "Event"}) and n != "Result_of_Response_or_string"}
        self.hashes = hashes
        self.ordered = []
        visiting = set()
        def visit(name):
            if name in self.ordered:
                return
            if name in visiting:
                raise SchemaError(f"recursive value type: {name}")
            visiting.add(name)
            def refs(ir):
                if isinstance(ir, dict):
                    if ir.get("kind") == "ref":
                        visit(ir["name"])
                    else:
                        for v in ir.values():
                            refs(v)
                elif isinstance(ir, list):
                    for v in ir:
                        refs(v)
            refs(self.types[name])
            visiting.remove(name)
            self.ordered.append(name)
        for name in sorted(self.types):
            visit(name)
        emitted = {compiler.name(name) for name in compiler.ir} | {b["cppName"] for b in compiler.tuples.values()} | {"NiriArgs", "NiriUnknownVariant"}
        for name, ir in self.types.items():
            candidates = []
            if ir["kind"] != "record":
                candidates.append(compiler.name(name) + "EnumRegistration")
            for variant in ir.get("variants", []):
                if variant["payload"] and variant["payload"]["kind"] == "record":
                    candidates.append(self.variant_record_name(name, variant["tag"]))
            for candidate in candidates:
                if candidate in emitted:
                    raise SchemaError(f"colliding generated type: {candidate}")
                emitted.add(candidate)

    def header(self, comment="//"):
        return "\n".join([f"{comment} Generated by tools/generate-ipc.py; do not edit."] + [f"{comment} {key} sha256: {value}" for key, value in sorted(self.hashes.items())]) + "\n"

    def cpp(self, ir):
        kind = ir["kind"]
        if kind == "ref":
            return self.c.name(ir["name"])
        if kind == "optional":
            return f"std::optional<{self.cpp(ir['item'])}>"
        if kind == "integer":
            fmt = ir["format"]
            return {"uint": "quint64", "int": "qint64"}.get(fmt, ("quint" + fmt[4:]) if fmt.startswith("uint") else ("qint" + fmt[3:]))
        if kind == "number":
            return "double" if ir["format"] == "double" else "float"
        if kind == "boolean":
            return "bool"
        if kind == "string":
            return "QString"
        if kind == "array":
            if ir["max"] is not None and ir["min"] == ir["max"]:
                return f"std::array<{self.cpp(ir['item'])}, {ir['max']}>"
            return f"QList<{self.cpp(ir['item'])}>"
        if kind == "tuple":
            items = [self.cpp(v) for v in ir["items"]]
            if len(set(items)) == 1:
                return f"std::array<{items[0]}, {len(items)}>"
            return "std::tuple<" + ", ".join(items) + ">"
        if kind == "map":
            return f"QMap<QString, {self.cpp(ir['item'])}>"
        raise SchemaError(f"unnamed {kind} in C++ value type")

    def property_type(self, ir):
        if ir["kind"] == "optional":
            return "QVariant"
        if ir["kind"] == "integer" and ir["format"] in {"uint", "uint64", "int", "int64"}:
            return "QString"
        if ir["kind"] in {"tuple", "array", "map"}:
            return "QVariant"
        return self.cpp(ir)

    def alias(self, name, tag):
        return self.c.bindings["types"].get(name, {}).get("variants", {}).get(tag, tag)

    def variant_record_name(self, name, tag):
        return self.c.name(name) + self.alias(name, tag) + "Payload"

    def record(self, cpp, qml, ir):
        used = {"extensions", "qmlExtensions"}
        for field in ir["fields"]:
            name = identifier(field["name"], cpp)
            getter = "qml" + name[:1].upper() + name[1:]
            if name in used or getter in used:
                raise SchemaError(f"{cpp}: colliding generated member {name}")
            used.update((name, getter))
        lines = [f"struct {cpp}", "{", "    Q_GADGET", f"    QML_VALUE_TYPE({qml})"]
        for f in ir["fields"]:
            field, typ = f["name"], self.property_type(f["type"])
            lines.append(f"    Q_PROPERTY({typ} {field} READ qml{field[:1].upper() + field[1:]} CONSTANT)")
        lines += ["    Q_PROPERTY(QVariant extensions READ qmlExtensions CONSTANT)", "", "public:"]
        for f in ir["fields"]:
            field, typ = f["name"], self.cpp(f["type"])
            lines.append(f"    {typ} {field}{{}};")
        lines += ["    NiriWire::WireValue::Object extensions;", ""]
        for f in ir["fields"]:
            field, typ = f["name"], self.property_type(f["type"])
            method = "qml" + field[:1].upper() + field[1:]
            if typ == "QVariant":
                value = f"NiriIpc::qmlValue({field})"
            elif typ == "QString" and f["type"]["kind"] == "integer":
                value = f"QString::number({field})"
            else:
                value = field
            lines.append(f"    {typ} {method}() const {{ return {value}; }}")
        lines += ["    QVariant qmlExtensions() const", "    {", "        const auto object = NiriWire::WireValue::object(extensions);", "        return object ? NiriWire::toQmlProjection(*object) : QVariant::fromValue(nullptr);", "    }", "", f"    bool operator==(const {cpp} &other) const", "    {"]
        equality = [f"{f['name']} == other.{f['name']}" for f in ir["fields"]] + ["extensions == other.extensions"]
        lines += ["        return " + "\n                && ".join(equality) + ";", "    }", f"    bool operator!=(const {cpp} &other) const {{ return !(*this == other); }}", "};", ""]
        return lines

    def emit(self):
        h = [self.header(), "#pragma once", "", '#include "niriwire.h"', "#include <QMap>", "#include <QVariant>", "#include <array>", "#include <optional>", "#include <tuple>", "#include <variant>", "", "namespace NiriIpc {", "template <typename T> QVariant qmlValue(const T &value) { return QVariant::fromValue(value); }", "inline QVariant qmlValue(quint64 value) { return QString::number(value); }", "inline QVariant qmlValue(qint64 value) { return QString::number(value); }", "template <typename T> QVariant qmlValue(const std::optional<T> &value)", "{", "    return value ? qmlValue(*value) : QVariant::fromValue(nullptr);", "}", "template <typename T, std::size_t N> QVariant qmlValue(const std::array<T, N> &value)", "{", "    QVariantList result;", "    for (const auto &item : value)", "        result.append(qmlValue(item));", "    return result;", "}", "template <typename T> QVariant qmlValue(const QList<T> &value)", "{", "    QVariantList result;", "    for (const auto &item : value)", "        result.append(qmlValue(item));", "    return result;", "}", "template <typename T> QVariant qmlValue(const QMap<QString, T> &value)", "{", "    QVariantMap result;", "    for (auto it = value.begin(); it != value.end(); ++it)", "        result.insert(it.key(), qmlValue(it.value()));", "    return result;", "}", "} // namespace NiriIpc", "", "struct NiriUnknownVariant", "{", "    QString tag;", "    NiriWire::WireValue raw;", "    bool operator==(const NiriUnknownVariant &other) const", "    {", "        return tag == other.tag && raw == other.raw;", "    }", "};", ""]
        h.insert(h.index('#include <QMap>'), '#include <QObject>')
        pos = h.index("template <typename T> QVariant qmlValue(const std::optional<T> &value)")
        h[pos:pos] = [
            "template <typename T, std::size_t N> QVariant qmlValue(const std::array<T, N> &value);",
            "template <typename T> QVariant qmlValue(const QList<T> &value);",
            "template <typename T> QVariant qmlValue(const QMap<QString, T> &value);",
            "template <typename... T> QVariant qmlValue(const std::tuple<T...> &value);",
        ]
        pos = h.index("} // namespace NiriIpc")
        h[pos:pos] = [
            "template <typename... T> QVariant qmlValue(const std::tuple<T...> &value)",
            "{", "    QVariantList result;",
            "    std::apply([&](const auto &...item) { (result.append(qmlValue(item)), ...); }, value);",
            "    return result;", "}",
        ]
        cpp = [self.header(), '#include "niriipctypes_generated.h"', '#include "niriipccodec_generated.h"', ""]
        codec_h = [self.header(), "#pragma once", "", '#include "niriipctypes_generated.h"', "#include <cmath>", "#include <limits>", "", "namespace NiriIpc {", "struct DecodeError", "{", "    QString path;", "    QString message;", "};", "enum class DecodeMode { Received, Outbound };", "bool fail(DecodeError &error, const QString &path, const QString &message);", "QString childPath(const QString &path, const QString &key);", ""]
        codec_cpp = [self.header(), '#include "niriipccodec_generated.h"', '#include <QJSValueIterator>', "", "namespace NiriIpc {", "using NiriWire::WireValue;", "using NiriWire::NumberToken;", ""]
        self.special_outputs = {}
        self.type_chunks = SourceBuckets(self, "niriipctypes", cpp[1:])
        self.codec_chunks = SourceBuckets(self, "niriipccodec", codec_cpp[1:], ["} // namespace NiriIpc"])
        for name in self.ordered:
            type_start = len(cpp)
            ir, typ = self.types[name], self.c.name(name)
            qml = self.c.bindings["types"].get(name, {}).get("qmlName", typ[:1].lower() + typ[1:])
            if ir["kind"] == "record":
                h += self.record(typ, qml, ir)
            else:
                variants = ir.get("variants", [{"tag": v, "payload": None} for v in ir.get("values", [])])
                units = all(v["payload"] is None for v in variants)
                for v in variants:
                    if v["payload"] and v["payload"]["kind"] == "record":
                        payload_name = self.variant_record_name(name, v["tag"])
                        h += self.record(payload_name, payload_name[:1].lower() + payload_name[1:], v["payload"])
                        self.codec_record(payload_name, v["payload"], codec_h, codec_cpp)
                h += [f"struct {typ}", "{", "    Q_GADGET", f"    QML_VALUE_TYPE({qml})", "    Q_PROPERTY(Kind kind READ kind CONSTANT)", "    Q_PROPERTY(bool known READ known CONSTANT)", "    Q_PROPERTY(bool valid READ valid CONSTANT)", "    Q_PROPERTY(QString wireName READ wireName CONSTANT)", "    Q_PROPERTY(QString tag READ wireName CONSTANT)", "    Q_PROPERTY(QVariant raw READ qmlRaw CONSTANT)", "    Q_PROPERTY(QString error READ error CONSTANT)", "", "public:", "    enum Kind { Invalid, Unknown, " + ", ".join(self.alias(name, v["tag"]) for v in variants) + " };", "    Q_ENUM(Kind)"]
                if units:
                    h += [f"    {typ}() = default;", f"    explicit {typ}(Kind kind);", f"    static {typ} fromWireName(const QString &name);", "    Kind kind() const;", "    bool known() const { return kind() != Unknown; }", "    bool valid() const { return true; }", "    QString wireName() const { return m_wireName; }", "    QVariant qmlRaw() const { return m_wireName; }", "    QString error() const { return {}; }", f"    bool operator==(const {typ} &other) const {{ return m_wireName == other.m_wireName; }}", f"    bool operator!=(const {typ} &other) const {{ return !(*this == other); }}", "", "private:", "    QString m_wireName;", "};", ""]
                    cpp += [f"{typ}::{typ}(Kind kind)", "{", "    switch (kind) {"]
                    for v in variants:
                        cpp += [f"    case {self.alias(name, v['tag'])}: m_wireName = QStringLiteral({json.dumps(v['tag'])}); break;"]
                    cpp += ["    default: break;", "    }", "}", f"{typ} {typ}::fromWireName(const QString &name)", "{", f"    {typ} result;", "    result.m_wireName = name;", "    return result;", "}", f"{typ}::Kind {typ}::kind() const", "{"]
                    for v in variants:
                        cpp += [f"    if (m_wireName == QStringLiteral({json.dumps(v['tag'])})) return {self.alias(name, v['tag'])};"]
                    cpp += ["    return Unknown;", "}", ""]
                    self.codec_unit(typ, codec_h, codec_cpp)
                else:
                    for v in variants:
                        alias = self.alias(name, v["tag"])
                        ptype = self.payload_type(name, v)
                        h += [f"    struct {alias}Value", "    {"]
                        if ptype:
                            h += [f"        {ptype} payload{{}};"]
                        h += [f"        bool operator==(const {alias}Value &other) const", "        {", "            return payload == other.payload;" if ptype else "            Q_UNUSED(other); return true;", "        }", "    };"]
                    h += ["    using Value = std::variant<std::monostate, " + ", ".join(self.alias(name, v["tag"]) + "Value" for v in variants) + ", NiriUnknownVariant>;", "    Value value;", "    QString invalidReason;", "    Kind kind() const;", "    bool known() const { return kind() != Unknown && kind() != Invalid; }", "    bool valid() const { return kind() != Invalid; }", "    QString wireName() const;", "    QVariant qmlRaw() const;", "    QString error() const { return invalidReason; }"]
                    for v in variants:
                        alias = self.alias(name, v["tag"])
                        ptype = self.payload_type(name, v)
                        if ptype:
                            prop = alias[:1].lower() + alias[1:]
                            h.append(f"    Q_PROPERTY(QVariant {prop} READ qml{alias} CONSTANT)")
                            h += [f"    QVariant qml{alias}() const;"]
                            self.type_chunks.add([f"QVariant {typ}::qml{alias}() const", "{", f"    const auto *item = std::get_if<{alias}Value>(&value);", "    return item ? NiriIpc::qmlValue(item->payload) : QVariant::fromValue(nullptr);", "}"])
                        parameter = ('const ' + ptype + ' &payload') if ptype else ''
                        h += [f"    static {typ} from{alias}({parameter});"]
                        self.type_chunks.add([f"{typ} {typ}::from{alias}({parameter})", "{", f"    {typ} result;", f"    result.value.emplace<{alias}Value>({alias}Value{{{'payload' if ptype else ''}}});", "    return result;", "}"])
                    h += [f"    {typ}();", f"    ~{typ}();", f"    {typ}(const {typ} &);", f"    {typ}({typ} &&) noexcept;", f"    {typ} &operator=(const {typ} &);", f"    {typ} &operator=({typ} &&) noexcept;", f"    bool operator==(const {typ} &other) const;", f"    bool operator!=(const {typ} &other) const {{ return !(*this == other); }}", "};", ""]
                    members = [f"{typ}::{typ}() = default;", f"{typ}::~{typ}() = default;", f"{typ}::{typ}(const {typ} &) = default;", f"{typ}::{typ}({typ} &&) noexcept = default;", f"{typ} &{typ}::operator=(const {typ} &) = default;", f"{typ} &{typ}::operator=({typ} &&) noexcept = default;", f"bool {typ}::operator==(const {typ} &other) const {{ return value == other.value && invalidReason == other.invalidReason; }}"]
                    if len(variants) > 32:
                        # Reuse the per-tag factories instead of instantiating variant's
                        # converting-copy machinery for every alternative in one TU.
                        copy = [f"{typ}::{typ}(const {typ} &other) : {typ}()", "{", "    switch (other.kind()) {"]
                        for v in variants:
                            alias, ptype = self.alias(name, v["tag"]), self.payload_type(name, v)
                            argument = f"std::get<{alias}Value>(other.value).payload" if ptype else ""
                            copy += [f"    case {alias}: *this = from{alias}({argument}); break;"]
                        copy += ["    case Unknown: value.emplace<NiriUnknownVariant>(std::get<NiriUnknownVariant>(other.value)); break;", "    default: break;", "    }", "    invalidReason = other.invalidReason;", "}"]
                        members[2] = "\n".join(copy)
                        members[3] = f"{typ}::{typ}({typ} &&other) noexcept : {typ}() {{ *this = std::move(other); }}"
                        members[4] = f"{typ} &{typ}::operator=(const {typ} &other) {{ if (this != &other) {{ {typ} copy(other); *this = std::move(copy); }} return *this; }}"
                    for i, member in enumerate(members):
                        if len(variants) > 32:
                            # Variant-wide dispatch tables are separate from per-tag factories.
                            key = f"generated/niriipctypes_{name.lower()}_special_{i}_generated.cpp"
                            self.special_outputs[key] = "\n".join([self.header(), '#include "niriipctypes_generated.h"', "// clang-format off", member, "// clang-format on", ""])
                        else:
                            self.type_chunks.add([member])
                    cpp += [f"{typ}::Kind {typ}::kind() const", "{", "    switch (value.index()) {", "    case 0: return Invalid;"]
                    for i, v in enumerate(variants, 1):
                        cpp += [f"    case {i}: return {self.alias(name, v['tag'])};"]
                    cpp += ["    default: return Unknown;", "    }", "}", f"QString {typ}::wireName() const", "{", "    switch (kind()) {"]
                    for v in variants:
                        cpp += [f"    case {self.alias(name, v['tag'])}: return QStringLiteral({json.dumps(v['tag'])});"]
                    cpp += ["    case Unknown: return std::get<NiriUnknownVariant>(value).tag;", "    default: return {};", "    }", "}", f"QVariant {typ}::qmlRaw() const", "{", "    const auto wire = NiriIpc::encode(*this);", "    return wire ? NiriWire::toQmlProjection(*wire) : QVariant::fromValue(nullptr);", "}", ""]
                    self.codec_union(name, typ, variants, codec_h, codec_cpp)
            if ir["kind"] == "record":
                self.codec_record(typ, ir, codec_h, codec_cpp)
            else:
                variants = ir.get("variants", [{"tag": v, "payload": None} for v in ir.get("values", [])])
                kinds = ["Invalid", "Unknown"] + [self.alias(name, v["tag"]) for v in variants]
                h += [f"class {typ}EnumRegistration : public QObject", "{", "    Q_OBJECT", f"    QML_NAMED_ELEMENT({typ})", '    QML_UNCREATABLE("Enum namespace")', "public:", "    enum Kind { " + ", ".join(f"{kind} = {typ}::{kind}" for kind in kinds) + " };", "    Q_ENUM(Kind)", "};", ""]
            self.type_chunks.take(cpp, type_start)
        for binding in self.c.tuples.values():
            codec_start = len(codec_cpp)
            typ = binding["cppName"]
            fields = list(zip(binding["members"], binding["type"]["items"]))
            h += [f"struct {typ}", "{", "    Q_GADGET", f"    QML_VALUE_TYPE({binding['qmlName']})", "public:"]
            for name, ir in fields:
                method = "qml" + name[:1].upper() + name[1:]
                h += [f"    Q_PROPERTY({self.property_type(ir)} {name} READ {method} CONSTANT)", f"    {self.cpp(ir)} {name}{{}};"]
                value = f"QString::number({name})" if self.property_type(ir) == "QString" else name
                h += [f"    {self.property_type(ir)} {method}() const {{ return {value}; }}"]
            h += [f"    bool operator==(const {typ} &other) const", "    {", "        return " + " && ".join(f"{name} == other.{name}" for name, _ in fields) + ";", "    }", f"    bool operator!=(const {typ} &other) const {{ return !(*this == other); }}", "};", f"Q_DECLARE_METATYPE({typ})", ""]
            self.declarations(typ, codec_h)
            tuple_type = "std::tuple<" + ", ".join(self.cpp(ir) for _, ir in fields) + ">"
            codec_cpp += [f"bool read(const WireValue &wire, {typ} &value, DecodeError &error, const QString &path, DecodeMode mode)", "{", f"    {tuple_type} tuple;", "    if (!read(wire, tuple, error, path, mode)) return false;"]
            codec_cpp += [f"    value.{name} = std::get<{i}>(tuple);" for i, (name, _) in enumerate(fields)]
            codec_cpp += ["    return true;", "}", f"bool write(const {typ} &value, WireValue &wire, DecodeError &error, const QString &path)", "{", "    return write(std::tie(" + ", ".join(f"value.{name}" for name, _ in fields) + "), wire, error, path);", "}", ""]
            self.codec_chunks.take(codec_cpp, codec_start)
        h += ["struct NiriInvalidArgument", "{", "    Q_GADGET", "    QML_VALUE_TYPE(niriInvalidArgument)", "    Q_PROPERTY(bool valid READ valid CONSTANT)", "    Q_PROPERTY(QString error READ error CONSTANT)", "public:", "    QString path;", "    QString message;", "    bool valid() const { return false; }", '    QString error() const { return path + QStringLiteral(": ") + message; }', "};", "Q_DECLARE_METATYPE(NiriInvalidArgument)", ""]
        for name in self.ordered:
            h += [f"Q_DECLARE_METATYPE({self.c.name(name)})"]
        h += [""]
        h += ['#include <QJSValue>', "", "class NiriArgs : public QObject", "{", "    Q_OBJECT", "    QML_NAMED_ELEMENT(NiriArgs)", "    QML_SINGLETON", "public:", "    using QObject::QObject;"]
        cpp += ["namespace {", "template <typename T, typename = void> struct HasInvalidReason : std::false_type {};", "template <typename T> struct HasInvalidReason<T, std::void_t<decltype(std::declval<T>().invalidReason)>> : std::true_type {};", "template <typename T>", "QVariant invalidArgument(const NiriIpc::DecodeError &error)", "{", "    if constexpr (HasInvalidReason<T>::value) {", "        T result;", '        result.invalidReason = error.path + QStringLiteral(": ") + error.message;', "        return QVariant::fromValue(result);", "    } else {", "        NiriInvalidArgument result;", "        result.path = error.path;", "        result.message = error.message;", "        return QVariant::fromValue(result);", "    }", "}", "template <typename T>", "QVariant constructArgument(const NiriWire::WireValue &wire)", "{", "    T typed{};", "    NiriIpc::DecodeError error;", "    if (!NiriIpc::decode(wire, typed, &error, QStringLiteral(\"$\"), NiriIpc::DecodeMode::Outbound))", "        return invalidArgument<T>(error);", "    return QVariant::fromValue(typed);", "}", "} // namespace", ""]
        argument_chunks = SourceBuckets(self, "niriipcarguments", cpp[1:])
        for name in self.ordered:
            ir, typ = self.types[name], self.c.name(name)
            if ir["kind"] == "record":
                methods = [(name[:1].lower() + name[1:], None, False)]
            else:
                variants = ir.get("variants", [{"tag": v, "payload": None} for v in ir.get("values", [])])
                methods = [(name[:1].lower() + name[1:] + self.alias(name, v["tag"]), v["tag"], v["payload"] is None) for v in variants]
            for method, tag, unit in methods:
                argument_start = len(cpp)
                h += [f"    Q_INVOKABLE QVariant {method}({'' if unit else 'const QJSValue &input'});"]
                cpp += [f"QVariant NiriArgs::{method}({'' if unit else 'const QJSValue &input'})", "{"]
                if unit:
                    cpp += [f"    return constructArgument<{typ}>(NiriWire::WireValue(QStringLiteral({json.dumps(tag)})));"]
                else:
                    cpp += ["    NiriWire::WireValue payload;", "    NiriIpc::DecodeError error;", '    if (!NiriIpc::qmlInput(input, payload, error, QStringLiteral("$")))', f"        return invalidArgument<{typ}>(error);"]
                    wire = f"*NiriWire::WireValue::object({{{{QStringLiteral({json.dumps(tag)}), payload}}}})" if tag else "payload"
                    cpp += [f"    return constructArgument<{typ}>({wire});"]
                cpp += ["}", ""]
                argument_chunks.take(cpp, argument_start)
        h += ["};", ""]
        # Qt's registrar emits one template call per value type in a single TU.
        # Explicit instantiations keep that TU a dispatcher, not another monolith.
        registration_types = [self.c.name(name) for name in self.ordered]
        registration_types += [self.c.name(name) + "EnumRegistration" for name in self.ordered if self.types[name]["kind"] != "record"]
        registration_types += [self.variant_record_name(name, v["tag"]) for name in self.ordered for v in self.types[name].get("variants", []) if v["payload"] and v["payload"]["kind"] == "record"]
        registration_types += [b["cppName"] for b in self.c.tuples.values()] + ["NiriInvalidArgument", "NiriArgs"]
        h += ['#include <QtQml/qqml.h>']
        registration_chunks = SourceBuckets(self, "niriipcregistration", ['#include "niriipctypes_generated.h"'], limit=1000)
        for typ in sorted(registration_types):
            signature = f"void qmlRegisterTypesAndRevisions<{typ}>(const char *, int, QList<int> *)"
            h += ["extern template " + signature + ";"]
            registration_chunks.add(["template " + signature + ";"])
        codec_h += ["bool decodeNamed(const QString &name, const NiriWire::WireValue &wire, QVariant &value, DecodeError &error, DecodeMode mode = DecodeMode::Received);", "std::optional<NiriWire::WireValue> encodeVariant(const QVariant &value, DecodeError *error = nullptr);", "bool qmlNumber(double input, NiriWire::WireValue &wire, DecodeError &error, const QString &path);", "bool qmlInput(const QJSValue &input, NiriWire::WireValue &wire, DecodeError &error, const QString &path, int depth = 0);", ""]
        codec_h += [self.runtime_header(), "} // namespace NiriIpc", ""]
        dispatch_h = [self.header(), "#pragma once", '#include "niriipccodec_generated.h"', "// clang-format off", "namespace NiriIpc {"]
        named_types = [(name, self.c.name(name)) for name in self.ordered] + [(b["cppName"], b["cppName"]) for b in self.c.tuples.values()]
        for name, typ in named_types:
            dispatch_h += [f"bool decode{typ}(const NiriWire::WireValue &, QVariant &, DecodeError &, DecodeMode);", f"std::optional<NiriWire::WireValue> encode{typ}(const QVariant &, DecodeError *);"]
            self.codec_chunks.add([f"bool decode{typ}(const WireValue &wire, QVariant &value, DecodeError &error, DecodeMode mode)", "{", f"    {typ} typed;", '    if (!decode(wire, typed, &error, QStringLiteral("$"), mode)) return false;', "    value = QVariant::fromValue(typed);", "    return true;", "}", f"std::optional<WireValue> encode{typ}(const QVariant &value, DecodeError *error)", "{", f"    if (value.metaType() != QMetaType::fromType<{typ}>()) return std::nullopt;", f"    return encode(value.value<{typ}>(), error);", "}"])
        dispatch_h += ["} // namespace NiriIpc", "// clang-format on", ""]
        codec_cpp.insert(2, '#include "niriipcdispatch_generated.h"')
        codec_cpp += [self.runtime_cpp(), "bool decodeNamed(const QString &name, const WireValue &wire, QVariant &value, DecodeError &error, DecodeMode mode)", "{"]
        for name, typ in named_types:
            codec_cpp += [f"    if (name == QStringLiteral({json.dumps(name)})) return decode{typ}(wire, value, error, mode);"]
        codec_cpp += ['    return fail(error, QStringLiteral("$"), QStringLiteral("unknown type name"));', "}", "std::optional<WireValue> encodeVariant(const QVariant &value, DecodeError *error)", "{"]
        codec_cpp += ["    if (value.metaType() == QMetaType::fromType<NiriInvalidArgument>()) {", "        const auto invalid = value.value<NiriInvalidArgument>();", "        if (error) *error = {invalid.path, invalid.message};", "        return std::nullopt;", "    }"]
        for name, typ in named_types:
            codec_cpp += [f"    if (const auto wire = encode{typ}(value, error)) return wire;"]
        codec_cpp += ["    return std::nullopt;", "}", "} // namespace NiriIpc", ""]
        def expanded_region(lines):
            first = next(i for i, line in enumerate(lines) if line and not line.startswith(("//", "#")))
            lines[first:first] = ["// Schema-expanded declarations/dispatch keep deterministic layout without a formatter dependency.", "// clang-format off"]
            return "\n".join(lines).rstrip() + "\n// clang-format on\n"
        result = {"generated/niriipctypes_generated.h": expanded_region(h), "generated/niriipccodec_generated.h": expanded_region(codec_h), "generated/niriipccodec_generated.cpp": expanded_region(codec_cpp), "generated/niriipcdispatch_generated.h": "\n".join(dispatch_h)}
        for chunks in (self.type_chunks, self.codec_chunks, argument_chunks, registration_chunks):
            result.update(chunks.outputs())
        result.update(self.special_outputs)
        return result

    def payload_type(self, name, variant):
        payload = variant["payload"]
        if payload is None:
            return None
        return self.variant_record_name(name, variant["tag"]) if payload["kind"] == "record" else self.cpp(payload)

    def declarations(self, typ, h):
        h += [f"bool read(const NiriWire::WireValue &wire, {typ} &value, DecodeError &error, const QString &path, DecodeMode mode);", f"bool write(const {typ} &value, NiriWire::WireValue &wire, DecodeError &error, const QString &path);", ""]

    def codec_record(self, typ, ir, h, cpp):
        start = len(cpp)
        self.declarations(typ, h)
        cpp += [f"bool read(const WireValue &wire, {typ} &value, DecodeError &error, const QString &path, DecodeMode mode)", "{", "    const auto *object = wire.objectValue();", '    if (!object) return fail(error, path, QStringLiteral("expected record"));', f"    {typ} result;"]
        for f in ir["fields"]:
            key, field = json.dumps(f["wire"]), f["name"]
            cpp += ["    {", f"        const auto key = QStringLiteral({key});", "        const auto *item = wire.lookup(key);"]
            if field != f["wire"]:
                cpp += [f"        if (mode == DecodeMode::Outbound && wire.lookup(QStringLiteral({json.dumps(field)}))) {{", '            if (item) return fail(error, childPath(path, key), QStringLiteral("duplicate field alias"));', f"            item = wire.lookup(QStringLiteral({json.dumps(field)}));", "        }"]
            cpp += ["        if (item) {", f"            if (!read(*item, result.{field}, error, childPath(path, key), mode)) return false;", "        }"]
            if f["required"]:
                cpp += ['        else return fail(error, childPath(path, key), QStringLiteral("missing required field"));']
            elif f["type"]["kind"] != "optional":
                raise SchemaError(f"{typ}/{field}: omittable non-optional without a default")
            cpp += ["    }"]
        cpp += ["    for (const auto &member : *object) {"]
        if ir["fields"]:
            known = [f"member.first != QStringLiteral({json.dumps(f['wire'])})" for f in ir["fields"]]
            known += [f"(mode != DecodeMode::Outbound || member.first != QStringLiteral({json.dumps(f['name'])}))" for f in ir["fields"] if f["wire"] != f["name"]]
            cpp += ["        if (" + " && ".join(known) + ") {"]
        else:
            cpp += ["        {"]
        closed = "" if ir["open"] else " || true"
        cpp += [f'            if (mode == DecodeMode::Outbound{closed}) return fail(error, childPath(path, member.first), QStringLiteral("unknown field"));', "            result.extensions.append(member);", "        }", "    }", "    value = std::move(result);", "    return true;", "}", f"bool write(const {typ} &value, WireValue &wire, DecodeError &error, const QString &path)", "{", "    WireValue::Object members;"]
        for f in ir["fields"]:
            cpp += ["    {", "        WireValue item;", f"        const auto key = QStringLiteral({json.dumps(f['wire'])});", f"        if (!write(value.{f['name']}, item, error, childPath(path, key))) return false;", "        members.append({key, item});", "    }"]
        cpp += ["    for (const auto &member : value.extensions) {", "        for (const auto &known : members) {", '            if (known.first == member.first) return fail(error, childPath(path, member.first), QStringLiteral("extension collides with a known field"));', "        }", "        members.append(member);", "    }", "    const auto object = WireValue::object(members);", '    if (!object) return fail(error, path, QStringLiteral("duplicate extension"));', "    wire = *object;", "    return true;", "}", ""]

        self.codec_chunks.take(cpp, start)

    def codec_unit(self, typ, h, cpp):
        start = len(cpp)
        self.declarations(typ, h)
        cpp += [f"bool read(const WireValue &wire, {typ} &value, DecodeError &error, const QString &path, DecodeMode mode)", "{", "    const auto *name = wire.stringValue();", '    if (!name) return fail(error, path, QStringLiteral("expected enum string"));', f"    const auto result = {typ}::fromWireName(*name);", '    if (mode == DecodeMode::Outbound && !result.known()) return fail(error, path, QStringLiteral("unknown enum value"));', "    value = result;", "    return true;", "}", f"bool write(const {typ} &value, WireValue &wire, DecodeError &error, const QString &path)", "{", "    Q_UNUSED(error); Q_UNUSED(path);", "    wire = WireValue(value.wireName());", "    return true;", "}", ""]

        self.codec_chunks.take(cpp, start)

    def codec_union(self, name, typ, variants, h, cpp):
        start = len(cpp)
        self.declarations(typ, h)
        cpp += [f"bool read(const WireValue &wire, {typ} &value, DecodeError &error, const QString &path, DecodeMode mode)", "{", "    QString tag;", "    const WireValue *payload = nullptr;", "    if (const auto *text = wire.stringValue()) tag = *text;", "    else if (const auto *object = wire.objectValue(); object && object->size() == 1) {", "        tag = object->first().first;", "        payload = &object->first().second;", '    } else return fail(error, path, QStringLiteral("expected enum string or single-key variant"));']
        for v in variants:
            alias, ptype = self.alias(name, v["tag"]), self.payload_type(name, v)
            cpp += [f"    if (tag == QStringLiteral({json.dumps(v['tag'])})) {{"]
            if ptype:
                cpp += ['        if (!payload) return fail(error, path, QStringLiteral("expected tagged payload"));', f"        {ptype} item{{}};", "        if (!read(*payload, item, error, childPath(path, tag), mode)) return false;", f"        value = {typ}::from{alias}(item);"]
            else:
                cpp += ['        if (payload) return fail(error, path, QStringLiteral("expected unit string"));', f"        value = {typ}::from{alias}();"]
            cpp += ["        return true;", "    }"]
        cpp += ['    if (mode == DecodeMode::Outbound) return fail(error, path, QStringLiteral("unknown variant"));', f"    {typ} result;", "    result.value = NiriUnknownVariant{tag, wire};", "    value = result;", "    return true;", "}", f"bool write(const {typ} &value, WireValue &wire, DecodeError &error, const QString &path)", "{", "    switch (value.kind()) {"]
        for v in variants:
            alias, ptype = self.alias(name, v["tag"]), self.payload_type(name, v)
            cpp += [f"    case {typ}::{alias}: {{"]
            if ptype:
                cpp += ["        WireValue payload;", f"        const auto tag = QStringLiteral({json.dumps(v['tag'])});", f"        if (!write(std::get<{typ}::{alias}Value>(value.value).payload, payload, error, childPath(path, tag))) return false;", "        wire = *WireValue::object({{tag, payload}});"]
            else:
                cpp += [f"        wire = WireValue(QStringLiteral({json.dumps(v['tag'])}));"]
            cpp += ["        return true;", "    }"]
        cpp += [f"    case {typ}::Unknown: wire = std::get<NiriUnknownVariant>(value.value).raw; return true;", '    default: return fail(error, path, value.invalidReason.isEmpty() ? QStringLiteral("invalid union construction") : value.invalidReason);', "    }", "}", ""]

        self.codec_chunks.take(cpp, start)

    def runtime_header(self):
        return r'''
bool idInput(const QVariant &input, std::optional<quint64> &value, DecodeError &error);
bool read(const NiriWire::WireValue &wire, QString &value, DecodeError &error, const QString &path, DecodeMode mode);
bool read(const NiriWire::WireValue &wire, bool &value, DecodeError &error, const QString &path, DecodeMode mode);
bool write(const QString &value, NiriWire::WireValue &wire, DecodeError &error, const QString &path);
bool write(bool value, NiriWire::WireValue &wire, DecodeError &error, const QString &path);

template <typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
bool read(const NiriWire::WireValue &wire, T &value, DecodeError &error, const QString &path, DecodeMode mode)
{
    auto token = wire.numberValue() ? std::optional<NiriWire::NumberToken>(*wire.numberValue()) : std::nullopt;
    if (!token && mode == DecodeMode::Outbound && sizeof(T) == 8 && wire.stringValue()) {
        const auto text = wire.stringValue()->toLatin1();
        if (QString::fromLatin1(text) != *wire.stringValue()) return fail(error, path, QStringLiteral("expected decimal integer"));
        token = NiriWire::NumberToken::fromText(text);
        if (token && !token->isIntegerToken()) token.reset();
    }
    if (!token) return fail(error, path, QStringLiteral("expected integer"));
    if constexpr (std::is_unsigned_v<T>) {
        const auto integer = token->toUInt64();
        if (!integer || *integer > std::numeric_limits<T>::max()) return fail(error, path, QStringLiteral("unsigned integer out of range"));
        value = T(*integer);
    } else {
        const auto integer = token->toInt64();
        if (!integer || *integer < std::numeric_limits<T>::min() || *integer > std::numeric_limits<T>::max()) return fail(error, path, QStringLiteral("signed integer out of range"));
        value = T(*integer);
    }
    return true;
}
template <typename T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
bool read(const NiriWire::WireValue &wire, T &value, DecodeError &error, const QString &path, DecodeMode mode)
{
    Q_UNUSED(mode);
    const auto number = wire.numberValue() ? wire.numberValue()->toDouble() : std::nullopt;
    if (!number || !std::isfinite(T(*number))) return fail(error, path, QStringLiteral("expected finite number"));
    value = T(*number);
    return true;
}
template <typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
bool write(T value, NiriWire::WireValue &wire, DecodeError &error, const QString &path)
{
    Q_UNUSED(error); Q_UNUSED(path);
    if constexpr (std::is_unsigned_v<T>) wire = NiriWire::WireValue(NiriWire::NumberToken::fromUInt64(value));
    else wire = NiriWire::WireValue(NiriWire::NumberToken::fromInt64(value));
    return true;
}
template <typename T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
bool write(T value, NiriWire::WireValue &wire, DecodeError &error, const QString &path)
{
    if (!std::isfinite(value)) return fail(error, path, QStringLiteral("expected finite number"));
    const auto token = NiriWire::NumberToken::fromText(QByteArray::number(value, 'g', std::numeric_limits<T>::max_digits10));
    if (!token) return fail(error, path, QStringLiteral("cannot encode number"));
    wire = NiriWire::WireValue(*token);
    return true;
}
// Forward declarations make nested containers work independently of definition order.
template <typename T> bool read(const NiriWire::WireValue &, std::optional<T> &, DecodeError &, const QString &, DecodeMode);
template <typename T> bool write(const std::optional<T> &, NiriWire::WireValue &, DecodeError &, const QString &);
template <typename T> bool read(const NiriWire::WireValue &, QList<T> &, DecodeError &, const QString &, DecodeMode);
template <typename T> bool write(const QList<T> &, NiriWire::WireValue &, DecodeError &, const QString &);
template <typename T, std::size_t N> bool read(const NiriWire::WireValue &, std::array<T, N> &, DecodeError &, const QString &, DecodeMode);
template <typename T, std::size_t N> bool write(const std::array<T, N> &, NiriWire::WireValue &, DecodeError &, const QString &);
template <typename T> bool read(const NiriWire::WireValue &, QMap<QString, T> &, DecodeError &, const QString &, DecodeMode);
template <typename T> bool write(const QMap<QString, T> &, NiriWire::WireValue &, DecodeError &, const QString &);
template <typename... T> bool read(const NiriWire::WireValue &, std::tuple<T...> &, DecodeError &, const QString &, DecodeMode);
template <typename... T> bool write(const std::tuple<T...> &, NiriWire::WireValue &, DecodeError &, const QString &);

template <typename... T> bool read(const NiriWire::WireValue &wire, std::tuple<T...> &value, DecodeError &error, const QString &path, DecodeMode mode)
{
    if (!wire.arrayValue() || wire.arrayValue()->size() != sizeof...(T)) return fail(error, path, QStringLiteral("expected tuple of length %1").arg(sizeof...(T)));
    std::tuple<T...> result{};
    qsizetype index = 0;
    const bool ok = std::apply([&](auto &...item) {
        auto next = [&](auto &typed) {
            const auto current = index++;
            return read(wire.arrayValue()->at(current), typed, error,
                        path + QStringLiteral("[%1]").arg(current), mode);
        };
        return (next(item) && ...);
    }, result);
    if (ok) value = std::move(result);
    return ok;
}
template <typename... T> bool write(const std::tuple<T...> &value, NiriWire::WireValue &wire, DecodeError &error, const QString &path)
{
    NiriWire::WireValue::Array result;
    const bool ok = std::apply([&](const auto &...item) {
        auto append = [&](const auto &typed) {
            NiriWire::WireValue encoded;
            if (!write(typed, encoded, error, path + QStringLiteral("[%1]").arg(result.size()))) return false;
            result.append(encoded);
            return true;
        };
        return (append(item) && ...);
    }, value);
    if (ok) wire = NiriWire::WireValue(result);
    return ok;
}

template <typename T> bool read(const NiriWire::WireValue &wire, std::optional<T> &value, DecodeError &error, const QString &path, DecodeMode mode)
{
    if (wire.type() == NiriWire::WireValue::Type::Null) { value.reset(); return true; }
    T item{};
    if (!read(wire, item, error, path, mode)) return false;
    value = std::move(item);
    return true;
}
template <typename T> bool write(const std::optional<T> &value, NiriWire::WireValue &wire, DecodeError &error, const QString &path)
{
    if (!value) { wire = NiriWire::WireValue(); return true; }
    return write(*value, wire, error, path);
}
template <typename T> bool read(const NiriWire::WireValue &wire, QList<T> &value, DecodeError &error, const QString &path, DecodeMode mode)
{
    if (!wire.arrayValue()) return fail(error, path, QStringLiteral("expected array"));
    QList<T> result;
    for (const auto &item : *wire.arrayValue()) {
        T typed{};
        if (!read(item, typed, error, path + QStringLiteral("[%1]").arg(result.size()), mode)) return false;
        result.append(std::move(typed));
    }
    value = std::move(result);
    return true;
}
template <typename T> bool write(const QList<T> &value, NiriWire::WireValue &wire, DecodeError &error, const QString &path)
{
    NiriWire::WireValue::Array result;
    for (const auto &item : value) {
        NiriWire::WireValue encoded;
        if (!write(item, encoded, error, path + QStringLiteral("[%1]").arg(result.size()))) return false;
        result.append(encoded);
    }
    wire = NiriWire::WireValue(result);
    return true;
}
template <typename T, std::size_t N> bool read(const NiriWire::WireValue &wire, std::array<T, N> &value, DecodeError &error, const QString &path, DecodeMode mode)
{
    if (!wire.arrayValue() || wire.arrayValue()->size() != qsizetype(N)) return fail(error, path, QStringLiteral("expected tuple of length %1").arg(N));
    std::array<T, N> result{};
    for (std::size_t i = 0; i < N; ++i) {
        if (!read(wire.arrayValue()->at(i), result[i], error, path + QStringLiteral("[%1]").arg(i), mode)) return false;
    }
    value = std::move(result);
    return true;
}
template <typename T, std::size_t N> bool write(const std::array<T, N> &value, NiriWire::WireValue &wire, DecodeError &error, const QString &path)
{
    NiriWire::WireValue::Array result;
    for (std::size_t i = 0; i < N; ++i) {
        NiriWire::WireValue item;
        if (!write(value[i], item, error, path + QStringLiteral("[%1]").arg(i))) return false;
        result.append(item);
    }
    wire = NiriWire::WireValue(result);
    return true;
}
template <typename T> bool read(const NiriWire::WireValue &wire, QMap<QString, T> &value, DecodeError &error, const QString &path, DecodeMode mode)
{
    if (!wire.objectValue()) return fail(error, path, QStringLiteral("expected map"));
    QMap<QString, T> result;
    for (const auto &member : *wire.objectValue()) {
        T item{};
        if (!read(member.second, item, error, childPath(path, member.first), mode)) return false;
        result.insert(member.first, std::move(item));
    }
    value = std::move(result);
    return true;
}
template <typename T> bool write(const QMap<QString, T> &value, NiriWire::WireValue &wire, DecodeError &error, const QString &path)
{
    NiriWire::WireValue::Object result;
    for (auto it = value.begin(); it != value.end(); ++it) {
        NiriWire::WireValue item;
        if (!write(it.value(), item, error, childPath(path, it.key()))) return false;
        result.append({it.key(), item});
    }
    wire = *NiriWire::WireValue::object(result);
    return true;
}
template <typename T> bool decode(const NiriWire::WireValue &wire, T &value, DecodeError *error = nullptr, const QString &path = QStringLiteral("$"), DecodeMode mode = DecodeMode::Received)
{
    DecodeError detail;
    T result{};
    const bool ok = read(wire, result, detail, path, mode);
    if (error) *error = detail;
    if (ok) value = std::move(result);
    return ok;
}
template <typename T> std::optional<NiriWire::WireValue> encode(const T &value, DecodeError *error = nullptr, const QString &path = QStringLiteral("$"))
{
    DecodeError detail;
    NiriWire::WireValue wire;
    const bool ok = write(value, wire, detail, path);
    if (error) *error = detail;
    return ok ? std::optional<NiriWire::WireValue>(wire) : std::nullopt;
}
'''

    def runtime_cpp(self):
        return r'''
bool qmlNumber(double input, WireValue &wire, DecodeError &error, const QString &path)
{
    // Preserve the exact integral binary64 value before formatting a decimal.
    // max_digits10 promises a floating roundtrip, not an exact integer token.
    if (std::isfinite(input) && std::floor(input) == input) {
        if (input >= 0 && input < 0x1p64) {
            wire = WireValue(NumberToken::fromUInt64(static_cast<quint64>(input)));
            return true;
        }
        if (input < 0 && input >= -0x1p63) {
            wire = WireValue(NumberToken::fromInt64(static_cast<qint64>(input)));
            return true;
        }
    }
    return write(input, wire, error, path);
}
bool idInput(const QVariant &input, std::optional<quint64> &value, DecodeError &error)
{
    if (!input.isValid() || input.metaType() == QMetaType::fromType<std::nullptr_t>()) {
        value.reset();
        return true;
    }
    WireValue wire;
    const auto path = QStringLiteral("$");
    switch (input.typeId()) {
    case QMetaType::QString: wire = WireValue(input.toString()); break;
    case QMetaType::UInt:
    case QMetaType::ULongLong: wire = WireValue(NumberToken::fromUInt64(input.toULongLong())); break;
    case QMetaType::Int:
    case QMetaType::LongLong: wire = WireValue(NumberToken::fromInt64(input.toLongLong())); break;
    case QMetaType::Double:
    case QMetaType::Float:
        if (!qmlNumber(input.toDouble(), wire, error, path)) return false;
        break;
    default: return fail(error, path, QStringLiteral("expected an ID string, exact number or null"));
    }
    return decode(wire, value, &error, path, DecodeMode::Outbound);
}
bool qmlInput(const QJSValue &input, WireValue &wire, DecodeError &error, const QString &path, int depth)
{
    if (depth > 128) return fail(error, path, QStringLiteral("input nesting exceeds 128"));
    // A typed gadget bypasses JS property enumeration, but still goes through
    // the constructor's versioned outbound decode contract.
    QVariant native;
    if (input.isObject()) {
        native = input.toVariant(QJSValue::RetainJSObjects);
        DecodeError detail;
        if (const auto typed = encodeVariant(native, &detail)) {
            wire = *typed;
            return true;
        }
        if (!detail.message.isEmpty()) {
            error = detail;
            return false;
        }
    }
    if (input.isNull()) { wire = WireValue(); return true; }
    if (input.isBool()) { wire = WireValue(input.toBool()); return true; }
    if (input.isString()) { wire = WireValue(input.toString()); return true; }
    if (input.isNumber()) return qmlNumber(input.toNumber(), wire, error, path);
    // Qt-backed QML sequences are array-like but QJSValue::isArray() is false.
    // Recognize the actual native sequence type, never guess from a length key.
    if (input.isArray() || native.typeId() == QMetaType::QVariantList || native.typeId() == QMetaType::QStringList) {
        WireValue::Array items;
        const auto length = input.property(QStringLiteral("length")).toUInt();
        for (quint32 i = 0; i < length; ++i) {
            WireValue item;
            if (!qmlInput(input.property(i), item, error, path + QStringLiteral("[%1]").arg(i), depth + 1)) return false;
            items.append(item);
        }
        wire = WireValue(items);
        return true;
    }
    if (input.isObject() && !input.isCallable() && !input.isQObject()) {
        WireValue::Object items;
        QJSValueIterator iterator(input);
        while (iterator.hasNext()) {
            iterator.next();
            WireValue item;
            if (!qmlInput(iterator.value(), item, error, childPath(path, iterator.name()), depth + 1)) return false;
            items.append({iterator.name(), item});
        }
        const auto object = WireValue::object(items);
        if (!object) return fail(error, path, QStringLiteral("duplicate object key"));
        wire = *object;
        return true;
    }
    return fail(error, path, QStringLiteral("expected a JSON value, not undefined or a callable"));
}
bool fail(DecodeError &error, const QString &path, const QString &message)
{
    error = {path, message};
    return false;
}
QString childPath(const QString &path, const QString &key)
{
    QString escaped = key;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('\''), QStringLiteral("\\'"));
    return path + QStringLiteral("['%1']").arg(escaped);
}
bool read(const WireValue &wire, QString &value, DecodeError &error, const QString &path, DecodeMode mode)
{
    Q_UNUSED(mode);
    if (!wire.stringValue()) return fail(error, path, QStringLiteral("expected string"));
    value = *wire.stringValue();
    return true;
}
bool read(const WireValue &wire, bool &value, DecodeError &error, const QString &path, DecodeMode mode)
{
    Q_UNUSED(mode);
    if (!wire.boolValue()) return fail(error, path, QStringLiteral("expected boolean"));
    value = *wire.boolValue();
    return true;
}
bool write(const QString &value, WireValue &wire, DecodeError &error, const QString &path)
{
    Q_UNUSED(error); Q_UNUSED(path);
    wire = WireValue(value);
    return true;
}
bool write(bool value, WireValue &wire, DecodeError &error, const QString &path)
{
    Q_UNUSED(error); Q_UNUSED(path);
    wire = WireValue(value);
    return true;
}
'''


class ActionEmitter(CppEmitter):
    """Schema-derived callable base; the consumer injects the existing request sink."""

    def action_fields(self, variant):
        tag = variant["tag"]
        binding = self.c.bindings.get("actions", {}).get(tag, {})
        fields = variant["payload"]["fields"]
        order = binding.get("arguments", [f["wire"] for f in sorted(fields, key=lambda f: (not f["required"], f["wire"]))])
        return [{**next(f for f in fields if f["wire"] == wire),
                 "name": binding.get("aliases", {}).get(wire, camel(wire))} for wire in order]

    def emit(self):
        h = [self.header(), "#pragma once", "", '#include "niriipctypes_generated.h"', '#include "niriipcreplies_generated.h"', "#include <QJSValue>", "", "// clang-format off", "class NiriIpcActions : public QObject", "{", "    Q_OBJECT", "public:", "    using QObject::QObject;"]
        cpp = [self.header(), '#include "niriipcactions_generated.h"', '#include "niriipccodec_generated.h"', "", "// clang-format off", "namespace {", "template <typename T>", "bool encodeOutbound(const T &value, NiriWire::WireValue &wire, NiriIpc::DecodeError &error, const QString &path)", "{", "    const auto encoded = NiriIpc::encode(value, &error, path);", "    if (!encoded) return false;", "    T checked{};", "    if (!NiriIpc::decode(*encoded, checked, &error, path, NiriIpc::DecodeMode::Outbound)) return false;", "    wire = *encoded;", "    return true;", "}", "} // namespace", ""]
        manifest = []
        docs = ["# Generated IPC API", "", self.header("<!--").replace("\n", " -->\n"), "## Actions", "", "All methods return a retainable NiriHandledReply through the shared request path.", "IDs/usize accept decimal strings or exact numbers; optional values accept null.", "Required booleans have no CLI-derived defaults. Optional slots before required", "arguments must be supplied explicitly as null. Union arguments accept NiriArgs", "values or plain objects using their wire tag and wire/camelCase field names.", "", "| # | QML call | Exact wire shape |", "|---|---|---|"]

        def shape(ir):
            if ir["kind"] == "ref": return ir["name"]
            if ir["kind"] == "optional": return "null or " + shape(ir["item"])
            if ir["kind"] == "integer": return ir["format"]
            if ir["kind"] == "number": return ir["format"]
            if ir["kind"] == "array": return "[" + shape(ir["item"]) + "]"
            return ir["kind"]

        def qml_shape(ir):
            if ir["kind"] == "optional": return "null / " + qml_shape(ir["item"])
            if ir["kind"] == "integer": return "string / exact number" if ir["format"] in {"uint", "uint64", "int", "int64"} else "number"
            if ir["kind"] == "number": return "number"
            if ir["kind"] == "boolean": return "bool"
            if ir["kind"] == "array": return "list<" + qml_shape(ir["item"]) + ">"
            if ir["kind"] == "ref": return self.c.name(ir["name"])[:1].lower() + self.c.name(ir["name"])[1:] + " / object"
            return shape(ir)

        chunks = SourceBuckets(self, "niriipcactions", cpp[1:])
        for index, variant in enumerate(self.c.ir["Action"]["variants"], 1):
            start = len(cpp)
            tag = variant["tag"]
            method = self.c.bindings.get("actions", {}).get(tag, {}).get("name", tag[:1].lower() + tag[1:])
            fields = self.action_fields(variant)
            names = ", ".join(f["name"] for f in fields)
            native = ", ".join(self.cpp(f["type"]) + " " + f["name"] for f in fields)
            qml = []
            for i, field in enumerate(fields):
                # Permit the QML call to enter our checked boundary even when a
                # required slot is missing. Undefined is rejected asynchronously;
                # this is not a semantic default for a required wire field.
                default = " = QJSValue()" if field["required"] else " = QJSValue(QJSValue::NullValue)"
                qml.append("const QJSValue &" + field["name"] + default)
            if fields:
                h += [f"    Q_INVOKABLE NiriHandledReply *{method}({', '.join(qml)});", f"    NiriHandledReply *{method}({native});"]
                optional_ids = {f["name"] for f in fields if f["type"] == {"kind": "optional", "item": {"kind": "integer", "format": "uint64", "minimum": 0}}}
                if optional_ids:
                    explicit_id_signature = ", ".join(("quint64" if f["name"] in optional_ids else self.cpp(f["type"])) + " " + f["name"] for f in fields)
                    explicit_id_args = ", ".join("std::optional<quint64>(" + f["name"] + ")" if f["name"] in optional_ids else f["name"] for f in fields)
                    h += [f"    NiriHandledReply *{method}({explicit_id_signature}) {{ return this->{method}({explicit_id_args}); }}"]
                if len(fields) == 1 and self.cpp(fields[0]["type"]) == "QList<QString>":
                    arg = fields[0]["name"]
                    h += [f"    NiriHandledReply *{method}(std::initializer_list<QString> {arg}) {{ return this->{method}(QList<QString>({arg})); }}"]
                cpp += [f"NiriHandledReply *NiriIpcActions::{method}({', '.join('const QJSValue &' + f['name'] for f in fields)})", "{", "    NiriIpc::DecodeError error;"]
                for f in fields:
                    arg, typ = f["name"], self.cpp(f["type"])
                    path = "QStringLiteral(" + json.dumps("$.Action." + tag + "." + f["wire"]) + ")"
                    cpp += [f"    {typ} typed_{arg}{{}};", "    {", "        NiriWire::WireValue wire;"]
                    guard = f"!{arg}.isUndefined() && " if not f["required"] else ""
                    cpp += [f"        if (({guard}!NiriIpc::qmlInput({arg}, wire, error, {path}))", f"            || !NiriIpc::decode(wire, typed_{arg}, &error, {path}, NiriIpc::DecodeMode::Outbound))", '            return submitAction({}, error.path + QStringLiteral(": ") + error.message);', "    }"]
                cpp += [f"    return this->{method}({', '.join('typed_' + f['name'] for f in fields)});", "}", ""]
            else:
                h += [f"    Q_INVOKABLE NiriHandledReply *{method}();"]
            cpp += [f"NiriHandledReply *NiriIpcActions::{method}({native})", "{"]
            if fields:
                cpp += ["    NiriIpc::DecodeError error;"]
            cpp += ["    NiriWire::WireValue::Object members;"]
            for f in fields:
                path = "QStringLiteral(" + json.dumps("$.Action." + tag + "." + f["wire"]) + ")"
                cpp += ["    {", "        NiriWire::WireValue wire;", f"        if (!encodeOutbound({f['name']}, wire, error, {path}))", '            return submitAction({}, error.path + QStringLiteral(": ") + error.message);', f"        members.append({{QStringLiteral({json.dumps(f['wire'])}), wire}});", "    }"]
            cpp += [f"    const auto action = *NiriWire::WireValue::object({{{{QStringLiteral({json.dumps(tag)}), *NiriWire::WireValue::object(members)}}}});", "    return submitAction(action, {});", "}", ""]
            manifest.append({"variant": tag, "method": method, "arguments": [{"wire": f["wire"], "name": f["name"], "type": f["type"]} for f in fields]})
            signature = ", ".join(f["name"] + ": " + qml_shape(f["type"]) for f in fields)
            wire_shape = '{"Action":{"' + tag + '":{' + ",".join(json.dumps(f["wire"]) + ":" + shape(f["type"]) for f in fields) + "}}}"
            docs.append(f"| {index} | `NiriActions.{method}({signature}) -> NiriHandledReply` | `{wire_shape}` |")
            chunks.take(cpp, start)
        h += ["protected:", "    virtual NiriHandledReply *submitAction(const NiriWire::WireValue &action, const QString &validationError) = 0;", "};", "// clang-format on", ""]
        cpp += ["// clang-format on", ""]
        return {"generated/niriipcactions_generated.h": "\n".join(h), **chunks.outputs(), "generated/ipc-actions.json": json.dumps(manifest, indent=2, sort_keys=True) + "\n", "docs/generated/IPC-API.md": "\n".join(docs) + "\n"}


class ReplyEmitter(CppEmitter):
    def emit(self):
        h = [self.header(), "#pragma once", "", '#include "niriipctypes_generated.h"', '#include "niripendingreply.h"', "", "// clang-format off", "struct NiriHandled", "{", "    Q_GADGET", "    QML_VALUE_TYPE(niriHandled)", "    Q_PROPERTY(QString wireName READ wireName CONSTANT)", "public:", '    QString wireName() const { return QStringLiteral("Handled"); }', "    bool operator==(const NiriHandled &) const { return true; }", "};", "Q_DECLARE_METATYPE(NiriHandled)", ""]
        cpp = [self.header(), '#include "niriipcreplies_generated.h"', '#include "niriipccodec_generated.h"', "", "// clang-format off", "namespace NiriIpc {", "NiriPendingReply *createReply(const QString &expected, QObject *parent)", "{"]
        variants = self.c.ir["Response"]["variants"]
        for variant in variants:
            tag, payload = variant["tag"], variant["payload"]
            name = "Niri" + tag + "Reply"
            typ = self.cpp(payload) if payload else "NiriHandled"
            prop = self.property_type(payload) if payload else "NiriHandled"
            h += [f"class {name} : public NiriPendingReply", "{", "    Q_OBJECT", f"    QML_NAMED_ELEMENT({name})", '    QML_UNCREATABLE("Returned by NiriRequests or NiriActions")', f"    Q_PROPERTY({prop} result READ qmlResult NOTIFY finished)", "public:", "    using NiriPendingReply::NiriPendingReply;", f"    using Result = {typ};", "    Result result() const { return nativeValue().value<Result>(); }"]
            projection = "NiriIpc::qmlValue(result())" if prop == "QVariant" else "result()"
            h += [f"    {prop} qmlResult() const {{ return {projection}; }}", "    QVariant value() const override", "    {", "        if (!completed() || isError()) return NiriPendingReply::value();"]
            h += ['        return QStringLiteral("Handled");' if payload is None else "        return NiriIpc::qmlValue(result());", "    }", "};", ""]
            cpp += [f"    if (expected == QStringLiteral({json.dumps(tag)})) return new {name}(parent);"]
        cpp += ["    return new NiriPendingReply(parent);", "}", "", "bool decodeResponse(const QString &expected, const NiriWire::WireValue &ok, QVariant &value, NiriError &error)", "{", '    const auto path = QStringLiteral("$.Ok");', '    if (expected == QStringLiteral("Handled")) {', '        if (!ok.stringValue() || *ok.stringValue() != QStringLiteral("Handled")) {', '            error.fieldPath = path; error.message = QStringLiteral("Expected Handled response"); return false;', "        }", "        value = QVariant::fromValue(NiriHandled{});", "        return true;", "    }", "    const auto *payload = ok.lookup(expected);", "    if (!ok.objectValue() || ok.objectValue()->size() != 1 || !payload) {", '        error.fieldPath = path + QLatin1Char(\'.\') + expected;', '        error.message = QStringLiteral("Expected %1 response").arg(expected);', "        return false;", "    }", "    DecodeError detail;"]
        for variant in variants:
            tag, payload = variant["tag"], variant["payload"]
            if payload is None:
                continue
            cpp += [f"    if (expected == QStringLiteral({json.dumps(tag)})) {{", f"        {self.cpp(payload)} typed{{}};", '        if (!decode(*payload, typed, &detail, path + QLatin1Char(\'.\') + expected)) {', "            error.fieldPath = detail.path; error.message = detail.message; return false;", "        }", "        value = QVariant::fromValue(typed);", "        return true;", "    }"]
        cpp += ['    error.fieldPath = path; error.message = QStringLiteral("Expected Err response");', "    return false;", "}", "} // namespace NiriIpc", "// clang-format on", ""]
        h += ["namespace NiriIpc {", "NiriPendingReply *createReply(const QString &expected, QObject *parent);", "bool decodeResponse(const QString &expected, const NiriWire::WireValue &ok, QVariant &value, NiriError &error);", "} // namespace NiriIpc", "// clang-format on", ""]
        return {"generated/niriipcreplies_generated.h": "\n".join(h), "generated/niriipcreplies_generated.cpp": "\n".join(cpp)}


class RequestEmitter(CppEmitter):
    def fields(self, variant):
        payload = variant["payload"]
        binding = self.c.bindings["requests"][variant["tag"]]
        if payload is None:
            return []
        if payload["kind"] != "record":
            return [{"name": binding.get("argument", "value"), "type": payload, "wire": None}]
        order = binding.get("arguments", [f["wire"] for f in payload["fields"]])
        return [next(f for f in payload["fields"] if f["wire"] == wire) for wire in order]

    def emit(self):
        h = [self.header(), "#pragma once", "", '#include "niriipcreplies_generated.h"', "#include <QJSValue>", "", "// clang-format off", "class NiriIpcRequests : public QObject", "{", "    Q_OBJECT", "public:", "    using QObject::QObject;"]
        cpp = [self.header(), '#include "niriipcrequests_generated.h"', '#include "niriipccodec_generated.h"', "", "// clang-format off", "namespace {", "template <typename T>", "bool requestArgument(const T &value, NiriWire::WireValue &wire, NiriIpc::DecodeError &error, const QString &path)", "{", "    const auto encoded = NiriIpc::encode(value, &error, path);", "    if (!encoded) return false;", "    T checked{};", "    if (!NiriIpc::decode(*encoded, checked, &error, path, NiriIpc::DecodeMode::Outbound)) return false;", "    wire = *encoded; return true;", "}", "} // namespace", ""]
        rows = []
        docs = ["", "## Requests", "", "Methods accept a trailing options object with timeoutMs/connectTimeoutMs.", "Picks default to timeoutMs -1 (no response deadline). EventStream observes the", "session's real acknowledgement and leaves its stream open; it never opens a", "one-shot socket. ReturnError expects Err and reports it through the common", "error contract; an unexpected Ok is a Decode error.", "", "| # | QML call | Expected reply | Wire shape | Source |", "|---|---|---|---|---|"]
        for index, variant in enumerate(self.c.ir["Request"]["variants"], 1):
            tag, payload = variant["tag"], variant["payload"]
            binding = self.c.bindings["requests"][tag]
            expected = binding["response"]
            method = binding.get("name", tag[:1].lower() + tag[1:])
            typ = "NiriPendingReply" if expected == "Err" else "Niri" + expected + "Reply"
            fields = self.fields(variant)
            tail = 'const QVariantMap &options'
            qml = ", ".join("const QJSValue &" + f["name"] + " = QJSValue()" for f in fields)
            native = ", ".join(self.cpp(f["type"]) + " " + f["name"] for f in fields)
            qml_decl = (qml + ", " if qml else "") + tail + " = {}"
            native_decl = (native + ", " if native else "") + tail + " = {}"
            h += [f"    Q_INVOKABLE {typ} *{method}({qml_decl});"]
            flags = f"QStringLiteral({json.dumps(expected)}), options, {'true' if tag in {'PickWindow', 'PickColor'} else 'false'}, {'true' if tag == 'EventStream' else 'false'}"
            error_result = f"static_cast<{typ} *>(submitRequest({{}}, {flags}, error.path + QStringLiteral(\": \") + error.message))"
            if fields:
                h += [f"    {typ} *{method}({native_decl});"]
                cpp += [f"{typ} *NiriIpcRequests::{method}({', '.join('const QJSValue &' + f['name'] for f in fields)}, {tail})", "{", "    NiriIpc::DecodeError error;"]
                for field in fields:
                    arg, argtype = field["name"], self.cpp(field["type"])
                    path = "QStringLiteral(" + json.dumps("$." + tag + ("." + field["wire"] if field["wire"] else "")) + ")"
                    cpp += [f"    {argtype} typed_{arg}{{}};", "    {", "        NiriWire::WireValue wire;", f"        if (!NiriIpc::qmlInput({arg}, wire, error, {path}) || !NiriIpc::decode(wire, typed_{arg}, &error, {path}, NiriIpc::DecodeMode::Outbound))", f"            return {error_result};", "    }"]
                cpp += [f"    return this->{method}({', '.join('typed_' + f['name'] for f in fields)}, options);", "}", ""]
            cpp += [f"{typ} *NiriIpcRequests::{method}({(native + ', ') if native else ''}{tail})", "{"]
            if fields:
                cpp += ["    NiriIpc::DecodeError error;"]
                if payload["kind"] == "record":
                    cpp += ["    NiriWire::WireValue::Object members;"]
                for field in fields:
                    path = "QStringLiteral(" + json.dumps("$." + tag + ("." + field["wire"] if field["wire"] else "")) + ")"
                    cpp += ["    NiriWire::WireValue wire_" + field["name"] + ";", f"    if (!requestArgument({field['name']}, wire_{field['name']}, error, {path})) return {error_result};"]
                    if field["wire"]:
                        cpp += [f"    members.append({{QStringLiteral({json.dumps(field['wire'])}), wire_{field['name']}}});"]
                body = "*NiriWire::WireValue::object(members)" if payload["kind"] == "record" else "wire_" + fields[0]["name"]
                wire = f"*NiriWire::WireValue::object({{{{QStringLiteral({json.dumps(tag)}), {body}}}}})"
            else:
                wire = f"NiriWire::WireValue(QStringLiteral({json.dumps(tag)}))"
            cpp += [f"    return static_cast<{typ} *>(submitRequest({wire}, {flags}, {{}}));", "}", ""]
            rows.append({"variant": tag, "method": method, "response": expected, "arguments": fields})
            wire_shape = json.dumps(tag) if not fields else '{"' + tag + '":' + ("{" + ",".join(json.dumps(f["wire"]) + ":" + self.cpp(f["type"]) for f in fields) + "}" if payload["kind"] == "record" else self.cpp(payload)) + "}"
            docs.append(f"| {index} | `NiriRequests.{method}({', '.join(f['name'] for f in fields)}{', ' if fields else ''}options = {{}})` | `{typ}` ({expected}) | `{wire_shape}` | `{binding['source']}` |")
        h += ["protected:", "    virtual NiriPendingReply *submitRequest(const NiriWire::WireValue &request, const QString &expected, const QVariantMap &options, bool interactive, bool eventStream, const QString &validationError) = 0;", "};", "// clang-format on", ""]
        cpp += ["// clang-format on", ""]
        docs += ["", "## Typed replies", "", "All 14 response kinds have uncreatable named reply classes. The result property", "is typed; native result() uses the canonical C++ type. Optional results are null", "in QML. The inherited value remains the QML convenience projection (Handled is", "the string Handled); result on NiriHandledReply is a niriHandled unit value.", "Success requires the expected discriminator and complete validated payload.", "Errors retain the full raw envelope and fieldPath; replies remain retainable.", "", "| # | Reply | Native result | QML result | Wire response |", "|---|---|---|---|---|"]
        for index, v in enumerate(self.c.ir["Response"]["variants"], 1):
            payload = v["payload"]
            docs.append(f"| {index} | `Niri{v['tag']}Reply` | `{self.cpp(payload) if payload else 'NiriHandled'}` | `{self.property_type(payload) if payload else 'niriHandled'}` | `{'{'+json.dumps(v['tag'])+':payload}' if payload else json.dumps(v['tag'])}` |")
        return {"generated/niriipcrequests_generated.h": "\n".join(h), "generated/niriipcrequests_generated.cpp": "\n".join(cpp), "generated/ipc-requests.json": json.dumps(rows, indent=2, sort_keys=True) + "\n", "docs/generated/IPC-API.md": "\n".join(docs) + "\n"}


class EventEmitter(CppEmitter):
    def fields(self, variant):
        tag = variant["tag"]
        binding = self.c.bindings["events"][tag]
        fields = variant["payload"]["fields"]
        order = binding.get("arguments", [f["wire"] for f in fields])
        result = []
        for wire in order:
            field = next(f for f in fields if f["wire"] == wire)
            tuple_binding = self.c.tuples.get(f"Event/{tag}/{wire}/items")
            result.append({**field, "argument": binding.get("aliases", {}).get(wire, field["name"]), "tupleBinding": tuple_binding})
        return result

    def signal_type(self, field, qml=False):
        ir = field["type"]
        if qml:
            if ir["kind"] == "array": return "const QVariantList &"
            if ir["kind"] == "optional": return "const QVariant &"
            if ir["kind"] == "integer": return "const QString &" if ir["format"] in {"uint64", "uint"} else "int "
        typ = f"QList<{field['tupleBinding']['cppName']}>" if field["tupleBinding"] else self.cpp(ir)
        return "const " + typ + " &" if ir["kind"] in {"array", "ref", "string"} else typ + " "

    def emit(self):
        h = [self.header(), "#pragma once", "", '#include "niriipccodec_generated.h"', "#include <functional>", "", "// clang-format off", "namespace NiriIpc {", "bool decodeEvent(const NiriWire::WireValue &wire, NiriEvent &event, DecodeError &error);", "} // namespace NiriIpc", "", "class NiriNativeEvents : public QObject", "{", "    Q_OBJECT", "public:", "    using QObject::QObject;", "signals:"]
        qml_signals = []
        forwarding = []
        publishing = []
        rows = []
        known_tags = " || ".join("tag == QStringLiteral(" + json.dumps(v["tag"]) + ")" for v in self.c.ir["Event"]["variants"])
        cpp = [self.header(), '#include "niriipcevents_generated.h"', "", "// clang-format off", "namespace NiriIpc {", "bool decodeEvent(const NiriWire::WireValue &wire, NiriEvent &event, DecodeError &error)", "{", "    QString tag;", "    const NiriWire::WireValue *payload = nullptr;", "    if (wire.stringValue()) tag = *wire.stringValue();", "    else if (wire.objectValue() && wire.objectValue()->size() == 1) {", "        tag = wire.objectValue()->first().first; payload = &wire.objectValue()->first().second;", "    }", f"    if (({known_tags}) && (!payload || !payload->objectValue()))", '        return fail(error, childPath(QStringLiteral("$"), tag), QStringLiteral("expected event payload object"));', '    return decode(wire, event, &error, QStringLiteral("$"));', "}", "} // namespace NiriIpc", ""]
        for variant in self.c.ir["Event"]["variants"]:
            tag = variant["tag"]
            method = self.c.bindings["events"][tag].get("name", tag[:1].lower() + tag[1:])
            fields = self.fields(variant)
            native_signature = ", ".join(self.signal_type(f) + f["argument"] for f in fields)
            qml_signature = ", ".join(self.signal_type(f, True) + f["argument"] for f in fields)
            h += [f"    void {method}({native_signature});"]
            qml_signals += [f"    void {method}({qml_signature});"]
            forwarding += [f"    connect(source, &NiriIpcEvents::{method}, this, &NiriIpcEvents::{method});"]
            publishing += [f"    case NiriEvent::{tag}: {{", f"        const auto &payload = std::get<NiriEvent::{tag}Value>(event.value).payload;"]
            native_args, qml_args = [], []
            for field in fields:
                value = "payload." + field["name"]
                if field["tupleBinding"]:
                    binding = field["tupleBinding"]
                    local = "native_" + field["name"]
                    publishing += [f"        QList<{binding['cppName']}> {local};", f"        for (const auto &tuple : {value}) {{", f"            {binding['cppName']} item;"]
                    publishing += [f"            item.{member} = std::get<{i}>(tuple);" for i, member in enumerate(binding["members"])]
                    publishing += [f"            {local}.append(item);", "        }"]
                    value = local
                native_args.append(value)
                ir = field["type"]
                if ir["kind"] in {"optional", "array"}:
                    value = "NiriIpc::qmlValue(" + value + ")" + (".toList()" if ir["kind"] == "array" else "")
                elif ir["kind"] == "integer" and ir["format"] in {"uint", "uint64"}:
                    value = "QString::number(" + value + ")"
                qml_args.append(value)
            publishing += [f"        emit m_native.{method}({', '.join(native_args)});", "        if (!current()) return;", f"        emit {method}({', '.join(qml_args)});", "        return;", "    }"]
            rows.append({"variant": tag, "signal": method, "arguments": [{"wire": f["wire"], "name": f["argument"], "type": f["type"]} for f in fields]})
        h += ["};", "", "class NiriIpcEvents : public QObject", "{", "    Q_OBJECT", "public:", "    using QObject::QObject;", "    NiriNativeEvents *nativeEvents() { return &m_native; }", "    void forwardEventSignals(NiriIpcEvents *source);", "    void publishEvent(const NiriEvent &event, const std::function<bool()> &current);", "signals:", *qml_signals, "private:", "    NiriNativeEvents m_native;", "};", "// clang-format on", ""]
        cpp += ["void NiriIpcEvents::forwardEventSignals(NiriIpcEvents *source)", "{", *forwarding, "}", "", "void NiriIpcEvents::publishEvent(const NiriEvent &event, const std::function<bool()> &current)", "{", "    if (!current()) return;", "    switch (event.kind()) {", *publishing, "    default: return;", "    }", "}", "// clang-format on", ""]
        return {"generated/niriipcevents_generated.h": "\n".join(h), "generated/niriipcevents_generated.cpp": "\n".join(cpp), "generated/ipc-events.json": json.dumps(rows, indent=2, sort_keys=True) + "\n"}


class ReferenceEmitter(EventEmitter):
    def qml_type(self, ir):
        kind = ir["kind"]
        if kind == "ref":
            name = self.c.name(ir["name"])
            return self.c.bindings["types"].get(ir["name"], {}).get("qmlName", name[:1].lower() + name[1:])
        if kind == "optional": return "null / " + self.qml_type(ir["item"])
        if kind == "integer": return "string" if ir["format"] in {"uint", "uint64", "int", "int64"} else "number"
        if kind == "number": return "real"
        if kind == "array": return "list<" + self.qml_type(ir["item"]) + ">"
        if kind == "tuple": return "[" + ", ".join(self.qml_type(v) for v in ir["items"]) + "]"
        if kind == "map": return "map<string, " + self.qml_type(ir["item"]) + ">"
        if kind == "boolean": return "bool"
        return kind

    def shape(self, ir):
        kind = ir["kind"]
        if kind == "ref": return "<" + ir["name"] + ">"
        if kind == "optional": return "null / " + self.shape(ir["item"])
        if kind == "integer": return "<" + ir["format"] + ">"
        if kind == "number": return "<finite " + ir["format"] + ">"
        if kind == "record": return "{" + ",".join(json.dumps(f["wire"]) + ":" + self.shape(f["type"]) for f in ir["fields"]) + "}"
        if kind == "array": return "[" + self.shape(ir["item"]) + (f" x {ir['max']}" if ir["max"] is not None else ", ...") + "]"
        if kind == "tuple": return "[" + ",".join(self.shape(v) for v in ir["items"]) + "]"
        if kind == "map": return '{"<arbitrary string>":' + self.shape(ir["item"]) + ", ...}"
        return "<" + kind + ">"

    def variant_shape(self, variant):
        return json.dumps(variant["tag"]) if variant["payload"] is None else "{" + json.dumps(variant["tag"]) + ":" + self.shape(variant["payload"]) + "}"

    def emit(self):
        docs = ["# Generated IPC API", "", self.header("<!--").replace("\n", " -->\n"),
                "Pinned to niri v26.04 with explicitly selected newer extensions listed below.",
                "All literal keys, tags and container shapes below are",
                "wire-exact. Angle-bracket values are type placeholders, NOT wire strings;",
                "`null / T` means null or T. Named placeholders refer to the value sections.",
                "Typed encoding emits every known field, including explicit null for absent",
                "options. Received optional record fields may be omitted. Required fields never",
                "receive CLI-derived defaults. Typed outbound values reject unknown fields and",
                "variants; received unknown data is retained. Raw access remains unrestricted.",
                "", "QML reads u64/usize/i64 as decimal strings and absent options as real null.",
                "Typed inputs also accept exact integral Number values, but cannot recover an",
                "integer already rounded by JavaScript. Use strings for arbitrary caller IDs.",
                "u32 and smaller integers remain numeric, with checked native width/range.",
                "Enums/unions expose kind, known, valid, wireName/tag and raw; inactive payloads",
                "are null. Invalid construction exposes valid=false and an error, never a",
                "default success. Records expose read-only fields and retained extensions.",
                "", "## Actions", "", f"All {len(self.c.ir['Action']['variants'])} methods return retainable NiriHandledReply through the one request",
                "path. Optional positions before required arguments need explicit null to skip;",
                "trailing optional positions may be omitted. Required bools remain required.",
                "Union arguments accept NiriArgs values or validated plain wire/camelCase objects.",
                "", "| # | QML call | Native arguments | Exact wire shape |", "|---|---|---|---|"]
        action_emitter = ActionEmitter(self.c, self.hashes)
        for i, variant in enumerate(self.c.ir["Action"]["variants"], 1):
            fields = action_emitter.action_fields(variant)
            method = self.c.bindings.get("actions", {}).get(variant["tag"], {}).get("name", variant["tag"][:1].lower() + variant["tag"][1:])
            sig = ", ".join(f["name"] + ": " + self.qml_type(f["type"]) for f in fields)
            native = ", ".join(self.cpp(f["type"]) + " " + f["name"] for f in fields)
            docs.append(f'| {i} | `NiriActions.{method}({sig}) -> NiriHandledReply` | `{native or "none"}` | `{{"Action":{self.variant_shape(variant)}}}` |')
        docs += ["", "## Requests", "", "All 16 requests accept trailing `options = {}` with timeoutMs/connectTimeoutMs.",
                 "Picks default timeoutMs=-1 (no response deadline); explicit overrides apply.",
                 "eventStream() observes the session's actual acknowledgement and leaves the",
                 "stream open. It never opens a one-shot socket, and late calls reuse the current",
                 "actual acknowledgement. Cancelling an observer does not stop the shared stream.",
                 "returnError() expects Err and reports it through the common error contract;",
                 "unexpected Ok is Decode. All completion is queued and replies are retainable.",
                 "", "| # | QML call | Reply / expected response | Wire shape | Pairing source |", "|---|---|---|---|---|"]
        request_emitter = RequestEmitter(self.c, self.hashes)
        for i, variant in enumerate(self.c.ir["Request"]["variants"], 1):
            binding = self.c.bindings["requests"][variant["tag"]]
            method = binding.get("name", variant["tag"][:1].lower() + variant["tag"][1:])
            sig = ", ".join(f["name"] + ": " + self.qml_type(f["type"]) for f in request_emitter.fields(variant))
            reply = "NiriPendingReply" if binding["response"] == "Err" else "Niri" + binding["response"] + "Reply"
            docs.append(f'| {i} | `NiriRequests.{method}({sig}{", " if sig else ""}options = {{}})` | `{reply}` / {binding["response"]} | `{self.variant_shape(variant)}` | `{binding["source"]}` |')
        docs += ["", "## Typed replies", "", "Each of the 14 response kinds has an uncreatable named pending-reply subclass.",
                 "`result` is read-only, notified with finished; native result() returns the",
                 "canonical C++ value. Success requires the expected discriminator and complete",
                 "validated payload. Decode failures retain the raw envelope and fieldPath.",
                 'Every reply envelope is exactly `{"Ok":<Response>}` or `{"Err":<string>}`.',
                 "Inherited value is the QML convenience projection; Handled remains the string",
                 '"Handled", while result is a niriHandled unit gadget with wireName="Handled".',
                 "", "| # | Reply | Native result() | QML result | Exact Ok response |", "|---|---|---|---|---|"]
        for i, variant in enumerate(self.c.ir["Response"]["variants"], 1):
            payload = variant["payload"]
            docs.append(f'| {i} | `Niri{variant["tag"]}Reply` | `{self.cpp(payload) if payload else "NiriHandled"}` | `{self.qml_type(payload) if payload else "niriHandled"}` | `{{"Ok":{self.variant_shape(variant)}}}` |')
        docs += ["", "## Events", "", f"The {len(self.c.ir['Event']['variants'])} native and QML signals below publish the SAME decoded event after the",
                  "session's canonical reducer/model updates. Generated buses do not parse or cache.",
                 "Native C++ uses NiriEvents::nativeEvents() (not invokable from QML). The QML",
                 "singleton inherits the projected signals. An epoch change during a native",
                 "callback suppresses the stale QML/raw continuation.",
                 "", "| # | QML signal | Native C++ signal | Exact event wire shape |", "|---|---|---|---|"]
        for i, variant in enumerate(self.c.ir["Event"]["variants"], 1):
            fields = self.fields(variant)
            method = self.c.bindings["events"][variant["tag"]].get("name", variant["tag"][:1].lower() + variant["tag"][1:])
            qml = ", ".join(f["argument"] + ": " + ("list<niriWindowLayoutChange>" if f["tupleBinding"] else self.qml_type(f["type"])) for f in fields)
            native = ", ".join(self.signal_type(f) + f["argument"] for f in fields)
            docs.append(f'| {i} | `{method}({qml})` | `{method}({native})` | `{self.variant_shape(variant)}` |')
        docs += ["", "Known event bodies must be objects before any field lookup. Malformed known",
                 "events emit decodeError(path, message, payload) then rawEvent(name, payload),",
                 "with no default typed signal or state update. Unknown variants emit",
                 "unknownEvent(name, payload) then rawEvent. rawEvent is retained for ALL events.",
                 "Its payload is QVariant/var, not QVariantMap: object bodies remain objects,",
                 "malformed scalar/array/null bodies remain intact, and future unit variants",
                 "retain their original string. Raw integers use the lossless raw projection.",
                 "", "## NiriArgs factories", "", "Factories take one checked input value for a record or variant payload; true",
                 "unit variants take no argument. Wire and camelCase field spellings are accepted",
                 "on input, but not both for one member. Output always uses the literal wire name.",
                 "", "| # | QML factory | Successful type | Exact wire value |", "|---|---|---|---|"]
        index = 0
        for name in self.ordered:
            ir = self.types[name]
            if ir["kind"] == "record":
                entries = [(name[:1].lower() + name[1:], "input", self.shape(ir))]
            else:
                variants = ir.get("variants", [{"tag": v, "payload": None} for v in ir.get("values", [])])
                entries = [(name[:1].lower() + name[1:] + self.alias(name, v["tag"]), "input" if v["payload"] is not None else "", self.variant_shape(v)) for v in variants]
            for method, args, wire in entries:
                index += 1
                docs.append(f'| {index} | `NiriArgs.{method}({args})` | `{self.qml_type({"kind":"ref", "name":name})}` | `{wire}` |')
        docs += ["", "## Records and variant payloads", "", "Every wire record and generated variant-payload record is listed below. Native",
                 "fields are canonical; QML properties bridge wide integers, optionals and",
                 "collections. All modeled fields and retained extensions participate in equality.",
                 "True unit variants are strings, not empty objects. Empty struct variants are",
                 "objects with an empty object body. See the factory table for each union tag."]
        records = []
        for name in self.ordered:
            ir = self.types[name]
            if ir["kind"] == "record":
                records.append((self.c.name(name), self.qml_type({"kind":"ref", "name":name}), ir))
            for variant in ir.get("variants", []):
                if variant["payload"] and variant["payload"]["kind"] == "record":
                    cpp = self.variant_record_name(name, variant["tag"])
                    records.append((cpp, cpp[:1].lower() + cpp[1:], variant["payload"]))
        for cpp, qml, ir in sorted(records):
            docs += ["", f"### {cpp} ({qml})", "", "Wire shape: `" + self.shape(ir) + "`.", "", "| # | Wire field | QML property/type | Native field/type | Required on receive |", "|---|---|---|---|---|"]
            for i, field in enumerate(ir["fields"], 1):
                docs.append(f'| {i} | `{field["wire"]}` | `{field["name"]}: {self.qml_type(field["type"])}` | `{self.cpp(field["type"])} {field["name"]}` | {"yes" if field["required"] else "no"} |')
            if not ir["fields"]:
                docs.append("| 1 | none | none | none | object body still required |")
            docs.append("\nRead-only `extensions` retains unknown received fields; outbound typed construction rejects them.")
        docs += ["", "## Tuple, enum and union values", "", "| # | Type | Native representation | QML / wire contract |", "|---|---|---|---|"]
        index = 0
        for name in self.ordered:
            ir = self.types[name]
            if ir["kind"] == "record": continue
            index += 1
            tags = [v["tag"] for v in ir.get("variants", [])] or ir.get("values", [])
            docs.append(f'| {index} | `{self.c.name(name)}` / `{self.qml_type({"kind":"ref", "name":name})}` | {"std::variant payloads" if any(v["payload"] is not None for v in ir.get("variants", [])) else "enum string wrapper"} | {", ".join("`" + self.alias(name, tag) + "`=" + "`" + tag + "`" for tag in tags)}; Unknown retains raw received value; Invalid is never successful construction |')
        for binding in self.c.tuples.values():
            index += 1
            members = ", ".join(member + ": " + self.qml_type(ir) for member, ir in zip(binding["members"], binding["type"]["items"]))
            docs.append(f'| {index} | `{binding["cppName"]}` / `{binding["qmlName"]}` | named typed tuple members | {members}; exact wire `{self.shape(binding["type"])}` |')
        docs += ["", "Local value `niriHandled` exposes wireName=Handled and represents wire unit",
                 '`"Handled"`. `niriInvalidArgument` exposes valid=false/error and has no valid',
                 "wire encoding. Native optional values use std::optional, arrays use std::array,",
                 "variable lists use QList, heterogeneous tuples use std::tuple, and maps use",
                 "QMap<QString,T>. Named layout-change entries project tuple IDs to strings.", ""]
        if self.c.extensions:
            docs += ["", "## Selected post-v26.04 surface", "",
                     f"Pinned main commit `{self.c.extensions['commit']}`. Each selection below has",
                     "`introducedAfter: v26.04`; it is not a v26.04 requirement. New output fields",
                     "are null when omitted. The released required fields/encoding do not acquire",
                     "main's serde defaults. Sending newer actions to older niri may return Err.", "",
                     "Touchpad state is null until TouchpadToggled is received, then reflects only",
                     "the runtime toggle, not hardware/config disablement. Main replicates it AFTER",
                     "CastsChanged; initialStateReceived still describes the released six-part burst,",
                     "not availability of this optional extension. touchpadOnChanged signals availability.", "",
                     "On a newer compositor, initialStateReceived can become true one event before",
                     "touchpadOn leaves null. Consumers needing touchpad state bind to touchpadOn;",
                     "null means unknown. Revisit the readiness terminator when adopting a newer",
                     "supported release baseline, not when selecting optional main extensions.", "",
                     "| # | Selected schema node | Introduced after | Decision |", "|---|---|---|---|"]
            for i, entry in enumerate(self.c.extensions["additions"], 1):
                docs.append(f"| {i} | `{entry['path']}` | `{entry['introducedAfter']}` | {entry['decision']} |")
        inventory = [self.header(), "#pragma once", "", "#include <array>", "", "// clang-format off", "namespace NiriIpcInventory {"]
        for group in ROOT_GROUPS:
            tags = [v["tag"] for v in self.c.ir[group]["variants"]]
            inventory += [f"inline constexpr std::array<const char *, {len(tags)}> {group} = {{", *["    " + json.dumps(tag) + "," for tag in tags], "};"]
        for name, group, section in (("ActionMethods", "Action", "actions"), ("RequestMethods", "Request", "requests"), ("EventSignals", "Event", "events")):
            methods = [self.c.bindings.get(section, {}).get(v["tag"], {}).get("name", v["tag"][:1].lower() + v["tag"][1:]) for v in self.c.ir[group]["variants"]]
            inventory += [f"inline constexpr std::array<const char *, {len(methods)}> {name} = {{", *["    " + json.dumps(method) + "," for method in methods], "};"]
        inventory += ["} // namespace NiriIpcInventory", "// clang-format on", ""]
        return {"docs/generated/IPC-API.md": "\n".join(docs), "generated/niriipcinventory_generated.h": "\n".join(inventory)}


class GuideEmitter(ReferenceEmitter):
    """Checked navigation and provenance, derived from the same inventory IR."""

    def guides(self, inventory, released):
        counts = {name: len(inventory["types"][name]["variants"]) for name in ROOT_GROUPS}
        base_counts = {name: len(released["$defs"][name]["oneOf"]) for name in ROOT_GROUPS}
        mapping = ["# Feature mapping", "", self.header("<!--").replace("\n", " -->\n"),
                   "Generated from schema/ipc-inventory.json's normalized model. Do not edit.",
                   "The [generated reference](docs/generated/IPC-API.md) supplies every exact",
                   "wire shape, native/QML signature, field constraint and factory. Raw access",
                   "remains available for all protocol values, including unknown future data.",
                   "This inventory describes API availability, not proof of every action effect.",
                   "", "| # | Root | Released v26.04 | With selected newer additions |",
                   "|---|---|---|---|"]
        for i, name in enumerate(ROOT_GROUPS, 1):
            mapping.append(f"| {i} | {name} | {base_counts[name]} | {counts[name]} |")
        mapping += ["", "Query replies are snapshots and never seed state. Output/layer metadata",
                    "are query-only, not invented stream parts. Current-state projections retain",
                    "windows, workspaces, optional layouts, overview, config and casts, plus optional",
                    "newer touchpad availability. ScreenshotCaptured is transient; EventLog is",
                    "bounded history rather than another reducer. Unknown/malformed data remains raw.",
                    "See [authored contracts](API.md) for readiness, selectors and notification rules.",
                    "The schema Reply alias Result_of_Response_or_string maps the Ok/Err envelope",
                    "to NiriPendingReply.document/json, not a fictional Request/Response value gadget."]
        sections = {"Action": ("actions", "NiriActions"), "Request": ("requests", "NiriRequests"),
                    "Event": ("events", "NiriEvents"), "OutputAction": ("niriargs-factories", "NiriArgs"),
                    "Response": ("typed-replies", "")}
        for group in ROOT_GROUPS:
            section, facade = sections[group]
            mapping += ["", f"## {group}", "", "| # | Wire variant / fields | QML entry | Native API | Availability / reference |", "|---|---|---|---|---|"]
            for i, variant in enumerate(inventory["types"][group]["variants"], 1):
                tag, payload = variant["tag"], variant["payload"]
                fields = (", ".join(f["wire"] for f in payload["fields"]) or "empty object") if payload and payload["kind"] == "record" else ("value" if payload else "unit")
                if group in {"Action", "Request", "Event"}:
                    method = self.c.bindings.get(section, {}).get(tag, {}).get("name", tag[:1].lower() + tag[1:])
                    qml = facade + "." + method
                    native = ("NiriNativeEvents::" if group == "Event" else facade + "::") + method
                elif group == "Response":
                    qml, native = "Niri" + tag + "Reply.result", "Niri" + tag + "Reply::result()"
                else:
                    qml = "NiriArgs.outputAction" + self.alias(group, tag) + " -> NiriRequests.output"
                    native = "NiriOutputAction::from" + self.alias(group, tag)
                availability = "post-v26.04" if variant.get("introducedAfter") else "v26.04"
                mapping.append(f"| {i} | `{tag}`: {fields} | `{qml}` | `{native}` | {availability}; [reference](docs/generated/IPC-API.md#{section}) |")
        mapping += ["", "## Supporting values and fields", "", "Every field below maps to a generated read-only QML property and canonical",
                    "native field; record/variant payload details and enum alternatives are in the",
                    "linked reference. Wide integers use QML strings; optional fields use null.",
                    "", "| # | Wire type | Native / QML value | Fields / alternatives | Reference |", "|---|---|---|---|---|"]
        index = 0
        for name, ir in inventory["types"].items():
            if name in {"Request", "Response", "Result_of_Response_or_string"}:
                continue  # These roots export methods/replies, not fictional value gadgets.
            index += 1
            if ir["kind"] == "record":
                entries = [f['wire'] + " -> " + f['name'] for f in ir["fields"]]
            else:
                entries = [v["tag"] for v in ir.get("variants", [])] or ir.get("values", [])
            section = "records-and-variant-payloads" if ir["kind"] == "record" else "tuple-enum-and-union-values"
            mapping.append(f"| {index} | `{name}` | `{ir['cppName']}` / `{self.qml_type({'kind': 'ref', 'name': name})}` | {', '.join(entries)} | [reference](docs/generated/IPC-API.md#{section}) |")
        mapping += ["", "Tuple members and every inline variant field are exhaustively listed in the",
                    "[record/tuple reference](docs/generated/IPC-API.md#records-and-variant-payloads).",
                    "[Selected newer nodes](docs/generated/IPC-API.md#selected-post-v2604-surface)",
                    "carry introducedAfter metadata; they do not relax released required fields.",
                    "", "## Verification links", "", "| # | Surface | Consumer check |", "|---|---|---|",
                    "| 1 | Requests/actions/output controls | tests/test_generated_requests.cpp, tests/test_generated_actions.cpp; independent serde socket bytes |",
                    "| 2 | Replies/events/records | tests/test_codec.cpp, tests/test_generated_events.cpp; native and real QML values |",
                    "| 3 | Exported surface | tests/test_ipc_inventory.cpp; actual Qt metaobjects vs inventory |",
                    "| 4 | Retained state/projections | tests/test_state.cpp, tests/test_state_projections.cpp, tests/test_projection_reset.cpp |",
                    "| 5 | Live outcomes | tests/test_live_acceptance.cpp; representative effects, not every destructive action |",
                    "| 6 | Mapping/schema/reference drift | tools/generate-ipc.py --check, tests/test_generator.py |", ""]
        schema = ["# IPC schema and checked inventory", "", self.header("<!--").replace("\n", " -->\n"),
                  "Generated by tools/generate-ipc.py from the same inventory as the typed API.",
                  "", "| # | Input | Provenance | SHA256 |", "|---|---|---|---|",
                  f"| 1 | `{SCHEMA}` | {inventory['source']['release']} / `{inventory['source']['commit']}` | `{self.hashes['schema']}` |",
                  f"| 2 | `{NEWER_SCHEMA}` | selected main `{self.c.extensions['commit']}` | `{self.hashes['newer_schema']}` |",
                  f"| 3 | `{EXTENSIONS}` | explicit additive selections, introducedAfter:v26.04 | `{self.hashes['extensions']}` |",
                  f"| 4 | `{BINDINGS}` | names/order/aliases and cited request-response pairings only | `{self.hashes['bindings']}` |",
                  "", "Dialect: JSON Schema Draft 2020-12, canonical deserialize contract.",
                  "The dumper pins schemars/serde_json and emits provenance in x-niri-source.",
                  "The synthetic six-root schema index is NOT an IPC Request wrapper.",
                  "", "## Actual protocol examples", "", "Unix socket selected from NIRI_SOCKET or explicit endpoint. One JSON document",
                  "per LF-delimited frame. Requests can be bare strings or tagged objects:",
                  "", "```json", '"Version"', '{"Action":{"MoveWindowToWorkspace":{"window_id":123,"reference":{"Id":456},"focus":false}}}',
                  '{"Output":{"output":"DP-1","action":"Off"}}', '{"Ok":"Handled"}', '{"Ok":{"Version":"26.04"}}', '{"Err":"example server error"}',
                  '{"WindowFocusChanged":{"id":null}}', "```", "", "Each line above is a separate example, not one combined document. There is",
                  "no extra Request key. Handled is a string; optional values are real null.",
                  "Wire u64 values are JSON numbers, while typed QML IDs are decimal strings.",
                  "Required released fields stay required; CLI/newer serde defaults are not inferred.",
                  "", "## Coverage", "", "| # | Root | Released | Selected surface |", "|---|---|---|---|"]
        for i, name in enumerate(ROOT_GROUPS, 1):
            schema.append(f"| {i} | {name} | {base_counts[name]} | {counts[name]} |")
        schema += ["", "[FeatureMapping](../FeatureMapping.md) and the [complete generated reference](generated/IPC-API.md)",
                   "include every field/variant, QML/native name and optional newer selection.",
                   "schema/ipc-inventory.json is the machine-readable normalized model. Normal",
                   "builds compile committed C++ and need no Rust or schema generation.",
                   "", "## Reproduction and release updates", "", "Run only in the named project guest/container from [DESIGN.md](../DESIGN.md#per-release-maintenance-procedure):",
                   "DigitalOcean niriqml-full-ipc-api, localhost/niriqml-dev:latest, /w/src,",
                   "TMPDIR=/w/tmp, CARGO_HOME=/w/cargo. The host guest helper transports commands.",
                   "", "```sh", "export CARGO_TARGET_DIR=/w/build/ipc-schema-dump",
                   "cargo run --locked --manifest-path tools/ipc-schema-dump/Cargo.toml > /w/tmp/schema.json",
                   f"cmp /w/tmp/schema.json {SCHEMA}", "python3 tools/generate-ipc.py", "python3 tools/generate-ipc.py --check", "```", "",
                   "Generation is stdlib-only, deterministic and network-free; --check diffs in",
                   "memory and never rewrites stale files. It checks C++, manifests, inventory,",
                   "this file, FeatureMapping and IPC-API together. Review all diffs, do not hand-edit.",
                   "For each release: dump twice, compare deserialize/serialize, review all schema",
                   "and extension changes, re-read state.rs/server.rs replication/ack order and",
                   "readiness, refresh independent native serde vectors, run full non-live/QML/lint",
                   "and owned live suites plus installed-consumer proof. Follow the exact procedure",
                   "in DESIGN.md and [dumper README](../tools/ipc-schema-dump/README.md).", ""]
        return {"FeatureMapping.md": "\n".join(mapping), "docs/SCHEMA.md": "\n".join(schema)}


def outputs(root=ROOT):
    schema, bindings = load(root / SCHEMA), load(root / BINDINGS)
    extensions = load(root / EXTENSIONS)
    schema = merge_extensions(schema, load(root / extensions["schema"]), extensions)
    compiler = Compiler(schema, bindings, extensions)
    hashes = {"schema": hashlib.sha256((root / SCHEMA).read_bytes()).hexdigest(),
              "newer_schema": hashlib.sha256((root / NEWER_SCHEMA).read_bytes()).hexdigest(),
              "extensions": hashlib.sha256((root / EXTENSIONS).read_bytes()).hexdigest(),
              "bindings": hashlib.sha256((root / BINDINGS).read_bytes()).hexdigest(),
              "generator": hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest()}
    inventory = compiler.inventory()
    inventory["generated"] = {"warning": "Generated by tools/generate-ipc.py; do not edit.", "sha256": hashes}
    result = {"schema/ipc-inventory.json": json.dumps(inventory, indent=2, sort_keys=True) + "\n"}
    result.update(CppEmitter(compiler, hashes).emit())
    result.update(ActionEmitter(compiler, hashes).emit())
    result.update(ReplyEmitter(compiler, hashes).emit())
    request_outputs = RequestEmitter(compiler, hashes).emit()
    result["docs/generated/IPC-API.md"] += request_outputs.pop("docs/generated/IPC-API.md")
    result.update(request_outputs)
    result.update(EventEmitter(compiler, hashes).emit())
    result.update(ReferenceEmitter(compiler, hashes).emit())
    result.update(GuideEmitter(compiler, hashes).guides(inventory, load(root / SCHEMA)))
    sources = sorted(p for p in result if p.startswith("generated/") and p.endswith((".h", ".cpp")))
    headers = [p for p in sources if p.endswith(".h")]
    result["generated/ipc-generated.cmake"] = CppEmitter(compiler, hashes).header("#") + "".join(
        f"set({variable}\n" + "".join(f"    {p}\n" for p in paths) + ")\n"
        for variable, paths in (("NIRIQML_GENERATED_SOURCES", sources), ("NIRIQML_GENERATED_HEADERS", headers)))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="diff in memory; never write")
    args = parser.parse_args()
    try:
        generated = outputs()
    except (SchemaError, KeyError, TypeError, ValueError) as error:
        print(f"generate-ipc: {error}", file=sys.stderr)
        return 2
    stale = False
    expected_paths = {ROOT / name for name in generated}
    extras = set((ROOT / "generated").glob("*_generated.*")) - expected_paths
    if extras:
        print("Unexpected generated files: " + ", ".join(str(p.relative_to(ROOT)) for p in sorted(extras)), file=sys.stderr)
        stale = True
    for name, text in sorted(generated.items()):
        path = ROOT / name
        if args.check:
            old = path.read_text() if path.exists() else ""
            if old != text:
                stale = True
                sys.stdout.writelines(difflib.unified_diff(old.splitlines(True), text.splitlines(True), fromfile=name, tofile=name + " (regenerated)"))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
