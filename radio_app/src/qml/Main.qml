import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Minimal validation view — confirms the ui_qml C++ backend can consume delivery_module on the
// platform (the thing a core module can't, upstream #31). Full UI is ported once this is proven.
Item {
    id: root
    width: 480; height: 640

    readonly property var backend: logos.module("radio")
    readonly property string deliveryState: backend ? backend.deliveryState : "no backend"
    readonly property string peerId: backend ? backend.peerId : ""
    readonly property string lastError: backend ? backend.lastError : ""

    Rectangle { anchors.fill: parent; color: "#171717" }

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 16
        width: parent.width - 48

        Label { text: "Radio — ui_qml backend"; color: "#FFFFFF"; font.pixelSize: 20; font.bold: true; Layout.alignment: Qt.AlignHCenter }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter; spacing: 8
            Rectangle {
                width: 12; height: 12; radius: 6
                color: root.deliveryState === "connected" ? "#22C55E"
                     : root.deliveryState === "ready" ? "#F59E0B" : "#FB3748"
            }
            Label { text: "Discovery: " + root.deliveryState; color: "#FFFFFF" }
        }

        Label {
            text: "Peer: " + (root.peerId.length ? root.peerId : "—")
            color: "#A4A4A4"; font.pixelSize: 11; Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter; elide: Text.ElideMiddle
        }
        Label {
            visible: root.lastError.length > 0
            text: "⚠ " + root.lastError; color: "#ff9a9a"; Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
        }

        Button {
            text: "Start Discovery"
            Layout.alignment: Qt.AlignHCenter
            onClicked: logos.watch(root.backend.startDiscovery(), function(r) { console.log("startDiscovery:", r) })
        }
    }
}
