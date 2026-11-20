import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A heading inside the connection card: where the client connects from.
ColumnLayout {
    id: section
    property string glyph: ""
    property string title: ""
    property string subtitle: ""
    property bool active: true

    Layout.fillWidth: true
    Layout.topMargin: 10
    spacing: 8

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: theme.selection
    }

    RowLayout {
        spacing: 8
        Label {
            text: section.glyph
            font.family: theme.font
            font.pixelSize: 15
            color: section.active ? theme.accent : theme.textDim
        }
        Label {
            text: section.title
            font.family: theme.font
            font.pixelSize: 11
            font.weight: Font.DemiBold
            font.letterSpacing: 1
            color: section.active ? theme.textBright : theme.textDim
        }
        Label {
            text: section.subtitle
            font.family: theme.font
            font.pixelSize: 11
            color: theme.textDim
            opacity: 0.8
        }
    }
}
