import QtQuick
import QtQuick.Controls

// The engine at a glance: its initials on a tint of its color. The initials
// carry the identity, so it never rests on color alone.
Rectangle {
    id: badge
    property string engine: ""
    property int size: 36

    readonly property var initials: ({ postgres: "PG", mysql: "MY", mariadb: "MA", redis: "RD", mongo: "MG", mssql: "MS", clickhouse: "CH" })
    readonly property color hue: engine === "postgres" ? theme.blue
        : engine === "mysql" ? theme.orange
        : engine === "mariadb" ? theme.cyan
        : engine === "redis" ? theme.red
        : engine === "mongo" ? theme.green
        : engine === "mssql" ? theme.magenta
        : engine === "clickhouse" ? theme.yellow : theme.textDim

    width: size
    height: size
    radius: theme.radius
    color: theme.tint(hue, 0.16)
    border.width: 1
    border.color: theme.tint(hue, 0.35)

    Label {
        anchors.centerIn: parent
        text: badge.initials[badge.engine] || "DB"
        font.family: theme.font
        font.pixelSize: Math.round(badge.size * 0.36)
        font.weight: Font.Bold
        color: badge.hue
    }
}
