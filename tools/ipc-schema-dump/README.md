# niri IPC schema extraction

Maintenance-only tool for niri v26.04, commit
`8ed0da44d974c32c6877d2f4630c314da0717ecb`. Normal niriqml builds use
committed schema/generated C++ and need neither Rust nor niri sources.

The independent crate pins niri-ipc's git revision, schemars 1.2.1 and
serde_json 1.0.149. Commit its Cargo.lock; always use `--locked` after the
initial lockfile generation in the assigned build guest.

## Extract and compare

Run only inside the assigned project guest, from the repository root, with
`TMPDIR`, `CARGO_HOME` and `CARGO_TARGET_DIR` on its disk-backed workspace.
For the full-ipc-api guest these are `/w/tmp`, `/w/cargo` and
`/w/build/ipc-schema-dump` respectively:

```sh
export CARGO_TARGET_DIR=/w/build/ipc-schema-dump
cargo run --locked --manifest-path tools/ipc-schema-dump/Cargo.toml > /w/tmp/schema-first.json
cargo run --locked --manifest-path tools/ipc-schema-dump/Cargo.toml > /w/tmp/schema-second.json
cmp /w/tmp/schema-first.json /w/tmp/schema-second.json
cmp /w/tmp/schema-first.json schema/niri-ipc-v26.04.schema.json
cargo run --locked --manifest-path tools/ipc-schema-dump/Cargo.toml -- --contract serialize > /w/tmp/schema-serialize.json
cargo test --locked --manifest-path tools/ipc-schema-dump/Cargo.toml
```

The default contract is explicitly Draft 2020-12 `for_deserialize`;
`--contract deserialize` is equivalent. Compare `serialize` at every release
update, especially optional-field requiredness or asymmetric serde attributes.
Review those differences before changing generation assumptions.

One synthetic `IpcRoots` record indexes Request, Reply, Response, Event,
Action and OutputAction through its properties' actual `$ref` values. It is
not an IPC message. These roots share one transitive `$defs` closure; in
particular, Reply's alias need not produce a definition named `Reply`.

Output is pretty JSON plus one final newline on stdout; diagnostics go to
stderr. Object keys are sorted recursively, while array order is preserved.
`x-niri-source` records release, immutable commit, schemars version,
contract and extraction format version. No timestamp or local path is emitted.

For a new release, resolve and inspect the immutable upstream commit, update
the dependency pins and provenance constants together, regenerate the lockfile
in the guest, then extract twice and review the complete schema diff. Check
the named records/enums and variant inventory rather than a magic `$defs`
count. Retain older schemas needed as compatibility inputs. Do not add Rust
or network fetching to the ordinary CMake build.

## Serde reference vectors

`--vectors` constructs released Rust records and every supporting enum/union,
Action, Request, OutputAction, Response and Event alternative directly, then
serializes them with niri-ipc's own serde. It does
not use the Python generator or schema to construct expected JSON. The fixture
includes absent/present options, signed bounds, u64 max, 2^53+1 and escaping.
`vectors` holds supporting values; `surfaceVectors` holds the five protocol
inventories (16/141/9/14/19), with every OutputAction also embedded in a Request.
All optional action/event fields and nullable responses have null and present
examples. The explicit workspace-move regression uses window 123, workspace
Id 456 and focus false. Width/height include all four SizeChange alternatives.

Run maintenance only in the assigned DigitalOcean guest/container through the
project's guest helper (cwd `/w/src`, TMPDIR `/w/tmp`):

```sh
export CARGO_TARGET_DIR=/w/build/ipc-schema-dump
cargo fmt --check --manifest-path tools/ipc-schema-dump/Cargo.toml
cargo test --locked --manifest-path tools/ipc-schema-dump/Cargo.toml
cargo run --locked --manifest-path tools/ipc-schema-dump/Cargo.toml -- --vectors > /w/build/serde-vectors.json
cmp /w/build/serde-vectors.json tests/fixtures/serde-v26.04/vectors.json
```

When intentionally updating the pin, replace the tracked fixture from this
output and run `test_generator` and `test_codec`. They require complete released
type/alternative coverage and null/present examples for each optional record,
action and event field. The inventory test rejects missing or extra protocol
variants; codec tests cover supporting values, while callable/event tests consume
the protocol vectors as their generated surfaces are integrated.
Hand-written unknown/future variants are tested separately; they cannot
be serialized by the pinned Rust enum. Ordinary CMake builds do not need Rust.

## Selected newer schema

The independent `main/Cargo.toml` and lockfile use the same extraction entry
point, pinned to niri commit `689c1e914a5af782a476e433b79c15554bf5a312`.
That source requires schemars 1.2.2 and serde_json 1.0.151, separately pinned;
the released manifest and schema retain their original versions and bytes.

Inside the same assigned DigitalOcean guest/container, cwd `/w/src`:

```sh
export CARGO_TARGET_DIR=/w/build/ipc-schema-dump-main
cargo fmt --check --manifest-path tools/ipc-schema-dump/main/Cargo.toml
cargo test --locked --manifest-path tools/ipc-schema-dump/main/Cargo.toml
cargo run --locked --manifest-path tools/ipc-schema-dump/main/Cargo.toml > /w/tmp/schema-main.json
cmp /w/tmp/schema-main.json schema/niri-ipc-main-689c1e9.schema.json
cargo run --locked --manifest-path tools/ipc-schema-dump/main/Cargo.toml -- --vectors > /w/tmp/vectors-main.json
cmp /w/tmp/vectors-main.json tests/fixtures/serde-main-689c1e9/vectors.json
python3 tools/generate-ipc.py --check
```

`schema/extensions.json` selects every compatible wire addition from the
complete v26.04-to-pin lib.rs diff: eight Action variants, MaxBpc control/enum,
optional Output.max_bpc, two CastKind values and TouchpadToggled. Every selected
node carries `introducedAfter: v26.04`. Shared types/constraints must match;
all additions require an explicit selection. Reviewed main-only serde defaults
are listed separately and do not relax released required fields or encoding.
Rustdoc changes, MaxBpc CLI conversions and default helper functions add no
other wire node. PR 4711 changes existing layout-field population, not schema.

The newer vectors use native pinned Rust/serde for all additions, null/present
window IDs, every MaxBpc string and output max_bpc presence/bounds. Released
vectors remain independent and unchanged. Generated encoding still emits null
for absent modeled optionals, including max_bpc; no released field changes.

Pinned state.rs appends touchpad replication after casts. The reducer ports
its assignment semantics but retains optional availability: 26.04 sends no
touchpad event, so a fabricated false default would be wrong. Readiness keeps
its released six-part meaning at CastsChanged; `touchpadOn` is null until the
extension event, then bool, and resets to null. No version guess, extra query
or indefinite wait is introduced. Re-check replication order at each pin.
On a newer compositor, `initialStateReceived` can become true one event before
`touchpadOn` leaves null. Consumers needing touchpad state bind to `touchpadOn`;
null means unknown. Moving the readiness terminator is a supported-release
baseline upgrade decision, not an optional main-extension selection.
