import Niri 1.0
import QtQuick
import QtQuick.Layouts

Rectangle {
    width: 400
    height: 600
    color: "#1e1e2e"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4
        Text { color: "#cdd6f4"; text: "Windows: " + NiriState.windowCount }
        Text { color: "#cdd6f4"; text: "Focused: " + (NiriState.focusedWindowAppId ?? "(none)") }
        ListView {
            objectName: "windowList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: NiriWindows {}
            delegate: Rectangle {
                id: windowRow
                required property var model
                width: ListView.view.width
                height: 32
                color: model.isFocused ? "#45475a" : "#313244"
                Text {
                    anchors.centerIn: parent
                    color: "#cdd6f4"
                    text: (windowRow.model.appId ?? "(unknown)") + ": " + (windowRow.model.title ?? "(untitled)")
                }
            }
        }
    }
}
