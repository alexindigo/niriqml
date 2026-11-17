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

All 19 released events have generated native C++ signals and QML projections.
See the [complete signal and wire reference](docs/generated/IPC-API.md#events).
The session decodes the exact parsed event once, applies its canonical reducer,
then publishes that same typed value. Native C++ observes
`NiriEvents::nativeEvents()` with quint64/std::optional and typed records/lists.
QML observes string IDs, real nulls and typed gadgets. The native accessor is not
invokable from QML. A stop/endpoint change suppresses stale continuation.

**Current snapshots** (bootstrap late subscribers without a query)

| # | Getter | Returns |
|---|---|---|
| 1 | `lastWindowsSnapshot()` | QML/native typed record list, numeric ID order |
| 2 | `lastWorkspacesSnapshot()` | QML/native typed record list, numeric ID order |
| 3 | `lastCastsSnapshot()` | QML/native typed record list, stream ID order |
| 4 | `lastKeyboardLayouts()` | Native `std::optional<NiriKeyboardLayouts>` |
| 5 | `qmlLastKeyboardLayouts()` | QML record or null |
| 6 | `lastOverviewOpen()`, `lastConfigFailed()` | QML/native bool |
| 7 | `lastFocusedWindowId()` | Native `std::optional<quint64>` |
| 8 | `qmlLastFocusedWindowId()` | QML string or null |

These aliases read current reducer values after every delta, not saved full
payloads. They return value copies and do not fabricate global events. Empty
replacements clear lists; absent layouts and null focus remain absent. Casts
are keyed by stream ID, so stopping one stream preserves session siblings.

All existing QML signal names remain. The complete surface adds
workspaceUrgencyChanged, windowUrgencyChanged, screenshotCaptured and castStopped.
Layout changes carry named niriWindowLayoutChange entries (id/layout).

| # | Diagnostic signal | Contract |
|---|---|---|
| 1 | `rawEvent(name, payload)` | Every current event, including known, unknown and malformed variants |
| 2 | `unknownEvent(name, payload)` | Unknown variant; no invented typed signal/state update |
| 3 | `decodeError(path, message, payload)` | Malformed known variant; no default typed signal/state update |

Known event bodies must be objects before any field lookup, including variants
whose fields are all optional. Diagnostic payloads are now QVariant/var instead
of QVariantMap: ordinary bodies remain objects; scalar/array/null bodies are not
discarded, and future unit variants retain their original string. Native
callbacks that explicitly required QVariantMap must accept QVariant instead.
The event log preserves the same raw value. A decoded impossible sequence
(missing same-part target, or layout switch before layouts) instead reports one
session ProtocolError and resets state, reconnecting only per connection policy.
Urgency/timestamp events for unknown IDs remain no-ops. Cross-part dangling IDs
are allowed. Synthetic C++ dispatch methods publish only; they do not write the
session's state.

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
| `initialStateReceived` | `bool` | `initialStateReceivedChanged` |

`initialStateReceived` is false until the first accepted `CastsChanged` has been
applied, even if its list is empty. It is already true inside that event's
handlers. Later casts replacements do not retrigger it. Disconnect, endpoint
replacement and subscription failure clear readiness together with canonical
state. A late-created NiriState reads the same readiness immediately.

This rule follows niri v26.04's initial burst: workspaces, windows, optional
keyboard layouts, overview, config, casts. Recheck upstream `state.rs` and
`server.rs` when updating the supported release. Query replies never write
stream state; before readiness, snapshot values may be partial/defaults.

**Signal:** `stateChanged()`

### NiriActions

All 141 released actions have generated methods with every wire field. See the
[checked method/wire reference](docs/generated/IPC-API.md#actions). Each returns
a retainable `NiriPendingReply`; success requires the released `Handled` reply.
Local argument failures complete asynchronously as Configuration/NotSent and
never open a request socket. Raw `sendAction(action)` remains unrestricted.

| # | Existing helper | QML argument contract |
|---|---|---|
| 1 | `spawn(command)` | String array |
| 2 | `focusWindow(windowId)` | Required decimal ID string (exact number also accepted) |
| 3 | `closeWindow(windowId)` | Decimal ID or null/omitted for focused window |
| 4 | `moveWindowToWorkspace(windowId, reference, focus)` | ID or explicit null, WorkspaceReferenceArg, required bool |
| 5 | `moveWindowToMonitor(windowId, output)` | ID or explicit null, output string; no monitor focus flag |
| 6 | `setWindowWidth(windowId, change)` | ID or explicit null, SizeChange |
| 7 | `setWindowHeight(windowId, change)` | ID or explicit null, SizeChange |
| 8 | `moveColumnToIndex(index)` | Decimal usize string (exact number accepted) |
| 9 | `toggleWindowFloating(windowId)` | Decimal ID or null/omitted for focused window |

Argument order is preserved. QML now uses explicit tagged values instead of
guessing a workspace name/index or fixed/proportional size from a scalar.
WorkspaceReferenceArg accepts `{Id: "456"}`, `{Index: 2}`, `{Name: "chat"}`
or the matching `NiriArgs.workspaceReferenceArgId/Index/Name(...)` value.
SizeChange supports all four alternatives: `{SetFixed: 800}`,
`{SetProportion: 50.0}`, `{AdjustFixed: -10}`, `{AdjustProportion: -12.5}`,
or `NiriArgs.sizeChangeSetFixed/SetProportion/AdjustFixed/AdjustProportion(...)`.
Unknown fields/variants and invalid types/ranges are rejected by typed methods;
use raw sending deliberately for future protocol extensions.

The workspace move's `focus` bool is no longer defaulted. Optional ID slots
before required arguments must be filled with null to target the focused item.
Native C++ overloads use quint64/std::optional and generated argument values;
the old QVariant workspace/size convenience adapters remain native-only and
require an explicit workspace-move focus bool.

### NiriRequests

All 16 requests have generated methods. The
[generated reference](docs/generated/IPC-API.md#requests) lists their complete
argument/wire shapes, expected response and source citation. Each successful
response kind has a named uncreatable reply subclass, such as NiriWindowsReply,
with a typed `result` property and native C++ `result()` getter. They inherit
NiriPendingReply retention, cancellation, raw envelope, identity and errors.
`value` remains the QML convenience projection; NiriHandledReply.result is an
explicit niriHandled unit value while its value remains the string "Handled".

Methods accept trailing `{timeoutMs, connectTimeoutMs}` options. `pickWindow()`
and `pickColor()` default to no response deadline; a caller can override it.
`action(value)` accepts a generated NiriArgs Action value or a validated plain
tagged object. `output(output, action)` accepts all nine released OutputAction
variants as NiriArgs values or plain objects, with every field validated.

`eventStream()` observes the shared session's actual acknowledgement and leaves
its event socket open. Already-subscribed calls retain that same acknowledgement,
without a second subscription. Cancelling/timing out one observer does not stop
the shared stream. `returnError()` expects a server Err, reported through the
normal error contract; an unexpected Ok is a Decode error.

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
Exported by name, uncreatable, retainable and cancellable. See
[request contracts](docs/REQUESTS.md) for the full shared API and migration notes.

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
