import QtQuick
import QtQuick.Controls

// A single series over the last couple of minutes, newest on the right. One
// 2px line on a faint fill; hovering shows the value under the pointer.
Item {
    id: spark
    property var values: []
    property int capacity: 60
    // The scale never shrinks below this, so an idle database draws a flat
    // line near the floor rather than magnifying noise into peaks.
    property real floor: 1
    property color color: theme.accent
    property var format: function (value) { return String(value) }

    readonly property real ceiling: {
        var max = floor
        for (var i = 0; i < values.length; ++i)
            max = Math.max(max, values[i])
        return max * 1.15
    }
    readonly property real step: width / Math.max(1, capacity - 1)
    property int hoverIndex: -1

    function xAt(i) { return width - (values.length - 1 - i) * step }
    function yAt(v) { return height - 2 - (v / ceiling) * (height - 6) }

    onValuesChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHoverIndexChanged: canvas.requestPaint()
    onColorChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var n = spark.values.length

            // Recessive baseline.
            ctx.strokeStyle = theme.selection
            ctx.lineWidth = 1
            ctx.beginPath()
            ctx.moveTo(0, height - 0.5)
            ctx.lineTo(width, height - 0.5)
            ctx.stroke()
            if (n < 2)
                return

            ctx.beginPath()
            ctx.moveTo(spark.xAt(0), spark.yAt(spark.values[0]))
            for (var i = 1; i < n; ++i)
                ctx.lineTo(spark.xAt(i), spark.yAt(spark.values[i]))
            ctx.lineTo(spark.xAt(n - 1), height)
            ctx.lineTo(spark.xAt(0), height)
            ctx.closePath()
            var gradient = ctx.createLinearGradient(0, 0, 0, height)
            gradient.addColorStop(0, Qt.rgba(spark.color.r, spark.color.g, spark.color.b, 0.22))
            gradient.addColorStop(1, Qt.rgba(spark.color.r, spark.color.g, spark.color.b, 0))
            ctx.fillStyle = gradient
            ctx.fill()

            ctx.beginPath()
            ctx.moveTo(spark.xAt(0), spark.yAt(spark.values[0]))
            for (var j = 1; j < n; ++j)
                ctx.lineTo(spark.xAt(j), spark.yAt(spark.values[j]))
            ctx.strokeStyle = spark.color
            ctx.lineWidth = 2
            ctx.lineJoin = "round"
            ctx.lineCap = "round"
            ctx.stroke()

            var k = spark.hoverIndex >= 0 ? spark.hoverIndex : n - 1
            var x = spark.xAt(k), y = spark.yAt(spark.values[k])
            if (spark.hoverIndex >= 0) {
                ctx.strokeStyle = theme.border
                ctx.lineWidth = 1
                ctx.beginPath()
                ctx.moveTo(Math.round(x) + 0.5, 0)
                ctx.lineTo(Math.round(x) + 0.5, height)
                ctx.stroke()
            }
            // The point, with a ring of the surface so it lifts off the line.
            ctx.beginPath()
            ctx.arc(x, y, 4, 0, 2 * Math.PI)
            ctx.fillStyle = spark.color
            ctx.fill()
            ctx.lineWidth = 2
            ctx.strokeStyle = theme.surface
            ctx.stroke()
        }
    }

    HoverHandler {
        id: hover
        onPointChanged: {
            if (!hovered || spark.values.length < 2) {
                spark.hoverIndex = -1
                return
            }
            var fromRight = Math.round((spark.width - point.position.x) / spark.step)
            spark.hoverIndex = Math.max(0, Math.min(spark.values.length - 1, spark.values.length - 1 - fromRight))
        }
        onHoveredChanged: if (!hovered) spark.hoverIndex = -1
    }

    // The readout names the value and how long ago it was, in text ink.
    Label {
        visible: spark.hoverIndex >= 0
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: -2
        padding: 4
        text: spark.hoverIndex < 0 ? "" : qsTr("%1 · %2 s ago")
              .arg(spark.format(spark.values[spark.hoverIndex]))
              .arg((spark.values.length - 1 - spark.hoverIndex) * 2)
        font.family: theme.font
        font.pixelSize: 11
        color: theme.textBright
        background: Rectangle { color: theme.sunken; radius: theme.radius; border.color: theme.selection }
    }
}
