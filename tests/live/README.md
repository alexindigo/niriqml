# Owned live niri fixtures

Live tests never discover or use a desktop socket. Each CTest `live` case wraps
its executable in `run-live.sh --suite`, starts a private headless cage and
nested niri 26.04, and creates three foot windows through niri's Spawn action.
It waits for each app ID before creating the next, making column order and
focus deterministic. Two named workspaces and disabled animations come from
`niri.kdl`. The tests discover actual IDs from stream state.

All builds/tests run inside DigitalOcean guest `niriqml-full-ipc-api`, Arch
container `localhost/niriqml-dev:latest`, source `/w/src`, disk-backed scratch
`/w/tmp`, through the project's guest helper. Never run these on a physical host.

From the host, transport only:

```sh
G=/home/user/TemporaryFiles/niriqml/plans/full-ipc-api/tools/guest
$G sync
$G run -t 1800 'cmake --build /w/build/E8 -j8 && ctest --test-dir /w/build/E8 -L live --output-on-failure'
```

The wrapper returns 77 (CTest skipped) only when niri is not installed.
Missing fixture tools, wrong niri version, compositor startup failure or missing
windows fail with diagnostics. Required tools are cage, foot, Python 3 and a
software EGL renderer. Test commands receive only the wrapper's explicit socket.
Mutating cases are serialized by CTest. Normal unit/QML runs exclude the live
label (or names beginning `test_live_`).

Each run prints its disk-backed `TMPDIR/niriqml-live.*` evidence directory:
suite/cage log, niri log, exact seeded windows and owned niri PID. EXIT cleanup
terminates only that nested niri; cage exits with its runner. No host compositor
or shell is involved. The installed cage 0.3.1 has no runtime option to disable
its compiled-in Xwayland integration: its released `cage.c` calls
`wlr_xwayland_create` unconditionally when `CAGE_HAS_XWAYLAND` is set.
`WLR_XWAYLAND` selects a binary; it is not a disable switch and does not prevent
listener allocation. Consequently cage itself uses `/tmp/.X11-unix` despite
TMPDIR. niri 26.04 has no built-in Xwayland server. Test payloads and outputs
stay in the workspace. Avoiding cage's path would require a different cage build,
not an invented runtime flag or a change to the test's compositor approach.
Source: <https://github.com/cage-kiosk/cage/blob/v0.3.1/cage.c>.

`test_live_acceptance` checks fresh state/model-only QML consumers, shared
subscription/readiness, every query, typed actions and output configuration,
nonfocused resize/workspace moves and within-row movement through reducer
outcomes. It compares early/late projections, waits beyond five seconds for
interactive picks then cancels locally, restarts its own compositor using a
stable socket symlink, and verifies reset/repopulation and valid empty state.
Local pick cancellation does not claim to cancel the compositor-side picker.
