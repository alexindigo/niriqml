import Niri 1.0
import QtQuick

Rectangle {
    width: 400
    height: 600
    color: "#1e1e2e"

    Column {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4

        Text {
            color: "#cdd6f4"
            text: "Windows: " + NiriState.windowCount
        }

        Text {
            color: "#cdd6f4"
            text: "Focused: " + NiriState.focusedWindowAppId
        }

        ListView {
            width: parent.width
            height: parent.height - 40
            model: NiriWindows {}
            delegate: Rectangle {
                width: parent.width
                height: 32
                color: model.window.isFocused ? "#45475a" : "#313244"

                Text {
                    anchors.centerIn: parent
                    color: "#cdd6f4"
                    text: model.window.appId + ": " + model.window.title
                }
            }
        }
    }
}
