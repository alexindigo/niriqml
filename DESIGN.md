# niriqml design and maintenance

## Current architecture

Raw access is permanent. Generated typed wrappers sit on the exact same request
path; no generated class opens a socket or invents a second completion policy.

```text
QML / native consumers
  -> engine-local facades -> application-owned NiriSession
  -> raw input / generated argument validation
  -> one exact WireValue encoder -> captured endpoint request operation
  <- full retained envelope <- bounded incremental line framing

one EventStream -> exact document -> one generated decode
  -> one NiriEventStreamState reducer -> ID-row model projections
  -> property notifications -> native / QML typed signals -> raw diagnostics
```

There is one enforced application thread, not a transport worker with cross-thread
engines. Facades have engine lifetime and CppOwnership. QML-created models and
trackers have ordinary QML ownership. JavaScript-owned replies/sequences are
retainable; facade teardown cancels scoped operations without deleting a retained
result. The application owns final session shutdown.

`NiriSession` owns endpoint selection independently of physical socket status.
Every attempt has an epoch; every request captures endpoint/revision/ID before
queued work. Switching endpoints retires old work, never retargets/replays it.
Any public consumer requests idempotent startup. Explicit stop is preserved
until start/connect, including when later consumers appear.

`WireValue` retains exact number lexemes and distinguishes missing from null.
The strict parser rejects duplicate keys and invalid UTF-8/numbers. One encoder
escapes values and preserves u64. Bounded cursor framing and bounded socket reads
avoid unbounded partial messages and repeated whole-buffer scans. Raw QML
projection preserves integers as strings and other numbers as exact-text values;
QJson/legacy QVariant adapters are explicitly lossy, not authoritative storage.

Request operations queue start and terminal delivery, commit terminal fields
before finished, and guard all lifetimes. Independent calls use separate sockets;
an opt-in sequence serializes on one socket without pipelining. Local failure
reports delivery certainty, not rollback. Named EventStream observers consume
the actual accepted acknowledgement of the shared stream, never a fake reply or
another subscription. Query results never seed retained state.

## One reducer, many projections

The reducer exists before subscription. A generated decoded event validates
completely before mutation and applies before public publication. It ports niri
v26.04's six parts: workspaces, windows, optional layouts, overview, config, casts.
Immutable keyed records and sparse touched before/after summaries avoid cloning
all state on every delta. Focus/name indexes live in this same owner. Unknown
fields/variants retain raw data; malformed known events publish diagnostics, not
default records. Impossible same-part sequences report protocol failure/reset;
permitted temporary dangling cross-part references are not repaired by inference.

NiriState, models, trackers and current snapshot aliases read only this owner.
Models retain ordered IDs, not record collections. Sparse before-images keep
removed rows readable inside about-to-remove callbacks. Exact ID diffs preserve
surviving persistent indexes and announce precise roles. Model projections finish
before property/event signals. Late consumers attach at the committed revision.
Trackers retain selectors plus their last published immutable entry identity,
not record copies or getter authority; one resolver defines name-over-ID
precedence. Selector changes and reducer updates share one notification path.
It compares against the last announced selection, not a fresh read of already
committed state, then reconciles current getters after every reentrant callback.
This preserves notifications when an earlier facade clears/retargets a tracker
before its own reducer-update callback. Per-field comparison values exist only
on the active stack; every getter still reads the canonical reducer.

Resets and event delivery share one outer session guard through all projections
and deferred resets. A reset inside a row bracket immediately invalidates the
transport epoch, then clears records at the safe boundary. Nested event loops
leave replacement bytes/EOF buffered; an identity-checked queued drain resumes
after the outer guard releases. This prevents stale publication and lost bursts.

Readiness is the first applied CastsChanged after accepted subscription, last in
the released initial burst. Reset clears readiness/state together. The optional
newer touchpad part follows casts: null until received, then bool, null on reset.
Readiness can become true one event before touchpad availability. It remains a
six-released-part promise, not a timeout heuristic or compositor-version guess.

EventLog is bounded history, not current state. It retains raw name/payload even
across resets; zero disables retention, negative normalizes to zero, and every
removal is announced. Examples consume the reducer through models/trackers/state
and keep only local UI selection, reply retention and query-only output metadata.

## Generated and hand-written boundary

| # | Owner | Responsibility |
|---|---|---|
| 1 | Pinned Rust maintenance dumper | Release/main schema and independent native serde vectors; never a normal build dependency |
| 2 | Committed schema + names/order bindings + explicit extension manifest | Wire facts, QML aliases/argument order, request-response pairing citations; no duplicate hand-authored wire types |
| 3 | tools/generate-ipc.py | Stdlib-only deterministic IR, exact codecs/types, constructors, callable/reply/event surfaces, source manifest, inventory and reference/mapping/schema docs |
| 4 | Hand-written transport/session | Endpoint, epochs, bounded I/O, lifetime, errors, deadlines, certainty and sequence ownership |
| 5 | Hand-written reducer/projections | Upstream behavioral semantics, readiness, indexes, selector/notification/model protocols |
| 6 | Authored API/README/REQUESTS | Contracts and runnable examples; schema cannot express lifetime, burst order or action effects |

Generated files are committed, never hand-edited. Ordinary CMake compiles them
without Rust, Python generation or network fetches (test mode needs Python for
checks). Semantic source partitions and out-of-line large union operations bound
compiler memory without altering public values. Installation uses CMake targets,
an explicit selected-Qt QML root, relative plugin-to-backing-library RUNPATH and
the exported niriqml::niriqml target, not copies of a build directory.

## Per-release maintenance procedure

All builds, dumps, generation and tests run in DigitalOcean guest
`niriqml-full-ipc-api`, Arch container `localhost/niriqml-dev:latest`, via the
project guest helper. Source `/w/src`, builds `/w/build/E8`, TMPDIR `/w/tmp`,
CARGO_HOME `/w/cargo`. If unavailable, stop; never run on the physical host.
For later release work, explicitly assign its guest/workspace before execution.

| # | Step | Required review/proof |
|---|---|---|
| 1 | Resolve immutable release commit; inspect niri-ipc lib.rs and lock versions | Update dumper pins/provenance together; keep existing baseline and newer selections distinct |
| 2 | Dump twice with locked Rust dependencies in guest | Byte-identical outputs; compare deserialize/serialize requiredness and all schema changes |
| 3 | Review explicit extension merge | Shared field types/constraints match; no unreviewed additions/default relaxations; baseline required fields stay required |
| 4 | Generate committed artifacts | Review inventory, every method/field, source manifest and docs diff; never hand-edit generated output |
| 5 | Re-read release state.rs AND src/ipc/server.rs | Acknowledgement precedes replication; check entire initial burst order, empty/optional parts, reducer semantics and readiness terminator; schema alone cannot prove them |
| 6 | Regenerate independent native serde vectors | Cover every variant/field and null/present cases; not fixtures derived from the production generator |
| 7 | Run full non-live/QML/example/snippet gates and format/lint | Exact wire requests, typed consumer values, malformed/unknown retention, early/late/reset/model outcomes |
| 8 | Run owned live suite against release | Actual query/action/reconnect/readiness outcomes, zero unexpected skips; update source-audited inferences only with real proof |
| 9 | Stage/install and consume independently | Qt-only QML import and exported C++ package with producer build unavailable; inspect RUNPATH; record Qt/toolchain/version limits |
| 10 | Update authored contracts/migration notes | Describe observed behavior, not blanket completeness or unrun minimum-Qt claims; release/version/publication is separate authorization |

Host transport only:

```sh
G=/home/user/TemporaryFiles/niriqml/plans/full-ipc-api/tools/guest
$G sync
$G run -t 1800 'export CARGO_TARGET_DIR=/w/build/ipc-schema-dump; cargo run --locked --manifest-path tools/ipc-schema-dump/Cargo.toml > /w/tmp/schema-first.json && cargo run --locked --manifest-path tools/ipc-schema-dump/Cargo.toml > /w/tmp/schema-second.json && cmp /w/tmp/schema-first.json /w/tmp/schema-second.json && cmp /w/tmp/schema-first.json schema/niri-ipc-v26.04.schema.json && cargo run --locked --manifest-path tools/ipc-schema-dump/Cargo.toml -- --vectors > /w/tmp/vectors.json && cmp /w/tmp/vectors.json tests/fixtures/serde-v26.04/vectors.json && python3 tools/generate-ipc.py --check && cmake --build /w/build/E8 -j8 && ctest --test-dir /w/build/E8 -E test_live_ --output-on-failure && ctest --test-dir /w/build/E8 -L live -V'
```

For intentional regeneration use `python3 tools/generate-ipc.py` inside that
guest, fetch outputs, review, then use --check. For the selected newer pin and
serialize comparison see the [dumper instructions](tools/ipc-schema-dump/README.md).
[docs/SCHEMA.md](docs/SCHEMA.md) records hashes and shapes;
[tests/live/README.md](tests/live/README.md) describes isolated compositor fixtures.
No atomic insertion, stable column identity, output hotplug event or animation
completion guarantee can be generated where niri provides none.
