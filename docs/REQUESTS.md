# Request examples

[API.md](../API.md#requests-actions-and-replies) is the canonical authored
request/reply contract. The [generated reference](generated/IPC-API.md#requests)
lists every callable/response type. These complete components are extracted,
linted and run against the mock by the documentation consumer tests.

## Retain each reply

```qml
import QtQml
import Niri 1.0

QtObject {
    id: root
    objectName: "rawQuery"
    property NiriPendingReply pending: null
    property int queryGeneration: 0
    property string version: ""
    function refresh() {
        const generation = ++queryGeneration
        const reply = NiriRequests.sendRaw("Version")
        pending = reply
        reply.finished.connect(function() {
            if (generation === root.queryGeneration && !reply.isError)
                root.version = reply.value.Version
        })
    }
    Component.onCompleted: refresh()
}
```

Raw Version's Ok payload is `{"Version":"..."}`, while typed version().result
is the string. Raw Handled stays a string, not an empty object. document/json
retain the full envelope, including Err and decode failures. Raw integers are
strings; decimal tokens have exact text/toNumber(). Use number(text) or
sendRawJson for exact outbound numeric tokens, not guessed numeric strings.

The local `reply` capture is intentional: replacing pending in a later refresh
must not make an earlier handler read the wrong reply. The generation also keeps
an older completion from overwriting a newer refresh. If a consumer clears query
data on readiness/subscription loss, invalidate its generation there too: one-shot
requests are independent of the subscription and can still succeed afterwards.
Retain all replies whose outcomes matter. They are JavaScript-owned, not
auto-deleted on finished.
completed handles late observers. cancel/timeout/GC/engine teardown only cancel
local waiting and are not rollback. Native callers delete replies or pass an
explicit owner to sendWire. Every completion, including validation errors, is
queued exactly once.

## Explicit order

```qml
import QtQml
import Niri 1.0

QtObject {
    id: root
    objectName: "sequenceQuery"
    property NiriRequestSequence lane: null
    property NiriPendingReply first: null
    property NiriPendingReply second: null
    function run() {
        lane = NiriRequests.sequence()
        first = lane.request("Version")
        second = lane.request("Windows")
    }
    Component.onCompleted: run()
}
```

A lane uses one persistent socket without pipelining. Its action(value) accepts
raw action values (e.g. `{FocusWindow: {id: NiriRequests.number("42")}}`).
Independent calls do not wait for it. Failure/cancel/connection loss/endpoint
change/deletion stops the lane and leaves unsent entries NotSent. No reconnect,
resume, global FIFO, transaction, rollback or animation-completion guarantee is
implied. Revalidate current state between layout-dependent actions.

Trailing `{timeoutMs, connectTimeoutMs}` options apply to raw and typed calls.
Defaults are 5000 ms; timeoutMs -1 disables the response deadline, not connection
establishment. Sequence deadlines begin on activation. Typed picks default to
-1; raw picks need it explicitly. EventStream observes the actual shared session
acknowledgement; observer cancel/deadline does not stop the stream.

Inspect executionCertainty/error.certainty: NotSent, MayHaveExecuted or
ReplyReceived. Failures after bytes enter the send buffer may have executed;
never retry implicitly. Endpoint changes cancel old-revision calls without
retargeting. Width/height/monitor helpers send id, workspace moves send window_id;
monitor focus was removed and workspace focus is an explicit required bool.
