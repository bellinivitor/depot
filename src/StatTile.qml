import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One measure of the container: what it is, its value now, a line of
// context, and, when there is history, how it moved.
Rectangle {
    id: tile
    property string glyph: ""
    property string title: ""
    property string value: "—"
    property string detail: ""
    property var history: null
    property real floor: 1
    property var format: function (v) { return String(v) }
    // Tiles without history show a second figure where the line would be.
    property string secondValue: ""
    property string secondDetail: ""

    implicitHeight: 150
    radius: theme.radius
    color: theme.surface
    border.color: theme.selection

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 2

        RowLayout {
            spacing: 6
            Label {
                text: tile.glyph
                font.family: theme.font
                font.pixelSize: 14
                color: theme.textDim
            }
            Label {
                text: tile.title
                font.family: theme.font
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.letterSpacing: 1
                color: theme.textDim
            }
        }

        Label {
            Layout.topMargin: 4
            text: tile.value
            font.family: theme.font
            font.pixelSize: 22
            font.weight: Font.DemiBold
            color: theme.textBright
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        Label {
            text: tile.detail
            font.family: theme.font
            font.pixelSize: 11
            color: theme.textDim
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        Item { Layout.fillHeight: true }

        Label {
            visible: tile.secondValue !== ""
            text: tile.secondValue
            font.family: theme.font
            font.pixelSize: 16
            font.weight: Font.DemiBold
            color: theme.text
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        Label {
            visible: tile.secondValue !== ""
            text: tile.secondDetail
            font.family: theme.font
            font.pixelSize: 11
            color: theme.textDim
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        Sparkline {
            visible: tile.history !== null
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            values: tile.history || []
            floor: tile.floor
            format: tile.format
        }
    }
}
