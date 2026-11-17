# niriqml Design

Typed, reactive QML bindings for niri IPC — following the `mpvqml` / `dbusqml`
house style.

## Architecture

```
                    ┌──────────────────────────────────────┐
                    │  Quickshell process (Atmosphera, etc) │
                    │                                       │
                    │  QML consumers (import Niri 1.0)      │
                    │      │        ▲                       │
                    │      │        │ Q_PROPERTY / signals  │
                    │      ▼        │ QAbstractListModel    │
                    │  ┌────────────────────────────┐       │
                    │  │  niriqml (Qt6 C++ plugin)  │       │
                    │  └────────────────────────────┘       │
                    └───────────────────┬───────────────────┘
                                        │ QLocalSocket
                                        ▼
                                  $NIRI_SOCKET
                                        │
                                ┌───────┴───────┐
                                │  niri compositor│
                                └───────────────┘
```

- **One `QLocalSocket`** per process; opened once, kept alive.
- **One `EventStream` subscription** — drives every model and every reactive property.
- **Short-lived sockets** for one-shot actions (matching `niri msg` client behavior).

## Wire protocol

`NiriSession` constructs its native `NiriEventStreamState` before subscribing.
One generated decode feeds a complete validated event transaction before any
typed/raw publication. The six retained parts follow niri v26.04 `state.rs`:
workspaces, windows, optional keyboard layouts, overview, config and casts.
Immutable keyed entries and per-transaction before/after field changes support
projections without cloning every record for every delta. Snapshots are current
copies in numeric ID order, not retained full-event payloads. Query replies
never seed this reducer.

Epoch replacement clears all parts and readiness. The first accepted
`CastsChanged`, last in v26.04's replication order, sets readiness before
notification. Strict same-part preconditions are validated for the entire event
before mutation; failures use the session's protocol-error/reset/retry path.
Cross-part dangling references remain valid and are never repaired or inferred.

NiriState, both models and both reactive trackers read this reducer only.
Models retain ID rows, not records; sparse transaction before-images keep removed
rows readable inside Qt's about-to-remove callbacks. ID diffs preserve surviving
persistent indexes, including across full replacements. Models finish row/role
projection before public property and typed/raw event notifications. Consumers
created during publication attach at the committed revision and skip that diff.
A reset requested inside a row bracket invalidates the epoch immediately but
waits for projection publication to unwind before clearing records and rows.

Name/focus indexes belong to the reducer. One selector resolver gives nonempty
workspace names precedence over optional IDs, including zero. Trackers retain
selectors only; a stack-owned notification reconciler compares current getters
after every callback, so reentrant setters cannot restore stale record data.
Field-specific NOTIFYs and exact model role sets suppress equal/irrelevant work.
NiriState materializes complete keyed maps/snapshots only when requested; it
exposes all six parts and readiness with string IDs and real null optionals.
Reset notifications empty all projections and clear readiness together.

- Unix socket at `$NIRI_SOCKET`
- JSON line-oriented: send one `Request` object, receive one `Reply` per line
- `EventStream` keeps the socket open and streams `Event` JSON objects forever
- Framing: simple `\n` delimiter (requires `QIODevice` line reader)

## QML API surface

| Category | Types |
|---|---|
| Value types (Q_GADGET) | NiriWindow, NiriWorkspace, NiriWindowLayout, NiriSize, NiriSizeChange, NiriPos, NiriOutput |
| Live models (QAbstractListModel) | NiriWindows, NiriWorkspaces |
| Reactive single-item (QObject) | NiriWindow { windowId }, NiriWorkspace { workspaceName } |
| Events | NiriEvents (singleton, typed signals + rawEvent catchall) |
| Actions | NiriActions (singleton, Q_INVOKABLE per action variant) |
| Derived state | NiriState (singleton: focusedWindowId, windowCount, etc.) |
| Async replies | NiriPendingReply (finished signal, isError, value) |
| Debug | NiriEventLog (ring-buffer model) |

## Schema-drift strategy

- `rawEvent(name, payload)` catchall for unknown future events
- `docs/SCHEMA.md` — committed schema snapshot of the target niri IPC version
- Version-track niri IPC; upgrade niri may require niriqml rebuild

## References

- [mpvqml](https://github.com/alexindigo/mpvqml) — companion QML library for libmpv
- [dbusqml](https://github.com/alexindigo/dbusqml) — companion QML library for D-Bus
- [niri](https://github.com/YaLTeR/niri) — scrollable-tiling Wayland compositor
- Full project write-up: `~/Documents/niriqml.md`
