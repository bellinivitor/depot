import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Every shortcut, one press of ? away.
Rectangle {
    id: overlay
    signal close()
    color: "#cc000000"

    TapHandler { onTapped: overlay.close() }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(460, parent.width - 48)
        height: list.implicitHeight + 48
        radius: theme.radius
        color: "#18181b"
        border.color: "#27272a"

        ColumnLayout {
            id: list
            anchors.fill: parent
            anchors.margins: 24
            spacing: 8

            Label {
                text: qsTr("Shortcuts")
                font.pixelSize: 18
                font.weight: Font.DemiBold
                color: "#fafafa"
                Layout.bottomMargin: 8
            }

            Repeater {
                model: [
                    ["N", qsTr("New database")],
                    ["↑ ↓  J K", qsTr("Select")],
                    ["Space", qsTr("Start or stop")],
                    ["T", qsTr("Share on the tailnet, or stop sharing")],
                    ["C", qsTr("Copy the connection string")],
                    ["P", qsTr("Show or hide the password")],
                    ["Delete", qsTr("Remove")],
                    ["R", qsTr("Refresh")],
                    ["?", qsTr("Show shortcuts")],
                    ["Q", qsTr("Quit")]
                ]
                delegate: RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    Label {
                        text: modelData[0]
                        Layout.preferredWidth: 90
                        font.family: "monospace"
                        color: theme.accent
                    }
                    Label {
                        text: modelData[1]
                        Layout.fillWidth: true
                        color: "#d4d4d8"
                    }
                }
            }
        }
    }
}
