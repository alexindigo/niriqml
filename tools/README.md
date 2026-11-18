# IPC maintenance

Normal builds compile the committed generated C++; Rust is only a release
maintenance tool. Run all commands below in the assigned project guest, not on
the development host. The full-ipc-api guest uses `/w/src`, `/w/build` and
disk-backed TMPDIR `/w/tmp` through the project's `guest` helper.

| # | Input/output | Purpose |
|---|---|---|
| 1 | `ipc-schema-dump/` | Pinned release schema and independent serde reference vectors; see its README |
| 2 | `../schema/niri-ipc-v26.04.schema.json` | Authoritative released wire fields/types |
| 3 | `../schema/qml-bindings.json` | Names and aliases only, including semantic names for tuple members; never a duplicate wire schema |
| 4 | `generate-ipc.py` | Stdlib-only compiler: load, normalize, validate, emit and compare |
| 5 | `../generated/`, `../schema/ipc-inventory.json` | Committed outputs, including hashes of schema, manifest and generator; no timestamps |

```sh
python3 tools/generate-ipc.py
python3 tools/generate-ipc.py --check
python3 tests/test_generator.py
```

Generation resolves paths relative to the script, not the current directory.
Check mode regenerates in memory and reports unified diffs and obsolete output
files without writing. Tests cover multiple hash seeds/working directories,
malformed schemas, name collisions, inventory and independent vector coverage.

The emitted C++ schema-expansion regions carry `clang-format off/on` markers:
property lists, type-dependent signatures and dispatch expressions vary with
schema names. Keeping their layout in this stdlib compiler avoids a formatter
dependency and version-dependent output. Includes and metadata remain outside
those regions; hand-written C++ uses the normal project formatter. Never edit
generated files; fix the compiler and regenerate them in the guest.

Generated records expose read-only camelCase QML properties. Their native C++
fields use exact Rust-width integer types, std::optional and typed containers.
u64/usize values project to decimal QString, smaller integers remain numbers,
and None projects to real QML null. QML `NiriArgs` factories take checked record
objects or variant payloads, return inspectable invalid values (`valid: false`,
`error`) for unknown fields/invalid inputs, and accept exact numeric or
decimal-string wide inputs. Use strings for
arbitrary caller-owned IDs: an already-rounded JavaScript number cannot be
recovered. Native codec failures include a JSON path and never publish a
partially defaulted value.

Unit enum namespaces such as `NiriTransform.Rotate90` accompany the lower-case
value types. Unknown received enum strings and union variants retain their
original value, and received record extensions participate in equality.
Outbound typed construction remains closed to the pinned schema. Raw calls are
the escape hatch for intentionally sending future wire shapes.

`test_codec` exercises reference vectors, QML properties/construction, socket
queries/events and consumer projections. The current generated slice is the
supporting records/enums/codecs; action/request/reply/event generation is a
separate implementation item.
