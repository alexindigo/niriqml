# niriqml

Qt 6 QML/C++ bindings for [niri](https://github.com/niri-wm/niri) IPC.
One session and one reducer feed reactive models/state; generated typed calls
cover the released protocol over a permanent lossless raw escape hatch.
No daemon or state polling is required. Requires niri >= 26.04.

The baseline has 16 requests, 141 actions, 9 output controls, 14 responses and
19 events. Explicit pinned newer additions are separate in the
[checked inventory](FeatureMapping.md), not a claim that older niri supports them.

## Quickstart

Any consumer starts the session. Bind to the exported **QML names**, not C++
class names. This complete component displays current windows:

```qml
import QtQuick
import QtQuick.Controls
import Niri 1.0

ListView {
    objectName: "quickstart"
    width: 400
    height: 600
    model: NiriWindows {}
    delegate: ItemDelegate {
        required property var model
        width: ListView.view.width
        highlighted: model.isFocused
        text: (model.title ?? "(untitled)") + " / " + (model.appId ?? "(unknown)")
    }
}
```

Late models immediately read current reducer state, without seed queries.
Before `NiriState.initialStateReceived`, the initial burst can be partial.
Disconnect clears models/state and readiness; the next burst repopulates them.

## Tour

Workspace roles are complete records with string IDs and nullable names/output:

```qml
import QtQuick
import QtQuick.Controls
import Niri 1.0

ListView {
    objectName: "workspaces"
    width: 400
    height: 200
    model: NiriWorkspaces {}
    delegate: ItemDelegate {
        required property var model
        width: ListView.view.width
        text: (model.name ?? "Workspace " + model.idx) + (model.isActive ? " (active)" : "")
        highlighted: model.isFocused
    }
}
```

`NiriWindow` and `NiriWorkspace` are trackers, while lowercase `niriWindow` and
`niriWorkspace` are record values. Native C++ model/tracker class names differ.
Name selection takes precedence over ID selection:

```qml
import QtQuick
import Niri 1.0

Column {
    objectName: "trackers"
    NiriWindow { id: tracked; windowId: NiriState.focusedWindowId }
    NiriWorkspace { id: workspace; workspaceId: NiriState.focusedWorkspaceId }
    Text { text: tracked.valid ? (tracked.title ?? "(untitled)") : "No focused window" }
    Text { text: workspace.valid ? (workspace.name ?? "(unnamed)") : "No focused workspace" }
    Text { text: NiriState.initialStateReceived ? "Ready" : "Waiting for initial state" }
}
```

QML IDs are **decimal strings**, native IDs are quint64. Null means absent;
zero/empty/false remain present values. Typed calls accept exact integral Numbers
too, but cannot recover JavaScript-rounded IDs. Never convert arbitrary IDs
through Number. Use `NiriState.window(id)` or a tracker for current values;
snapshot getters and query results are frozen copies.

Typed queries return retainable replies. Capture each local reply in its own
handler, and retain it while waiting/displaying the result:

```qml
import QtQml
import Niri 1.0

QtObject {
    id: root
    objectName: "versionQuery"
    property NiriVersionReply pending: null
    property int queryGeneration: 0
    property string version: ""
    property string failure: ""
    function refresh() {
        const generation = ++queryGeneration
        const reply = NiriRequests.version()
        pending = reply
        reply.finished.connect(function() {
            if (generation !== root.queryGeneration)
                return
            if (reply.isError)
                root.failure = reply.error.message
            else
                root.version = reply.result
        })
    }
    Component.onCompleted: refresh()
}
```

Completion is always queued, including invalid input. Replies no longer
auto-delete on finished. `cancel()` stops local waiting, not compositor work.
Typed picks default to no response deadline. See
[request examples](docs/REQUESTS.md) and [API contracts](API.md) for full
envelopes, certainty, deadlines, retention and ordered sequences.

All actions have [generated methods](docs/generated/IPC-API.md#actions), e.g.
`NiriActions.moveWindowToWorkspace(id, {Id: workspaceId}, false)` or
`NiriActions.setWindowWidth(id, {SetFixed: 800})`. Use explicit null to skip an
optional ID before required arguments. Monitor moves have no focus parameter.
Independent actions are not ordered/atomic; verify actual state, not Handled.

`NiriEvents` provides every [typed event](docs/generated/IPC-API.md#events) plus
rawEvent for all events, unknownEvent and decodeError. Prefer models/state for
current data instead of writing another reducer. `NiriRequests.sendRaw` and
sendRawJson preserve unmodeled protocol access; [raw contracts](API.md#raw-access-and-ordering)
explain exact numbers and full envelopes.

## Install and packaging

Requires Qt >= 6.5, CMake >= 3.16 and a C++17 compiler. Current runtime proof
uses Qt 6.12; the minimum Qt runtime is not yet verified. Ordinary tests-off
builds need no Rust, Python generator or network schema fetch. Test mode uses
Python 3, Qt Test and QuickTest. Rust is a maintainer-only schema/vector tool.

Arch packages are `qt6-niriqml` and `qt6-niriqml-git` on AUR. Published packages
may precede this Unreleased API; consult their packaged version. Packaging uses
the same CMake install targets as source installs.

```sh
yay -S qt6-niriqml      # release; use qt6-niriqml-git for git master
```

If AUR is unavailable, the packaging repositories also have
[release](https://github.com/alexindigo/aur-qt6-niriqml) and
[git](https://github.com/alexindigo/aur-qt6-niriqml-git) mirrors.

Run development workloads only in the assigned project guest. From its checkout:

```sh
scripts/build -DBUILD_TEST_MODE=OFF  # incremental Release, build-release/
scripts/install                    # configured CMAKE_INSTALL_PREFIX
scripts/build-test                 # Debug tests, coverage off, build-debug/
scripts/run-tests
```

Relative BUILD_DIR resolves from the source root, independent of caller cwd.
Builders accept cache/generator options; choose the directory with BUILD_DIR,
not another -B/preset. `--clean` cleans generated targets only in a verified
build; it never recursively removes caller directories. Coverage is an explicit
separate build with `-DENABLE_COVERAGE=ON`.

Installation follows the **selected Qt** layout relative to the prefix, e.g.
lib/qt6/qml on Arch or qml for an SDK. Set NIRIQML_INSTALL_QMLDIR to override the
import root (without /Niri). Relative overrides relocate; absolute overrides are
fixed destinations and support DESTDIR staging. Cross layouts without usable
selected-Qt metadata need an explicit override, not a PATH qmake guess.

The backing library installs in CMAKE_INSTALL_LIBDIR; plugin/qmldir/qmltypes in
NIRIQML_INSTALL_QMLDIR/Niri; headers in CMAKE_INSTALL_INCLUDEDIR/niriqml
(including generated/); package metadata in CMAKE_INSTALL_LIBDIR/cmake/niriqml.
Plugin RUNPATH locates the backing library
relative to itself, without niriqml-specific LD_LIBRARY_PATH. A private prefix
needs QML_IMPORT_PATH set to its import root; Qt itself must still resolve via
the application's SDK/system loader. `PREFIX=/path scripts/install` explicitly
overrides the configured prefix. Release archives use `cmake --install`, not
manual copies of build products.

Native consumers use the installed config or the same build-tree alias:

```cmake
find_package(niriqml CONFIG REQUIRED)
target_link_libraries(app PRIVATE niriqml::niriqml)
```

CMAKE_PREFIX_PATH selects private installs; Core/Qml/Network requirements
propagate. The existing package version remains unchanged and exact-version
only; no cross-release pre-1.0 ABI promise is implied.

## Examples and verification

`examples/WindowList.qml` is a model/state consumer. `DashboardVisualizer.qml`
adds local window/workspace selection and detail views, typed query-only output
metadata and bounded event history. Clicking a workspace only selects it locally;
it does not focus/move the compositor. `scripts/run-test-dash` requires an
already-built plugin and explicit NIRI_SOCKET for the intended test compositor.

CTest labels distinguish unit, qml and live checks. The QML lane runs without
coverage; a separate C++ coverage lane remains. Documentation fences and examples
are linted/loaded against a protocol-faithful mock, with consumer observations
and dashboard click/removal coverage. [Owned live fixtures](tests/live/README.md)
start isolated headless cage/niri instances; they never discover a desktop socket.
Missing niri is the only clean live skip; a requested broken fixture fails.

See [DESIGN.md](DESIGN.md) for the named guest route and per-release dump,
generate, burst-order review, serde-vector and live verification procedure.
[docs/SCHEMA.md](docs/SCHEMA.md) and the [generated reference](docs/generated/IPC-API.md)
are checked by `python3 tools/generate-ipc.py --check` in that guest.
[KNOWN_ISSUES.md](KNOWN_ISSUES.md) states remaining proof limits.

## License and related projects

GPL-3.0; see [LICENSE](LICENSE).
[mpvqml](https://github.com/alexindigo/mpvqml) and
[dbusqml](https://github.com/alexindigo/dbusqml) are companion Qt/QML libraries.
