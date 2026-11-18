# niriqml contracts

Import `Niri 1.0`. The supported baseline is niri >= 26.04, pinned to
`8ed0da44d974c32c6877d2f4630c314da0717ecb`. Selected optional additions are
pinned separately to main `689c1e9`; they are not requirements on 26.04.

This is the hand-written behavioral reference. The checked
[generated reference](docs/generated/IPC-API.md) lists **every method, signal,
record, field, enum, argument factory and response**, including exact wire
shapes and native/QML types. [FeatureMapping](FeatureMapping.md) maps the
inventory to that reference. [Requests](docs/REQUESTS.md) provides runnable
retention/sequence examples; it uses the contracts below.

## Architecture and ownership

Raw access is the base; generated typed calls/codecs are an abstraction over
it, not a separate transport. One application-owned `NiriSession` selects the
endpoint, owns one event subscription and reduces stream events into one
canonical state. Queries/actions normally use independent short-lived sockets.
An explicit sequence owns its own persistent request socket.

`NiriState`, models, reactive trackers and snapshot getters only project the
reducer. They neither query to seed state nor replay events into private caches.
Late consumers see current state immediately. Query results are value snapshots
and never overwrite stream state.

| # | QML export | Ownership / purpose |
|---|---|---|
| 1 | `NiriConnection`, `NiriEvents`, `NiriState`, `NiriActions`, `NiriRequests` | Engine-local singleton facades, CppOwnership; shared application session |
| 2 | `NiriWindows`, `NiriWorkspaces` | QML-created list models, owned with their QML objects; C++ classes are NiriWindowsModel/NiriWorkspacesModel |
| 3 | `NiriWindow`, `NiriWorkspace` | QML-created reactive trackers; C++ classes are NiriWindowReactive/NiriWorkspaceReactive |
| 4 | `niriWindow`, `niriWorkspace`, other lowercase names | Immutable generated value gadgets, not tracker objects; native records are NiriWindow/NiriWorkspace |
| 5 | `NiriPendingReply`, generated reply subclasses, `NiriRequestSequence` | Uncreatable by QML; returned retainable JavaScript-owned objects |
| 6 | `NiriEventLog` | QML-created bounded history model |

`NiriArgs` is an engine-local generated value-constructor singleton, not a
session consumer or transport owner. Constructing a value alone needs no IPC.

All service/model/tracker QObject acquisition and use, including getters,
requests and cancellation, must occur on the **application thread**. Engines
must live on that thread too. Pure copied value records/codecs are not session
consumers.
Wrong-thread acquisition/calls are rejected with a diagnostic, not marshalled.
Multiple same-thread engines have separate facades over the same session.
Destroying an engine cancels its facade's operations, not other engines' work.
The application owns session shutdown; there is no process-static QObject that
outlives Qt's event dispatcher.

## Startup, endpoint and connection

Acquiring **any** public consumer starts the session automatically, including
state-only, model-only, reactive-only and event-log-only applications. No prior
`NiriConnection` or `NiriEvents` access is needed. Startup is queued so receivers
exist before the first subscription. Later consumers do not undo an explicit
stop. Call `NiriConnection.start()` to resume.

`stop()` (also `disconnect()`) stops subscription/retries, invalidates the epoch
and clears canonical state/readiness. It cancels pending stream observers;
independent request calls remain separate operations and are not rolled back.
Setting `autoReconnect` does not itself override explicit stop.

| # | Endpoint operation | Meaning |
|---|---|---|
| 1 | Default / `useEnvironmentEndpoint()` | Select current `NIRI_SOCKET`, including unset/empty; explicit refresh, not continuous environment watching |
| 2 | `selectEndpoint(path)` | Select Explicit even for empty path; no environment fallback |
| 3 | `connectToSocket(path)` | Compatibility adapter: nonempty selects Explicit; empty/no argument refreshes Environment; starts subscription |
| 4 | `start()` | Resume selected endpoint without changing its source |

Selection is independent of socket liveness. Every request captures endpoint,
revision and request ID before queued work. Endpoint replacement cancels old
revision operations as EndpointChanged; they are never retargeted or replayed.
A stream reconnect at the same endpoint does not cancel independent requests.

| # | Connection property | Notify / meaning |
|---|---|---|
| 1 | `socketPath: string` | `socketPathChanged`; selected path, not necessarily connected |
| 2 | `endpointSource: NiriConnection.Environment/Explicit` | `endpointSourceChanged` |
| 3 | `status` | `statusChanged`; Stopped, Connecting, Subscribing, Streaming, Backoff, Failed |
| 4 | `isConnected: bool` | `connectedChanged`; physical stream socket connected, not necessarily subscribed |
| 5 | `subscribed: bool` | `subscribedChanged`; accepted exact semantic `{"Ok":"Handled"}` acknowledgement |
| 6 | `autoReconnect: bool` | `autoReconnectChanged`; retry policy; `reconnectScheduled(delayMs, attempt)` announces retries |
| 7 | `errorString: string`, `error: var` | `errorChanged`; structured stream failure; empty on recovery |
| 8 | `peerInfo: peerInfo` | `peerInfoChanged`; current socket peer metadata, cleared on retirement |

Connection establishment and subscription acknowledgement have finite deadlines.
Malformed/rejected acknowledgement, framing, parsing and impossible event
sequences fail visibly and reset state. Retry follows `autoReconnect`, never
an implicit switch to another compositor. Native `setMaxFrameBytes()` configures
the next stream attempt; the default 64 MiB is a resource bound, not an upstream
record limit. Requests capture their separately configurable frame limit.

`peerInfo` exposes pid/uid/gid, credentialsAvailable/credentialsError,
peerSecurityContext and securityContextAvailable/securityContextError.
Credential and security-label availability are independent. Native
peerSecurityContextBytes retains exact label bytes; the QML string is a UTF-8
display projection. A missing label is not a fabricated identity.

## Readiness and reset

`NiriState.initialStateReceived` is false until the first `CastsChanged` after
an accepted subscription is applied, including an empty casts list. It is true
before that event's notifications. The released burst order is workspaces,
windows, optional keyboard layouts, overview, config, casts. An absent keyboard
layout part is valid. An empty ready desktop is not a disconnected desktop.

Later casts replacements do not retrigger readiness. Disconnect, stop, endpoint
replacement or subscription failure clears all parts, focus and readiness with
notifications. Models remove rows; reactive trackers become invalid; the next
burst repopulates the same consumers. Before readiness, values can be partial or
default. Recheck upstream `state.rs` and `server.rs` on every release update.

The selected newer `TouchpadToggled` part follows casts. `touchpadOn` is null
until received, then bool, and resets to null. On a newer compositor,
`initialStateReceived` can become true **one event before** `touchpadOn` leaves
null. Consumers needing touchpad state bind to `touchpadOn`; null means unknown,
false is a present value. This is the runtime toggle, not hardware/config
disablement. Optional main extensions do not change released readiness; revisit
the terminator when adopting a new supported release baseline.

## IDs, nulls and typed values

QML u64 IDs and wide integer counters/indices (`u64`, `usize`, `i64`) are decimal
**strings**, even when small. Native C++ uses exact integers (IDs: quint64).
Typed inputs also accept finite, integral Number values exactly representable
by JavaScript and within the target range. They cannot detect/recover an integer
already rounded before the call: use strings for arbitrary IDs, never
`Number(id)`, numeric ID sorting or arithmetic on IDs. u32 and smaller remain
numbers with checked width/range. Column/stack indices are not window IDs.

An absent optional is real QML `null` / native `std::optional`, not 0, false,
empty string, empty list or undefined. ID zero is valid. Missing optional receive
fields and explicit null both decode as absence; present empty/zero values stay
present. Required fields are validated. Complete replacements clear old optional
values. Encoding emits explicit null slots; released required booleans do not
inherit CLI or newer-main defaults.

Records expose read-only camelCase fields and retained `extensions`. Enums and
unions retain unknown received values (`raw`, wireName/tag); inactive payloads
are null. `NiriArgs` factories or plain tagged objects construct typed arguments.
Both wire and camelCase input aliases are accepted, but not both for one field.
Invalid construction has `valid: false` and `error`, not a default success.
Typed outbound calls reject unknown fields/variants; raw calls are the escape
hatch. All details are in the [value/factory reference](docs/generated/IPC-API.md#niriargs-factories).

## Requests, actions and replies

All [requests](docs/generated/IPC-API.md#requests),
[actions](docs/generated/IPC-API.md#actions) and output controls use one checked
operation/encoder/completion path. A result completes exactly once, always
queued after return, even for invalid input or an empty endpoint. Attaching
`finished` in the same turn is safe. After a nested loop or for a late observer,
check `completed` as well. Terminal fields are committed before `finished()`.

Replies **do not auto-delete on finished**. Retain a QML reference while waiting
or showing a result; dropping the last reference can collect it and cancel local
waiting. `cancel()` completes pending work as Cancelled; terminal cancel is a
no-op. Engine/facade teardown cancels its scoped operations. Native callers
delete retained replies or use `sendWire(request, options, owner)` for explicit
QObject ownership. Deletion inside a finished handler is supported.

| # | Reply property | Contract |
|---|---|---|
| 1 | `completed`, `status` | Pending, Succeeded or Failed |
| 2 | `isError`, `error` | Structured failure, not a fabricated acknowledgement |
| 3 | `value` | Full raw Ok payload, or named typed convenience projection; Handled stays string `"Handled"` |
| 4 | `document`, `json` | Whole Ok/Err envelope projection and original JSON text, retained on decode/protocol errors |
| 5 | `responseKind` | Response discriminator, including unknown names |
| 6 | `endpoint`, `endpointRevision`, `requestId` | Captured identity; revisions/IDs are strings |
| 7 | `executionCertainty` | NotSent, MayHaveExecuted, ReplyReceived |
| 8 | Generated subclass `result` | Typed native/QML result; success requires expected discriminator and complete valid payload |

The [14 reply classes](docs/generated/IPC-API.md#typed-replies) are exported and
uncreatable. Named actions return `NiriHandledReply`; its result is a
`niriHandled` unit gadget with wireName `"Handled"`. Optional query results are
null, never fabricated records. Wrong response kinds/fields fail Decode while
preserving the envelope and `error.fieldPath`.

Errors expose numeric code, category/message, certainty, identity, phase,
fieldPath, byteOffset and socketError where applicable. Categories are
Configuration, Transport, Timeout, Framing, Parse, Protocol, Server, Decode,
Cancelled and EndpointChanged. Once bytes enter the socket send buffer, a
failure conservatively reports MayHaveExecuted. A complete reply frame reports
ReplyReceived, even if its envelope is rejected. NotSent means no send occurred.
Cancellation, timeout and disconnect are **not rollback**; requests never retry.

NiriRequests methods/raw calls and sequence entries accept trailing per-call
options `{timeoutMs: 5000, connectTimeoutMs: 5000}`. Named NiriActions methods
have only their documented schema arguments, not a trailing options parameter.
`timeoutMs: -1` disables the total response deadline, not the connect deadline.
Other negative, fractional or unknown options fail. Typed `pickWindow()` and
`pickColor()` default to -1; raw picks need that option explicitly. Local cancel
does not dismiss niri's picker UI.

`eventStream()` observes the shared session's actual accepted acknowledgement,
opens no one-shot request socket and leaves events streaming. Late observers
reuse the current acknowledgement. Observer cancellation/deadline does not stop
the session; explicit stop cancels pending observers. `returnError()` expects
Err, through the common error contract; unexpected Ok is Decode.

Existing helper argument order is preserved. Optional slots before required
arguments need explicit null. For example:

| # | Call | Meaning |
|---|---|---|
| 1 | `moveWindowToWorkspace(id, {Id: workspaceId}, false)` | Stable workspace ID, explicit required focus bool; wire key window_id |
| 2 | `moveWindowToMonitor(null, "DP-1")` | Focused window; wire key id; unsupported focus parameter removed |
| 3 | `setWindowWidth(id, {SetFixed: 800})` | Wire key id; also SetProportion, AdjustFixed, AdjustProportion |
| 4 | `closeWindow(null)` | Focused window, not ID zero |

Native typed overloads use quint64/std::optional and generated arguments.
Legacy QVariant workspace/size conveniences remain native-only, with explicit
workspace focus bool. Scalar workspace-name/index or size inference is not a
QML typed-call contract.

This complete component builds a stable-ID move without defaulting focus:

```qml
import QtQml
import Niri 1.0

QtObject {
    objectName: "actionCall"
    property NiriHandledReply pending: null
    function move(windowId, workspaceId) {
        pending = NiriActions.moveWindowToWorkspace(
            windowId, NiriArgs.workspaceReferenceArgId(workspaceId), false)
    }
}
```

## Raw access and ordering

`NiriRequests.sendRaw("Version")` encodes a bare JSON string query, not source
text. `sendRaw(object, options)` uses the same lossless encoder.
`sendRawJson(text, options)` validates one complete JSON
document and preserves numeric tokens; extra newline-delimited requests fail.
`NiriRequests.number(text)` builds exact numbers in raw objects; invalid text
returns undefined and sending it fails asynchronously. Unknown-field strings
are never guessed to be numeric IDs.

Raw integer tokens project to strings (including small integers); other numeric
tokens project to `niriJsonNumber` with exact `text` and explicit `toNumber()`.
Native `wireDocument()` retains the exact tree and frame bytes. Compatibility
C++ send/sendJson and synthetic QVariant dispatch are explicitly lossy adapters,
not the authoritative wire path.
The legacy `NiriActions.sendAction` accepts QJsonObject through the shared
operation path, but its QJson input bridge can already have rounded numbers.
Use sendRaw/sendRawJson for exact unknown numeric fields, for example:

```qml
import QtQml
import Niri 1.0

QtObject {
    objectName: "exactRawCall"
    property NiriPendingReply pending: null
    function focus(id) {
        pending = NiriRequests.sendRaw({
            Action: {FocusWindow: {id: NiriRequests.number(id)}}
        })
    }
}
```

Independent calls have no cross-call ordering. Await replies before submitting
dependent calls, or retain `NiriRequests.sequence()`. Its `request()`/`action()`
entries share one persistent socket, without pipelining. Deadlines start when an
entry activates, not while queued. Failure, cancel, endpoint change, connection
loss or deleting pending entries stops the sequence; unsent entries are NotSent.
A stopped lane never reconnects/resumes; create another deliberately. Retain
the lane and desired replies. See the [sequence example](docs/REQUESTS.md).
Neither ordering nor Handled means state convergence, animation completion,
global FIFO, a compositor transaction or rollback.

## Events and current state

All [typed signals](docs/generated/IPC-API.md#events) publish the same generated
decoded value after reducer/model updates. Native `NiriEvents::nativeEvents()`
(C++ only) uses exact integers/optionals; QML uses strings/null/gadgets. A stop
or endpoint change during publication suppresses stale continuation.

`NiriEvents.rawEvent(name, payload)` covers known, unknown and malformed events.
Unknown variants additionally emit unknownEvent; malformed known bodies emit
decodeError(path, message, payload), with no default typed signal/state update.
Bodies must be objects even for all-optional known records. Raw payload is var,
not object-only: arrays, scalar/null bodies and future unit strings survive.
Malformed JSON/framing is instead a stream error, not a decodable event.
`NiriConnection.eventReceived` is an object-only compatibility projection; use
NiriEvents.rawEvent for complete raw coverage.

Impossible decoded same-part sequences use ProtocolError/reset/reconnect;
unknown-ID urgency/timestamp deltas are permitted no-ops. Cross-part dangling
references remain valid and are not repaired by inference. Synthetic C++ event
publication does not mutate production state.

| # | NiriState properties | Type / notification |
|---|---|---|
| 1 | focusedWindowId, focusedWorkspaceId | string/null; respective Changed |
| 2 | focusedWindowTitle, focusedWindowAppId, activeOutput | string/null; respective Changed |
| 3 | focusedWindow, focusedWorkspace | complete record/null; respective Changed |
| 4 | windowCount, workspaceCount, castCount | int; respective Changed |
| 5 | windowsById, workspacesById, castsByStreamId | string-keyed record maps; windowsChanged/workspacesChanged/castsChanged |
| 6 | keyboardLayouts | record/null; keyboardLayoutsChanged |
| 7 | overview, overviewOpen | niriOverview/bool; overviewOpenChanged |
| 8 | configFailed, touchpadOn, initialStateReceived | bool, bool/null, bool; respective Changed |

`activeOutput` comes from the focused workspace, not an output metadata cache.
Null keyboard focus is never inferred from its active window. Casts are keyed
by stream ID; siblings sharing a session ID remain distinct.

Every property notifies only on observable change. `stateChanged()` aggregates
actual exposed state changes, **not every event**. Equal snapshots and transient
ScreenshotCaptured do not emit it. Use rawEvent for event traffic.

QML-invokable `window(id)`, `workspace(id)`, `workspaceByName(name)`,
`cast(streamId)` return complete records/null. `windowsSnapshot()`,
`workspacesSnapshot()`, `castsSnapshot()` return copies in numeric-ID order.
`snapshot()` copies all keyed maps, layouts, overview/open, configFailed,
touchpadOn, readiness, focused IDs and activeOutput. Copies are not live objects.
Native optional keyed getters are nativeWindow/nativeWorkspace/
nativeWorkspaceByName/nativeCast; native snapshot getters have a native prefix.
Native focus/layout/touchpad getters retain std::optional.

NiriEvents snapshot aliases also read **current** reducer values after deltas:
lastWindowsSnapshot/lastWorkspacesSnapshot/lastCastsSnapshot, lastOverviewOpen,
lastConfigFailed are invokable in QML. Use qmlLastKeyboardLayouts and
qmlLastFocusedWindowId for null/string projections; their unprefixed counterparts
are native optional getters. These reads never emit fake global events.

## Models, trackers and history

Models sort by numeric ID without converting IDs through Number. Full snapshots
use ID diffs; surviving persistent indexes remain valid. Exact role changes
include the whole-record role. Equal replacements emit nothing. Reset removes
rows with normal removal notifications, not an unconditional modelReset.

| # | Model | QML roles |
|---|---|---|
| 1 | NiriWindows | window, id, title, appId, pid, workspaceId, isFocused, isFloating, isUrgent, layout, focusTimestamp |
| 2 | NiriWorkspaces | workspace, id, idx, name, output, isUrgent, isActive, isFocused, activeWindowId |

Role types match the [generated record fields](docs/generated/IPC-API.md#records-and-variant-payloads).
Use a delegate's `model.id` (not the QML declaration id). Models are not
CppOwnership process singletons.

`NiriWindow { windowId: ... }` tracks an optional ID. `NiriWorkspace` selects
by workspaceName and/or workspaceId. Nonempty name wins, including unmatched
names; there is no ID fallback until name is cleared. Duplicate names choose
the lowest numeric ID. Clearing ID preserves a name selection; null clears an
ID selector, zero selects zero. Invalid input warns and leaves selection intact.

Trackers expose valid, complete window/workspace record (null when invalid),
and all corresponding convenience fields. Fields notify via their own Changed
signals; windowChanged/workspaceChanged are equality-suppressed aggregates.
Selectors commit before notification, including reentrant changes. Reading a
tracker never maintains a second event reducer.
Notification comparison uses its last published selection/immutable entry
identity, not a new read of already-committed state. Clearing or retargeting a
selector from an earlier state/facade/tracker callback cannot suppress the
record/valid notifications needed by bound consumers.

| # | Tracker | Properties in addition to valid and selectors |
|---|---|---|
| 1 | NiriWindow | window, id, title, appId, pid, workspaceId, isFocused, isFloating, isUrgent, layout, focusTimestamp |
| 2 | NiriWorkspace | workspace, id, idx, name, output, isUrgent, isActive, isFocused, activeWindowId |

Each property has its matching `propertyNameChanged()` signal. IDs and optional
fields are string/null as above; invalid tracker id/record is null. Nonoptional
conveniences (bools, idx, layout) have default values when invalid; use valid or
the nullable complete record to distinguish absence from a real default value.

`NiriEventLog` retains raw name/payload roles, default capacity 500. Negative
capacity normalizes to zero; zero clears/disables retention. Shrink/eviction
announces every removal. Nested capacity/appends serialize outside row brackets.
History survives stream reset and does not include snapshot reads.

## Compositor limits and within-row moves

niri 26.04 has no atomic arbitrary-ID insertion into a destination column/stack,
no stable column ID, and no output hotplug event. Outputs/configuration are query
data; refresh deliberately. Layout positions/indices can change between calls.
Ordinary bounded moves stay in the row; OrToWorkspace/OrToMonitor variants may
leave it. Consume/expel/stack require tiled windows. There is no animate:false
or silent-focus option; focus may switch workspace/output, scroll or warp.

| # | Primitive | Contract / proof |
|---|---|---|
| 1 | FocusWindow then MoveColumnToIndex (1-based, clamped) | Whole source column, not arbitrary window insertion; representative real outcome tested |
| 2 | ConsumeOrExpelWindowLeft/Right(optional ID) | Stacked source isolates on chosen side; singleton appends into neighbor and removes source column; no neighbor is no-op; preserves local active status, not global focus; explicit nonfocused ID outcomes tested |
| 3 | FocusWindow then MoveWindowUp/Down | Adjacent swap in current column, bounded at edges; representative real outcome tested |
| 4 | ConsumeWindowIntoColumn | Active destination takes topmost right-neighbor tile and appends; keeps destination active tile; source-audited inference, not live acceptance |
| 5 | ExpelWindowFromColumn | Expels bottommost tile right, not necessarily focused; does not activate new column; source-audited inference |
| 6 | SwapWindowLeft/Right | Swaps with neighbor active tile and follows source, not insertion; source-audited inference |

The audited minimal recipes below are **inferences**, not blanket live-tested
algorithms. Assume tiled W/current layout, semantic window-ID neighbors, and
skip satisfied steps. Recalculate indices after removing W. j is the new
1-based destination gap, r the insertion row, m the destination's old tile count.
Explicit-ID consume/expel needs no prior global focus, even on a nonfocused
workspace. FocusColumn(index) and FocusWindowInColumn(index) can prepare an
active column/tile, with the same focus side effects as FocusWindow.

| # | Drop | Minimal inferred sequence |
|---|---|---|
| 1 | Singleton between columns | Focus W if needed, MoveColumnToIndex(j); at most 2 actions, 0 for no change |
| 2 | Stacked W between columns | ID consume/expel isolates; immediate gap needs only that action; otherwise focus W if needed, recalculate j, move column; at most 3 |
| 3 | Within current column | Focus W if needed, repeat ordinary Up/Down by row difference |
| 4 | Singleton into adjacent target | ID consume/expel toward target appends without prior focus; for nonappend focus W and repeat Up to r |
| 5 | General cross-column insertion | Isolate stacked W; focus if needed; move singleton immediately right of target (index excludes W); ID consume left; repeat Up m+1-r times; at most 4 preparation/insertion actions before swaps |

Await each reply, re-read/revalidate state between dependent steps, then verify
final workspaceId and layout.posInScrollingLayout. The owned live acceptance
tests prove representative column, explicit-ID consume/expel and Up/Down
outcomes, not all recipes or arbitrary interference. Handled only means handler
execution. Disappearing IDs, focus changes and reordered columns can invalidate
later steps; focus restoration is best effort. Atomic insertion without focus
effects requires new compositor IPC, not a library promise.

See [DESIGN.md](DESIGN.md) for release maintenance and
[KNOWN_ISSUES.md](KNOWN_ISSUES.md) for verification limits.
