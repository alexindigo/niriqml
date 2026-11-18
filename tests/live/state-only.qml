import QtQml
import Niri 1.0

QtObject {
    property bool ready: NiriState.initialStateReceived
    property int observed: NiriState.windowCount
    property var title: NiriState.focusedWindowTitle
}
