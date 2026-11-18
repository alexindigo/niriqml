# Known issues and verification limits

## Historical QML teardown/coverage crash: cause unconfirmed

On 2026-11-17, E9 ran test_api, test_events and test_session_review in fresh
processes ten times each in three Debug builds: coverage OFF, GCC --coverage ON,
and AddressSanitizer with halt_on_error=1 and normal leak detection. All 90 suite
executions passed without a crash or sanitizer diagnostic. They exercise real
QML reply/state consumers, engine destruction and reconnect/exit paths.

Environment: DigitalOcean guest niriqml-full-ipc-api, Arch container, Qt 6.12.0,
GCC 16.2.1. The bounded investigation took less than 45 minutes. Evidence is in
the external E9 execution record's teardown-matrix.log. It does not reproduce the
older toolchain, establish a Qt bug, identify the historical cause, attribute it
to a particular earlier correction, or guarantee absence of lifecycle defects.
CI retains a separate coverage-OFF QML lane without claiming an external cause.

## Minimum Qt and newer compositor runtime

Qt >= 6.5 remains the declared build requirement. Runtime/build/import results
reported for this branch use Qt 6.12; Qt 6.5 has not been exercised at runtime.
Source inspection and layout fixtures are not a minimum-Qt runtime proof.

Selected post-v26.04 types/calls/events are verified with pinned independent
Rust serde vectors and mocks, not a running main compositor. Live suites use
niri 26.04. Optional touchpad state can arrive one event after released readiness;
null is unknown, not false. Older niri may reject newer operations.

## Headless cage X11 socket path

Installed cage 0.3.1 unconditionally creates its compiled-in Xwayland listener
and can allocate `/tmp/.X11-unix` despite TMPDIR. WLR_XWAYLAND chooses an
executable, not a disable flag. niri 26.04 has no built-in Xwayland server.
Project checkouts, fixtures, logs and scratch stay in the disk-backed workspace;
this caveat concerns cage itself. See [live harness notes](tests/live/README.md).
No shared infrastructure or different compositor approach is substituted.

## niri 26.04 Broken pipe warnings

When a client closes before a reply write, released niri's regular request
handler can log `error writing reply` / `Broken pipe (os error 32)`. Its event
stream handler treats BrokenPipe as normal disconnection; the regular write path
propagates it. Earlier VM investigation reported this logging race and an
upstream submission. This document does not assert the submission's current
status or that every warning has the same cause.

A warning alone neither proves request failure nor proves successful delivery.
Inspect the reply's certainty and actual consumer state. The current session
uses guarded retirement/epochs; it no longer has the old facade-owned socket
destructor described in historical notes. Do not infer that all lifecycle/data
flow is correct or that no data loss is possible from a cosmetic-log observation.

## Tooling metadata and compositor limits

qmllint can report an unresolved template-base import for
NiriRecordModel<NiriWindow/Workspace>. Fresh QML consumers load/populate in the
guest. No lint categories are suppressed; this metadata warning is not a Qt
lifecycle attribution. Exact diagnostics are retained with verification logs.

niri provides no stable column IDs, atomic arbitrary-ID stack insertion, output
hotplug event or animation-completion acknowledgement. Multi-action layout
recipes are revalidated inferences, not transactions. See
[API limits](API.md#compositor-limits-and-within-row-moves).
