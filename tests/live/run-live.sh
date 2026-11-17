#!/usr/bin/env bash
# Run only in the assigned guest/container. Every suite owns its compositor.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

stop_niri() {
    if [[ -f "$NIRIQML_LIVE_RUN/niri.pid" ]]; then
        pid=$(cat "$NIRIQML_LIVE_RUN/niri.pid")
        if kill -0 "$pid" 2>/dev/null; then
            kill -TERM "$pid"
            for ((i=0; i<100; i++)); do
                [[ ! -S "$NIRI_SOCKET" ]] && break
                sleep .05
            done
        fi
    fi
}

start_niri() {
    local run=$NIRIQML_LIVE_RUN
    niri --config "$here/niri.kdl" >> "$run/niri.log" 2>&1 &
    local pid=$!
    echo "$pid" > "$run/niri.pid"
    local found=""
    for ((i=0; i<200; i++)); do
        kill -0 "$pid" 2>/dev/null || { cat "$run/niri.log" >&2; return 1; }
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
        for ((i=0; i<100; i++)); do
            if niri msg --json windows | python3 -c \
                'import json,sys; n=int(sys.argv[1]); w=json.load(sys.stdin); sys.exit(not any(x["app_id"]==f"niriqml-e9-{n}" for x in w))' "$index"; then
                break
            fi
            sleep .05
        done
    done
    niri msg --json windows > "$run/windows.json"
    python3 - "$run/windows.json" <<'PY'
import json, sys
windows = json.load(open(sys.argv[1]))
assert {w["app_id"] for w in windows} == {f"niriqml-e9-{i}" for i in (1, 2, 3)}, windows
PY
}

case ${1:-} in
    --stop) stop_niri; exit ;;
    --start) start_niri; exit ;;
    --inside)
        shift
        trap 'stop_niri' EXIT
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
