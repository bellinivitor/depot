import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One database in the sidebar. It takes keyboard focus with Tab, and
// focusing it selects it, so Tab and the arrows walk the same list.
Item {
    id: item
    property string name: ""
    property int number: 0
    readonly property var db: win.byName[name] || null
    readonly property bool selected: win.selected !== null && win.selected.name === name
    readonly property bool isDatabaseItem: true

    activeFocusOnTab: true
    implicitHeight: 60
    onActiveFocusChanged: if (activeFocus) win.select(name)

    // Enter moves into the database's actions, the way Tab would.
    Keys.onReturnPressed: win.focusActions()
    Keys.onEnterPressed: win.focusActions()

    Rectangle {
        anchors.fill: parent
        radius: theme.radius
        color: item.selected ? theme.selection : hover.hovered ? theme.surface : "transparent"
        border.width: item.activeFocus ? 2 : 0
        border.color: theme.accent

        // A bar on the selected row, so it stays findable when focus is
        // elsewhere.
        Rectangle {
            visible: item.selected
            width: 3
            height: parent.height
            color: theme.accent
        }

        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: item.forceActiveFocus() }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 12
            spacing: 12

            EngineBadge {
                engine: item.db ? item.db.engine : ""
                size: 34
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Label {
                    Layout.fillWidth: true
                    text: item.name
                    font.family: theme.font
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: item.selected ? theme.textBright : theme.text
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: !item.db ? ""
                        : item.db.state === "creating" ? qsTr("creating…")
                        : item.db.stats ? qsTr("%1 · %2").arg(item.db.stats.cpuText).arg(item.db.stats.memoryText)
                        : item.db.port > 0 ? ":" + item.db.port : item.db.engineLabel
                    font.family: theme.font
                    font.pixelSize: 11
                    color: theme.textDim
                    elide: Text.ElideRight
                }
            }

            Label {
                visible: item.db !== null && item.db.exposed === true
                text: "󰌘"
                font.family: theme.font
                font.pixelSize: 15
                color: theme.accent
                ToolTip.visible: tailnetHover.hovered
                ToolTip.text: qsTr("Shared on the tailnet")
                HoverHandler { id: tailnetHover }
            }

            Rectangle {
                width: 8
                height: 8
                radius: 4
                color: !item.db ? theme.border
                     : item.db.state === "creating" || item.db.busy ? theme.yellow
                     : item.db.running ? theme.green : theme.border
                SequentialAnimation on opacity {
                    running: item.db !== null && item.db.busy === true
                    loops: Animation.Infinite
                    alwaysRunToEnd: true
                    NumberAnimation { to: 0.25; duration: 500 }
                    NumberAnimation { to: 1; duration: 500 }
                }
            }

            Label {
                visible: item.number > 0 && item.number < 10
                text: item.number
                font.family: theme.font
                font.pixelSize: 11
                color: theme.textDim
                opacity: 0.6
            }
        }
    }
}
