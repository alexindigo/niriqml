# niriqml

Typed, reactive QML bindings for [niri](https://github.com/YaLTeR/niri) IPC —
windows, workspaces, events, actions — as a Qt 6 QML plugin.

No JSON parsing in QML, no external daemon, no polling. Every niri state value
arrives as a `Q_PROPERTY` or `QAbstractListModel`. Every niri event arrives as
a typed Qt signal. Every niri action is a `Q_INVOKABLE` returning an async
`NiriPendingReply`.

---

## Why niriqml

- **Reactive, zero glue** — `NiriWindowsModel` and `NiriWorkspacesModel` are
  standard `QAbstractListModel` instances. Bind them to `Repeater`, `ListView`,
  or any model-based QML view. State flows automatically; no manual JSON dispatch.

- **Forward-compatible by design** — every typed event signal has a corresponding
  `rawEvent(name, payload)` catchall. Actions you haven't wrapped yet are reachable
  via `NiriRequests::sendRaw(...)`. Schema drift across niri upgrades doesn't
  silently break.

- **Qt-native, instant startup** — a single `QLocalSocket` per process, one
  `EventStream` subscription, no polling. Bootstraps late subscribers from cached
  snapshots so your UI populates immediately regardless of connection timing.

---

## Quickstart

```qml
import Niri 1.0

Column {
    Repeater {
        model: NiriWindowsModel {}

        delegate: Rectangle {
            border.color: model.isFocused ? "orange" : "transparent"
            Text { text: model.title }
        }
    }
}
```

---

## Feature tour

### Window and workspace models

`NiriWindowsModel` and `NiriWorkspacesModel` are reactive lists. Each row exposes
typed roles — no `modelData` guessing.

```qml
ListView {
    model: NiriWindowsModel {}

    delegate: ItemDelegate {
        highlighted: model.isFocused
        text: model.title + "\n" + model.appId
    }
}
```

Workspace roles:

```qml
ListView {
    model: NiriWorkspacesModel {}

    delegate: ItemDelegate {
        text: model.name + (model.isActive ? " (active)" : "")
        font.bold: model.isFocused
    }
}
```

### Derived state

`NiriState` exposes seven pre-computed globals — no model iteration needed in QML
for common queries.

```qml
Text {
    text: "Focused: " + NiriState.focusedWindowTitle + " | " +
          NiriState.focusedWindowAppId + "\n" +
          "Windows: " + NiriState.windowCount +
          "  Workspaces: " + NiriState.workspaceCount +
          "  Output: " + NiriState.activeOutput
}
```

### Typed events

`NiriEvents` exposes one signal per niri event. Subscribe with `Connections`.
The `rawEvent` catchall covers events not yet wrapped.

```qml
Connections {
    target: NiriEvents

    function onWindowFocusChanged(windowId) {
        console.log("focus →", windowId)
    }
    function onWorkspaceActivated(workspaceId, focused) {
        console.log("workspace activated", workspaceId, focused)
    }
    function onOverviewOpenedOrClosed(isOpen) {
        console.log("overview", isOpen ? "opened" : "closed")
    }
    function onRawEvent(name, payload) {
        console.log("untyped event:", name, JSON.stringify(payload))
    }
}
```

### Reactive single-item tracking

`NiriWindowReactive` and `NiriWorkspaceReactive` track a window by id or a
workspace by name/id. They bootstrap immediately from the cached snapshot so
`valid` can be `true` before any new event fires.

```qml
NiriWindowReactive {
    id: trackedWindow
    windowId: 42
}

Text {
    text: trackedWindow.valid ? trackedWindow.title : "window 42 not found"
    color: trackedWindow.isFocused ? "orange" : "gray"
}
```

Workspace by name:

```qml
NiriWorkspaceReactive {
    id: trackedWs
    workspaceName: "1"
}

Text {
    text: trackedWs.valid ? trackedWs.output : "no workspace named '1'"
}
```

### Actions (write path)

Every action is a `Q_INVOKABLE` on `NiriActions`, returning a `NiriPendingReply`.
Use the `finished` signal for async handling — the reply carries `isError`, `error`,
and `value`.

```qml
Button {
    text: "Close window"
    onClicked: {
        let reply = NiriActions.closeWindow(trackedWindow.id)
        reply.finished.connect(() => {
            if (reply.isError)
                console.log("close failed:", reply.error.message)
        })
    }
}
```

```qml
NiriActions.spawn(["alacritty", "-e", "htop"])
```

### Typed queries

`NiriRequests` wraps the niri query API. Results arrive as typed gadgets
(`NiriWindow`, `NiriOutput`, `NiriKeyboardLayouts`, etc.) — no manual JSON unwinding.

```qml
Button {
    text: "Show niri version"
    onClicked: {
        let reply = NiriRequests.version()
        reply.finished.connect(() => {
            versionLabel.text = reply.value  // QString
        })
    }
}

Text { id: versionLabel }
```

Outputs return a map keyed by output name:

```qml
let reply = NiriRequests.outputs()
reply.finished.connect(() => {
    for (let name in reply.value)
        console.log(name, reply.value[name].make, reply.value[name].model)
})
```

### Raw escape hatch

For operations without a typed wrapper, `sendRaw` accepts a bare-string query
or a full JSON object:

```qml
NiriRequests.sendRaw("Version")              // bare-string query
NiriRequests.sendRaw({"Action": {"Quit": {}}})  // raw action object
```

---

## Install

Prerequisites: Qt 6.5+, CMake 3.16+, niri ≥ 26.04.

```bash
scripts/build          # → build-release/
scripts/install        # installs to system Qt QML path
```

For development builds with tests:

```bash
scripts/build-test     # Debug + coverage, → build-debug/
scripts/run-tests      # or: ctest --test-dir build-debug
```

---

## Testing

8 test binaries, split into two categories:

**Headless** — run anywhere, use mock socket or pure unit-test logic:
- `test_framing` — line-buffer framing (7 tests)
- `test_types` — gadget schema shape
- `test_converter` — dynamic JSON → gadget conversion (11 tests)
- `test_connection` — mock integration (8 tests)

**Live** — require a running niri, set `NIRI_SOCKET` (or auto-detect from
`/run/user/<uid>/`):
- `test_live_wire` — connection establish + clean shutdown
- `test_live_events` — typed event dispatch + gadget shape (4 tests)
- `test_live_layer2` — models, state, reactive tracking (8 tests)
- `test_live_write` — actions, typed queries, sendRaw (11 tests)

`NIRI_SOCKET` is auto-detected when not set. Live tests `QSKIP` if no
socket is reachable.

---

## Status

| Phase | Layer | Status |
|---|---|---|
| 1 | Wire protocol (`NiriConnection`, `NiriLineBuffer`, auto-reconnect) | Done |
| 2 | Types (`NiriWindow`, `NiriWorkspace`, output gadgets, etc.) | Done |
| 3 | Layer 2 QML surface (models, state, reactives) | Done |
| 4 | Write/test path (`NiriActions`, `NiriRequests` queries, `sendRaw`) | Done |
| — | Window-rules, keybinds, touch-gestures IPC | Not yet |
| — | Packaging (AUR, vcpkg, etc.) | Not yet |

Schema snapshots live in `docs/SCHEMA.md`. The library is tested against niri
26.04. See `FutureDevelopment.md` for roadmap items.

---

## License

GPL-3.0 — see [LICENSE](LICENSE).

---

## Related

- [niri](https://github.com/YaLTeR/niri) — scrollable-tiling Wayland compositor
- [mpvqml](https://github.com/alexindigo/mpvqml) — typed QML bindings for libmpv
- [dbusqml](https://github.com/alexindigo/dbusqml) — typed QML bindings for D-Bus
