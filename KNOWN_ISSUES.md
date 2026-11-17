# Known Issues

## Historical QML teardown/coverage crash: cause unconfirmed

On 2026-11-17, the E9 investigation ran `test_api`, `test_events` and
`test_session_review` in fresh processes ten times each in three separate Debug
builds: coverage OFF, GCC `--coverage` ON, and AddressSanitizer with
`halt_on_error=1` and leak detection enabled. All 90 suite executions passed;
no teardown crash or sanitizer diagnostic was observed. The tests cover real
QML reply/state consumers, engine destruction and reconnect/exit paths.

Environment: DigitalOcean guest `niriqml-full-ipc-api`, Arch container,
Qt 6.12.0, GCC 16.2.1. The bounded investigation took less than 45 minutes.
Raw evidence is in the external E9 execution record's `teardown-matrix.log`.
This does not reproduce the older Qt/toolchain environment, establish a Qt bug,
identify the historical cause, or guarantee absence of lifecycle defects.
Earlier lifecycle corrections on this branch have independent regression
evidence; this non-reproduction cannot attribute the old crash to any one fix.
CI retains a distinct coverage-OFF QML lane without claiming an external cause.

## niri 26.04: "error writing reply" / Broken pipe WARN in niri log

**Status:** niri-side bug — fix submitted upstream (PR #4180 / mailing list patch)
**Reported:** 2026-08-07 (Atmosphera VM testing)
**Severity:** cosmetic — WARN-level log, no functional impact

### Symptom

niri 26.04 logs the following when an niriqml client disconnects:

```
WARN niri::ipc::server: error handling IPC client: error writing reply
Caused by: Broken pipe (os error 32)
```

This appears for every client disconnect. With niriqml's auto-reconnect
enabled, short-lived clients (test binaries, quick-connect-and-disconnect
shells) produce two broken pipe entries per run.

### Root cause

In niri's `src/ipc/server.rs`, client disconnect is handled inconsistently:

- `handle_event_stream_client()` — correctly handles `BrokenPipe` as normal
  disconnection (returns `Ok`)
- `handle_client()` — does NOT handle `BrokenPipe` on the reply write path.
  When a client sends a query/action and disconnects before niri writes
  the `{"Ok":"Handled"}` reply, `write_all()` returns `EPIPE`, which
  propagates through `.context("error writing reply")` and is logged as a
  WARN.

The event stream path already has the pattern. The regular request/response
path is missing it.

### Why this is NOT an niriqml bug

niriqml's connection lifecycle is correct:

1. `NiriConnection::disconnect()` uses `m_socket.abort()` — immediate
   close, which prevents the QTimer-in-destructor SIGSEGV (fixed in
   v0.1.2)
2. The `NiriConnection` destructor also calls `abort()` as a safety net
   for process teardown
3. niri's IPC server has no way to know the client disconnected until
   it tries to write — EPIPE is inherent to Unix socket teardown

We investigated alternative approaches (graceful `disconnectFromServer()`
+ `waitForDisconnected()`, `shutdown(SHUT_WR)` + `nanosleep()`) on the VM.
None prevented the EPIPE on niri's side. The race is in niri's write path,
not niriqml's close path.

### Impact

**None.** The WARN is cosmetic. niri continues operating. niriqml's
auto-reconnect handles the event stream seamlessly. Events stream
correctly. No data loss. No functional degradation.

### Fix

The fix is in niri's `handle_client()`. After the `write_all()` call:

```rust
match write.write_all(&buf).await {
    Ok(()) => (),
    Err(err) if err.kind() == io::ErrorKind::BrokenPipe => return Ok(()),
    Err(err) => return Err(err).context("error writing reply"),
}
```

This pattern already exists in `handle_event_stream_client()`. It brings
the regular request/response path to parity.

The fix has been submitted to niri upstream (PR #4180, mailing list patch
to `~nikoyak/niri-dev@lists.sr.ht`).

### Workaround

None needed. The WARN is safe to ignore. No action required from niriqml
consumers.

### Verification

Reproduced in a VM with cage headless + nested niri 26.04. The WARN fires
2× per test run (test_live_wire, test_live_events, etc.). Confirmed events
stream correctly (all 6 initial event types received). Confirmed no
functional regression.
