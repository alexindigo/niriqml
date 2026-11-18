import Niri 1.0
import QtQuick
import QtTest

TestCase {
    id: root
    name: "NiriModule"

    property string observedTitle: NiriState.focusedWindowTitle || ""
    property var pending: null
    property bool replyFinished: false
    property var result: null

    function test_behavioralConsumers() {
        tryCompare(NiriState, "initialStateReceived", true)
        compare(NiriState.windowCount, 1)
        compare(NiriState.focusedWindowId, "7")
        compare(observedTitle, "Mock title")
        pending = NiriRequests.windows()
        // Attach after return, retaining the exact reply in the closure.
        const reply = pending
        reply.finished.connect(function() {
            root.result = reply.result
            root.replyFinished = true
        })
        tryCompare(root, "replyFinished", true)
        verify(!reply.isError)
        compare(result.length, 1)
        compare(result[0].id, "7")
        compare(result[0].title, "Mock title")
        compare(result[0].appId, null)
        replyFinished = false
        pending = NiriActions.focusWindow("7")
        const action = pending
        action.finished.connect(function() { root.replyFinished = true })
        tryCompare(root, "replyFinished", true)
        verify(!action.isError)
        compare(action.result.wireName, "Handled")
    }

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
