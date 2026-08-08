# Future Development

## Executable README snippets

Adopt the approach from [reamde](https://github.com/alexindigo/reamde): extract
code fences from README.md and run them as tests. For niriqml this would mean:

- QML snippets in README become `qmltestrunner` test cases
- Headless-viable snippets (model instantiation, property checks) run in CI
- Live-dependent snippets (actions, queries) tagged and skipped without `NIRI_SOCKET`

This ensures documentation stays accurate — every breaking API change fails the
README-snippet test.

## Remaining niri IPC surface

- **Window-rules** — niri's `window-rule` IPC is not yet bound
- **Keybinds** — programmatic keybind management via IPC
- **Touch gestures** — gesture definition and query
- **Input** — keyboard/mouse state queries

## Connection lifecycle & diagnostics

Observed while VM-testing PeerInfo (nested niri under headless cage,
2026-08-07):

- **Event-stream reconnect churn** — confirmed 2026-08-07 as a niri-side
  bug (not niriqml). niri 26.04 logs `WARN niri::ipc::server: error
  handling IPC client: error writing reply` (Broken pipe) on every client
  disconnect. niriqml's auto-reconnect handles it seamlessly. Root cause
  is in niri's `src/ipc/server.rs` — `handle_client()` lacks the
  `BrokenPipe` guard that `handle_event_stream_client()` already has.
  Fix submitted upstream (PR #4180, mailing list patch). Impact: cosmetic
  only. See `KNOWN_ISSUE.md` for details.
- **`PeerInfo.peerSocketPath` is always empty** — SO_PEERNAME on the server
  side of an *accepted* unix connection is unnamed, so the field can never
  carry a value in practice. Meanwhile the existing `socketPath` property
  already holds the path we connected to (the useful identity, matching
  `$NIRI_SOCKET`). Suggest: drop `peerSocketPath` from PeerInfo, or alias it
  to the connect-time path.
- **Auto-connect with empty discovery** — with `NIRI_SOCKET` unset, the
  singleton attempts `connecting to "(empty!)"` and warns
  `QLocalSocket::connectToServer: Invalid name`. Gate the attempt on
  successful socket discovery and log one clean info line instead.
- **Document/autoConnect opt-out** — the singleton connects on creation
  (observed connected before an explicit `connectToSocket()` call). Fine as
  a default; add an `autoConnect` Q_PROPERTY (default `true`) so test
  harnesses and tooling can control connect timing explicitly.

## Testing

- **Nested-niri harness pattern** — `cage` headless (`WLR_BACKENDS=headless`)
  hosting nested niri (winit) hosting the test client exercises real
  compositor IPC without a display; distinct nested instances get distinct
  sockets/PIDs, which is what validated PeerInfo's multi-instance identity.
  Worth codifying as a dev test script. Gotcha: niri spawns startup commands
  via systemd (`niri::utils::spawning::systemd`), so child stdout lands in
  the user journal — read `journalctl --user` or the qs log file, not the
  niri process's stdout.

## Packaging

- **AUR** — `niriqml-git` package
- **vcpkg / conan** — for non-Arch workflows
- **CI pre-built artifacts** — built libraries downloadable from GitHub releases

## Performance

- **Lazy event parsing** — currently every event is deserialized into a QVariantMap
  eagerly. Batch events or defer parsing when no subscriber exists.
- **Model diff** — `NiriWindowsModel` and `NiriWorkspacesModel` rebuild the full
  internal map on every change. Switch to incremental row insert/remove for
  large sessions.

## Tooling

- **`clang-format` enforcement in CI** — the lint job in `.github/workflows/ci.yml`
  runs `clang-format --dry-run --Werror` on every push and PR. Non-conformant
  code cannot merge.
- **Zed format-on-save** — not yet enabled in the project. Add `.zed/settings.json`
  with `format_on_save: "on"` for C++ to catch drift before commit.
- **Pre-commit hook** — the `add-clang-format` skill can install one locally;
  not committed since `.git/hooks/` is per-checkout.

## Documentation

- **Autogen API.md from headers** — currently API.md is manually written from a
  grep of the headers. A script could walk Q_PROPERTY/Q_INVOKABLE/signal
  declarations and emit the markdown automatically. Bonus: catch drift as a
  lint check.
- **API.md sync discipline** — when adding a new gadget field, Q_PROPERTY,
  signal, role, or Q_INVOKABLE, update API.md in the same commit.
