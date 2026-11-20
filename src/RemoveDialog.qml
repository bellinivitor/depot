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
    topPadding: 8
    font.family: theme.font
    Material.roundedScale: theme.radius
    Material.background: theme.surface
    Material.accent: theme.red

    property var db: null

    function openFor(database) {
        db = database
        dataBox.checked = false
        open()
    }

    // Focus starts on Cancel, so a stray Enter never removes anything.
    onOpened: cancelButton.forceActiveFocus(Qt.TabFocusReason)

    function confirm() {
        backend.remove(db.name, dataBox.checked)
        close()
    }

    title: db ? qsTr("Remove %1?").arg(db.name) : ""

    Shortcut { sequence: "D"; enabled: dialog.visible && dataBox.visible; onActivated: dataBox.checked = !dataBox.checked }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme.text
            text: qsTr("The container stops and goes away, and the tailnet stops reaching it.")
        }

        CheckBox {
            id: dataBox
            visible: dialog.db !== null && dialog.db.managed === true
            text: qsTr("Also delete its data")
            font.family: theme.font
            Material.accent: theme.red
        }

        Label {
            visible: dataBox.checked
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: 12
            color: theme.red
            text: qsTr("Every table and row in it is gone for good.")
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 10
            Label {
                Layout.fillWidth: true
                text: qsTr("Tab next · Esc cancel")
                font.pixelSize: 11
                color: theme.textDim
            }
            DepotButton {
                id: cancelButton
                kind: "quiet"
                text: qsTr("Cancel")
                onClicked: dialog.close()
            }
            DepotButton {
                kind: "danger"
                glyph: "󰩺"
                text: qsTr("Remove")
                onClicked: dialog.confirm()
            }
        }
    }
}
