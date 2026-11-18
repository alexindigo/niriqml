#!/usr/bin/env bash
# Exercise the live runner's control contract without a compositor.
set -euo pipefail
root=${1:?usage: test_fixture_runner.sh SCRATCH_ROOT [CASE]}
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
runner="$here/live/run-live.sh"
# shellcheck source=tests/socket-runtime.sh
source "$here/socket-runtime.sh"
mkdir -p "$root"
fixture=$(mktemp -d "$root/fixture-runner.XXXXXXXX")
owned=""
runtime=""
cleanup() {
    # Old/broken start paths can overwrite niri.pid; keep every test-owned PID.
    local pid launches=${NIRIQML_LIVE_RUN:-$fixture/run}/launches
    if [[ -f "$launches" ]]; then
        while read -r pid; do
            builtin kill -KILL "$pid" 2>/dev/null || true
        done < "$launches"
    fi
    if [[ -n "$owned" ]]; then
        builtin kill -KILL "$owned" 2>/dev/null || true
        wait "$owned" 2>/dev/null || true
    fi
    [[ -z "$runtime" ]] || rm -rf -- "$runtime"
    rm -rf -- "$fixture"
}
trap cleanup EXIT
runtime=$(niriqml_socket_runtime)
mkdir "$fixture/bin"
cat > "$fixture/bin/niri" <<'PY'
#!/usr/bin/env python3
import json, os, pathlib, signal, socket, sys
run = pathlib.Path(os.environ["NIRIQML_LIVE_RUN"])
if sys.argv[1] == "--version":
    print("niri 26.04 fixture")
elif sys.argv[1] == "--config":
    with (run / "launches").open("a") as log:
        log.write(f"{os.getpid()}\n")
    sock = socket.socket(socket.AF_UNIX)
    sock.bind(str(pathlib.Path(os.environ["XDG_RUNTIME_DIR"]) / f"niri.fixture.{os.getpid()}.sock"))
    if os.environ.get("STUB_IGNORE_TERM") == "1":
        signal.signal(signal.SIGTERM, lambda *_: (run / "term-seen").touch())
    (run / "stub-ready").touch()
    while True:
        signal.pause()
elif "spawn" in sys.argv:
    with (run / "spawns").open("a") as log:
        log.write(next(arg for arg in sys.argv if arg.startswith("--app-id=")) + "\n")
elif "windows" in sys.argv:
    spawns = (run / "spawns").read_text().splitlines()
    # Simulate the first window arriving only after another window was spawned.
    count = 0 if len(spawns) == 1 and os.environ.get("STUB_WINDOWS_READY") != "1" else len(spawns)
    print(json.dumps([{"app_id": f"niriqml-e9-{i}"} for i in range(1, count + 1)]))
elif "version" in sys.argv:
    print('"26.04 fixture"')
PY
cat > "$fixture/bin/sleep" <<'SH'
#!/usr/bin/env bash
# Shorten bounded polling in these synthetic controls only.
exec /usr/bin/sleep .001
SH
chmod +x "$fixture/bin/niri" "$fixture/bin/sleep"
cat > "$fixture/bin/cage" <<'SH'
#!/usr/bin/env bash
shift # --
exec "$@"
SH
cp "$fixture/bin/cage" "$fixture/bin/foot"
chmod +x "$fixture/bin/cage" "$fixture/bin/foot"
export PATH="$fixture/bin:$PATH"
export NIRIQML_LIVE_RUN="$fixture/run"
export XDG_RUNTIME_DIR="$runtime"
export NIRI_SOCKET="$XDG_RUNTIME_DIR/ipc.sock"

prepare() {
    mkdir -p "$NIRIQML_LIVE_RUN"
}
start_stub() {
    STUB_IGNORE_TERM=1 niri --config unused &
    owned=$!
    for ((i=0; i<200; i++)); do
        [[ -f "$NIRIQML_LIVE_RUN/stub-ready" ]] && break
        /usr/bin/sleep .01
    done
    test -f "$NIRIQML_LIVE_RUN/stub-ready"
    echo "$owned" > "$NIRIQML_LIVE_RUN/niri.pid"
}
stop_case() {
    prepare
    start_stub
    # Socket disappearance must not allow an alive TERM-ignoring process to escape.
    rm -- "$XDG_RUNTIME_DIR/niri.fixture.$owned.sock"
    bash "$runner" --stop
    test -f "$NIRIQML_LIVE_RUN/term-seen"
    if [[ -r /proc/$owned/stat ]]; then
        read -r state < "/proc/$owned/stat"
        [[ ${state##*) } == Z\ * ]]
    fi
    wait "$owned" 2>/dev/null || true
    owned=""
    test ! -e "$NIRIQML_LIVE_RUN/niri.pid"
    bash "$runner" --stop # Repeated cleanup is harmless.
}
refuse_start_case() {
    local mode=${1:---start} state
    local args=("$mode")
    prepare
    start_stub
    if [[ $mode == --inside ]]; then
        args+=(touch "$NIRIQML_LIVE_RUN/inside-ran")
    fi
    if bash "$runner" "${args[@]}" > "$fixture/start.log" 2>&1; then
        echo "$mode accepted a live PID" >&2
        exit 1
    fi
    grep -q 'still running' "$fixture/start.log"
    builtin kill -0 "$owned"
    read -r state < "/proc/$owned/stat"
    [[ ${state##*) } != Z\ * && ${state##*) } != X\ * ]]
    test "$(cat "$NIRIQML_LIVE_RUN/niri.pid")" == "$owned"
    test "$(wc -l < "$NIRIQML_LIVE_RUN/launches")" == 1
    test ! -e "$NIRIQML_LIVE_RUN/term-seen"
    test ! -e "$NIRIQML_LIVE_RUN/inside-ran"
}
unreclaimed_case() {
    prepare
    start_stub
    # Inject refused signals while keeping the real process-existence probe.
    kill() {
        case $1 in -TERM|-KILL) return 1 ;; esac
        builtin kill "$@"
    }
    export -f kill
    kill -0 "$owned"
    status=0
    bash "$runner" --stop > "$fixture/stop.log" 2>&1 || status=$?
    unset -f kill
    test "$status" != 0
    grep -q 'Cannot reclaim owned niri' "$fixture/stop.log"
    test "$(cat "$NIRIQML_LIVE_RUN/niri.pid")" == "$owned"
    builtin kill -0 "$owned"
}
timeout_case() {
    prepare
    status=0
    bash "$runner" --inside true > "$fixture/timeout.log" 2>&1 || status=$?
    test "$status" != 0
    grep -q 'Timed out waiting for fixture window 1' "$fixture/timeout.log"
    test "$(wc -l < "$NIRIQML_LIVE_RUN/spawns")" == 1
    test ! -e "$NIRIQML_LIVE_RUN/niri.pid"
}

runtime_case() {
    local deep status suite_runtime
    deep="$fixture/$(printf '%0120d' 0)"
    mkdir -p "$deep"
    # Both success and wrapped-command failure must reclaim the suite runtime,
    # leaving its deep evidence directory intact. Neither cage nor niri may bind
    # beneath that evidence path (even a connect via a long symlink would fail).
    for status in 0 1; do
        local actual=0
        STUB_WINDOWS_READY=1 NIRIQML_LIVE_SCRATCH_ROOT="$deep" \
            bash "$runner" --suite bash -c '
                test -S "$NIRI_SOCKET" || exit 2
                test "$(stat -c %a "$XDG_RUNTIME_DIR")" = 700 || exit 2
                printf "%s\n" "$XDG_RUNTIME_DIR" > "$1"
                exit "$2"
            ' bash "$fixture/runtime-path" "$status" > "$fixture/suite.log" 2>&1 || actual=$?
        [[ $actual == "$status" ]] || { cat "$fixture/suite.log" >&2; return 1; }
        suite_runtime=$(cat "$fixture/runtime-path")
        test ! -e "$suite_runtime"
        compgen -G "$deep/niriqml-live.*/suite.log" >/dev/null
    done
    # A long TMPDIR is not permission to silently use /tmp or the build tree.
    if TMPDIR="$deep" bash "$runner" --suite true > "$fixture/long-tmp.log" 2>&1; then
        echo 'Runner accepted an overlong TMPDIR' >&2
        return 1
    fi
    grep -q 'TMPDIR is too long for Unix fixture socket paths' "$fixture/long-tmp.log"
    # Check the unset/empty fallback without writing to the guest's /tmp.
    bash -c '
        unset TMPDIR
        source "$1"
        mktemp() { test "$1" = -d && test "$2" = /tmp/nqr.XXXXXXXX; }
        niriqml_socket_runtime
    ' bash "$here/socket-runtime.sh"
    TMPDIR="" bash -c '
        source "$1"
        mktemp() { test "$1" = -d && test "$2" = /tmp/nqr.XXXXXXXX; }
        niriqml_socket_runtime
    ' bash "$here/socket-runtime.sh"
}

case ${2:-all} in
    stop) stop_case ;;
    refuse-start) refuse_start_case ;;
    refuse-inside) refuse_start_case --inside ;;
    unreclaimed) unreclaimed_case ;;
    timeout) timeout_case ;;
    runtime) runtime_case ;;
    all)
        deep_root="$fixture/$(printf '%0120d' 0)"
        for name in stop refuse-start refuse-inside unreclaimed timeout runtime; do
            bash "$0" "$deep_root" "$name"
        done ;;
    *) exit 2 ;;
esac
echo "Fixture runner control passed: ${2:-all}"
