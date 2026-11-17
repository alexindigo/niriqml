#!/usr/bin/env bash
# Usage: test_scripts.sh SCRATCH_ROOT
# CTest passes a directory inside the build tree, so the test needs no
# particular TMPDIR and leaves nothing behind.
set -euo pipefail
SCRATCH_ROOT=${1:?usage: test_scripts.sh SCRATCH_ROOT}
mkdir -p "$SCRATCH_ROOT"
SCRIPTS="$(cd -- "$(dirname -- "$0")/../scripts" && pwd -P)"
FIXTURE=$(mktemp -d "${SCRATCH_ROOT}/niriqml-scripts.XXXXXX")
trap 'rm -rf -- "$FIXTURE"' EXIT
SOURCE="${FIXTURE}/source with spaces"
mkdir -p "$SOURCE/scripts" "$FIXTURE/caller"
cp "$SCRIPTS/"* "$SOURCE/scripts/"
cat > "$SOURCE/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.16)
project(niriqml LANGUAGES NONE)
option(ENABLE_COVERAGE "Coverage" OFF)
add_custom_target(output ALL COMMAND ${CMAKE_COMMAND} -E touch generated
    BYPRODUCTS generated)
enable_testing()
add_test(NAME fixture COMMAND ${CMAKE_COMMAND} -E true)
install(FILES CMakeLists.txt DESTINATION share/niriqml-fixture)
CMAKE
printf 'keep source\n' > "$SOURCE/sentinel"
cd "$FIXTURE/caller"

reject() {
    if "$@" > "$FIXTURE/rejection.log" 2>&1; then
        echo "unexpected success: $*" >&2
        exit 1
    fi
    grep -q '^error:' "$FIXTURE/rejection.log"
    test -f "$SOURCE/sentinel"
}

for path in "$SOURCE" "$FIXTURE" / "$SOURCE/.."; do
    reject env BUILD_DIR="$path" "$SOURCE/scripts/build"
    reject env BUILD_DIR="$path" "$SOURCE/scripts/build-test" --clean
    reject env BUILD_DIR="$path" "$SOURCE/scripts/run-test-dash"
done
ln -s "$SOURCE" "$FIXTURE/source-alias"
reject env BUILD_DIR="$FIXTURE/source-alias" "$SOURCE/scripts/build" --clean
mkdir "$FIXTURE/not-a-build"
printf 'keep unrelated\n' > "$FIXTURE/not-a-build/sentinel"
reject env BUILD_DIR="$FIXTURE/not-a-build" "$SOURCE/scripts/build" --clean
test -f "$FIXTURE/not-a-build/sentinel"

mkdir "$FIXTURE/foreign"
cat > "$FIXTURE/foreign/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.16)
project(other LANGUAGES NONE)
CMAKE
cmake -S "$FIXTURE/foreign" -B "$FIXTURE/foreign-build" -G Ninja
ln -s "$FIXTURE/foreign-build" "$FIXTURE/foreign-alias"
for path in "$FIXTURE/foreign-build" "$FIXTURE/foreign-alias"; do
    reject env BUILD_DIR="$path" "$SOURCE/scripts/build"
    reject env BUILD_DIR="$path" "$SOURCE/scripts/build-test" --clean
done

# Relative paths resolve against the project, not this unrelated caller cwd.
BUILD_DIR='build with spaces' "$SOURCE/scripts/build" -G Ninja
BUILD="$SOURCE/build with spaces"
test -f "$BUILD/generated"
printf 'keep build neighbor\n' > "$BUILD/sentinel"
BUILD_DIR='build with spaces' "$SOURCE/scripts/build"
BUILD_DIR="$BUILD" "$SOURCE/scripts/build" --clean
test -f "$BUILD/sentinel"
cp -a "$BUILD" "$FIXTURE/copied-cache"
reject env BUILD_DIR="$FIXTURE/copied-cache" "$SOURCE/scripts/build" --clean
reject env BUILD_DIR="$BUILD" "$SOURCE/scripts/build" -B "$SOURCE"
reject env BUILD_DIR="$BUILD" "$SOURCE/scripts/build-test" -S "$FIXTURE/foreign"
reject env BUILD_DIR="$BUILD" "$SOURCE/scripts/build" --preset other
test ! -e "$FIXTURE/caller/build with spaces"
BUILD_DIR="$BUILD" PREFIX="$FIXTURE/install" "$SOURCE/scripts/install"
test -f "$FIXTURE/install/share/niriqml-fixture/CMakeLists.txt"
BUILD_DIR='debug with spaces' "$SOURCE/scripts/build-test" -G Ninja
"$SOURCE/scripts/run-tests" 'debug with spaces'
reject env BUILD_DIR=missing "$SOURCE/scripts/run-test-dash"
test ! -e "$SOURCE/missing"
# Dashboard requires both an existing plugin and an explicitly selected endpoint.
mkdir -p "$BUILD/Niri"
touch "$BUILD/Niri/libniriqmlplugin.so"
reject env -u NIRI_SOCKET BUILD_DIR="$BUILD" "$SOURCE/scripts/run-test-dash"
test -f "$SOURCE/sentinel"
echo "Script path, incremental build and clean checks passed: $FIXTURE"

# Exercise the layout resolver independently of the host distro's Qt layout.
mkdir "$FIXTURE/layout"
cp "$SCRIPTS/../cmake/NiriqmlInstallPaths.cmake" "$FIXTURE/layout/"
cat > "$FIXTURE/qtpaths" <<'TOOL'
#!/usr/bin/env bash
case "$2" in
    QT_INSTALL_PREFIX) echo /selected-sdk ;;
    QT_INSTALL_QML) echo /selected-sdk/imports ;;
    *) exit 1 ;;
esac
TOOL
chmod +x "$FIXTURE/qtpaths"
cat > "$FIXTURE/layout/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.16)
project(layout LANGUAGES NONE)
if(MODE STREQUAL "metadata")
    set(QT6_INSTALL_PREFIX /selected-sdk)
    set(QT6_INSTALL_QML qml)
elseif(MODE STREQUAL "absolute-metadata")
    set(QT6_INSTALL_PREFIX /selected-sdk)
    set(QT6_INSTALL_QML /selected-sdk/imports)
elseif(MODE STREQUAL "outside-metadata")
    set(QT6_INSTALL_PREFIX /selected-sdk)
    set(QT6_INSTALL_QML /external-sdk/imports)
elseif(MODE STREQUAL "tool")
    add_executable(Qt6::qtpaths IMPORTED)
    set_target_properties(Qt6::qtpaths PROPERTIES
        IMPORTED_CONFIGURATIONS RELEASE IMPORTED_LOCATION_RELEASE "${TOOL}")
elseif(MODE STREQUAL "cross")
    set(CMAKE_CROSSCOMPILING TRUE)
endif()
include(NiriqmlInstallPaths.cmake)
if(NOT NIRIQML_INSTALL_QMLDIR STREQUAL EXPECTED)
    message(FATAL_ERROR "Wrong layout: ${NIRIQML_INSTALL_QMLDIR}, expected ${EXPECTED}")
endif()
CMAKE
cmake -S "$FIXTURE/layout" -B "$FIXTURE/layout-metadata" -DMODE=metadata -DEXPECTED=qml
cmake -S "$FIXTURE/layout" -B "$FIXTURE/layout-absolute" -DMODE=absolute-metadata -DEXPECTED=imports
cmake -S "$FIXTURE/layout" -B "$FIXTURE/layout-tool" -DMODE=tool \
    -DTOOL="$FIXTURE/qtpaths" -DEXPECTED=imports
cmake -S "$FIXTURE/layout" -B "$FIXTURE/layout-cross-override" -DMODE=cross \
    -DNIRIQML_INSTALL_QMLDIR=qml -DEXPECTED=qml
if cmake -S "$FIXTURE/layout" -B "$FIXTURE/layout-cross" -DMODE=cross \
        > "$FIXTURE/cross.log" 2>&1; then
    echo 'cross-build without an explicit import root unexpectedly succeeded' >&2
    exit 1
fi
grep -q 'Set NIRIQML_INSTALL_QMLDIR' "$FIXTURE/cross.log"
if cmake -S "$FIXTURE/layout" -B "$FIXTURE/layout-outside" -DMODE=outside-metadata \
        -DEXPECTED=../external-sdk/imports > "$FIXTURE/outside.log" 2>&1; then
    echo 'Qt QML root outside the Qt prefix unexpectedly accepted' >&2
    exit 1
fi
grep -q 'outside its prefix' "$FIXTURE/outside.log"
grep -q 'Set NIRIQML_INSTALL_QMLDIR' "$FIXTURE/outside.log"
cmake -S "$FIXTURE/layout" -B "$FIXTURE/layout-outside-override" -DMODE=outside-metadata \
    -DNIRIQML_INSTALL_QMLDIR=qml -DEXPECTED=qml
echo 'Selected-Qt layout metadata, tool-query fallback and override checks passed'
