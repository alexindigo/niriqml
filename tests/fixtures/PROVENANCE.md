# Protocol fixtures

`serde-v26.04/vectors.json` comes from native Rust values serialized by the
committed dumper's `--vectors` mode, pinned to niri v26.04 commit
`8ed0da44d974c32c6877d2f4630c314da0717ecb`. Neither the C++ generator nor its
encoder produces this independent reference data. See the dumper README for
regeneration and inventory checks.

The scripted `NiriMock` loads those response/event values without a QJson
round trip. Its explicit Windows/Workspaces controls are compatibility QJson
inputs; exact wide-ID cases use raw fixture bytes and `setResponse`.
The mock does not simulate compositor action effects or reduce events.

Initial replication order is behavior, not JSON Schema: released
`niri-ipc/src/state.rs` `EventStreamState::replicate` sends Workspaces, Windows,
optional KeyboardLayouts, Overview, Config, Casts, including empty lists.
`src/ipc/server.rs` `handle_client` writes the Ok/Handled acknowledgement first
and accepts repeated requests on a non-stream socket. Re-check both files when
updating the release pin.

Faults deliberately replace the next response independently of valid fixtures:
delay, drop, raw bytes (including Err/malformed/oversize/truncated frames),
fragment sizes and peer close. Per-client input cursors and guarded stream
registrations are tested separately from the production transport.

Live tests use only the isolated cage/niri session and foot windows created by
`tests/live/run-live.sh`. No captured user's desktop data is part of this corpus.
