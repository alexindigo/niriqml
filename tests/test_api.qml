import Niri 1.0
import QtQuick
import QtTest

TestCase {
    name: "NiriModule"

    function test_singletonsResolvable() {
        verify(NiriConnection, "NiriConnection singleton not found")
        verify(NiriEvents, "NiriEvents singleton not found")
        verify(NiriState, "NiriState singleton not found")
        verify(NiriActions, "NiriActions singleton not found")
        verify(NiriRequests, "NiriRequests singleton not found")
    }

    function test_connectionProperties() {
        verify(typeof NiriConnection.isConnected !== "undefined")
        verify(typeof NiriConnection.socketPath !== "undefined")
        verify(typeof NiriConnection.errorString !== "undefined")
    }

    function test_stateProperties() {
        verify(typeof NiriState.focusedWindowId !== "undefined")
        verify(typeof NiriState.focusedWindowTitle !== "undefined")
        verify(typeof NiriState.windowCount !== "undefined")
        verify(typeof NiriState.workspaceCount !== "undefined")
        verify(typeof NiriState.activeOutput !== "undefined")
    }

    function test_eventLog() {
        let log = createTemporaryQmlObject(
            'import Niri 1.0; NiriEventLog {}', this)
        verify(log, "NiriEventLog creation failed")
        verify(typeof log.capacity !== "undefined")
    }

    function test_windowsModel() {
        let w = createTemporaryQmlObject(
            'import Niri 1.0; NiriWindows {}', this)
        verify(w, "NiriWindows model creation failed")
    }

    function test_workspacesModel() {
        let w = createTemporaryQmlObject(
            'import Niri 1.0; NiriWorkspaces {}', this)
        verify(w, "NiriWorkspaces model creation failed")
    }

    function test_windowReactive() {
        let w = createTemporaryQmlObject(
            'import Niri 1.0; NiriWindow {}', this)
        verify(w, "NiriWindow reactive creation failed")
    }

    function test_workspaceReactive() {
        let w = createTemporaryQmlObject(
            'import Niri 1.0; NiriWorkspace {}', this)
        verify(w, "NiriWorkspace reactive creation failed")
    }
}
