import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// radio_ui — two-tab shell. Logic lives in radio_module (sandbox blocks network/subprocess
// in QML). Call it via logos.callModule("radio_module", method, [args]).
// Sandbox rules (qml-sandbox-restrictions): no QtMultimedia, no QtGraphicalEffects, no network
// URLs, no FileDialog, no Qt.openUrlExternally. Inside layouts use implicitHeight, never height.
Item {
    id: root
    width: 480; height: 640

    property var streamCard: null   // set after startStream succeeds (#7); null = setup form
    property string streamState: "idle"  // polled from getStreamStatus (#8)

    // #8: poll origin status while streaming (skill qml-timer-state-polling).
    Timer {
        interval: 1500; repeat: true
        running: root.streamCard !== null
        onTriggered: { var r = root.callParse("getStreamStatus", []); if (r && r.state) root.streamState = r.state }
    }
    function stateLabel() {
        return root.streamState === "live"      ? "🔴 Live (announcing)"
             : root.streamState === "receiving" ? "Receiving stream…"
                                                 : "Waiting for OBS…"
    }
    function stateColor() {
        return root.streamState === "live"      ? "#e5484d"
             : root.streamState === "receiving" ? "#f5a623"
                                                 : "#8b949e"
    }

    // ---- Listen (#9) ----
    property var stations: []
    property string playingName: ""
    property bool discoveryStarted: false
    property int volume: 75

    Timer {  // poll the live directory while the Listen tab is open
        interval: 2000; repeat: true
        running: tabs.currentIndex === 1
        onTriggered: { var r = root.callParse("getStations", []); if (r && r.ok) root.stations = r.stations || [] }
    }
    Connections {
        target: tabs
        function onCurrentIndexChanged() {
            if (tabs.currentIndex === 1 && !root.discoveryStarted) {
                root.call("startDiscovery", []); root.discoveryStarted = true
            }
        }
    }
    function playStation(s) {
        var r = root.call("play", [s.streamUrl, s.name || ""])
        if (r && r.ok) root.playingName = s.name || s.path
    }
    function uptime(startedAtMs) {
        if (!startedAtMs) return ""
        var sec = Math.floor((Date.now() - startedAtMs) / 1000)
        return sec < 60 ? sec + "s" : sec < 3600 ? Math.floor(sec/60) + "m" : Math.floor(sec/3600) + "h"
    }

    property string lastError: ""   // #15 — surfaced in the banner; "" hides it

    function callParse(method, args) {
        try {
            var raw = logos.callModule("radio_module", method, args || [])
            var t = JSON.parse(raw)
            return (typeof t === "string") ? JSON.parse(t) : t
        } catch (e) { return null }
    }

    // #15 — human-readable copy for backend error codes (no silent dead-ends).
    function errorMessage(code) {
        var m = {
            "name_required": "Enter a station name first.",
            "already_streaming": "You're already broadcasting.",
            "mediamtx_not_found": "Broadcast server (MediaMTX) isn't available on this system.",
            "mediamtx_spawn_failed": "Couldn't start the broadcast server.",
            "mediamtx_port_or_config": "Broadcast server failed to start — a port may already be in use.",
            "config_write_failed": "Couldn't write the broadcast server config.",
            "ffplay_not_found": "Playback unavailable — ffplay (ffmpeg) is missing.",
            "ffplay_failed": "Couldn't start playback for that station.",
            "no_url": "That station didn't provide a stream URL.",
            "no_delivery_client": "Discovery service (delivery_module) is unavailable.",
            "discovery_not_started": "Open the Listen tab to start discovery first."
        }
        return m[code] || ("Something went wrong (" + code + ").")
    }

    // Checked call: runs the method and surfaces any {ok:false} error in the banner.
    function call(method, args) {
        var r = callParse(method, args)
        if (!r) root.lastError = "No response from radio_module."
        else if (r.ok === false) root.lastError = errorMessage(r.error)
        return r
    }

    // Clipboard via a hidden TextEdit — Qt.openUrlExternally/clipboard APIs are blocked in the sandbox.
    TextEdit { id: clipHelper; visible: false }
    function copyText(t) { clipHelper.text = t; clipHelper.selectAll(); clipHelper.copy(); clipHelper.text = "" }

    function startStream() {
        var cfg = JSON.stringify({
            name: nameField.text,
            visibility: visGroup.checkedButton === privateBtn ? "private" : "public",
            description: descField.text
        })
        var r = call("startStream", [cfg])
        if (r && r.ok) { root.streamState = "waiting"; root.streamCard = r }
    }
    function stopStream() { callParse("stopStream", []); root.streamCard = null; root.streamState = "idle" }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // #15 error banner — surfaces backend failures instead of silent dead-ends.
        Rectangle {
            Layout.fillWidth: true
            color: "#3a1d1d"
            visible: root.lastError.length > 0
            implicitHeight: visible ? errRow.implicitHeight + 16 : 0   // implicitHeight, not height (layout bug)
            RowLayout {
                id: errRow
                anchors.fill: parent; anchors.margins: 8; spacing: 8
                Label { text: "⚠"; color: "#ff9a9a" }
                Label { text: root.lastError; color: "#ff9a9a"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Button { text: "✕"; flat: true; onClicked: root.lastError = "" }
            }
        }

        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: "Stream" }
            TabButton { text: "Listen" }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // ---------------- Stream tab (#7) ----------------
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 14

                    Label { text: "Stream"; font.pixelSize: 22; font.bold: true }

                    // --- Setup form (shown until a stream starts) ---
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        visible: root.streamCard === null

                        Label { text: "Station name" }
                        TextField {
                            id: nameField
                            Layout.fillWidth: true
                            placeholderText: "What listeners see"
                            text: "My Station"
                        }

                        Label { text: "Visibility" }
                        RowLayout {
                            spacing: 16
                            ButtonGroup { id: visGroup }
                            RadioButton { id: publicBtn; text: "Public"; checked: true; ButtonGroup.group: visGroup }
                            RadioButton { id: privateBtn; text: "Private"; ButtonGroup.group: visGroup }
                        }

                        Label { text: "Description (optional)" }
                        TextField {
                            id: descField
                            Layout.fillWidth: true
                            placeholderText: "Genre or a short note"
                        }

                        Button {
                            text: "Start"
                            enabled: nameField.text.length > 0
                            onClicked: root.startStream()
                        }
                    }

                    // --- OBS setup card (shown once streaming) ---
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        visible: root.streamCard !== null

                        // #8 live status light — polled from getStreamStatus.
                        RowLayout {
                            spacing: 8
                            Rectangle { width: 12; height: 12; radius: 6; color: root.stateColor() }
                            Label { text: root.stateLabel(); font.pixelSize: 15; font.bold: true }
                        }

                        Label { text: "Point OBS here"; font.pixelSize: 16; font.bold: true }
                        Label {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap; opacity: 0.7
                            text: "In OBS → Settings → Stream, paste the WHIP URL (Service: WHIP), or use RTMP with the Server + Stream Key below."
                        }

                        component CopyRow: RowLayout {
                            property string label: ""
                            property string value: ""
                            Layout.fillWidth: true
                            spacing: 8
                            Label { text: parent.label; Layout.preferredWidth: 90 }
                            TextField { Layout.fillWidth: true; readOnly: true; text: parent.value }
                            Button { text: "Copy"; onClicked: root.copyText(parent.value) }
                        }

                        CopyRow { label: "WHIP URL"; value: root.streamCard ? root.streamCard.whipUrl : "" }
                        CopyRow { label: "RTMP Server"; value: root.streamCard ? root.streamCard.rtmpUrl : "" }
                        CopyRow { label: "Stream Key"; value: root.streamCard ? root.streamCard.streamKey : "" }

                        Button { text: "Stop"; onClicked: root.stopStream() }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            // ---------------- Listen tab (#9) ----------------
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 12
                    Label { text: "Listen"; font.pixelSize: 22; font.bold: true }

                    ListView {
                        id: stationList
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true; spacing: 6
                        model: root.stations
                        delegate: ItemDelegate {
                            required property var modelData
                            width: ListView.view ? ListView.view.width : 0
                            onClicked: root.playStation(modelData)
                            contentItem: ColumnLayout {
                                spacing: 2
                                Label { text: modelData.name || "Unknown"; font.bold: true }
                                Label {
                                    text: (modelData.host || "") + " · " + root.uptime(modelData.startedAt)
                                    opacity: 0.7; font.pixelSize: 12
                                }
                            }
                        }
                    }
                    // #14 empty / transitional state
                    ColumnLayout {
                        visible: root.stations.length === 0
                        Layout.fillWidth: true; spacing: 8
                        BusyIndicator {
                            running: root.discoveryStarted
                            Layout.alignment: Qt.AlignHCenter
                            implicitWidth: 28; implicitHeight: 28
                        }
                        Label {
                            text: root.discoveryStarted ? "Listening for stations…" : "Open to discover stations"
                            opacity: 0.6; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    RowLayout {  // #13 now-playing: name, volume, stop (no pause for live)
                        visible: root.playingName.length > 0
                        Layout.fillWidth: true; spacing: 8
                        Label { text: "▶ " + root.playingName; Layout.fillWidth: true; elide: Text.ElideRight }
                        Slider {
                            from: 0; to: 100; value: root.volume
                            Layout.preferredWidth: 100
                            onMoved: { root.volume = Math.round(value); root.callParse("setVolume", [root.volume]) }
                        }
                        Button { text: "Stop"; onClicked: { root.callParse("stop", []); root.playingName = "" } }
                    }

                    RowLayout {  // + add private topic
                        Layout.fillWidth: true
                        TextField { id: topicField; Layout.fillWidth: true; placeholderText: "Add a private topic" }
                        Button {
                            text: "Add"; enabled: topicField.text.length > 0
                            onClicked: { root.call("addTopic", [topicField.text]); topicField.text = "" }
                        }
                    }
                }
            }
        }
    }
}
