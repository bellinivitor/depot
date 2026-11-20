import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The databases down the side, the selected one beside them: what it is, what
// it costs this machine, and how to reach it. Tab walks every control.
ApplicationWindow {
    id: win
    width: 1120
    height: 760
    minimumWidth: 820
    minimumHeight: 540
    visible: true
    title: "Depot"
    color: theme.background
    font.family: theme.font
    font.pixelSize: 13

    Material.theme: theme.dark ? Material.Dark : Material.Light
    Material.accent: theme.accent
    Material.background: theme.surface
    Material.foreground: theme.text
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    property string selectedName: ""
    property bool revealPassword: false

    readonly property var byName: {
        var map = {}
        var list = backend.databases
        for (var i = 0; i < list.length; ++i)
            map[list[i].name] = list[i]
        return map
    }
    readonly property var selected: byName[selectedName]
        || (backend.names.length > 0 ? byName[backend.names[0]] || null : null)

    readonly property bool dialogOpen: createDialog.visible || removeDialog.visible
    readonly property bool shortcutsOn: !dialogOpen && !helpVisible
    // Space and Enter belong to a focused button or switch; the shortcuts
    // only take them when the list or nothing has focus.
    readonly property bool controlFocused: activeFocusItem !== null && activeFocusItem.depotControl === true
    readonly property bool ready: selected !== null && selected.state !== "creating"

    function select(name) {
        if (selectedName !== name)
            revealPassword = false
        selectedName = name
    }

    function selectIndex(index, focusItem) {
        if (index < 0 || index >= backend.names.length)
            return
        select(backend.names[index])
        var item = list.itemAt(index)
        if (item && focusItem)
            item.forceActiveFocus()
    }

    function move(step) {
        var index = selected ? backend.names.indexOf(selected.name) : -1
        selectIndex(Math.max(0, Math.min(backend.names.length - 1, index + step)), true)
    }

    function focusActions() {
        if (ready)
            runButton.forceActiveFocus(Qt.TabFocusReason)
    }

    function copy(text, what) {
        backend.copy(text)
        toast.show(qsTr("Copied: %1").arg(what), false)
    }

    function openCreate() {
        if (backend.dockerProblem === "")
            createDialog.open()
    }

    function connectionUrl() {
        return !selected ? "" : selected.exposed ? selected.url : selected.localUrl
    }

    // Keyboard focus starts on the list, so the arrows, Enter, and Tab work
    // from the first key press.
    function focusSelectedItem() {
        if (activeFocusItem !== null && activeFocusItem !== contentItem)
            return
        var index = selected ? backend.names.indexOf(selected.name) : -1
        var item = list.itemAt(index)
        if (item)
            item.forceActiveFocus(Qt.TabFocusReason)
    }

    Connections {
        target: backend
        function onNamesChanged() { Qt.callLater(win.focusSelectedItem) }
        function onNotice(message, error) { toast.show(message, error) }
        function onCreated(name) {
            win.select(name)
            toast.show(qsTr("%1 is ready").arg(name), false)
        }
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; enabled: win.helpVisible; onActivated: win.helpVisible = false }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: win.shortcutsOn; onActivated: Qt.quit() }
    Shortcut { sequence: "N"; context: Qt.ApplicationShortcut; enabled: win.shortcutsOn; onActivated: win.openCreate() }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; enabled: win.shortcutsOn; onActivated: backend.refresh() }
    Shortcut { sequences: ["Down", "J"]; context: Qt.ApplicationShortcut; enabled: win.shortcutsOn; onActivated: win.move(1) }
    Shortcut { sequences: ["Up", "K"]; context: Qt.ApplicationShortcut; enabled: win.shortcutsOn; onActivated: win.move(-1) }
    Instantiator {
        model: 9
        delegate: Shortcut {
            sequence: String(index + 1)
            context: Qt.ApplicationShortcut
            enabled: win.shortcutsOn
            onActivated: win.selectIndex(index, true)
        }
    }
    Shortcut {
        sequence: "Space"; context: Qt.ApplicationShortcut
        enabled: win.shortcutsOn && win.ready && !win.controlFocused
        onActivated: backend.setRunning(win.selected.name, !win.selected.running)
    }
    Shortcut {
        sequence: "T"; context: Qt.ApplicationShortcut; enabled: win.shortcutsOn && win.ready
        onActivated: backend.setExposed(win.selected.name, !win.selected.exposed)
    }
    Shortcut {
        sequence: "C"; context: Qt.ApplicationShortcut; enabled: win.shortcutsOn && win.ready && win.connectionUrl() !== ""
        onActivated: win.copy(win.connectionUrl(), qsTr("connection string"))
    }
    Shortcut {
        sequence: "L"; context: Qt.ApplicationShortcut; enabled: win.shortcutsOn && win.ready && !!win.selected.localUrl
        onActivated: win.copy(win.selected.localUrl, qsTr("local connection string"))
    }
    Shortcut {
        sequence: "Y"; context: Qt.ApplicationShortcut; enabled: win.shortcutsOn && win.ready && win.selected.password !== ""
        onActivated: win.copy(win.selected.password, qsTr("password"))
    }
    Shortcut {
        sequence: "P"; context: Qt.ApplicationShortcut; enabled: win.shortcutsOn && win.ready
        onActivated: win.revealPassword = !win.revealPassword
    }
    Shortcut {
        sequences: ["D", "Delete"]; context: Qt.ApplicationShortcut; enabled: win.shortcutsOn && win.ready
        onActivated: removeDialog.openFor(win.selected)
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // Header: the app, this machine on the tailnet, the totals, and
            // the one action that is always there.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 60
                color: theme.sunken
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.selection }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 16
                    spacing: 14

                    Label {
                        text: "󰆼"
                        font.pixelSize: 22
                        color: theme.accent
                    }
                    Label {
                        text: "depot"
                        font.pixelSize: 17
                        font.weight: Font.Bold
                        color: theme.textBright
                    }

                    Rectangle {
                        Layout.leftMargin: 8
                        implicitHeight: 28
                        implicitWidth: tailnetRow.implicitWidth + 20
                        radius: theme.radius
                        color: theme.surface
                        border.color: theme.selection
                        RowLayout {
                            id: tailnetRow
                            anchors.centerIn: parent
                            spacing: 8
                            Rectangle {
                                width: 7; height: 7; radius: 4
                                color: backend.tailscaleProblem === "" && backend.tailnetName !== "" ? theme.green : theme.red
                            }
                            Label {
                                text: backend.tailnetName !== ""
                                      ? qsTr("%1 · %2").arg(backend.tailnetHost).arg(backend.tailnetIp)
                                      : qsTr("Not on a tailnet")
                                font.pixelSize: 12
                                color: theme.text
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Label {
                        visible: backend.totals.count > 0
                        text: qsTr("%1 of %2 running").arg(backend.totals.running).arg(backend.totals.count)
                        font.pixelSize: 12
                        color: theme.textDim
                    }
                    Label {
                        visible: backend.totals.running > 0 && backend.totals.sampled
                        text: "󰘚 " + backend.totals.cpuText
                        font.pixelSize: 12
                        color: theme.text
                    }
                    Label {
                        visible: backend.totals.running > 0 && backend.totals.sampled
                        text: "󰍛 " + backend.totals.memoryText
                        font.pixelSize: 12
                        color: theme.text
                    }

                    DepotButton {
                        id: newButton
                        Layout.leftMargin: 8
                        kind: "primary"
                        glyph: "󰐕"
                        text: qsTr("New database")
                        enabled: backend.dockerProblem === ""
                        onClicked: win.openCreate()
                    }
                }
            }

            // Problems are shown, never worked around.
            Rectangle {
                visible: backend.dockerProblem !== ""
                Layout.fillWidth: true
                Layout.margins: 16
                Layout.bottomMargin: 0
                implicitHeight: dockerRow.implicitHeight + 24
                radius: theme.radius
                color: theme.tint(theme.red, 0.1)
                border.color: theme.tint(theme.red, 0.4)
                RowLayout {
                    id: dockerRow
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12
                    Label { text: "󰀦"; font.pixelSize: 18; color: theme.red }
                    Label {
                        Layout.fillWidth: true
                        text: backend.dockerProblem
                        wrapMode: Text.Wrap
                        color: theme.textBright
                    }
                    DepotButton {
                        visible: backend.dockerNeedsPermission
                        text: qsTr("Turn on sudoless Docker")
                        onClicked: backend.openDockerSetup()
                    }
                }
            }

            Rectangle {
                visible: backend.tailscaleProblem !== "" && backend.loaded
                Layout.fillWidth: true
                Layout.margins: 16
                Layout.bottomMargin: 0
                implicitHeight: tailscaleRow.implicitHeight + 24
                radius: theme.radius
                color: theme.tint(theme.yellow, 0.1)
                border.color: theme.tint(theme.yellow, 0.4)
                RowLayout {
                    id: tailscaleRow
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12
                    Label { text: "󰀦"; font.pixelSize: 18; color: theme.yellow }
                    Label {
                        Layout.fillWidth: true
                        text: backend.tailscaleProblem
                        wrapMode: Text.Wrap
                        color: theme.textBright
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // The databases.
                Rectangle {
                    Layout.preferredWidth: 290
                    Layout.fillHeight: true
                    color: theme.sunken
                    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: theme.selection }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        anchors.rightMargin: 13
                        spacing: 8

                        RowLayout {
                            Layout.leftMargin: 6
                            Layout.topMargin: 6
                            Layout.bottomMargin: 4
                            Label {
                                text: qsTr("DATABASES")
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                font.letterSpacing: 1
                                color: theme.textDim
                            }
                            Label {
                                text: backend.names.length
                                font.pixelSize: 11
                                color: theme.textDim
                                opacity: 0.7
                            }
                        }

                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            contentWidth: availableWidth

                            Column {
                                width: parent.width
                                spacing: 4
                                Repeater {
                                    id: list
                                    model: backend.names
                                    delegate: DatabaseItem {
                                        width: parent ? parent.width : 0
                                        name: modelData
                                        number: index + 1
                                    }
                                }
                            }
                        }

                    }
                }

                // The selected database, or, with none, an invitation to make one.
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                ScrollView {
                    id: detailScroll
                    anchors.fill: parent
                    visible: win.selected !== null
                    clip: true
                    contentWidth: availableWidth

                    ColumnLayout {
                        width: detailScroll.availableWidth
                        spacing: 0

                        ColumnLayout {
                            visible: win.selected !== null
                            Layout.fillWidth: true
                            Layout.margins: 24
                            spacing: 18

                            // What it is.
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 16

                                EngineBadge {
                                    engine: win.selected ? win.selected.engine : ""
                                    size: 52
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4
                                    Label {
                                        Layout.fillWidth: true
                                        text: win.selected ? win.selected.name : ""
                                        font.pixelSize: 22
                                        font.weight: Font.Bold
                                        color: theme.textBright
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: !win.selected ? ""
                                            : win.selected.port > 0 ? qsTr("%1 · %2 · port %3").arg(win.selected.engineLabel).arg(win.selected.image).arg(win.selected.port)
                                            : qsTr("%1 · %2").arg(win.selected.engineLabel).arg(win.selected.image)
                                        font.pixelSize: 12
                                        color: theme.textDim
                                        elide: Text.ElideRight
                                    }
                                }

                                Rectangle {
                                    id: statePill
                                    readonly property color hue: !win.selected ? theme.border
                                        : win.selected.state === "creating" || win.selected.busy ? theme.yellow
                                        : win.selected.running ? theme.green : theme.textDim
                                    implicitHeight: 26
                                    implicitWidth: stateRow.implicitWidth + 20
                                    radius: theme.radius
                                    color: theme.tint(hue, 0.12)
                                    border.color: theme.tint(hue, 0.35)
                                    RowLayout {
                                        id: stateRow
                                        anchors.centerIn: parent
                                        spacing: 7
                                        Rectangle { width: 7; height: 7; radius: 4; color: statePill.hue }
                                        Label {
                                            text: !win.selected ? ""
                                                : win.selected.state === "creating" ? qsTr("Creating")
                                                : win.selected.busy ? qsTr("Working")
                                                : win.selected.running ? qsTr("Running") : qsTr("Stopped")
                                            font.pixelSize: 12
                                            font.weight: Font.Medium
                                            color: theme.textBright
                                        }
                                    }
                                }
                            }

                            Label {
                                visible: win.selected !== null && win.selected.state === "creating"
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: qsTr("Downloading the image and starting the database. The first time can take a few minutes.")
                                color: theme.textDim
                            }

                            // What you can do to it.
                            RowLayout {
                                visible: win.ready
                                Layout.fillWidth: true
                                spacing: 10

                                DepotButton {
                                    id: runButton
                                    glyph: win.selected && win.selected.running ? "󰓛" : "󰐊"
                                    text: win.selected && win.selected.running ? qsTr("Stop") : qsTr("Start")
                                    enabled: win.selected !== null && !win.selected.busy
                                    onClicked: backend.setRunning(win.selected.name, !win.selected.running)
                                }

                                Rectangle {
                                    implicitHeight: 36
                                    implicitWidth: shareSwitch.implicitWidth + 12
                                    radius: theme.radius
                                    color: shareSwitch.checked ? theme.tint(theme.accent, 0.1) : theme.surface
                                    border.width: shareSwitch.visualFocus ? 2 : 1
                                    border.color: shareSwitch.visualFocus ? theme.accent : theme.selection

                                    Switch {
                                        id: shareSwitch
                                        readonly property bool depotControl: true
                                        anchors.centerIn: parent
                                        focusPolicy: Qt.StrongFocus
                                        text: qsTr("Share on tailnet")
                                        font.family: theme.font
                                        font.pixelSize: 13
                                        enabled: win.selected !== null && !win.selected.busy && win.selected.port > 0
                                                 && backend.tailscaleProblem === ""
                                        checked: win.selected !== null && win.selected.exposed === true
                                        onToggled: {
                                            backend.setExposed(win.selected.name, checked)
                                            // The backend owns the state; keep
                                            // following it after the click.
                                            checked = Qt.binding(function () { return win.selected !== null && win.selected.exposed === true })
                                        }
                                    }
                                }

                                Item { Layout.fillWidth: true }

                                DepotButton {
                                    kind: "danger"
                                    glyph: "󰩺"
                                    text: qsTr("Remove")
                                    enabled: win.selected !== null && !win.selected.busy
                                    onClicked: removeDialog.openFor(win.selected)
                                }
                            }

                            // What it costs this machine.
                            RowLayout {
                                visible: win.ready
                                Layout.topMargin: 6
                                Label {
                                    text: qsTr("USAGE")
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                    font.letterSpacing: 1
                                    color: theme.textDim
                                }
                                Label {
                                    text: win.selected && win.selected.running ? qsTr("live, last 2 minutes") : ""
                                    font.pixelSize: 11
                                    color: theme.textDim
                                    opacity: 0.7
                                }
                            }

                            GridLayout {
                                id: usage
                                visible: win.ready
                                Layout.fillWidth: true
                                columns: width > 680 ? 4 : 2
                                columnSpacing: 12
                                rowSpacing: 12
                                readonly property var stats: win.selected && win.selected.stats ? win.selected.stats : null
                                readonly property string idle: win.selected && win.selected.running ? qsTr("measuring…") : qsTr("stopped")

                                StatTile {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    glyph: "󰘚"
                                    title: qsTr("CPU")
                                    value: usage.stats ? usage.stats.cpuText : "—"
                                    detail: usage.stats ? qsTr("%n processes", "", usage.stats.processes) : usage.idle
                                    history: usage.stats ? usage.stats.cpuHistory : null
                                    floor: 5
                                    format: function (v) { return v.toLocaleString(Qt.locale(), 'f', 1) + "%" }
                                }
                                StatTile {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    glyph: "󰍛"
                                    title: qsTr("MEMORY")
                                    value: usage.stats ? usage.stats.memoryText : "—"
                                    detail: usage.stats ? qsTr("of %1").arg(usage.stats.memoryLimitText) : usage.idle
                                    history: usage.stats ? usage.stats.memoryHistory : null
                                    floor: 64 * 1024 * 1024
                                    format: function (v) { return (v / 1048576).toLocaleString(Qt.locale(), 'f', 1) + " MiB" }
                                }
                                StatTile {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    glyph: "󰓡"
                                    title: qsTr("NETWORK")
                                    value: usage.stats ? usage.stats.netInText : "—"
                                    detail: usage.stats ? qsTr("↓ received") : usage.idle
                                    secondValue: usage.stats ? usage.stats.netOutText : ""
                                    secondDetail: qsTr("↑ sent")
                                }
                                StatTile {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    glyph: "󰋊"
                                    title: qsTr("DISK")
                                    value: usage.stats ? usage.stats.diskWriteText : "—"
                                    detail: usage.stats ? qsTr("written") : usage.idle
                                    secondValue: usage.stats ? usage.stats.diskReadText : ""
                                    secondDetail: qsTr("read")
                                }
                            }

                            // How to reach it: always from this machine, for the projects
                            // here, and from the tailnet too while it is shared.
                            RowLayout {
                                visible: win.ready
                                Layout.fillWidth: true
                                Layout.topMargin: 6
                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("CONNECTION")
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                    font.letterSpacing: 1
                                    color: theme.textDim
                                }
                                // One switch for everything hidden in the card: the
                                // password and the URLs that carry it.
                                DepotButton {
                                    visible: win.selected !== null && win.selected.password !== ""
                                    kind: "quiet"
                                    compact: true
                                    glyph: win.revealPassword ? "󰈉" : "󰈈"
                                    text: win.revealPassword ? qsTr("Hide") : qsTr("Show")
                                    onClicked: win.revealPassword = !win.revealPassword
                                }
                            }

                            Rectangle {
                                visible: win.ready
                                Layout.fillWidth: true
                                implicitHeight: fields.implicitHeight + 28
                                radius: theme.radius
                                color: theme.surface
                                border.color: theme.selection

                                ColumnLayout {
                                    id: fields
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    spacing: 4

                                    Label {
                                        visible: win.selected !== null && win.selected.exposed && win.selected.password === ""
                                        Layout.fillWidth: true
                                        Layout.bottomMargin: 6
                                        wrapMode: Text.Wrap
                                        text: "󰀦  " + qsTr("This database has no password, so every device on your tailnet can log in. Databases made in Depot get one.")
                                        font.pixelSize: 12
                                        color: theme.yellow
                                    }
                                    Label {
                                        visible: win.selected !== null && win.selected.publishedEverywhere === true
                                        Layout.fillWidth: true
                                        Layout.bottomMargin: 6
                                        wrapMode: Text.Wrap
                                        text: "󰀦  " + qsTr("Docker publishes this port on every network interface, so your local network can reach it too.")
                                        font.pixelSize: 12
                                        color: theme.yellow
                                    }

                                    // The same everywhere.
                                    ConnectionField {
                                        label: qsTr("User")
                                        value: win.selected ? win.selected.user : ""
                                    }
                                    ConnectionField {
                                        label: qsTr("Password")
                                        value: win.selected ? win.selected.password : ""
                                        secret: value
                                        revealed: win.revealPassword
                                    }
                                    ConnectionField {
                                        label: win.selected && win.selected.extraPorts && win.selected.extraPorts.length > 0
                                               ? qsTr("HTTP port") : qsTr("Port")
                                        value: win.selected && win.selected.port > 0 ? String(win.selected.port) : ""
                                    }
                                    // ClickHouse also speaks its native protocol, for clickhouse-client.
                                    Repeater {
                                        model: win.selected && win.selected.extraPorts ? win.selected.extraPorts : []
                                        delegate: ConnectionField {
                                            label: modelData.key === "native" ? qsTr("Native port") : modelData.key
                                            value: String(modelData.port)
                                        }
                                    }

                                    ConnectionSection {
                                        glyph: "󰟀"
                                        title: qsTr("THIS MACHINE")
                                        subtitle: qsTr("for projects running here")
                                    }
                                    ConnectionField {
                                        label: qsTr("Host")
                                        value: win.selected && win.selected.port > 0 ? "127.0.0.1" : ""
                                    }
                                    ConnectionField {
                                        label: qsTr("URL")
                                        value: win.selected ? win.selected.localUrl || "" : ""
                                        secret: win.selected ? win.selected.password : ""
                                        revealed: win.revealPassword
                                    }

                                    ConnectionSection {
                                        glyph: "󰌘"
                                        title: qsTr("TAILNET")
                                        subtitle: qsTr("from your other devices")
                                        active: win.selected !== null && win.selected.exposed === true
                                    }
                                    Label {
                                        visible: win.selected !== null && !win.selected.exposed
                                        Layout.fillWidth: true
                                        Layout.topMargin: 2
                                        wrapMode: Text.Wrap
                                        text: qsTr("Not shared. Turn on Share on tailnet (T) to reach it from your other devices.")
                                        font.pixelSize: 12
                                        color: theme.textDim
                                    }
                                    ConnectionField {
                                        visible: win.selected !== null && win.selected.exposed === true
                                        label: qsTr("Host")
                                        value: win.selected ? win.selected.host || "" : ""
                                    }
                                    ConnectionField {
                                        visible: win.selected !== null && win.selected.exposed === true
                                        label: qsTr("URL")
                                        value: win.selected ? win.selected.url || "" : ""
                                        secret: win.selected ? win.selected.password : ""
                                        revealed: win.revealPassword
                                    }
                                }
                            }
                        }

                    }
                }

                ColumnLayout {
                    visible: win.selected === null && backend.loaded && backend.dockerProblem === ""
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 48, 420)
                    spacing: 10

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: "󰆼"
                        font.pixelSize: 44
                        color: theme.border
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 4
                        text: qsTr("No databases yet")
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        color: theme.textBright
                    }
                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: qsTr("Databases you create show up here, with their usage and how to connect.")
                        color: theme.textDim
                    }
                    DepotButton {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 8
                        kind: "primary"
                        glyph: "󰐕"
                        text: qsTr("New database")
                        enabled: backend.dockerProblem === ""
                        onClicked: win.openCreate()
                    }
                }
                }
            }

            // The keys worth knowing, always in view.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 30
                color: theme.sunken
                Rectangle { width: parent.width; height: 1; color: theme.selection }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 20
                    width: parent.width - 40
                    elide: Text.ElideRight
                    text: qsTr("Tab move · ↑↓ select · Space start/stop · T tailnet · C copy URL · L local URL · Y copy password · N new · ? all keys")
                    font.pixelSize: 11
                    color: theme.textDim
                }
            }
        }

        // Messages slide in at the bottom and leave on their own.
        Rectangle {
            id: toast
            property bool error: false
            function show(message, isError) {
                toastText.text = message
                error = isError
                opacity = 1
                toastTimer.restart()
            }
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 48
            width: Math.min(parent.width - 80, toastText.implicitWidth + 36)
            height: toastText.implicitHeight + 22
            radius: theme.radius
            color: error ? theme.tint(theme.red, 0.95) : theme.selection
            border.color: error ? theme.red : theme.border
            opacity: 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 180 } }
            Label {
                id: toastText
                anchors.centerIn: parent
                width: Math.min(implicitWidth, win.width - 116)
                wrapMode: Text.Wrap
                color: toast.error ? "white" : theme.textBright
            }
            Timer { id: toastTimer; interval: toast.error ? 7000 : 2200; onTriggered: toast.opacity = 0 }
        }

        HelpOverlay {
            anchors.fill: parent
            visible: win.helpVisible
            onClose: win.helpVisible = false
        }
    }

    CreateDialog { id: createDialog }

    RemoveDialog { id: removeDialog }
}
