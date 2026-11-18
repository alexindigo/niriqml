#!/usr/bin/env bash
# Exercise the live runner's control contract without a compositor.
set -euo pipefail
root=${1:?usage: test_fixture_runner.sh SCRATCH_ROOT [CASE]}
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
runner="$here/live/run-live.sh"
mkdir -p "$root"
fixture=$(mktemp -d "$root/fixture-runner.XXXXXXXX")
owned=""
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
    rm -rf -- "$fixture"
}
trap cleanup EXIT
mkdir "$fixture/bin"
cat > "$fixture/bin/niri" <<'PY'
#!/usr/bin/env python3
import json, os, pathlib, signal, socket, sys
run = pathlib.Path(os.environ["NIRIQML_LIVE_RUN"])
if sys.argv[1] == "--config":
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
    count = 0 if len(spawns) == 1 else len(spawns)
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
export PATH="$fixture/bin:$PATH"
export NIRIQML_LIVE_RUN="$fixture/run"
export XDG_RUNTIME_DIR="$NIRIQML_LIVE_RUN/runtime"
export NIRI_SOCKET="$NIRIQML_LIVE_RUN/ipc.sock"

prepare() {
    mkdir -p "$XDG_RUNTIME_DIR"
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
    prepare
    start_stub
    if bash "$runner" --start > "$fixture/start.log" 2>&1; then
        echo "start overwrote a live PID" >&2
        exit 1
    fi
    grep -q 'still running' "$fixture/start.log"
    test "$(cat "$NIRIQML_LIVE_RUN/niri.pid")" == "$owned"
    test "$(wc -l < "$NIRIQML_LIVE_RUN/launches")" == 1
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

case ${2:-all} in
    stop) stop_case ;;
    refuse-start) refuse_start_case ;;
    unreclaimed) unreclaimed_case ;;
    timeout) timeout_case ;;
    all)
        for name in stop refuse-start unreclaimed timeout; do
            bash "$0" "$root" "$name"
        done ;;
    *) exit 2 ;;
esac
echo "Fixture runner control passed: ${2:-all}"
