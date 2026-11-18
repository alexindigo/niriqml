import QtQuick
import Niri 1.0

Item {
    id: root
    property bool loadConsumers: false
    property bool passed: consumers.item ? consumers.item.passed : false
    Loader {
        id: consumers
        active: root.loadConsumers
        sourceComponent: Item {
            id: page
            property var expected: ["0", "9007199254740993", "9223372036854775808", "18446744073709551615"]
            component ProjectionRow: QtObject {
                required property var model
                readonly property string observedId: model.id
            }
            property NiriWindows windows: NiriWindows {}
            property NiriWorkspaces workspaces: NiriWorkspaces {}
            property NiriWindow window: NiriWindow { windowId: page.expected[3] }
            property NiriWorkspace workspace: NiriWorkspace { workspaceId: page.expected[3] }
            property var current: NiriState.snapshot()
            property bool passed: NiriState.initialStateReceived
                    && current.initialStateReceived && NiriState.overviewOpen
                    && NiriState.overview.isOpen && NiriState.configFailed
                    && NiriState.keyboardLayouts.names.length === 2
                    && NiriState.focusedWindowId === expected[3]
                    && NiriState.focusedWorkspaceId === expected[3]
                    && NiriState.activeOutput === "output"
                    && NiriState.windowsById[expected[3]].id === expected[3]
                    && NiriState.workspacesById[expected[3]].id === expected[3]
                    && NiriState.castsByStreamId[expected[3]].streamId === expected[3]
                    && NiriState.window(expected[3]).id === expected[3]
                    && NiriState.workspace(expected[3]).id === expected[3]
                    && NiriState.workspaceByName(expected[3]).id === expected[3]
                    && NiriState.cast(expected[3]).streamId === expected[3]
                    && NiriState.window("9") === null
                    && NiriState.windowsSnapshot().length === 4
                    && NiriEvents.lastWindowsSnapshot().length === 4
                    && window.valid && window.id === expected[3]
                    && window.window.layout.tilePosInWorkspaceView[0] === 11
                    && workspace.valid && workspace.id === expected[3]
                    && windowRows.count === 4 && workspaceRows.count === 4
                    && rowsMatch(windowRows) && rowsMatch(workspaceRows)
            function rowsMatch(rows: Instantiator): bool {
                for (let i = 0; i < expected.length; ++i) {
                    const row = rows.objectAt(i) as ProjectionRow;
                    if (!row || row.observedId !== expected[i])
                        return false;
                }
                return true;
            }
            Instantiator {
                id: windowRows
                model: page.windows
                delegate: ProjectionRow {}
            }
            Instantiator {
                id: workspaceRows
                model: page.workspaces
                delegate: ProjectionRow {}
            }
        }
    }
}
