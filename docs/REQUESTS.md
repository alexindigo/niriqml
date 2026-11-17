# Requests and replies

`NiriRequests` and `NiriActions` share one operation path. Independent calls use
separate sockets and have no cross-call ordering. Every completion is queued,
even an empty endpoint or invalid input. The same-turn pattern
`let r = NiriRequests.version(); r.finished.connect(handler)` observes completion.
If a nested event loop ran before attaching the handler, inspect `r.completed`.

## Retention and cancellation

QML replies are unparented with JavaScriptOwnership. They no longer delete
themselves after `finished`. Keep a reference while waiting or displaying a result:

```qml
property NiriPendingReply pending: null
function refresh() {
    const reply = NiriRequests.version()
    pending = reply // retention only; each handler observes its own reply
    reply.finished.connect(function() {
        if (reply.isError)
            console.log(reply.error.category, reply.error.message)
    })
}
```

Collecting/deleting a pending reply cancels its local operation. Engine/facade
teardown also cancels that facade's operations. `cancel()` completes a pending
reply once with `Cancelled`; after terminal completion it is a no-op. Cancellation,
timeout and connection loss are not rollback and cannot dismiss niri's pick UI.

C++ callers own replies they retain. For QObject ownership, use
`sendWire(request, options, owner)`; otherwise explicitly delete the reply.
Completion handlers may safely delete the reply. All acquisition and calls,
including cancellation, use the application thread.

## Reply data

| # | Property | Meaning |
|---|---|---|
| 1 | `completed`, `status` | Pending, Succeeded or Failed; terminal data is coherent before `finished()` |
| 2 | `isError`, `error` | Structured failure; legacy `code`/`message` remain |
| 3 | `value` | Complete raw Ok payload; `{"Ok":"Handled"}` yields `"Handled"`. Named queries retain their existing typed projection |
| 4 | `document`, `json` | Whole Ok/Err envelope as QML projection and original JSON text; retained on decode errors |
| 5 | `responseKind` | String or single-key Response discriminator, including unknown names |
| 6 | `endpoint`, `endpointRevision`, `requestId` | Captured call identity, with integer identities exposed as strings |
| 7 | `executionCertainty` | NotSent, MayHaveExecuted or ReplyReceived |

Raw integer tokens project to decimal strings, including small integers. Decimal
tokens expose a `niriJsonNumber` with exact `text` and explicit `toNumber()`.
C++ `wireDocument()` preserves the exact tree and frame bytes. The legacy C++
`send`/`sendJson` callbacks remain lossy object-only adapters, not the full raw API.

Errors carry category, certainty, request/endpoint identity and phase; socket
errors, parse offsets and decode field paths are supplied where applicable.
Categories are Configuration, Transport, Timeout, Framing, Parse, Protocol,
Server, Decode, Cancelled and EndpointChanged. Once bytes enter the socket send
buffer, failure conservatively reports MayHaveExecuted. A complete reply frame
reports ReplyReceived, including rejected envelopes. Requests are never retried.

An endpoint change cancels old-revision work without retargeting it. Stream
reconnect at the same endpoint does not cancel independent requests.

## Raw input and deadlines

`sendRaw("Version")` encodes the JSON string request, not JSON source text.
Objects, arrays, strings, finite numbers, booleans and null use the same encoder.
`sendRawJson(text)` validates complete JSON and preserves numeric tokens through
encoding; it does not permit a second LF-delimited request. Use
`NiriRequests.number("18446744073709551615")` for exact numbers in structured raw
objects. Ordinary strings in unknown fields remain strings, never guessed IDs.
Invalid number-builder text returns undefined; sending it is a queued failure.

Raw methods accept `{timeoutMs: 5000, connectTimeoutMs: 5000}`. `timeoutMs: -1`
disables the total response deadline, while connection establishment remains
bounded. Other negative values, non-integers and unknown options fail visibly.
The defaults are 5000 ms. Sequence entries start their deadlines when activated,
not while waiting behind another entry. Raw interactive picks should pass
`{timeoutMs: -1}`; generated pick helpers will select that default.

The seven existing named queries reject a wrong Response discriminator or outer
payload type with Decode, preserving the envelope. Their existing gadget
conversion remains transitional until generated field/range codecs replace it.

## Explicit sequences

```qml
property NiriRequestSequence lane: null
property NiriPendingReply first: null
property NiriPendingReply second: null
function run() {
    lane = NiriRequests.sequence()
    first = lane.request("Version")
    second = lane.action({FocusWindow: {id: NiriRequests.number("42")}})
}
```

The retainable sequence owns one persistent socket. It sends the next entry only
after the previous reply, without pipelining. Independent calls do not wait for
it. Error, timeout, cancellation, deleting a pending entry, endpoint change or
connection loss stops the sequence; unsent entries complete as NotSent. A stopped
sequence cannot resume or reconnect; create a new sequence deliberately.

Keep the sequence and each desired reply referenced. Deleting/collecting the
sequence cancels its entries. `lane.cancel()` stops it explicitly; `stopped`
reports retirement. No global FIFO, transaction, rollback, event-stream
convergence or animation-completion guarantee is implied by ordering or Handled.

The monitor helper now emits `id` without the unsupported `focus` member and
accepts only `(windowId, output)`. Width/height helpers emit `id` as well.
Generated APIs will supply the remaining optional/focused argument forms.
