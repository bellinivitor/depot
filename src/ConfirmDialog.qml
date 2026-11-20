import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Asks before something that can't be taken back, like RemoveDialog: focus
// starts on Cancel, so a stray Enter never confirms.
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
    Overlay.modal: Rectangle { color: theme.tint("#000000", theme.dark ? 0.55 : 0.3) }
    Material.accent: theme.red

    property string message: ""
    property string confirmText: ""
    signal confirmed()

    onOpened: cancelButton.forceActiveFocus(Qt.TabFocusReason)

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme.text
            text: dialog.message
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
                text: dialog.confirmText
                onClicked: {
                    dialog.close()
                    dialog.confirmed()
                }
            }
        }
    }
}
