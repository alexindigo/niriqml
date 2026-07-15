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
