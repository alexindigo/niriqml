import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import Niri 1.0

ApplicationWindow {
    id: root
    title: "niriqml dashboard"
    width: 1400; height: 900
    visible: true
    background: Rectangle { color: "#11111b" }

    // ── Accumulated state ──
    property var windowsMap: ({})
    property var workspacesMap: ({})
    property var keyboardLayouts: null
    property int keyboardIdx: 0
    property bool overviewOpen: false
    property bool configLoadFailed: false
    property string configLoadedAt: ""
    property var castsList: []
    property int eventCount: 0
    property real eventRate: 0.0
    property int selectedWindowId: 0
    property int focusedWindowId: 0
    property int eventLogCap: 500
    property string eventFilter: ""
    property var outputsList: []
    property string niriVersion: ""

    // ── List models ──
    ListModel { id: eventLogModel }
    ListModel { id: windowsModel }
    ListModel { id: workspacesModel }

    // ── Rate timer ──
    property int lastEventCount: 0
    Timer {
        interval: 3000; running: true; repeat: true
        onTriggered: {
            root.eventRate = (root.eventCount - root.lastEventCount) / 3.0;
            root.lastEventCount = root.eventCount;
        }
    }

    // ── Extract gadget to plain JS (sever Qt ownership) ──
    function extractWindow(w) {
        return {
            id: w.id,
            title: String(w.title || ""),
            appId: String(w.appId || ""),
            pid: w.pid,
            workspaceId: w.workspaceId,
            isFocused: w.isFocused,
            isFloating: w.isFloating,
            isUrgent: w.isUrgent,
            layout: {
                posInScrollingLayout: w.layout && w.layout.posInScrollingLayout ? Array.from(w.layout.posInScrollingLayout) : [],
                tileSize: w.layout && w.layout.tileSize ? Array.from(w.layout.tileSize) : [],
                windowSize: w.layout && w.layout.windowSize ? Array.from(w.layout.windowSize) : [],
                tilePosInWorkspaceView: w.layout && w.layout.tilePosInWorkspaceView ? Array.from(w.layout.tilePosInWorkspaceView) : [],
                windowOffsetInTile: w.layout && w.layout.windowOffsetInTile ? Array.from(w.layout.windowOffsetInTile) : []
            },
            focusTimestamp: {
                secs: w.focusTimestamp ? w.focusTimestamp.secs : 0,
                nanos: w.focusTimestamp ? w.focusTimestamp.nanos : 0
            }
        };
    }

    function extractWorkspace(ws) {
        return {
            id: ws.id, idx: ws.idx,
            name: String(ws.name || ""),
            output: String(ws.output || ""),
            isUrgent: ws.isUrgent, isActive: ws.isActive, isFocused: ws.isFocused,
            activeWindowId: ws.activeWindowId
        };
    }

    // ── Model helpers ──
    function rebuildWindowsModel(m) {
        windowsModel.clear();
        var ids = Object.keys(m).sort(function(a,b) { return Number(a) - Number(b); });
        for (var i = 0; i < ids.length; i++) {
            var w = m[ids[i]];
            windowsModel.append({
                id: w.id, appId: w.appId, title: w.title,
                workspaceId: w.workspaceId,
                isFocused: w.isFocused, isFloating: w.isFloating, isUrgent: w.isUrgent
            });
        }
    }

    function upsertWindowInModel(w) {
        for (var i = 0; i < windowsModel.count; i++) {
            if (windowsModel.get(i).id === w.id) {
                windowsModel.set(i, {
                    id: w.id, appId: w.appId, title: w.title,
                    workspaceId: w.workspaceId,
                    isFocused: w.isFocused, isFloating: w.isFloating, isUrgent: w.isUrgent
                });
                return;
            }
        }
        windowsModel.append({
            id: w.id, appId: w.appId, title: w.title,
            workspaceId: w.workspaceId,
            isFocused: w.isFocused, isFloating: w.isFloating, isUrgent: w.isUrgent
        });
    }

    function removeWindowFromModel(id) {
        for (var i = 0; i < windowsModel.count; i++) {
            if (windowsModel.get(i).id === id) {
                windowsModel.remove(i);
                return;
            }
        }
    }

    function rebuildWorkspacesModel(m) {
        workspacesModel.clear();
        var ids = Object.keys(m).sort(function(a,b) { return Number(a) - Number(b); });
        for (var i = 0; i < ids.length; i++) {
            var ws = m[ids[i]];
            workspacesModel.append({
                id: ws.id, idx: ws.idx, name: ws.name,
                output: ws.output,
                isActive: ws.isActive, isFocused: ws.isFocused,
                activeWindowId: ws.activeWindowId
            });
        }
    }

    // ── Event subscription ──
    Connections {
        target: NiriEvents

        function onWindowsChanged(windows) {
            console.log("[QML] onWindowsChanged:", windows.length, "windows");
            var m = {};
            for (var i = 0; i < windows.length; i++) {
                var w = extractWindow(windows[i]);
                if (w.isFocused) root.focusedWindowId = w.id;
                m[w.id] = w;
            }
            root.windowsMap = m;
            rebuildWindowsModel(m);
        }

        function onWorkspacesChanged(workspaces) {
            console.log("[QML] onWorkspacesChanged:", workspaces.length, "workspaces");
            var m = {};
            for (var i = 0; i < workspaces.length; i++) {
                var ws = extractWorkspace(workspaces[i]);
                m[ws.id] = ws;
            }
            root.workspacesMap = m;
            rebuildWorkspacesModel(m);
        }

        function onWindowOpenedOrChanged(window) {
            var w = extractWindow(window);
            console.log("[QML] onWindowOpenedOrChanged: id=" + w.id + " app=" + w.appId + " title=" + w.title);
            if (w.isFocused) root.focusedWindowId = w.id;
            root.windowsMap[w.id] = w;
            root.windowsMap = root.windowsMap;
            upsertWindowInModel(w);
        }

        function onWindowClosed(id) {
            console.log("[QML] onWindowClosed: id=" + id);
            delete root.windowsMap[id];
            root.windowsMap = root.windowsMap;
            removeWindowFromModel(id);
            if (id === root.selectedWindowId)
                root.selectedWindowId = 0;
        }

        function onWindowFocusChanged(id) {
            console.log("[QML] onWindowFocusChanged: id=" + id);
            root.focusedWindowId = id;
        }

        function onKeyboardLayoutsChanged(layouts) {
            // Extract to plain JS to sever Qt gadget ownership
            root.keyboardLayouts = {
                names: layouts.names ? Array.from(layouts.names).map(String) : [],
                currentIdx: layouts.currentIdx
            };
            root.keyboardIdx = layouts.currentIdx;
        }

        function onKeyboardLayoutSwitched(idx) {
            root.keyboardIdx = idx;
        }

        function onOverviewOpenedOrClosed(isOpen) {
            root.overviewOpen = isOpen;
        }

        function onConfigLoaded(failed) {
            root.configLoadFailed = failed;
            root.configLoadedAt = new Date().toLocaleTimeString();
        }

        function onCastsChanged(casts) {
            // Copy to plain JS array to sever Qt ownership
            root.castsList = casts ? Array.from(casts) : [];
        }

        function onRawEvent(name, payload) {
            console.log("[QML] rawEvent:", name);
            root.eventCount++;
            // Avoid JSON.stringify on complex payloads — it can crash on some
            // event shapes (e.g. WindowLayoutsChanged). Just show keys.
            var keys = payload ? Object.keys(payload).join(", ") : "";
            eventLogModel.append({
                ts: new Date().toISOString().substring(11, 23),
                name: name,
                data: "{" + keys + "}"
            });
            while (eventLogModel.count > root.eventLogCap)
                eventLogModel.remove(0);
        }
    }

    // ── Connection strip ──
    Rectangle {
        id: connectionStrip
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 8
        height: 42
        radius: 4
        color: NiriConnection.isConnected ? "#1e1e2e" : "#1e1e2e"
        border.color: NiriConnection.isConnected ? "#45475a" : "#45475a"
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8
            Rectangle {
                width: 14; height: 14; radius: 7
                color: NiriConnection.isConnected ? "#a6e3a1" : "#f38ba8"
            }
            Label {
                color: "#cdd6f4"
                text: NiriConnection.isConnected
                    ? "Connected — " + NiriConnection.socketPath
                    : "Disconnected" + (NiriConnection.errorString ? " (" + NiriConnection.errorString + ")" : "")
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Button {
                text: "Reconnect"
                highlighted: true
                flat: true
                onClicked: NiriConnection.connectToSocket()
            }
        }
    }

    // ── Main columns ──
    RowLayout {
        anchors.top: connectionStrip.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 8
        spacing: 8

        // ── LEFT COLUMN: Windows + Window detail ──
        ColumnLayout {
            Layout.preferredWidth: root.width * 0.30
            Layout.maximumWidth: root.width * 0.30
            Layout.fillHeight: true
            spacing: 8

            // WINDOWS panel
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#1e1e2e"
                radius: 4
                border.color: "#313244"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 4
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: "WINDOWS (" + Object.keys(root.windowsMap).length + ")"
                            color: "#f5c2e7"
                            font.bold: true
                            font.pixelSize: 11
                        }
                        Button {
                            text: "Focus"
                            enabled: root.selectedWindowId > 0
                            flat: true; font.pixelSize: 9
                            padding: 2
                            onClicked: {
                                var reply = NiriActions.focusWindow(root.selectedWindowId);
                                reply.finished.connect(function() {
                                    console.log("Focus:", reply.isError ? "err " + reply.error.message : "ok");
                                });
                            }
                        }
                        Button {
                            text: "Close"
                            enabled: root.selectedWindowId > 0
                            flat: true; font.pixelSize: 9
                            padding: 2
                            onClicked: {
                                var reply = NiriActions.closeWindow(root.selectedWindowId);
                                reply.finished.connect(function() {
                                    console.log("Close:", reply.isError ? "err " + reply.error.message : "ok");
                                });
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#313244" }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: windowsModel
                        clip: true
                        delegate: Rectangle {
                            width: ListView.view.width
                            height: 26
                            color: (model.id === root.focusedWindowId) ? "#45475a" : (model.id === root.selectedWindowId ? "#585b70" : (index % 2 === 0 ? "#1e1e2e" : "#181825"))
                            Row {
                                anchors.fill: parent
                                anchors.margins: 4
                                spacing: 6
                                Text { text: model.id; color: "#cdd6f4"; width: 32; font.family: "monospace"; font.pixelSize: 10 }
                                Text { text: model.appId || "(none)"; color: model.appId ? "#89b4fa" : "#7f849c"; width: 130; elide: Text.ElideRight; font.pixelSize: 10 }
                                Text { text: "ws:" + model.workspaceId; color: "#a6adc8"; width: 40; font.pixelSize: 10 }
                                Text { text: model.title || "(untitled)"; color: model.title ? "#cdd6f4" : "#7f849c"; width: parent.width - 264; elide: Text.ElideRight; font.pixelSize: 10 }
                                Text { text: (model.id === root.focusedWindowId) ? "●" : ""; color: "#a6e3a1"; width: 12 }
                                Text { text: model.isUrgent ? "!" : ""; color: "#f38ba8"; width: 10 }
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: root.selectedWindowId = model.id
                            }
                        }
                    }
                }
            }

            // Detail panel
            Rectangle {
                id: detailPanel
                Layout.fillWidth: true
                Layout.preferredHeight: 360
                color: "#1e1e2e"
                radius: 4
                border.color: "#313244"
                border.width: 1

                property var w: root.windowsMap[root.selectedWindowId] || null

                Flickable {
                    anchors.fill: parent
                    anchors.margins: 6
                    contentWidth: parent.width
                    contentHeight: detailCol.implicitHeight + 12
                    clip: true
                    ScrollBar.vertical: ScrollBar {}

                    ColumnLayout {
                        id: detailCol
                        width: parent.width
                        spacing: 2

                        Label {
                            text: detailPanel.w
                                ? "WINDOW " + detailPanel.w.id
                                : "SELECT A WINDOW"
                            color: "#f5c2e7"
                            font.bold: true
                            font.pixelSize: 11
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: "#313244" }

                        Item { height: 4; visible: !detailPanel.w }
                        Label {
                            visible: !detailPanel.w
                            text: "Click a window in the list to inspect all its properties."
                            color: "#7f849c"
                            font.pixelSize: 10
                        }

                        Item { height: 4; visible: detailPanel.w }
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: detailPanel.w ? "id: " + detailPanel.w.id : ""; color: "#cdd6f4"; font.pixelSize: 10 }
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: detailPanel.w ? "title: " + (detailPanel.w.title || "(empty)") : ""; color: detailPanel.w && detailPanel.w.title ? "#cdd6f4" : "#7f849c"; font.pixelSize: 10 }
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: detailPanel.w ? "appId: " + (detailPanel.w.appId || "(empty)") : ""; color: detailPanel.w && detailPanel.w.appId ? "#cdd6f4" : "#7f849c"; font.pixelSize: 10 }
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: detailPanel.w ? "pid: " + detailPanel.w.pid : ""; color: "#cdd6f4"; font.pixelSize: 10 }
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: detailPanel.w ? "workspaceId: " + detailPanel.w.workspaceId : ""; color: "#cdd6f4"; font.pixelSize: 10 }
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: detailPanel.w ? "isFocused: " + (detailPanel.w.isFocused ? "●" : "○") : ""; color: detailPanel.w && detailPanel.w.isFocused ? "#a6e3a1" : "#585b70"; font.pixelSize: 10 }
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: detailPanel.w ? "isFloating: " + (detailPanel.w.isFloating ? "●" : "○") : ""; color: detailPanel.w && detailPanel.w.isFloating ? "#a6e3a1" : "#585b70"; font.pixelSize: 10 }
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: detailPanel.w ? ("isUrgent: " + (detailPanel.w.isUrgent ? "!" : "○")) : ""; color: detailPanel.w && detailPanel.w.isUrgent ? "#f9e2af" : "#585b70"; font.pixelSize: 10 }

                        Rectangle { Layout.fillWidth: true; height: 1; color: "#313244"; Layout.topMargin: 6; Layout.bottomMargin: 4 }

                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "── layout ──"; color: "#f5c2e7"; font.bold: true; font.pixelSize: 10 }
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: detailPanel.w ? "  posInScrollingLayout: [" + (detailPanel.w.layout.posInScrollingLayout ? detailPanel.w.layout.posInScrollingLayout.join(", ") : "") + "]" : ""; color: "#cdd6f4"; font.pixelSize: 10 }
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: detailPanel.w ? ("  tileSize: [" + detailPanel.w.layout.tileSize.join(", ") + "]") : ""; color: "#cdd6f4"; font.pixelSize: 10 }
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: detailPanel.w ? ("  windowSize: [" + detailPanel.w.layout.windowSize.join(", ") + "]") : ""; color: "#cdd6f4"; font.pixelSize: 10 }
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: detailPanel.w ? ("  tilePosInWorkspaceView: " + (detailPanel.w.layout.tilePosInWorkspaceView.length ? "[" + detailPanel.w.layout.tilePosInWorkspaceView.join(", ") + "]" : "null")) : ""; color: "#cdd6f4"; font.pixelSize: 10 }
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: detailPanel.w ? ("  windowOffsetInTile: [" + detailPanel.w.layout.windowOffsetInTile.join(", ") + "]") : ""; color: "#cdd6f4"; font.pixelSize: 10 }

                        Rectangle { Layout.fillWidth: true; height: 1; color: "#313244"; Layout.topMargin: 6; Layout.bottomMargin: 4 }
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "── focusTimestamp ──"; color: "#f5c2e7"; font.bold: true; font.pixelSize: 10 }
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: detailPanel.w ? "  secs: " + detailPanel.w.focusTimestamp.secs : ""; color: "#cdd6f4"; font.pixelSize: 10 }
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: detailPanel.w ? "  nanos: " + detailPanel.w.focusTimestamp.nanos : ""; color: "#cdd6f4"; font.pixelSize: 10 }
                    }
                }
            }
        }

        // ── RIGHT COLUMN: Event log + Workspaces + Mini panels + Stats (fills) ──
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            // Event log
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#1e1e2e"
                radius: 4
                border.color: "#313244"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 4
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: "EVENT LOG (" + root.eventCount + " total, " + root.eventRate.toFixed(1) + "/s)"
                            color: "#f5c2e7"
                            font.bold: true
                            font.pixelSize: 11
                        }
                        TextField {
                            id: filterField
                            Layout.preferredWidth: 150
                            placeholderText: "filter..."
                            color: "#cdd6f4"
                            font.pixelSize: 10
                            background: Rectangle {
                                color: "#313244"
                                radius: 2
                                border.color: "#45475a"
                            }
                            onTextChanged: root.eventFilter = text.toLowerCase()
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#313244" }
                    ListView {
                        id: eventListView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: eventLogModel
                        clip: true

                        property bool autoScroll: true
                        onContentYChanged: autoScroll = atYEnd
                        onCountChanged: { if (autoScroll) positionViewAtEnd(); }

                        delegate: Rectangle {
                            width: ListView.view.width
                            height: visible ? 28 : 0
                            visible: root.eventFilter === "" || model.name.toLowerCase().includes(root.eventFilter)
                            color: index % 2 === 0 ? "#1e1e2e" : "#181825"
                            Row {
                                anchors.fill: parent
                                anchors.margins: 3
                                spacing: 6
                                Text { text: model.ts; color: "#7f849c"; font.family: "monospace"; font.pixelSize: 9 }
                                Text { text: model.name; color: "#89b4fa"; width: 150; elide: Text.ElideRight; font.pixelSize: 10 }
                                Text { text: model.data; color: "#a6adc8"; width: parent.width - 210; elide: Text.ElideRight; font.family: "monospace"; font.pixelSize: 9 }
                            }
                        }
                    }
                }
            }

            // Workspaces panel
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 160
                color: "#1e1e2e"
                radius: 4
                border.color: "#313244"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 4
                    Label {
                        text: "WORKSPACES (" + Object.keys(root.workspacesMap).length + ")"
                        color: "#f5c2e7"
                        font.bold: true
                        font.pixelSize: 11
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#313244" }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: workspacesModel
                        clip: true
                        delegate: Rectangle {
                            width: ListView.view.width
                            height: 26
                            color: model.isFocused ? "#45475a" : (index % 2 === 0 ? "#1e1e2e" : "#181825")
                            Row {
                                anchors.fill: parent
                                anchors.margins: 4
                                spacing: 6
                                Rectangle {
                                    width: 8; height: 8; radius: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: model.isActive ? "#a6e3a1" : "#585b70"
                                }
                                Text { text: model.idx + "[" + model.id + "]"; color: "#cdd6f4"; width: 58; font.pixelSize: 10 }
                                Text { text: model.name || "(unnamed)"; color: model.name ? "#cdd6f4" : "#7f849c"; width: 70; elide: Text.ElideRight; font.pixelSize: 10 }
                                Text { text: model.output || "-"; color: "#89b4fa"; width: 55; elide: Text.ElideRight; font.pixelSize: 10 }
                                Text { text: model.isFocused ? "★" : ""; color: "#f9e2af"; width: 14 }
                                Text { text: "wid:" + (model.activeWindowId || "-"); color: "#a6adc8"; font.family: "monospace"; font.pixelSize: 10 }
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: root.selectedWorkspaceId = model.id
                            }
                        }
                    }
                }
            }

            // Mini panels: Keyboard | Overview | Config | Casts | Outputs
            GridLayout {
                Layout.fillWidth: true
                columns: 2; rowSpacing: 6; columnSpacing: 6

                Rectangle {
                    Layout.fillWidth: true
                    Layout.columnSpan: 2
                    Layout.preferredHeight: 90
                    color: "#1e1e2e"; radius: 4
                    border.color: "#313244"; border.width: 1
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 6; spacing: 3
                        Label {
                            text: "OUTPUTS (" + root.outputsList.length + ")  •  niri " + root.niriVersion
                            color: "#f5c2e7"; font.bold: true; font.pixelSize: 10
                        }
                        Repeater {
                            model: root.outputsList
                            delegate: Row {
                                spacing: 6
                                Text { text: modelData.name; color: "#89b4fa"; width: 60; font.pixelSize: 10 }
                                Text { text: modelData.make + " " + modelData.model; color: "#cdd6f4"; width: 180; elide: Text.ElideRight; font.pixelSize: 10 }
                                Text { text: modelData.modeCount + " modes"; color: "#a6adc8"; font.pixelSize: 10 }
                                Text { text: modelData.vrrEnabled ? "vrr" : ""; color: "#a6e3a1"; font.pixelSize: 10 }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 70
                    color: "#1e1e2e"; radius: 4
                    border.color: "#313244"; border.width: 1
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 6; spacing: 3
                        Label { text: "KEYBOARD"; color: "#f5c2e7"; font.bold: true; font.pixelSize: 10 }
                        Text { color: "#cdd6f4"; text: root.keyboardLayouts ? "Layouts: " + root.keyboardLayouts.names.join(", ") : "(no data yet)"; elide: Text.ElideRight; font.pixelSize: 10 }
                        Text { color: "#a6e3a1"; text: root.keyboardLayouts ? "Active: " + root.keyboardLayouts.names[root.keyboardIdx] : ""; font.pixelSize: 10 }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 70
                    color: "#1e1e2e"; radius: 4
                    border.color: "#313244"; border.width: 1
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 6; spacing: 3
                        Label { text: "OVERVIEW"; color: "#f5c2e7"; font.bold: true; font.pixelSize: 10 }
                        Row {
                            spacing: 6
                            Rectangle {
                                width: 12; height: 12; radius: 6
                                anchors.verticalCenter: parent.verticalCenter
                                color: root.overviewOpen ? "#a6e3a1" : "#585b70"
                            }
                            Text { color: "#cdd6f4"; text: root.overviewOpen ? "Open" : "Closed"; font.pixelSize: 10 }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 70
                    color: "#1e1e2e"; radius: 4
                    border.color: "#313244"; border.width: 1
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 6; spacing: 3
                        Label { text: "CONFIG"; color: "#f5c2e7"; font.bold: true; font.pixelSize: 10 }
                        Text { color: root.configLoadFailed ? "#f38ba8" : "#a6e3a1"
                               text: root.configLoadedAt
                                   ? (root.configLoadFailed ? "FAILED at " : "OK at ") + root.configLoadedAt
                                   : "(never loaded)"
                               font.pixelSize: 10 }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 70
                    color: "#1e1e2e"; radius: 4
                    border.color: "#313244"; border.width: 1
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 6; spacing: 3
                        Label { text: "CASTS"; color: "#f5c2e7"; font.bold: true; font.pixelSize: 10 }
                        Text { color: "#cdd6f4"; text: "Active: " + root.castsList.length; font.pixelSize: 10 }
                    }
                }
            }

            // Stats
            Label {
                color: "#7f849c"
                text: "NiriState: " + NiriState.windowCount + " win / "
                    + NiriState.workspaceCount + " ws"
                    + "  |  focused: " + (NiriState.focusedWindowAppId || "(none)")
                    + "  |  events: " + root.eventCount
                    + " (" + root.eventRate.toFixed(1) + "/s)"
                font.pixelSize: 10
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
    }

    // Query outputs + version once connected
    function fetchStaticInfo() {
        var outReply = NiriRequests.outputs();
        outReply.finished.connect(function() {
            if (!outReply.isError) {
                var m = outReply.value;
                var list = [];
                for (var key in m) {
                    var out = m[key];
                    list.push({
                        name: out.name,
                        make: out.make,
                        model: out.model,
                        currentMode: out.currentMode,
                        modeCount: out.modes ? out.modes.length : 0,
                        vrrEnabled: out.vrrEnabled
                    });
                }
                root.outputsList = list;
            }
        });
        var verReply = NiriRequests.version();
        verReply.finished.connect(function() {
            if (!verReply.isError)
                root.niriVersion = verReply.value;
        });
    }

    Connections {
        target: NiriConnection
        function onConnectedChanged() {
            if (NiriConnection.isConnected)
                fetchStaticInfo();
        }
    }

    Component.onCompleted: {
        console.log("[QML] Dashboard ready. isConnected =", NiriConnection.isConnected,
                    "socketPath =", NiriConnection.socketPath,
                    "errorString =", NiriConnection.errorString);
        if (!NiriConnection.isConnected)
            NiriConnection.connectToSocket();
        else
            fetchStaticInfo();
    }
}
