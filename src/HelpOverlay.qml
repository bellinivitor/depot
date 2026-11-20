import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Every shortcut, one press of ? away.
Rectangle {
    id: overlay
    signal close()
    color: theme.tint(theme.sunken, 0.85)

    TapHandler { onTapped: overlay.close() }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(520, parent.width - 48)
        height: list.implicitHeight + 48
        radius: theme.radius
        color: theme.surface
        border.color: theme.selection

        ColumnLayout {
            id: list
            anchors.fill: parent
            anchors.margins: 24
            spacing: 7

            Label {
                text: qsTr("Shortcuts")
                font.pixelSize: 17
                font.weight: Font.Bold
                color: theme.textBright
                Layout.bottomMargin: 8
            }

            Repeater {
                model: [
                    ["Tab  ⇧Tab", qsTr("Move between the list and every button")],
                    ["↑↓  J K", qsTr("Select a database")],
                    ["1–9", qsTr("Jump to a database")],
                    ["Enter", qsTr("From the list, go to its buttons")],
                    ["N", qsTr("New database")],
                    ["Space", qsTr("Start or stop")],
                    ["T", qsTr("Share on the tailnet, or stop sharing")],
                    ["C", qsTr("Copy the connection string")],
                    ["L", qsTr("Copy the connection string for this machine")],
                    ["Y", qsTr("Copy the password")],
                    ["P", qsTr("Show or hide the password")],
                    ["D", qsTr("Remove")],
                    ["R", qsTr("Refresh")],
                    ["?", qsTr("Show shortcuts")],
                    ["Q", qsTr("Quit")]
                ]
                delegate: RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    Label {
                        text: modelData[0]
                        Layout.preferredWidth: 110
                        font.weight: Font.DemiBold
                        color: theme.accent
                    }
                    Label {
                        text: modelData[1]
                        Layout.fillWidth: true
                        color: theme.text
                    }
                }
            }
        }
    }
}
