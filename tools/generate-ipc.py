#!/usr/bin/env python3
"""Compile the pinned schemars subset. Python stdlib only; never run at build time."""

import argparse
import difflib
import hashlib
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCHEMA = "schema/niri-ipc-v26.04.schema.json"
BINDINGS = "schema/qml-bindings.json"
ANNOTATIONS = {"description", "title", "$schema", "$id", "x-niri-source", "default", "examples", "deprecated"}
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


class Compiler:
    def __init__(self, schema, bindings):
        self.schema = schema
        self.bindings = bindings
        if set(bindings) != {"formatVersion", "types"} or bindings["formatVersion"] != 1:
            raise SchemaError("unsupported binding manifest")
        self.defs = schema.get("$defs", {})
        self.ir = {}
        for name in sorted(self.defs):
            self.ir[name] = self.normalize(self.defs[name], f"#/$defs/{name}")
        self.root = self.normalize(schema, "#")
        self.validate_bindings()
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
                           "type": self.normalize(value, path + "/properties/" + key)} for key, value in sorted(props.items())]
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
                result = {"kind": "array", "item": self.normalize(node["items"], path + "/items"), "min": minimum, "max": maximum}
        elif kind in {"integer", "number"}:
            allowed |= {"format", "minimum", "maximum"}
            fmt = node.get("format")
            if fmt not in FORMATS or (kind == "number") != (fmt in {"double", "float"}):
                raise SchemaError(f"{path}: unsupported numeric format {fmt}")
            result = {"kind": kind, "format": fmt}
            for key in ("minimum", "maximum"):
                if key in node:
                    result[key] = node[key]
        elif kind in {"string", "boolean", "null"}:
            result = {"kind": kind}
        else:
            raise SchemaError(f"{path}: unsupported shape")
        if structural - allowed:
            raise SchemaError(f"{path}: unsupported constraints {sorted(structural - allowed)}")
        return result

    def validate_bindings(self):
        for name, binding in self.bindings["types"].items():
            if name not in self.ir or set(binding) - {"cppName", "qmlName", "variants", "fields"}:
                raise SchemaError(f"invalid type binding: {name}")
            ir = self.ir[name]
            tags = [v["tag"] for v in ir.get("variants", [])] + ir.get("values", [])
            if set(binding.get("variants", {})) - set(tags):
                raise SchemaError(f"{name}: binding names unknown variants")
            if set(binding.get("fields", {})) - {f["wire"] for f in ir.get("fields", [])}:
                raise SchemaError(f"{name}: binding names unknown fields")
        cpp_names, qml_names = set(), set()
        for name, ir in self.ir.items():
            cpp = self.name(name)
            qml = self.bindings["types"].get(name, {}).get("qmlName", cpp[:1].lower() + cpp[1:])
            identifier(cpp, name)
            identifier(qml, name)
            if cpp in cpp_names or qml in qml_names:
                raise SchemaError(f"{name}: colliding public type names")
            cpp_names.add(cpp)
            qml_names.add(qml)
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
        return {"formatVersion": 1, "source": self.schema["x-niri-source"], "roots": roots, "types": types}


def outputs(root=ROOT):
    schema, bindings = load(root / SCHEMA), load(root / BINDINGS)
    compiler = Compiler(schema, bindings)
    hashes = {"schema": hashlib.sha256((root / SCHEMA).read_bytes()).hexdigest(),
              "bindings": hashlib.sha256((root / BINDINGS).read_bytes()).hexdigest(),
              "generator": hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest()}
    inventory = compiler.inventory()
    inventory["generated"] = {"warning": "Generated by tools/generate-ipc.py; do not edit.", "sha256": hashes}
    return {"schema/ipc-inventory.json": json.dumps(inventory, indent=2, sort_keys=True) + "\n"}


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
