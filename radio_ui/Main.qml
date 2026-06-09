import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// radio_ui scaffold — two-tab shell. Logic lives in radio_module (sandbox blocks
// network/subprocess in QML). Call it via logos.callModule("radio_module", method, [args]).
// Sandbox rules (qml-sandbox-restrictions): no QtMultimedia, no QtGraphicalEffects, no
// network URLs, no FileDialog. Inside layouts use implicitHeight, never height (layout bug).
Item {
    id: root
    width: 480; height: 640

    // Parse the JSON-string responses radio_module returns. Double-parse guard per
    // qml-callmoduleparse-double-json.
    function callParse(method, args) {
        try {
            var raw = logos.callModule("radio_module", method, args || [])
            var t = JSON.parse(raw)
            return (typeof t === "string") ? JSON.parse(t) : t
        } catch (e) { return null }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

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

            // --- Stream tab (Epic D: #7 setup card, #8 status light) ---
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 16
                    Label { text: "Stream"; font.pixelSize: 20 }
                    Label {
                        text: "Broadcast setup — name, visibility, OBS card (#7) + live status (#8)"
                        wrapMode: Text.WordWrap; Layout.fillWidth: true; opacity: 0.7
                    }
                    Item { Layout.fillHeight: true }
                }
            }

            // --- Listen tab (Epic E: #9 directory + tap-to-play, #12 add topic) ---
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 16
                    Label { text: "Listen"; font.pixelSize: 20 }
                    Label {
                        text: "Live stations appear here as heartbeats arrive (#9). Tap to play."
                        wrapMode: Text.WordWrap; Layout.fillWidth: true; opacity: 0.7
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
