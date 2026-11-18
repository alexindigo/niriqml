import QtQuick
import QtTest
import Niri 1.0
import "../examples" as Examples

TestCase {
    id: tests
    name: "DocumentationAndExamples"
    when: windowShown
    width: 800
    height: 600

    Component { id: windowList; Examples.WindowList {} }
    Component { id: dashboard; Examples.DashboardVisualizer {} }

    function init() {
        tryCompare(NiriState, "initialStateReceived", true)
        compare(NiriState.windowCount, 1)
    }
    function cleanup() {
        compare(examplePeer.warnings.length, 0, "QML engine warnings: " + examplePeer.warnings.join("\n"))
    }
    function test_documentation_data() {
        return documentationSnippets
    }
    function test_documentation(row) {
        const item = createTemporaryQmlObject(row.source, tests, row.path)
        verify(item !== null)
        if (item.objectName === "quickstart" || item.objectName === "workspaces") {
            tryCompare(item, "count", 1)
            wait(100)
            verify(item.itemAtIndex(0) !== null)
            compare(item.itemAtIndex(0).text, item.objectName === "quickstart"
                    ? "Mock title / mock.app" : "main (active)")
        } else if (item.objectName === "trackers") {
            tryCompare(item.children[0], "text", "Mock title")
            compare(item.children[1].text, "main")
            compare(item.children[2].text, "Ready")
        } else if (item.objectName === "versionQuery" || item.objectName === "rawQuery") {
            tryCompare(item, "version", "mock-26.04")
            verify(!item.pending.isError)
            verify(item.pending.completed)
        } else if (item.objectName === "sequenceQuery") {
            tryCompare(item.second, "completed", true)
            verify(!item.first.isError)
            verify(!item.second.isError)
            compare(item.first.value.Version, "mock-26.04")
            compare(item.second.value.Windows[0].id, "7")
        } else if (item.objectName === "actionCall") {
            item.move("7", "1")
            tryCompare(item.pending, "completed", true)
            verify(!item.pending.isError)
            compare(item.pending.result.wireName, "Handled")
            verify(examplePeer.lastActionJson.includes('"window_id":7'))
            verify(examplePeer.lastActionJson.includes('"Id":1'))
            verify(examplePeer.lastActionJson.includes('"focus":false'))
        } else if (item.objectName === "exactRawCall") {
            item.focus("18446744073709551615")
            tryCompare(item.pending, "completed", true)
            verify(!item.pending.isError)
            compare(item.pending.document.Ok, "Handled")
            verify(examplePeer.lastActionJson.includes('"id":18446744073709551615'))
        } else {
            fail("Missing consumer assertion for " + row.tag)
        }
    }
    function test_windowList() {
        const item = createTemporaryObject(windowList, tests)
        verify(item !== null)
        const list = findChild(item, "windowList")
        verify(list !== null)
        tryCompare(list, "count", 1)
        wait(100)
        const row = list.itemAtIndex(0)
        verify(row !== null)
        compare(row.model.id, "7")
        compare(row.children[0].text, "mock.app: Mock title")
    }
    // Last: removal intentionally changes the shared fixture, not a UI-only cache.
    function test_z_dashboardWorkspaceSelection() {
        const item = createTemporaryObject(dashboard, tests)
        verify(item !== null)
        tryCompare(item, "niriVersion", "mock-26.04")
        const windows = findChild(item, "dashboardWindows")
        tryCompare(windows, "count", 1)
        wait(150)
        verify(examplePeer.capture("dashboard-initial"))
        const before = examplePeer.actionCount
        mouseClick(windows.itemAtIndex(0), windows.width / 2, windows.itemAtIndex(0).height / 2)
        tryCompare(item, "selectedWindowId", "7")
        compare(item.selectedWindow.id, "7")
        const windowDetail = findChild(item.contentItem, "windowDetails")
        verify(windowDetail !== null)
        const windowRecord = JSON.parse(windowDetail.text)
        compare(windowRecord.id, "7")
        compare(windowRecord.title, "Mock title")
        compare(windowRecord.pid, null)
        compare(windowRecord.workspaceId, "1")
        const workspaces = findChild(item, "dashboardWorkspaces")
        verify(workspaces !== null)
        tryCompare(workspaces, "count", 1)
        verify(workspaces.width > 0 && workspaces.height > 0,
               "Workspace list geometry: " + workspaces.width + " x " + workspaces.height)
        const row = workspaces.itemAtIndex(0)
        verify(row !== null, "Workspace delegate not instantiated")
        compare(item.selectedWorkspaceId, null)
        compare(item.selectedWorkspace, null)
        mouseClick(row, row.width / 2, row.height / 2)
        tryCompare(item, "selectedWorkspaceId", "1")
        compare(item.selectedWorkspace.id, "1")
        compare(item.selectedWorkspace.name, "main")
        const detail = findChild(item.contentItem, "workspaceDetails")
        compare(detail.text, "ID 1: main")
        const panel = findChild(item, "workspaceDetailPanel")
        verify(panel !== null)
        const heading = findChild(panel, "panel-heading-Workspace details")
        verify(heading !== null)
        const headingPosition = heading.mapToItem(panel, 0, 0)
        const detailPosition = detail.mapToItem(panel, 0, 0)
        verify(Math.abs(headingPosition.y - 8) <= 1, "Heading must stay in its top band")
        verify(Math.abs(detailPosition.y - headingPosition.y - heading.height - 6) <= 1,
               "Workspace detail must follow its heading structurally")
        compare(examplePeer.actionCount, before, "Local selection must not send compositor actions")
        wait(100)
        verify(examplePeer.capture("dashboard-selected"))
        examplePeer.removeWorkspace()
        tryCompare(item, "selectedWorkspaceId", null)
        tryCompare(item, "selectedWorkspace", null)
        tryCompare(detail, "text", "Select a workspace")
        examplePeer.removeWindow()
        tryCompare(item, "selectedWindowId", null)
        tryCompare(item, "selectedWindow", null)
        tryCompare(windows, "count", 0)
        tryCompare(windowDetail, "text", "Select a window to inspect its properties.")
        wait(100)
        verify(examplePeer.capture("dashboard-removed"))
        wait(150)
    }
}
