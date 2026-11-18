#!/usr/bin/env bash
# Run only in the assigned guest/container. Every suite owns its compositor.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
started_niri=0

owned_pid() {
    local pid
    pid=$(cat "$NIRIQML_LIVE_RUN/niri.pid")
    [[ $pid =~ ^[1-9][0-9]*$ && $pid != 1 ]] || {
        echo "Invalid owned niri PID: $pid" >&2
        return 1
    }
    printf '%s\n' "$pid"
}

process_running() {
    local state
    kill -0 "$1" 2>/dev/null || return 1
    # A dead child may await reaping by its parent (including container PID 1).
    # kill -0 alone reports those zombies as alive, though they cannot run.
    IFS= read -r state < "/proc/$1/stat" 2>/dev/null || return 1
    state=${state##*) }
    [[ $state != Z\ * && $state != X\ * ]]
}

stop_niri() {
    local pid signal i
    if [[ -f "$NIRIQML_LIVE_RUN/niri.pid" ]]; then
        pid=$(owned_pid) || return 1
        for signal in TERM KILL; do
            process_running "$pid" || break
            kill -"$signal" "$pid" 2>/dev/null || true
            for ((i=0; i<100; i++)); do
                process_running "$pid" || break
                sleep .05
            done
        done
        if process_running "$pid"; then
            echo "Cannot reclaim owned niri $pid after TERM and KILL" >&2
            return 1
        fi
        wait "$pid" 2>/dev/null || true # Reap when this shell is the parent.
        rm -- "$NIRIQML_LIVE_RUN/niri.pid"
    fi
}

start_niri() {
    local run=$NIRIQML_LIVE_RUN pid i candidate index appeared
    if [[ -f "$run/niri.pid" ]]; then
        pid=$(owned_pid) || return 1
        if process_running "$pid"; then
            echo "Owned niri $pid is still running; stop it before starting another" >&2
            return 1
        fi
    fi
    niri --config "$here/niri.kdl" >> "$run/niri.log" 2>&1 &
    pid=$!
    echo "$pid" > "$run/niri.pid"
    started_niri=1
    local found=""
    for ((i=0; i<200; i++)); do
        process_running "$pid" || { cat "$run/niri.log" >&2; return 1; }
        for candidate in "$XDG_RUNTIME_DIR"/niri.*."$pid".sock; do
            [[ -S "$candidate" ]] && found=$candidate
        done
        [[ -n "$found" ]] && break
        sleep .05
    done
    [[ -n "$found" ]] || { echo "No IPC socket from owned niri $pid" >&2; return 1; }
    ln -sfn -- "$found" "$NIRI_SOCKET"
    niri msg --json version
    niri msg action focus-workspace e9-main
    for index in 1 2 3; do
        niri msg action spawn -- foot --app-id="niriqml-e9-$index" \
            --title="niriqml fixture $index" sh -c 'exec sleep 600'
        # Enforce deterministic column creation/focus order, not process-start order.
        appeared=0
        for ((i=0; i<100; i++)); do
            if niri msg --json windows | python3 -c \
                'import json,sys; n=int(sys.argv[1]); w=json.load(sys.stdin); sys.exit(not any(x["app_id"]==f"niriqml-e9-{n}" for x in w))' "$index"; then
                appeared=1
                break
            fi
            sleep .05
        done
        if ((!appeared)); then
            echo "Timed out waiting for fixture window $index" >&2
            return 1
        fi
    done
    niri msg --json windows > "$run/windows.json"
    python3 - "$run/windows.json" <<'PY'
import json, sys
windows = json.load(open(sys.argv[1]))
assert {w["app_id"] for w in windows} == {f"niriqml-e9-{i}" for i in (1, 2, 3)}, windows
PY
}

cleanup_failed_start() {
    local status=$?
    if ((status && started_niri)); then
        stop_niri || status=1
    fi
    exit "$status"
}

case ${1:-} in
    --stop) stop_niri; exit ;;
    --start)
        trap cleanup_failed_start EXIT
        start_niri
        exit ;;
    --inside)
        shift
        trap 'status=$?; stop_niri || status=1; exit "$status"' EXIT
        start_niri
        export NIRIQML_LIVE_FIXTURE=1
        "$@"
        exit ;;
    --suite)
        shift
        if ! command -v niri >/dev/null; then
            echo "Live suite unavailable: niri is not installed" >&2
            exit 77
        fi
        for tool in cage foot python3; do
            if ! command -v "$tool" >/dev/null; then
                echo "Live suite unavailable: missing $tool" >&2
                exit 1
            fi
        done
        [[ $(niri --version) == 'niri 26.04 '* ]] || {
            echo "Live suite requires released niri 26.04" >&2; exit 1;
        }
        : "${TMPDIR:?Set disk-backed TMPDIR inside the project guest workspace}"
        export NIRIQML_LIVE_RUN
        NIRIQML_LIVE_RUN=$(mktemp -d "$TMPDIR/niriqml-live.XXXXXXXX")
        export XDG_RUNTIME_DIR="$NIRIQML_LIVE_RUN/runtime"
        mkdir -m 700 "$XDG_RUNTIME_DIR"
        export XDG_CACHE_HOME="$NIRIQML_LIVE_RUN/cache"
        export XDG_CONFIG_HOME="$NIRIQML_LIVE_RUN/config"
        mkdir -p "$XDG_CACHE_HOME" "$XDG_CONFIG_HOME"
        export NIRI_SOCKET="$NIRIQML_LIVE_RUN/ipc.sock"
        export NIRIQML_LIVE_CONTROL="$here/run-live.sh"
        export WLR_BACKENDS=headless WLR_RENDERER=pixman
        export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
        unset DISPLAY WAYLAND_DISPLAY
        # Cage/niri diagnostics and exact fixtures remain in this disk-backed run.
        echo "Owned live fixture: $NIRIQML_LIVE_RUN"
        status=0
        timeout 180 cage -- bash "$here/run-live.sh" --inside "$@" \
            > "$NIRIQML_LIVE_RUN/suite.log" 2>&1 || status=$?
        cat "$NIRIQML_LIVE_RUN/suite.log"
        if ((status)); then
            cat "$NIRIQML_LIVE_RUN/niri.log" >&2
        fi
        exit "$status" ;;
    *)
        [[ $# == 1 ]] || { echo "Usage: $0 BUILD_DIRECTORY" >&2; exit 2; }
        exec ctest --test-dir "$1" -L live --output-on-failure ;;
esac
