import QtQml
import Niri 1.0

QtObject {
    id: root
    property bool ready: NiriState.initialStateReceived
    property int notifications: 0
    property bool observedAtCasts: false
    property bool expectLayouts: false
    property int expectedWindows: 2
    property var expectedFocus: "1"
    property Connections stateListener: Connections {
        target: NiriState
        function onInitialStateReceivedChanged() { root.notifications++; }
    }
    property Connections eventListener: Connections {
        target: NiriEvents
        function onCastsChanged(casts) {
            root.observedAtCasts = NiriState.initialStateReceived
                    && NiriEvents.lastWindowsSnapshot().length === root.expectedWindows
                    && NiriEvents.lastConfigFailed()
                    && (NiriEvents.qmlLastKeyboardLayouts() !== null) === root.expectLayouts
                    && NiriEvents.qmlLastFocusedWindowId() === root.expectedFocus
        }
    }
}
