import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// radio_ui — dark theme, matching keeper/stash/beacon (palette + header title + dependency
// status pill on the right). Logic lives in radio_module (the QML sandbox blocks network/
// subprocess). Sandbox rules (qml-sandbox-restrictions): no QtMultimedia/QtGraphicalEffects/
// QtQuick.Shapes/FileDialog/network/Qt.openUrlExternally. Inside layouts use implicitHeight.
Item {
    id: root
    width: 480; height: 640

    // ── Dark palette (keeper/stash) ──────────────────────────────────────────
    readonly property color bgPrimary:     "#171717"
    readonly property color bgSecondary:   "#262626"
    readonly property color bgActive:      "#332A27"
    readonly property color textPrimary:   "#FFFFFF"
    readonly property color textSecondary: "#A4A4A4"
    readonly property color textMuted:     "#5D5D5D"
    readonly property color accentOrange:  "#FF5000"
    readonly property color successGreen:  "#22C55E"
    readonly property color warningYellow: "#F59E0B"
    readonly property color errorRed:      "#FB3748"
    readonly property color borderColor:   "#383838"

    // ── State ────────────────────────────────────────────────────────────────
    property var    streamCard:   null
    property string streamState:  "idle"
    property string streamPrivacy: "public"   // public | onion (this host's broadcast)
    property int    listenBuffer:  8           // #17 listener jitter buffer (seconds)
    property string onionAddr:    ""          // our .onion once published (onion mode)
    property bool   onionReady:   false        // hidden-service descriptor published → reachable
    property string onionError:   ""           // non-empty → Tor setup failed/timed out
    property var    stations:     []
    property string playingName:  ""
    property bool   discoveryStarted: false
    property int    volume:       75
    property string deliveryState: "offline"   // offline | ready | connected
    property string deliveryPeerId: ""

    // ── Backend bridge ───────────────────────────────────────────────────────
    function callParse(method, args) {
        try {
            var raw = logos.callModule("radio_module", method, args || [])
            var t = JSON.parse(raw)
            return (typeof t === "string") ? JSON.parse(t) : t
        } catch (e) { return null }
    }
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
            "unsafe_url": "That station's URL is not a safe http(s) stream.",
            "no_url": "That station didn't provide a stream URL.",
            "no_delivery_client": "Discovery service (delivery_module) is unavailable.",
            "invalid_topic": "That topic isn't valid (use /path/like/this).",
            "discovery_not_started": "Open the Listen tab to start discovery first."
        }
        return m[code] || ("Something went wrong (" + code + ").")
    }
    function call(method, args) {
        var r = callParse(method, args)
        // All errors go to the activity log — no banners/toasts.
        if (!r) logEvent("No response from radio_module.", "error")
        else if (r.ok === false) logEvent(errorMessage(r.error), "error")
        return r
    }

    function startStream() {
        var onion = privacyGroup.checkedButton === onionBtn
        var cfg = JSON.stringify({
            name: nameField.text,
            visibility: visGroup.checkedButton === privateBtn ? "private" : "public",
            privacy: onion ? "onion" : "public",
            description: descField.text
        })
        root.streamPrivacy = onion ? "onion" : "public"
        root.onionAddr = ""; root.onionReady = false; root.onionError = ""
        var r = call("startStream", [cfg])
        if (r && r.ok) { root.streamState = "waiting"; root.streamCard = r
            logEvent("Stream started: " + nameField.text + (onion ? " · onion" : " · direct"), "success") }
    }
    function stopStream() {
        logEvent("Stream stopped", "info")
        call("stopStream", []); root.streamCard = null; root.streamState = "idle"
        root.streamPrivacy = "public"; root.onionAddr = ""; root.onionReady = false; root.onionError = ""
    }
    // #11 — after a Basecamp restart, rehydrate the OBS card if a stream auto-resumed in the backend.
    Component.onCompleted: {
        var c = root.callParse("getStreamCard", [])
        if (c && c.ok) {
            root.streamCard = c; root.streamState = "waiting"
            root.streamPrivacy = c.privacy || "public"
            logEvent("Resumed stream after restart: " + (c.name || ""), "success")
        }
    }
    function playStation(s) {
        var r = root.call("play", [s.streamUrl, s.name || ""])
        if (r && r.ok) root.playingName = s.name || s.path
    }
    function uptime(ms) {
        if (!ms) return ""
        var sec = Math.floor((Date.now() - ms) / 1000)
        return sec < 60 ? sec + "s" : sec < 3600 ? Math.floor(sec/60) + "m" : Math.floor(sec/3600) + "h"
    }
    function stateLabel() {
        // In onion mode the announce is held until the Tor descriptor publishes (~30–60s).
        if (root.streamPrivacy === "onion" && !root.onionReady
            && (root.streamState === "live" || root.streamState === "receiving"))
            return "Publishing over Tor…"
        return root.streamState === "live" ? "Live (announcing)"
             : root.streamState === "receiving" ? "Receiving stream…" : "Waiting for OBS…"
    }
    function stateColor() {
        return root.streamState === "live" ? root.errorRed
             : root.streamState === "receiving" ? root.warningYellow : root.textMuted
    }
    function deliveryDotColor() {
        return root.deliveryState === "connected" ? root.successGreen
             : root.deliveryState === "ready" ? root.warningYellow : root.errorRed
    }
    function deliveryLabel() {
        return root.deliveryState === "connected" ? "Discovery online"
             : root.deliveryState === "ready" ? "Discovery ready" : "Discovery offline"
    }
    // OBS pill (#15) — visible while streaming
    function obsLive() { return root.streamState === "live" || root.streamState === "receiving" }
    function obsDotColor() { return obsLive() ? root.successGreen : root.warningYellow }
    function obsLabel() { return obsLive() ? "OBS live" : "Waiting for OBS" }
    // Onion pill (#15) — visible while streaming in onion mode
    function onionDotColor() { return root.onionError.length > 0 ? root.errorRed : root.onionReady ? root.successGreen : root.warningYellow }
    function onionLabel() { return root.onionError.length > 0 ? "Tor error" : root.onionReady ? "Onion ready" : "Publishing over Tor…" }

    // ── Activity log (#12 / #15): every pill change + error is a timestamped record ──────────
    function ts2(n) { return (n < 10 ? "0" : "") + n }
    function nowTs() { var d = new Date(); return "[" + ts2(d.getHours()) + ":" + ts2(d.getMinutes()) + ":" + ts2(d.getSeconds()) + "]" }
    function logEvent(msg, level) {  // oldest-first, newest at bottom + auto-scroll (keycard ActivityLog)
        logModel.append({ "ts": nowTs(), "msg": msg, "level": level || "info" })
        if (logModel.count > 100) logModel.remove(0)
        logList.positionViewAtEnd()
    }
    function levelColor(l) { return l === "success" ? root.successGreen : l === "warning" ? root.warningYellow : l === "error" ? root.errorRed : root.textSecondary }
    ListModel { id: logModel }

    // Fold every status/error transition into the activity log (onXChanged fires only on real change).
    onDeliveryStateChanged: logEvent(deliveryLabel(), deliveryState === "connected" ? "success" : deliveryState === "ready" ? "warning" : "error")
    onStreamStateChanged: if (streamCard !== null) logEvent("OBS: " + obsLabel(), obsLive() ? "success" : "warning")
    onOnionReadyChanged: if (onionReady) logEvent("Onion ready · " + onionAddr, "success")   // the tor link
    onOnionErrorChanged: if (onionError.length > 0) logEvent("Tor: " + onionError, "error")
    onOnionAddrChanged: if (onionAddr.length > 0 && !onionReady) logEvent("Tor: publishing descriptor…", "warning")
    onPlayingNameChanged: logEvent(playingName.length > 0 ? "▶ Playing " + playingName : "■ Stopped playback", "info")

    function copyText(t) { clipHelper.text = t; clipHelper.selectAll(); clipHelper.copy(); clipHelper.text = "" }
    TextEdit { id: clipHelper; visible: false }

    // ── Pollers ──────────────────────────────────────────────────────────────
    Timer {  // delivery node status (always — drives the header pill)
        interval: 2000; repeat: true; running: true; triggeredOnStart: true
        onTriggered: { var r = root.callParse("getDeliveryStatus", []);
            if (r && r.ok) { root.deliveryState = r.state; root.deliveryPeerId = r.peerId || "" } }
    }
    Timer {  // origin status + card sync (#8/#11) — always on, keeps the UI in lock-step with the backend
        interval: 1500; repeat: true; running: true; triggeredOnStart: true
        onTriggered: {
            var r = root.callParse("getStreamStatus", [])
            if (!r) return
            var streaming = r.state && r.state !== "idle"
            // Backend broadcasting but the UI has no card (restart / auto-resume / module reopened) →
            // rehydrate, so the Stream form is never shown while a station is live (no "already broadcasting").
            if (streaming && root.streamCard === null) {
                var c = root.callParse("getStreamCard", [])
                if (c && c.ok) { root.streamCard = c; root.streamPrivacy = c.privacy || "public" }
            } else if (!streaming && root.streamCard !== null) {
                root.streamCard = null   // backend stopped (stream ended / stopped elsewhere) → drop stale card
            }
            if (r.state) { root.streamState = r.state
                if (r.privacy) root.streamPrivacy = r.privacy
                if (r.onion !== undefined) root.onionAddr = r.onion
                if (r.onionReady !== undefined) root.onionReady = r.onionReady
                root.onionError = r.onionError || "" }
        }
    }
    Timer {  // live directory while on the Listen tab (#9)
        interval: 2000; repeat: true; running: tabs.currentIndex === 1
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

    // ── Reusable dark controls ───────────────────────────────────────────────
    component StatusPill: Rectangle {
        id: pill
        property color dot: root.textMuted
        property string label: ""
        height: 28; radius: 14
        implicitWidth: spRow.implicitWidth + 20
        color: Qt.rgba(0.149, 0.149, 0.149, 0.85)
        border.color: root.borderColor; border.width: 1
        Layout.alignment: Qt.AlignVCenter
        RowLayout {
            id: spRow
            anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
            spacing: 6
            Rectangle { width: 7; height: 7; radius: 4; Layout.alignment: Qt.AlignVCenter; color: pill.dot }
            Text { text: pill.label; font.pixelSize: 11; color: root.textPrimary }
        }
    }
    component DarkButton: Button {
        id: db
        contentItem: Text {
            text: db.text; font.pixelSize: 14
            color: !db.enabled ? root.textMuted : root.textPrimary
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 6; implicitHeight: 34; implicitWidth: 72
            color: db.down ? root.bgActive : db.hovered ? "#3a3a3a" : root.bgSecondary
            border.color: root.borderColor; border.width: 1
        }
    }
    component AccentButton: Button {
        id: ab
        contentItem: Text {
            text: ab.text; font.pixelSize: 14; font.bold: true
            color: ab.enabled ? "#FFFFFF" : root.textMuted
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 6; implicitHeight: 34; implicitWidth: 84
            color: !ab.enabled ? root.bgSecondary : ab.down ? "#CC4000" : root.accentOrange
        }
    }
    component DarkField: TextField {
        color: root.textPrimary
        placeholderTextColor: root.textMuted
        selectionColor: root.accentOrange
        background: Rectangle {
            radius: 6; implicitHeight: 34
            color: root.bgSecondary
            border.color: parent && parent.activeFocus ? root.accentOrange : root.borderColor
            border.width: 1
        }
    }
    // Native text layout (no overlap) + dark indicator; label recoloured via palette.
    component DarkRadio: RadioButton {
        id: dr
        spacing: 8
        palette.windowText: root.textPrimary
        indicator: Rectangle {
            implicitWidth: 18; implicitHeight: 18; radius: 9
            x: dr.leftPadding; y: dr.topPadding + (dr.availableHeight - height) / 2
            color: "transparent"; border.width: 2
            border.color: dr.checked ? root.accentOrange : root.textMuted
            Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 4
                color: root.accentOrange; visible: dr.checked }
        }
    }

    // ── Background ────────────────────────────────────────────────────────────
    Rectangle { anchors.fill: parent; color: root.bgPrimary }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Header: title (left) + delivery status pill (right) ──────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16; Layout.rightMargin: 16; Layout.topMargin: 14; Layout.bottomMargin: 6
            spacing: 8
            ColumnLayout {
                spacing: 1
                Label { text: "Radio"; color: root.textPrimary; font.pixelSize: 22; font.bold: true }
                Label { text: "Decentralized broadcast & discovery"; color: root.textSecondary; font.pixelSize: 11 }
            }
            Item { Layout.fillWidth: true }
            RowLayout {  // status pills (#15): Discovery · OBS · Onion
                spacing: 8
                Layout.alignment: Qt.AlignVCenter
                StatusPill { dot: root.deliveryDotColor(); label: root.deliveryLabel() }
                StatusPill { visible: root.streamCard !== null; dot: root.obsDotColor(); label: root.obsLabel() }
                StatusPill { visible: root.streamCard !== null && root.streamPrivacy === "onion"
                             dot: root.onionDotColor(); label: root.onionLabel() }
            }
        }

        // No error banner — all errors go to the activity log (#12).

        // ── Tabs ──────────────────────────────────────────────────────────────
        TabBar {
            id: tabs
            Layout.fillWidth: true
            Layout.topMargin: 6
            background: Rectangle { color: "transparent" }
            component DarkTab: TabButton {
                id: tb
                contentItem: Text {
                    text: tb.text; font.pixelSize: 15; font.bold: tb.checked
                    color: tb.checked ? root.textPrimary : root.textSecondary
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: "transparent"
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2
                        color: tb.checked ? root.accentOrange : "transparent" }
                }
            }
            DarkTab { text: "Stream" }
            DarkTab { text: "Listen" }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: root.borderColor }

        StackLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // ── Stream tab ────────────────────────────────────────────────────
            Item {
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 12

                    // setup form
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 10
                        visible: root.streamCard === null
                        Label { text: "Station name"; color: root.textSecondary; font.pixelSize: 12 }
                        DarkField { id: nameField; Layout.fillWidth: true; placeholderText: "What listeners see"; text: "My Station" }
                        Label { text: "Visibility"; color: root.textSecondary; font.pixelSize: 12 }
                        RowLayout {
                            spacing: 16
                            ButtonGroup { id: visGroup }
                            DarkRadio { id: publicBtn;  text: "Public";  checked: true; ButtonGroup.group: visGroup }
                            DarkRadio { id: privateBtn; text: "Private"; ButtonGroup.group: visGroup }
                        }
                        Label { text: "Privacy"; color: root.textSecondary; font.pixelSize: 12 }
                        RowLayout {
                            spacing: 16
                            ButtonGroup { id: privacyGroup }
                            // Onion is the default — internet radio shouldn't be LAN-only or leak the host IP.
                            DarkRadio { id: onionBtn;  text: "Onion (Tor)";  checked: true; ButtonGroup.group: privacyGroup }
                            DarkRadio { id: directBtn; text: "Direct (LAN)"; ButtonGroup.group: privacyGroup }
                        }
                        Label {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 11
                            color: privacyGroup.checkedButton === onionBtn ? root.textMuted : root.warningYellow
                            text: privacyGroup.checkedButton === onionBtn
                                ? "🧅 Listeners reach you over Tor — your IP stays hidden and it works through NAT (no port-forwarding). First connect is slower."
                                : "⚠ Direct mode is LAN-only and exposes your IP to listeners. Use it only for local/low-latency streams."
                        }
                        Label { text: "Description (optional)"; color: root.textSecondary; font.pixelSize: 12 }
                        DarkField { id: descField; Layout.fillWidth: true; placeholderText: "Genre or a short note" }
                        AccentButton { text: "Start"; enabled: nameField.text.length > 0; onClicked: root.startStream() }
                    }

                    // Stream-credentials card — live status is in the header pills (OBS / Onion);
                    // every state change + the .onion link land in the activity log.
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 10
                        visible: root.streamCard !== null
                        Label { text: "Stream credentials"; color: root.textPrimary; font.pixelSize: 16; font.bold: true }
                        Label {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: root.textSecondary; font.pixelSize: 12
                            text: "In OBS → Settings → Stream: set Service to “Custom…”, paste the RTMP Server and Stream Key below, then Start Streaming. The key is secret — don't share it."
                        }
                        component CopyRow: RowLayout {
                            id: cr
                            property string label: ""
                            property string value: ""
                            property bool secret: false   // masked with dots (safe for screenshots)
                            property bool revealed: false
                            property bool canRegen: false
                            signal regen()
                            Layout.fillWidth: true; spacing: 8
                            Label { text: cr.label; color: root.textSecondary; Layout.preferredWidth: 90; font.pixelSize: 12 }
                            DarkField {
                                Layout.fillWidth: true; readOnly: true; text: cr.value
                                echoMode: (cr.secret && !cr.revealed) ? TextInput.Password : TextInput.Normal
                            }
                            DarkButton { visible: cr.secret; text: cr.revealed ? "Hide" : "Show"; onClicked: cr.revealed = !cr.revealed }
                            DarkButton { visible: cr.canRegen; text: "⟳ New"; onClicked: cr.regen() }   // #17 rotate key
                            DarkButton { text: "Copy"; onClicked: root.copyText(cr.value) }   // copies the real value
                        }
                        CopyRow { label: "RTMP Server"; value: root.streamCard ? root.streamCard.rtmpUrl : "" }
                        CopyRow {
                            label: "Stream Key"; value: root.streamCard ? root.streamCard.streamKey : ""
                            secret: true; canRegen: true
                            onRegen: {  // #17 rotate the publish key (revokes the old OBS key)
                                var r = root.callParse("regenerateKey", [])
                                if (r && r.ok) { root.streamCard = r
                                    logEvent("Stream key rotated — re-enter the new key in OBS", "warning") }
                            }
                        }
                        RowLayout {  // #17 Tor address persists across restarts; rotate on demand
                            visible: root.streamPrivacy === "onion"
                            Layout.fillWidth: true; spacing: 8
                            Label { text: "Tor address"; color: root.textSecondary; Layout.preferredWidth: 90; font.pixelSize: 12 }
                            Label {
                                Layout.fillWidth: true; font.pixelSize: 11; color: root.textMuted; elide: Text.ElideRight
                                text: root.onionReady ? "stable · persists across restarts" : "publishing…"
                            }
                            DarkButton { text: "⟳ New address"; onClicked: {  // rotate the .onion identity
                                var r = root.callParse("regenerateOnion", [])
                                if (r && r.ok) { root.onionReady = false; root.onionAddr = ""
                                    logEvent("Rotating Tor address — listeners will rediscover", "warning") }
                            } }
                        }
                        DarkButton { text: "Stop"; onClicked: root.stopStream() }
                    }
                    Item { Layout.fillHeight: true }
                }
            }

            // ── Listen tab ────────────────────────────────────────────────────
            Item {
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 12

                    ListView {
                        id: stationList
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true; spacing: 6
                        model: root.stations
                        delegate: ItemDelegate {
                            required property var modelData
                            width: ListView.view ? ListView.view.width : 0
                            onClicked: root.playStation(modelData)
                            background: Rectangle { color: parent.hovered ? root.bgSecondary : "transparent"; radius: 6 }
                            contentItem: ColumnLayout {
                                spacing: 2
                                RowLayout {
                                    spacing: 6
                                    Label { text: modelData.name || "Unknown"; color: root.textPrimary; font.bold: true }
                                    Label {  // over-Tor badge — backend-computed flag (Senty ISSUE-1),
                                             // consistent with playback routing; not a spoofable substring
                                        visible: modelData._onion === true
                                        text: "🧅 Tor"; color: root.warningYellow; font.pixelSize: 10; font.bold: true
                                    }
                                }
                                Label { text: (modelData.host || "") + " · " + root.uptime(modelData.startedAt)
                                        color: root.textSecondary; font.pixelSize: 12 }
                                Label {  // #13 — station description
                                    visible: (modelData.description || "").length > 0
                                    text: modelData.description || ""
                                    color: root.textMuted; font.pixelSize: 11
                                    Layout.fillWidth: true; elide: Text.ElideRight
                                }
                            }
                        }
                    }
                    ColumnLayout {
                        visible: root.stations.length === 0
                        Layout.fillWidth: true; spacing: 8
                        BusyIndicator { running: root.discoveryStarted; Layout.alignment: Qt.AlignHCenter; implicitWidth: 28; implicitHeight: 28 }
                        Label {
                            text: root.discoveryStarted ? "Searching for stations…" : "Open to discover stations"
                            color: root.textMuted; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter
                        }
                    }
                    RowLayout {  // now-playing (no pause for live)
                        visible: root.playingName.length > 0
                        Layout.fillWidth: true; spacing: 8
                        Label { text: "▶ " + root.playingName; color: root.textPrimary; Layout.fillWidth: true; elide: Text.ElideRight }
                        Slider { from: 0; to: 100; value: root.volume; Layout.preferredWidth: 100
                            onMoved: { root.volume = Math.round(value); root.call("setVolume", [root.volume]) } }
                        DarkButton { text: "Stop"; onClicked: { root.call("stop", []); root.playingName = "" } }
                    }
                    RowLayout {  // listener jitter buffer (#17) — deeper rides out Tor latency spikes
                        Layout.fillWidth: true; spacing: 8
                        Label { text: "Buffer"; color: root.textSecondary; font.pixelSize: 12; Layout.preferredWidth: 48 }
                        Slider {
                            id: bufSlider; from: 2; to: 20; stepSize: 1; value: root.listenBuffer
                            Layout.fillWidth: true
                            onMoved: { root.listenBuffer = Math.round(value); root.call("setListenBuffer", [root.listenBuffer]) }
                        }
                        Label { text: root.listenBuffer + "s"; color: root.textPrimary; font.pixelSize: 12; Layout.preferredWidth: 30 }
                    }
                    RowLayout {  // + add private topic
                        Layout.fillWidth: true; spacing: 8
                        DarkField { id: topicField; Layout.fillWidth: true; placeholderText: "Add a private topic" }
                        DarkButton { text: "Add"; enabled: topicField.text.length > 0
                            onClicked: { root.call("addTopic", [topicField.text]); topicField.text = "" } }
                    }
                }
            }
        }

        // ── Activity log (#12) — keycard ActivityLog: fixed height, top border, icon copy button ──
        Rectangle {
            Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16; Layout.bottomMargin: 10
            Layout.preferredHeight: 172   // fixed size — does not float with content (matches keycard)
            color: root.bgSecondary; radius: 6

            // top border
            Rectangle {
                anchors { top: parent.top; left: parent.left; right: parent.right }
                height: 1; color: root.borderColor
            }
            Text { anchors { top: parent.top; left: parent.left; topMargin: 8; leftMargin: 12 }
                   text: "Activity"; color: root.textSecondary; font.pixelSize: 12; font.bold: true }

            // clear icon
            Rectangle {
                visible: logModel.count > 0
                anchors { top: parent.top; right: copyBtn.left; topMargin: 8; rightMargin: 10 }
                width: 18; height: 18; color: "transparent"; opacity: clearArea.containsMouse ? 0.9 : 0.45
                Text { anchors.centerIn: parent; text: "✕"; color: root.textMuted; font.pixelSize: 13 }
                MouseArea { id: clearArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: logModel.clear() }
                ToolTip { visible: clearArea.containsMouse; text: "Clear"; delay: 500 }
            }
            // copy icon — two overlapping rectangles (keycard ActivityLog style)
            Rectangle {
                id: copyBtn
                anchors { top: parent.top; right: parent.right; topMargin: 8; rightMargin: 10 }
                width: 20; height: 20; color: "transparent"; opacity: copyArea.containsMouse ? 0.9 : 0.5
                Behavior on opacity { NumberAnimation { duration: 150 } }
                Rectangle { x: 3; y: 6; width: 10; height: 10; color: "transparent"; border.color: root.textMuted; border.width: 1; radius: 2 }
                Rectangle { x: 6; y: 3; width: 10; height: 10; color: root.bgSecondary; border.color: root.textMuted; border.width: 1; radius: 2 }
                MouseArea {
                    id: copyArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: { var s = ""; for (var i = 0; i < logModel.count; i++) { var e = logModel.get(i); s += e.ts + " " + e.msg + "\n" }
                        root.copyText(s); copyBtn.opacity = 0.25; copyFb.restart() }
                }
                ToolTip { visible: copyArea.containsMouse; text: "Copy all"; delay: 500 }
            }
            Timer { id: copyFb; interval: 200; onTriggered: copyBtn.opacity = copyArea.containsMouse ? 0.9 : 0.5 }

            ListView {
                id: logList
                anchors { top: parent.top; left: parent.left; right: parent.right; bottom: parent.bottom
                          topMargin: 30; leftMargin: 12; rightMargin: 12; bottomMargin: 10 }
                spacing: 4; clip: true; model: logModel
                ScrollBar.vertical: ScrollBar {}
                delegate: TextEdit {  // monospace "ts message", colored by level
                    width: logList.width
                    text: model.ts + " " + model.msg
                    color: root.levelColor(model.level)
                    font.pixelSize: 11; font.family: "monospace"
                    wrapMode: TextEdit.WordWrap
                    readOnly: true; selectByMouse: true; selectByKeyboard: true
                }
            }
            Text { visible: logModel.count === 0; anchors.centerIn: parent
                   text: "No activity yet"; color: root.textMuted; font.pixelSize: 11 }
        }
    }
}
