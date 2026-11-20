import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Removing is the one thing Depot can't take back, so it always asks.
Dialog {
    id: dialog
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(480, Overlay.overlay ? Overlay.overlay.width - 48 : 480)
    padding: 24
    Material.roundedScale: theme.radius
    Material.background: "#18181b"

    property var db: null

    function openFor(database) {
        db = database
        dataBox.checked = false
        open()
    }

    function confirm() {
        backend.remove(db.name, dataBox.checked)
        close()
    }

    title: db ? qsTr("Remove %1?").arg(db.name) : ""

    Shortcut { sequences: ["Return", "Enter"]; enabled: dialog.visible; onActivated: dialog.confirm() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: "#d4d4d8"
            text: qsTr("The container stops and goes away, and the tailnet stops reaching it.")
        }

        CheckBox {
            id: dataBox
            visible: dialog.db !== null && dialog.db.managed === true
            text: qsTr("Also delete its data")
            Material.accent: "#f87171"
        }

        Label {
            visible: dataBox.checked
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: 13
            color: "#f87171"
            text: qsTr("Every table and row in it is gone for good.")
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                Material.roundedScale: theme.radius
                text: qsTr("Cancel")
                flat: true
                onClicked: dialog.close()
            }
            Button {
                Material.roundedScale: theme.radius
                text: qsTr("Remove")
                highlighted: true
                Material.accent: "#dc2626"
                onClicked: dialog.confirm()
            }
        }
    }
}
