# niriqml API Reference

Complete QML API surface for niriqml, derived from library headers.
Verified against `niriqml v0.1.0`.

## Import

```qml
import Niri 1.0
```

---

## Singletons

Available as global QML identifiers. No instantiation needed.
All use `QML_SINGLETON` with `CppOwnership`.

### NiriConnection

Wire protocol lifecycle. Connects to niri's Unix socket at `$NIRI_SOCKET`.

| Property | Type | Access | Notify |
|---|---|---|---|
| `isConnected` | `bool` | read | `connectedChanged` |
| `socketPath` | `string` | read | `connectedChanged` |
| `errorString` | `string` | read | `connectedChanged` |

**Slots**

| Signature | Description |
|---|---|
| `connectToSocket(path)` | Connect to given path or `$NIRI_SOCKET` env |
| `disconnect()` | Close socket, disable auto-reconnect |
| `setAutoReconnect(enabled)` | Enable/disable exponential backoff reconnect |

**Signals**

| Signal | Parameters |
|---|---|
| `connectedChanged()` | — |
| `eventReceived(name, payload)` | `string name`, `var payload` |
| `reconnectScheduled(delayMs, attempt)` | `int delayMs`, `int attempt` |

### NiriEvents

Typed event dispatch. Subscribes to `NiriConnection::eventReceived` and emits
one signal per niri event, plus a `rawEvent` catchall for forward-compat.

**Cached snapshots** (bootstrap late subscribers)

| Getter | Returns |
|---|---|
| `lastWindowsSnapshot()` | `list<niriWindow>` |
| `lastWorkspacesSnapshot()` | `list<niriWorkspace>` |
| `lastKeyboardLayouts()` | `niriKeyboardLayouts` |
| `lastOverviewOpen()` | `bool` |
| `lastFocusedWindowId()` | `quint64` |

**Signals**

| Signal | Parameters |
|---|---|
| `workspacesChanged(workspaces)` | `list<niriWorkspace>` |
| `workspaceActivated(workspaceId, focused)` | `quint64 id`, `bool focused` |
| `workspaceActiveWindowChanged(workspaceId, windowId)` | `quint64`, `quint64` |
| `windowsChanged(windows)` | `list<niriWindow>` |
| `windowOpenedOrChanged(window)` | `niriWindow` |
| `windowClosed(windowId)` | `quint64` |
| `windowFocusChanged(windowId)` | `quint64` |
| `windowLayoutsChanged(changes)` | `list<var>` |
| `keyboardLayoutsChanged(layouts)` | `niriKeyboardLayouts` |
| `keyboardLayoutSwitched(idx)` | `int` |
| `overviewOpenedOrClosed(isOpen)` | `bool` |
| `configLoaded(failed)` | `bool` |
| `castsChanged(casts)` | `list<var>` |
| `windowFocusTimestampChanged(windowId, focusTimestamp)` | `quint64`, `niriTimestamp` |
| `rawEvent(name, payload)` | `string`, `var` |

### NiriState

Derived globals computed from event snapshots.

| Property | Type | Notify |
|---|---|---|
| `focusedWindowId` | `quint64` | `stateChanged` |
| `focusedWindowAppId` | `string` | `stateChanged` |
| `focusedWindowTitle` | `string` | `stateChanged` |
| `focusedWorkspaceId` | `quint64` | `stateChanged` |
| `activeOutput` | `string` | `stateChanged` |
| `windowCount` | `int` | `stateChanged` |
| `workspaceCount` | `int` | `stateChanged` |

**Signal:** `stateChanged()`

### NiriActions

Write path. Every action returns a `NiriPendingReply`. Wire formats verified
against niri 26.04.

| Method | Description |
|---|---|
| `spawn(command)` | `stringlist` — launch a command |
| `focusWindow(windowId)` | `quint64` — focus the given window |
| `closeWindow(windowId)` | `quint64` — close the given window |
| `moveWindowToWorkspace(windowId, reference, followFocus)` | `quint64`, `var`, `bool=true` |
| `moveWindowToMonitor(windowId, output, followFocus)` | `quint64`, `string`, `bool=true` |
| `setWindowWidth(windowId, change)` | `quint64`, `var` — `SetFixed` or `SetProportion` |
| `setWindowHeight(windowId, change)` | `quint64`, `var` — `SetFixed` or `SetProportion` |
| `moveColumnToIndex(index)` | `int` |
| `toggleWindowFloating(windowId)` | `quint64` |
| `sendAction(action)` | `var` — raw action object for untyped dispatch |

### NiriRequests

Query API + raw escape hatch. Each method returns `NiriPendingReply`.

**Typed queries**

| Method | Returns |
|---|---|
| `windows()` | `list<niriWindow>` |
| `workspaces()` | `list<niriWorkspace>` |
| `outputs()` | `map<string, niriOutput>` |
| `focusedWindow()` | `niriWindow` (may be null) |
| `focusedOutput()` | `niriOutput` (may be null) |
| `keyboardLayouts()` | `niriKeyboardLayouts` |
| `version()` | `string` |

**Raw escape hatch**

| Overload | Description |
|---|---|
| `sendRaw(query)` | `string` — bare-string query (e.g. `"Version"`) |
| `sendRaw(request)` | `var` — full JSON object request |

---

## Elements

Creatable in QML via `QML_NAMED_ELEMENT`. Use directly in QML declarations.

### NiriWindows (`NiriWindowsModel`)

`QAbstractListModel` tracking all windows.

| Role | Type | QML access |
|---|---|---|
| `WindowRole` | `niriWindow` | `model.window` |
| `IdRole` | `int` | `model.id` |
| `TitleRole` | `string` | `model.title` |
| `AppIdRole` | `string` | `model.appId` |
| `PidRole` | `int` | `model.pid` |
| `WorkspaceIdRole` | `int` | `model.workspaceId` |
| `IsFocusedRole` | `bool` | `model.isFocused` |
| `IsFloatingRole` | `bool` | `model.isFloating` |
| `IsUrgentRole` | `bool` | `model.isUrgent` |

### NiriWorkspaces (`NiriWorkspacesModel`)

`QAbstractListModel` tracking all workspaces.

| Role | Type | QML access |
|---|---|---|
| `WorkspaceRole` | `niriWorkspace` | `model.workspace` |
| `IdRole` | `int` | `model.id` |
| `IdxRole` | `int` | `model.idx` |
| `NameRole` | `string` | `model.name` |
| `OutputRole` | `string` | `model.output` |
| `IsUrgentRole` | `bool` | `model.isUrgent` |
| `IsActiveRole` | `bool` | `model.isActive` |
| `IsFocusedRole` | `bool` | `model.isFocused` |
| `ActiveWindowIdRole` | `int` | `model.activeWindowId` |

### NiriWindow (`NiriWindowReactive`)

Reactive tracker for a single window, identified by `windowId`.
Bootstraps from cached snapshot (no event needed).

| Property | Type | Access | Notify |
|---|---|---|---|
| `windowId` | `quint64` | read/write | `windowIdChanged` |
| `valid` | `bool` | read | `windowChanged` |
| `id` | `quint64` | read | `windowChanged` |
| `title` | `string` | read | `windowChanged` |
| `appId` | `string` | read | `windowChanged` |
| `pid` | `quint32` | read | `windowChanged` |
| `workspaceId` | `quint64` | read | `windowChanged` |
| `isFocused` | `bool` | read | `windowChanged` |
| `isFloating` | `bool` | read | `windowChanged` |
| `isUrgent` | `bool` | read | `windowChanged` |
| `layout` | `niriWindowLayout` | read | `windowChanged` |
| `focusTimestamp` | `niriTimestamp` | read | `windowChanged` |

**Signals:** `windowIdChanged()`, `windowChanged()`

### NiriWorkspace (`NiriWorkspaceReactive`)

Reactive tracker for a single workspace, identified by `workspaceName`
or `workspaceId`. Bootstraps from cached snapshot.

| Property | Type | Access | Notify |
|---|---|---|---|
| `workspaceName` | `string` | read/write | `workspaceNameChanged` |
| `workspaceId` | `quint64` | read/write | `workspaceIdChanged` |
| `valid` | `bool` | read | `workspaceChanged` |
| `id` | `quint64` | read | `workspaceChanged` |
| `idx` | `int` | read | `workspaceChanged` |
| `name` | `string` | read | `workspaceChanged` |
| `output` | `string` | read | `workspaceChanged` |
| `isUrgent` | `bool` | read | `workspaceChanged` |
| `isActive` | `bool` | read | `workspaceChanged` |
| `isFocused` | `bool` | read | `workspaceChanged` |
| `activeWindowId` | `quint64` | read | `workspaceChanged` |

**Signals:** `workspaceNameChanged()`, `workspaceIdChanged()`, `workspaceChanged()`

### NiriEventLog

Ring-buffer event log, `QAbstractListModel`.

| Property | Type | Access | Notify |
|---|---|---|---|
| `capacity` | `int` | read/write | `capacityChanged` |

| Role | Type |
|---|---|
| `NameRole` | `string` |
| `PayloadRole` | `var` |

---

## Value Types

`Q_GADGET` structs exposed via `QML_VALUE_TYPE`. Passable as typed QML
properties. Decoded by `NiriUtils::jsonToGadget()` using Q_PROPERTY reflection
(automatic `camelCase`↔`snake_case`, null-safe, extra-key-safe).

### niriWindow

| Field | Type | Default |
|---|---|---|
| `id` | `quint64` | 0 |
| `title` | `string` | `""` |
| `appId` | `string` | `""` |
| `pid` | `quint32` | 0 |
| `workspaceId` | `quint64` | 0 |
| `isFocused` | `bool` | false |
| `isFloating` | `bool` | false |
| `isUrgent` | `bool` | false |
| `layout` | `niriWindowLayout` | (default-constructed) |
| `focusTimestamp` | `niriTimestamp` | (default-constructed) |

### niriWorkspace

| Field | Type | Default |
|---|---|---|
| `id` | `quint64` | 0 |
| `idx` | `int` | 0 |
| `name` | `string` | `""` |
| `output` | `string` | `""` |
| `isUrgent` | `bool` | false |
| `isActive` | `bool` | false |
| `isFocused` | `bool` | false |
| `activeWindowId` | `quint64` | 0 |

### niriOutput

| Field | Type | Default |
|---|---|---|
| `name` | `string` | `""` |
| `make` | `string` | `""` |
| `model` | `string` | `""` |
| `serial` | `string` | `""` |
| `physicalSize` | `list<int>` | (empty) |
| `modes` | `list<niriMode>` | (empty) |
| `currentMode` | `int` | -1 |
| `isCustomMode` | `bool` | false |
| `vrrSupported` | `bool` | false |
| `vrrEnabled` | `bool` | false |
| `logical` | `niriLogicalOutput` | (default-constructed) |

### niriMode

| Field | Type | Default |
|---|---|---|
| `width` | `quint32` | 0 |
| `height` | `quint32` | 0 |
| `refreshRate` | `quint32` | 0 |
| `isPreferred` | `bool` | false |

### niriLogicalOutput

| Field | Type | Default |
|---|---|---|
| `x` | `int` | 0 |
| `y` | `int` | 0 |
| `width` | `quint32` | 0 |
| `height` | `quint32` | 0 |
| `scale` | `double` | 1.0 |
| `transform` | `string` | `""` |

### niriWindowLayout

| Field | Type | Default |
|---|---|---|
| `posInScrollingLayout` | `list<int>` | (empty) |
| `tileSize` | `list<double>` | (empty) |
| `windowSize` | `list<int>` | (empty) |
| `tilePosInWorkspaceView` | `list<double>` | (empty) |
| `windowOffsetInTile` | `list<double>` | (empty) |

### niriTimestamp

| Field | Type | Default |
|---|---|---|
| `secs` | `quint64` | 0 |
| `nanos` | `quint32` | 0 |

### niriKeyboardLayouts

| Field | Type | Default |
|---|---|---|
| `names` | `stringlist` | (empty) |
| `currentIdx` | `int` | 0 |

---

## Non-creatable classes

### NiriPendingReply

Returned by `NiriActions` and `NiriRequests` methods.
`QML_UNCREATABLE("Use NiriActions methods instead")`.

| Property | Type | Notify |
|---|---|---|
| `isError` | `bool` | `finished` |
| `error` | `NiriError` | `finished` |
| `value` | `var` | `finished` |

**Signal:** `finished()`

### NiriError

Carries error details on a failed `NiriPendingReply`.

| Field | Type |
|---|---|
| `code` | `int` |
| `message` | `string` |

---

## Notes

- All singletons and models use `CppOwnership` to prevent QML engine
  garbage-collection of objects needed by the C++ side at shutdown.
- Value types are decoded via `NiriUtils::jsonToGadget()` using Q_PROPERTY
  reflection — automatic `camelCase`↔`snake_case`, null-safe, extra-key-safe.
- Every event has a `rawEvent(name, payload)` catchall signal for
  forward-compatibility with future niri IPC extensions.

## Maintenance

When adding a new gadget field, Q_PROPERTY, signal, role, or Q_INVOKABLE,
update the corresponding section here. This file is manually verified against
the headers; autogen is tracked in `FutureDevelopment.md`.
