import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One database in the list: what it is, whether it runs, whether the tailnet
// sees it.
Item {
    id: row
    property var db
    property bool selected: false
    signal clicked()

    height: 68

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        radius: theme.radius
        color: row.selected ? "#1c1c21" : hover.hovered ? "#16161a" : "transparent"
        border.width: row.selected ? 1 : 0
        border.color: theme.accent

        HoverHandler { id: hover }
        TapHandler { onTapped: row.clicked() }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 12
            spacing: 14

            Rectangle {
                width: 10; height: 10; radius: 5
                color: db.state === "creating" ? theme.accent
                     : db.running ? "#4ade80" : "#52525b"
                SequentialAnimation on opacity {
                    running: db.busy === true
                    loops: Animation.Infinite
                    alwaysRunToEnd: true
                    NumberAnimation { to: 0.25; duration: 500 }
                    NumberAnimation { to: 1; duration: 500 }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    text: db.name
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    color: "#f4f4f5"
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    text: db.state === "creating"
                          ? qsTr("Creating %1… the first download can take a few minutes").arg(db.engineLabel)
                          : db.port > 0 ? qsTr("%1 · port %2").arg(db.engineLabel).arg(db.port)
                                        : qsTr("%1 · no published port").arg(db.engineLabel)
                    font.pixelSize: 13
                    color: "#a1a1aa"
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            Label {
                visible: db.exposed === true && db.password === ""
                text: qsTr("No password")
                font.pixelSize: 12
                color: "#fbbf24"
            }

            Switch {
                text: qsTr("Running")
                visible: db.state !== "creating"
                enabled: db.busy !== true
                checked: db.running === true
                Material.accent: theme.accent
                onToggled: backend.setRunning(db.name, checked)
            }

            Switch {
                text: qsTr("Tailnet")
                visible: db.state !== "creating"
                enabled: db.busy !== true && db.port > 0 && backend.tailscaleProblem === ""
                checked: db.exposed === true
                Material.accent: theme.accent
                onToggled: backend.setExposed(db.name, checked)
            }
        }
    }
}
