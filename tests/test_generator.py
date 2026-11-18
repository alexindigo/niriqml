"""Maintenance compiler contracts. Execute only inside the assigned test guest."""

import importlib.util
import copy
import json
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("generator", ROOT / "tools/generate-ipc.py")
GEN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GEN)


class GeneratorTest(unittest.TestCase):
    def setUp(self):
        self.schema = GEN.load(ROOT / GEN.SCHEMA)
        self.bindings = GEN.load(ROOT / GEN.BINDINGS)
        self.bindings["types"].pop("MaxBpc", None)
        self.bindings["events"].pop("TouchpadToggled", None)

    def compiler(self):
        return GEN.Compiler(self.schema, self.bindings)

    def test_release_inventory(self):
        compiler = self.compiler()
        for name, count in GEN.ROOT_GROUPS.items():
            self.assertEqual(len(compiler.ir[name]["variants"]), count)
        records = {"Overview", "PickedColor", "ConfiguredMode", "ConfiguredPosition", "VrrToSet", "Output", "Mode", "LogicalOutput", "Window", "Timestamp", "WindowLayout", "Workspace", "KeyboardLayouts", "LayerSurface", "Cast"}
        self.assertEqual({n for n, ir in compiler.ir.items() if ir["kind"] == "record"}, records)
        self.assertEqual(compiler.ir["Window"]["fields"][0]["name"], "appId")
        self.assertIn("reply", compiler.inventory()["roots"])

    def test_supported_shapes(self):
        compiler = self.compiler()
        normalize = lambda value: compiler.normalize(value, "test")
        self.assertEqual(normalize({"type": ["string", "null"]}), normalize({"anyOf": [{"type": "string"}, {"type": "null"}]}))
        self.assertEqual(normalize({"type": "integer", "format": "uint64", "minimum": 0})["format"], "uint64")
        self.assertEqual(normalize({"type": "integer", "format": "uint", "minimum": 0})["format"], "uint")
        self.assertEqual(normalize({"type": "array", "prefixItems": [{"type": "string"}], "minItems": 1, "maxItems": 1})["kind"], "tuple")
        self.assertEqual(normalize({"type": "object", "additionalProperties": {"$ref": "#/$defs/Window"}})["kind"], "map")
        self.assertEqual(normalize({"type": "string", "enum": ["A", "B"]})["kind"], "enum")

    def test_reject_unknown_structure(self):
        self.schema["$defs"]["Window"]["patternProperties"] = {}
        with self.assertRaisesRegex(GEN.SchemaError, "unsupported structural"):
            self.compiler()

    def test_reject_collisions(self):
        self.bindings["types"]["Window"] = {"fields": {"app_id": "title"}}
        with self.assertRaisesRegex(GEN.SchemaError, "colliding member"):
            self.compiler()
        self.bindings["types"]["Window"] = {"cppName": "NiriOutput"}
        with self.assertRaisesRegex(GEN.SchemaError, "colliding public"):
            self.compiler()

    def test_reject_manifest_wire_definitions(self):
        self.bindings["types"]["Window"] = {"properties": {"made_up": "string"}}
        with self.assertRaisesRegex(GEN.SchemaError, "invalid type binding"):
            self.compiler()

    def test_action_bindings_are_names_only(self):
        self.bindings["actions"]["MoveWindowToMonitor"]["arguments"] = ["window_id", "output"]
        with self.assertRaisesRegex(GEN.SchemaError, "field permutation"):
            self.compiler()
        self.setUp()
        self.bindings["actions"]["FocusWindow"]["fields"] = {"id": "uint64"}
        with self.assertRaisesRegex(GEN.SchemaError, "more than names/order"):
            self.compiler()

    def test_request_pairings_and_argument_names(self):
        compiler = self.compiler()
        replies = {v["tag"] for v in compiler.ir["Response"]["variants"]}
        self.assertEqual({v["response"] for v in self.bindings["requests"].values()} - {"Err"}, replies)
        self.bindings["requests"]["Windows"]["response"] = "Typo"
        with self.assertRaisesRegex(GEN.SchemaError, "invalid request/response"):
            self.compiler()
        self.setUp()
        self.bindings["requests"]["Output"]["arguments"] = ["output", "typo"]
        with self.assertRaisesRegex(GEN.SchemaError, "field permutation"):
            self.compiler()
        self.setUp()
        del self.bindings["requests"]["EventStream"]
        with self.assertRaisesRegex(GEN.SchemaError, "exactly the request inventory"):
            self.compiler()

    def test_event_names_and_arguments(self):
        compiler = self.compiler()
        event = next(v for v in compiler.ir["Event"]["variants"] if v["tag"] == "WorkspaceActiveWindowChanged")
        fields = GEN.EventEmitter(compiler, {}).fields(event)
        self.assertEqual([f["wire"] for f in fields], ["workspace_id", "active_window_id"])
        self.assertEqual([f["argument"] for f in fields], ["workspaceId", "windowId"])
        self.bindings["events"]["WindowClosed"]["fields"] = {"id": "uint64"}
        with self.assertRaisesRegex(GEN.SchemaError, "more than names/order"):
            self.compiler()

    def test_reference_completeness(self):
        compiler = self.compiler()
        emitter = GEN.ReferenceEmitter(compiler, {})
        outputs = emitter.emit()
        text = outputs["docs/generated/IPC-API.md"]
        for group in GEN.ROOT_GROUPS:
            for variant in compiler.ir[group]["variants"]:
                self.assertIn(variant["tag"], text)
        for name in emitter.ordered:
            ir = emitter.types[name]
            if ir["kind"] == "record":
                self.assertIn("### " + compiler.name(name), text)
            for variant in ir.get("variants", []):
                if variant["payload"] and variant["payload"]["kind"] == "record":
                    self.assertIn("### " + emitter.variant_record_name(name, variant["tag"]), text)
        self.assertIn("19> Event", outputs["generated/niriipcinventory_generated.h"])

    def test_reject_bad_ref_and_ambiguous_union(self):
        for ref in ("https://example.invalid/schema", "#/$defs/DoesNotExist"):
            with self.assertRaises(GEN.SchemaError):
                self.compiler().normalize({"$ref": ref}, "test")
        with self.assertRaisesRegex(GEN.SchemaError, "colliding variant"):
            self.compiler().normalize({"oneOf": [{"type": "string", "const": "A"}] * 2}, "test")

    def test_reject_duplicate_json_keys(self):
        with self.assertRaisesRegex(GEN.SchemaError, "duplicate JSON key"):
            json.loads('{"x":1,"x":2}', object_pairs_hook=GEN.unique_object)

    def test_tuple_binding_names_and_shape(self):
        compiler = self.compiler()
        entry = compiler.tuples["Event/WindowLayoutsChanged/changes/items"]
        self.assertEqual(entry["members"], ["id", "layout"])
        self.assertEqual(entry["type"]["items"][0]["format"], "uint64")
        binding = self.bindings["tuples"]["Event/WindowLayoutsChanged/changes/items"]
        binding["members"] = ["id"]
        with self.assertRaisesRegex(GEN.SchemaError, "shape mismatch"):
            self.compiler()

    def test_generated_name_collisions(self):
        self.bindings["tuples"]["Event/WindowLayoutsChanged/changes/items"]["qmlName"] = "niriWindow"
        with self.assertRaisesRegex(GEN.SchemaError, "tuple name collision"):
            self.compiler()
        self.setUp()
        self.bindings["types"]["Window"] = {"cppName": "NiriCastTargetWindowPayload"}
        with self.assertRaisesRegex(GEN.SchemaError, "colliding generated type"):
            GEN.CppEmitter(self.compiler(), {}).emit()
        self.bindings["types"]["Window"] = {"fields": {"title": "qmlId"}}
        with self.assertRaisesRegex(GEN.SchemaError, "colliding generated member"):
            GEN.CppEmitter(self.compiler(), {}).emit()

    def test_unsupported_numeric_constraints_are_not_ignored(self):
        with self.assertRaisesRegex(GEN.SchemaError, "unsupported numeric bound"):
            self.compiler().normalize({"type": "integer", "format": "uint32", "minimum": 1}, "test")

    def test_serde_vector_coverage(self):
        compiler = self.compiler()
        document = GEN.load(ROOT / "tests/fixtures/serde-v26.04/vectors.json")
        self.assertEqual(document["source"]["commit"], self.schema["x-niri-source"]["commit"])
        by_type = {}
        names = set()
        for row in document["vectors"]:
            self.assertNotIn(row["name"], names)
            names.add(row["name"])
            by_type.setdefault(row["type"], []).append(row["value"])
        types = {name: ir for name, ir in compiler.ir.items() if name not in GEN.ROOT_GROUPS and name != "Result_of_Response_or_string"}
        self.assertEqual(set(by_type), set(types))
        for name, ir in types.items():
            values = by_type[name]
            if ir["kind"] == "record":
                for field in ir["fields"]:
                    self.assertTrue(all(field["wire"] in value for value in values), (name, field))
                    if field["type"]["kind"] == "optional":
                        self.assertTrue(any(value[field["wire"]] is None for value in values), (name, field))
                        self.assertTrue(any(value[field["wire"]] is not None for value in values), (name, field))
            else:
                expected = {v["tag"] for v in ir.get("variants", [])} | set(ir.get("values", []))
                actual = {value if isinstance(value, str) else next(iter(value)) for value in values}
                self.assertEqual(actual, expected, name)

    def test_serde_surface_inventory(self):
        compiler = self.compiler()
        document = GEN.load(ROOT / "tests/fixtures/serde-v26.04/vectors.json")
        by_type = {}
        names = set()
        for row in document["surfaceVectors"]:
            self.assertNotIn(row["name"], names)
            names.add(row["name"])
            by_type.setdefault(row["type"], []).append(row["value"])
        self.assertEqual(set(by_type), set(GEN.ROOT_GROUPS))
        for name, count in GEN.ROOT_GROUPS.items():
            ir = compiler.ir[name]
            expected = {v["tag"] for v in ir["variants"]}
            values = by_type[name]
            actual = {value if isinstance(value, str) else next(iter(value)) for value in values}
            self.assertEqual(len(expected), count)
            self.assertEqual(actual, expected, name)
            for variant in ir["variants"]:
                payload = variant["payload"]
                matching = [value[variant["tag"]] for value in values if isinstance(value, dict) and variant["tag"] in value]
                if payload is None:
                    self.assertIn(variant["tag"], values)
                elif payload["kind"] == "record":
                    for field in payload["fields"]:
                        self.assertTrue(all(field["wire"] in value for value in matching), (name, variant["tag"], field))
                        if field["type"]["kind"] == "optional":
                            self.assertTrue(any(value[field["wire"]] is None for value in matching), (name, variant["tag"], field))
                            self.assertTrue(any(value[field["wire"]] is not None for value in matching), (name, variant["tag"], field))
                elif payload["kind"] == "optional":
                    self.assertIn(None, matching)
                    self.assertTrue(any(value is not None for value in matching))
        explicit = {"Action": {"MoveWindowToWorkspace": {"window_id": 123, "reference": {"Id": 456}, "focus": False}}}
        self.assertIn(explicit, by_type["Request"])
        # The independent oracle includes all OutputAction alternatives embedded
        # in actual Request::Output, not only isolated supporting values.
        embedded = [value["Output"]["action"] for value in by_type["Request"] if isinstance(value, dict) and "Output" in value]
        self.assertEqual(embedded, by_type["OutputAction"])

    def test_determinism_and_read_only_check(self):
        with tempfile.TemporaryDirectory(prefix="niriqml-generator-") as temporary:
            root = pathlib.Path(temporary)
            for name in (GEN.SCHEMA, GEN.BINDINGS, GEN.EXTENSIONS, GEN.NEWER_SCHEMA, "tools/generate-ipc.py"):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / name, path)
            command = [os.sys.executable, str(root / "tools/generate-ipc.py")]
            def run(*args, seed="1", cwd=root):
                return subprocess.run(command + list(args), cwd=cwd, env={**os.environ, "PYTHONHASHSEED": seed}, capture_output=True, timeout=30)
            first = run()
            self.assertEqual(first.returncode, 0, first.stderr)
            snapshot = {str(p.relative_to(root)): p.read_bytes() for p in root.rglob("*") if p.is_file()}
            second = run(seed="918273", cwd=root / "schema")
            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertEqual(snapshot, {str(p.relative_to(root)): p.read_bytes() for p in root.rglob("*") if p.is_file()})
            self.assertEqual(run("--check").returncode, 0)
            inventory = root / "schema/ipc-inventory.json"
            inventory.write_text("stale\n")
            stale = run("--check")
            self.assertEqual(stale.returncode, 1)
            self.assertIn(b"regenerated", stale.stdout)
            self.assertEqual(inventory.read_text(), "stale\n")
            extra = root / "generated/obsolete_generated.cpp"
            extra.parent.mkdir(exist_ok=True)
            extra.write_text("preserve\n")
            self.assertIn(b"Unexpected generated", run("--check").stderr)
            self.assertEqual(extra.read_text(), "preserve\n")

    def test_semantic_source_partitioning(self):
        generated = GEN.outputs()
        manifest = generated["generated/ipc-generated.cmake"]
        sources = {p for p in generated if p.startswith("generated/") and p.endswith(".cpp")}
        for source in sources:
            self.assertEqual(manifest.count("    " + source + "\n"), 1, source)
        self.assertNotIn("generated/niriipctypes_generated.cpp", sources)
        self.assertNotIn("generated/niriipcactions_generated.cpp", sources)
        for family in ("types", "codec", "actions", "arguments", "registration"):
            self.assertGreater(len([p for p in sources if f"niriipc{family}_" in p]), 1)
        header = generated["generated/niriipctypes_generated.h"]
        self.assertIn("static NiriAction fromCloseWindow(const NiriActionCloseWindowPayload &payload);", header)
        self.assertIn("NiriAction(const NiriAction &);", header)
        self.assertIn("extern template void qmlRegisterTypesAndRevisions<NiriAction>", header)
        self.assertNotIn("result.value =", header)
        chunks = GEN.SourceBuckets(GEN.CppEmitter(self.compiler(), {}), "test", [], limit=4)
        chunks.add(["first complete definition"])
        chunks.add(["second complete definition"])
        output = chunks.outputs()
        self.assertEqual(len(output), 2)
        self.assertIn("first complete definition", output["generated/test_001_generated.cpp"])

    def test_selected_extensions_and_released_requiredness(self):
        newer, manifest = GEN.load(ROOT / GEN.NEWER_SCHEMA), GEN.load(ROOT / GEN.EXTENSIONS)
        merged = GEN.merge_extensions(self.schema, newer, manifest)
        compiler = GEN.Compiler(merged, GEN.load(ROOT / GEN.BINDINGS), manifest)
        self.assertEqual({name: len(compiler.ir[name]["variants"]) for name in GEN.ROOT_GROUPS},
                         {"Request": 16, "Action": 149, "OutputAction": 10, "Response": 14, "Event": 20})
        output = next(f for f in compiler.ir["Output"]["fields"] if f["wire"] == "max_bpc")
        self.assertEqual(output["type"]["kind"], "optional")
        self.assertFalse(output["required"])
        self.assertEqual(output["introducedAfter"], "v26.04")
        self.assertEqual(compiler.ir["MaxBpc"]["introducedAfter"], "v26.04")
        for name in self.schema["$defs"]:
            # All shared required sets survive exactly, despite main's defaults.
            def compare(a, b):
                if isinstance(a, dict):
                    self.assertEqual(a.get("required"), b.get("required"))
                    if "oneOf" in a:
                        for branch in a["oneOf"]:
                            tag = branch.get("const") or next(iter(branch["properties"]))
                            other = next(v for v in b["oneOf"] if (v.get("const") or next(iter(v.get("properties", {})), None)) == tag)
                            compare(branch, other)
                    for key, value in a.items():
                        if key not in {"oneOf", "required"} and isinstance(value, dict):
                            compare(value, b[key])
            compare(self.schema["$defs"][name], merged["$defs"][name])
        for entry in manifest["additions"]:
            self.assertIn(entry["path"], GEN.ReferenceEmitter(compiler, {}).emit()["docs/generated/IPC-API.md"])
        self.assertIn("true one event before", GEN.ReferenceEmitter(compiler, {}).emit()["docs/generated/IPC-API.md"])

    def test_extension_merge_rejects_unreviewed_drift(self):
        newer, manifest = GEN.load(ROOT / GEN.NEWER_SCHEMA), GEN.load(ROOT / GEN.EXTENSIONS)
        for mutation, message in [
            (lambda n, m: n["$defs"]["Output"]["properties"]["name"].update(type="boolean"), "shared field type"),
            (lambda n, m: m["additions"].pop(), "unselected newer addition"),
            (lambda n, m: m["ignoredDefaults"].pop(), "unreviewed newer defaults"),
            (lambda n, m: m.update(commit="moving-main"), "provenance"),
            (lambda n, m: m["additions"].append(dict(m["additions"][0])), "duplicate extension"),
            (lambda n, m: n["$defs"]["Action"]["oneOf"].append(copy.deepcopy(n["$defs"]["Action"]["oneOf"][0])), "colliding variant"),
        ]:
            n, m = copy.deepcopy(newer), copy.deepcopy(manifest)
            mutation(n, m)
            with self.assertRaisesRegex(GEN.SchemaError, message):
                GEN.merge_extensions(self.schema, n, m)

    def test_newer_serde_selection_coverage(self):
        manifest = GEN.load(ROOT / GEN.EXTENSIONS)
        newer = GEN.load(ROOT / GEN.NEWER_SCHEMA)
        merged = GEN.merge_extensions(self.schema, newer, manifest)
        compiler = GEN.Compiler(merged, GEN.load(ROOT / GEN.BINDINGS), manifest)
        document = GEN.load(ROOT / "tests/fixtures/serde-main-689c1e9/vectors.json")
        self.assertEqual(document["source"]["commit"], manifest["commit"])
        rows = document["vectors"]
        for selection in manifest["additions"]:
            parts = selection["path"].split("/")
            name = parts[2]
            values = [row["value"] for row in rows if row["type"] == name]
            self.assertTrue(values, name)
            if len(parts) == 3: # Complete new supporting type.
                variants = compiler.ir[name].get("variants", [])
                self.assertEqual(set(values), {v["tag"] for v in variants})
            elif parts[3] == "variants":
                tag = parts[4]
                self.assertTrue(any(value == tag or isinstance(value, dict) and tag in value for value in values), selection)
            else:
                field = parts[4]
                self.assertTrue(any(value[field] is None for value in values))
                self.assertTrue(any(value[field] is not None for value in values))


if __name__ == "__main__":
    unittest.main()
