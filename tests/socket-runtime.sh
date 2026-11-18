#!/usr/bin/env bash
# Socket storage is independent of evidence/build paths. The caller owns cleanup.
niriqml_socket_runtime() {
    local base template candidate
    local LC_ALL=C # sun_path counts bytes, not locale-dependent characters.
    base=$(cd -- "${TMPDIR:-/tmp}" && pwd) || return 1
    template="$base/nqr.XXXXXXXX"
    # niri includes its Wayland display and PID. Reserve their full signed-int
    # decimal widths; this also covers cage's Wayland socket/lock and our stub.
    candidate="$template/niri.wayland-2147483647.2147483647.sock"
    if ((${#candidate} >= 108)); then
        echo "TMPDIR is too long for Unix fixture socket paths (${#candidate} bytes; maximum 107): $base. Set TMPDIR to a shorter disk-backed directory." >&2
        return 1
    fi
    mktemp -d "$template"
}
