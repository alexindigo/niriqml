#!/usr/bin/env bash
# Shared source/build identity checks. Source this from a scripts/ entry point.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
PROJECT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd -P)"

fail() {
    echo "error: $*" >&2
    exit 1
}

resolve_build_dir() {
    local requested=$1 cached_source cached_build
    case "$requested" in
        /*) BUILD_DIR=$(realpath -m -- "$requested") ;;
        *) BUILD_DIR=$(realpath -m -- "${PROJECT_DIR}/${requested}") ;;
    esac
    if [[ "$BUILD_DIR" == / || "${PROJECT_DIR}/" == "${BUILD_DIR}/"* ]]; then
        fail "build directory is the source tree or its ancestor: ${BUILD_DIR}"
    fi
    if [[ -e "${BUILD_DIR}/CMakeCache.txt" ]]; then
        cached_source=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "${BUILD_DIR}/CMakeCache.txt")
        [[ -n "$cached_source" && "$(realpath -m -- "$cached_source")" == "$PROJECT_DIR" ]] \
            || fail "build cache belongs to another source tree: ${BUILD_DIR}"
        cached_build=$(sed -n 's/^CMAKE_CACHEFILE_DIR:INTERNAL=//p' "${BUILD_DIR}/CMakeCache.txt")
        [[ -n "$cached_build" && "$(realpath -m -- "$cached_build")" == "$BUILD_DIR" ]] \
            || fail "build cache belongs to another build directory: ${BUILD_DIR}"
    fi
}

configure_build() {
    # Keep source/build identity owned here, even when forwarding configure options.
    # Accept cache definitions and generator options, not a second -S/-B or preset.
    local args=()
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -D|-G|-A|-T)
                [[ $# -ge 2 ]] || fail "missing value for $1"
                args+=("$1" "$2")
                shift 2
                ;;
            -D?*|-G?*|-A?*|-T?*) args+=("$1"); shift ;;
            *) fail "expected a -D cache definition or -G/-A/-T generator option, got: $1" ;;
        esac
    done
    cmake -B "$BUILD_DIR" -S "$PROJECT_DIR" "${args[@]}"
}

require_build_dir() {
    [[ -f "${BUILD_DIR}/CMakeCache.txt" && -d "${BUILD_DIR}/CMakeFiles" ]] \
        || fail "no configured niriqml build: ${BUILD_DIR}"
    grep -qx 'CMAKE_PROJECT_NAME:STATIC=niriqml' "${BUILD_DIR}/CMakeCache.txt" \
        || fail "not a niriqml build: ${BUILD_DIR}"
}

clean_build_if_requested() {
    if [[ "$1" == --clean ]]; then
        require_build_dir
        echo "Cleaning generated targets in ${BUILD_DIR}"
        # Let CMake remove only its generated outputs, not the caller's directory.
        cmake --build "$BUILD_DIR" --target clean
    fi
}
