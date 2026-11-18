pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Niri 1.0

ApplicationWindow {
    id: root
    title: "niriqml dashboard"
    width: 1400
    height: 900
    visible: true
    color: "#11111b"
    palette.window: "#1e1e2e"
    palette.base: "#181825"
    palette.text: "#cdd6f4"
    palette.placeholderText: "#a6adc8"
    palette.button: "#313244"
    palette.buttonText: "#cdd6f4"
    palette.highlight: "#45475a"
    palette.highlightedText: "#cdd6f4"

    // Only UI selection, query results and traffic statistics are local.
    // Current compositor state always comes from the shared reducer.
    property var selectedWindowId: null
    property var selectedWorkspaceId: null
    readonly property var selectedWindow: windowTracker.window
    readonly property var selectedWorkspace: workspaceTracker.workspace
    property NiriOutputsReply outputsReply: null
    property NiriVersionReply versionReply: null
    property var outputs: ({})
    property string niriVersion: ""
    property string lastAction: ""
    property var pendingActions: []
    property int eventCount: 0
    property int previousEventCount: 0
    property real eventRate: 0
    property string eventFilter: ""

    NiriWindows { id: windows }
    NiriWorkspaces { id: workspaces }
    NiriEventLog { id: history; capacity: 500 }
    NiriWindow { id: windowTracker; windowId: root.selectedWindowId }
    NiriWorkspace { id: workspaceTracker; workspaceId: root.selectedWorkspaceId }

    Connections {
        target: NiriState
        function onWindowsChanged() {
            if (root.selectedWindowId !== null && NiriState.window(root.selectedWindowId) === null)
                root.selectedWindowId = null
        }
        function onWorkspacesChanged() {
            if (root.selectedWorkspaceId !== null && NiriState.workspace(root.selectedWorkspaceId) === null)
                root.selectedWorkspaceId = null
        }
    }
    Connections {
        target: NiriEvents
        function onRawEvent() { root.eventCount++ }
    }
    Timer {
        interval: 3000
        repeat: true
        running: true
        onTriggered: {
            root.eventRate = (root.eventCount - root.previousEventCount) / 3
            root.previousEventCount = root.eventCount
        }
    }

    function retainAction(reply) {
        pendingActions = pendingActions.concat([reply])
        reply.finished.connect(function() {
            root.lastAction = reply.isError ? reply.error.message : "Handled (observe state for effect)"
            root.pendingActions = root.pendingActions.filter(function(item) { return item !== reply })
        })
    }
    function refreshQueries() {
        const out = NiriRequests.outputs()
        outputsReply = out
        out.finished.connect(function() {
            if (!out.isError)
                root.outputs = out.result
        })
        const version = NiriRequests.version()
        versionReply = version
        version.finished.connect(function() {
            if (!version.isError)
                root.niriVersion = version.result
        })
    }
    Connections {
        target: NiriConnection
        function onSubscribedChanged() {
            if (NiriConnection.subscribed)
                root.refreshQueries()
            else {
                root.outputs = ({})
                root.niriVersion = ""
            }
        }
    }
    Component.onCompleted: {
        if (NiriConnection.subscribed)
            refreshQueries()
    }

    component Panel: Rectangle {
        id: panel
        property string heading
        property bool stretchContent: false
        default property alias content: body.data
        color: "#1e1e2e"
        radius: 4
        border.color: "#313244"
        implicitHeight: frame.implicitHeight + 16
        ColumnLayout {
            id: frame
            anchors.fill: parent
            anchors.margins: 8
            spacing: 6
            Label {
                objectName: "panel-heading-" + panel.heading
                text: panel.heading
                color: "#f5c2e7"
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            ColumnLayout {
                id: body
                spacing: 4
                Layout.fillWidth: true
                Layout.fillHeight: panel.stretchContent
            }
            Item { visible: !panel.stretchContent; Layout.fillHeight: true }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Rectangle {
                implicitWidth: 14; implicitHeight: 14; radius: 7
                color: NiriState.initialStateReceived ? "#a6e3a1" : "#f38ba8"
            }
            Label {
                text: (NiriState.initialStateReceived ? "Ready: " : "Waiting: ")
                      + NiriConnection.socketPath + " " + NiriConnection.errorString
                color: "#cdd6f4"
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Button {
                text: "Start"
                visible: NiriConnection.status === NiriConnection.Stopped
                         || NiriConnection.status === NiriConnection.Failed
                onClicked: NiriConnection.start()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            ColumnLayout {
                Layout.preferredWidth: 450
                Layout.maximumWidth: 450
                Layout.minimumWidth: 0
                Layout.fillHeight: true
                Panel {
                    heading: "Windows (" + NiriState.windowCount + ")"
                    stretchContent: true
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    RowLayout {
                        Button {
                            text: "Focus"
                            visible: windowTracker.valid
                            onClicked: root.retainAction(NiriActions.focusWindow(root.selectedWindowId))
                        }
                        Button {
                            text: "Close"
                            visible: windowTracker.valid
                            onClicked: root.retainAction(NiriActions.closeWindow(root.selectedWindowId))
                        }
                        Label { text: root.lastAction; color: "#cdd6f4"; elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                    ListView {
                        id: windowList
                        objectName: "dashboardWindows"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: windows
                        clip: true
                        delegate: ItemDelegate {
                            required property var model
                            width: ListView.view.width
                            highlighted: model.id === root.selectedWindowId
                            text: model.id + " / " + (model.appId ?? "(unknown)")
                                  + ": " + (model.title ?? "(untitled)") + " / ws " + (model.workspaceId ?? "(none)")
                            onClicked: root.selectedWindowId = model.id
                        }
                    }
                }
                Panel {
                    heading: windowTracker.valid ? "Window " + windowTracker.id : "Window details"
                    stretchContent: true
                    Layout.fillWidth: true
                    Layout.preferredHeight: 340
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        TextArea {
                            objectName: "windowDetails"
                            readOnly: true
                            text: root.selectedWindow === null ? "Select a window to inspect its properties."
                                  : JSON.stringify(root.selectedWindow, null, 2)
                            color: "#cdd6f4"
                            wrapMode: TextEdit.Wrap
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Panel {
                    heading: "Event log (" + root.eventCount + ", " + root.eventRate.toFixed(1) + "/s)"
                    stretchContent: true
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    TextField {
                        placeholderText: "Filter events"
                        Layout.fillWidth: true
                        onTextChanged: root.eventFilter = text.toLowerCase()
                    }
                    ListView {
                        id: eventList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: history
                        clip: true
                        delegate: ItemDelegate {
                            required property var model
                            width: ListView.view.width
                            visible: root.eventFilter === "" || model.name.toLowerCase().includes(root.eventFilter)
                            height: visible ? implicitHeight : 0
                            text: model.name + ": " + JSON.stringify(model.payload)
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: false
                    Layout.preferredHeight: 190
                    Panel {
                        heading: "Workspaces (" + NiriState.workspaceCount + ")"
                        stretchContent: true
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        ListView {
                            id: workspaceList
                            objectName: "dashboardWorkspaces"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            model: workspaces
                            clip: true
                            delegate: ItemDelegate {
                                required property var model
                                objectName: "workspace-" + model.id
                                width: ListView.view.width
                                highlighted: model.id === root.selectedWorkspaceId
                                text: model.idx + ": " + (model.name ?? "(unnamed)") + " / " + (model.output ?? "(no output)")
                                onClicked: root.selectedWorkspaceId = model.id
                            }
                        }
                    }
                    Panel {
                        objectName: "workspaceDetailPanel"
                        heading: "Workspace details"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Label {
                            objectName: "workspaceDetails"
                            text: workspaceTracker.valid
                                  ? "ID " + workspaceTracker.id + ": " + (workspaceTracker.name ?? "(unnamed)")
                                  : "Select a workspace"
                            color: "#cdd6f4"
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: "Output: " + (workspaceTracker.output ?? "(none)")
                            color: "#cdd6f4"
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: "Active window: " + (workspaceTracker.activeWindowId ?? "(none)")
                            color: "#cdd6f4"
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: "Index: " + workspaceTracker.idx + " / active: " + workspaceTracker.isActive
                                  + " / focused: " + workspaceTracker.isFocused + " / urgent: " + workspaceTracker.isUrgent
                            color: "#cdd6f4"
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label { text: "Selection is local, not compositor focus."; color: "#cdd6f4"; elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                }
                Panel {
                    heading: "Outputs (query snapshot) / niri " + root.niriVersion
                    Layout.fillWidth: true
                    RowLayout {
                        Label {
                            text: Object.keys(root.outputs).join(", ") || "No queried outputs"
                            color: "#cdd6f4"
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Button { text: "Refresh"; onClicked: root.refreshQueries() }
                    }
                    Repeater {
                        model: Object.keys(root.outputs)
                        delegate: Label {
                            required property string modelData
                            readonly property var output: root.outputs[modelData]
                            text: modelData + ": " + output.make + " " + output.model
                                  + " / " + output.modes.length + " modes / VRR " + output.vrrEnabled
                            color: "#cdd6f4"
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }
                Panel {
                    heading: "Keyboard / overview / config / casts"
                    Layout.fillWidth: true
                    Label {
                        text: "Layouts: " + (NiriState.keyboardLayouts === null ? "unknown"
                              : NiriState.keyboardLayouts.names.join(", ") + " [" + NiriState.keyboardLayouts.currentIdx + "]")
                        color: "#cdd6f4"
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        text: "Overview: " + NiriState.overviewOpen + " / Config failed: " + NiriState.configFailed + " / Casts: " + NiriState.castCount
                        color: "#cdd6f4"
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }
}
