import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One piece of the connection: a label, the value, and a copy button.
// Secrets show as dots until revealed.
RowLayout {
    id: field
    property string label: ""
    property string value: ""
    property string secret: ""      // The part of value to hide
    property bool revealed: false
    property bool revealable: false
    signal reveal()

    spacing: 12
    Layout.fillWidth: true
    implicitHeight: 32

    Label {
        text: field.label
        Layout.preferredWidth: 96
        font.family: theme.font
        font.pixelSize: 12
        color: theme.textDim
    }

    Label {
        Layout.fillWidth: true
        text: field.value === "" ? qsTr("none")
            : field.secret !== "" && !field.revealed ? field.value.split(field.secret).join("••••••••")
            : field.value
        font.family: theme.font
        font.pixelSize: 13
        color: field.value === "" ? theme.textDim : theme.textBright
        elide: Text.ElideMiddle
    }

    DepotButton {
        visible: field.revealable && field.value !== ""
        kind: "quiet"
        compact: true
        glyph: field.revealed ? "󰈉" : "󰈈"
        text: field.revealed ? qsTr("Hide") : qsTr("Show")
        onClicked: field.reveal()
    }

    DepotButton {
        visible: field.value !== ""
        kind: "quiet"
        compact: true
        glyph: "󰆏"
        text: qsTr("Copy")
        onClicked: win.copy(field.value, field.label)
    }
}
