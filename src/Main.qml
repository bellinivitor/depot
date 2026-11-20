import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 980
    height: 700
    minimumWidth: 720
    minimumHeight: 480
    visible: true
    title: "Depot"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    property string selectedName: ""
    property bool revealPassword: false
    readonly property var databases: backend.databases
    readonly property var selected: {
        for (var i = 0; i < databases.length; ++i)
            if (databases[i].name === selectedName)
                return databases[i]
        return databases.length > 0 ? databases[0] : null
    }
    // Shortcuts are single letters, so they must stand down while a dialog
    // takes typing.
    readonly property bool dialogOpen: createDialog.visible || removeDialog.visible
    readonly property bool shortcutsOn: !dialogOpen && !helpVisible

    function move(step) {
        if (databases.length === 0)
            return
        var index = 0
        for (var i = 0; i < databases.length; ++i)
            if (selected && databases[i].name === selected.name)
                index = i
        index = Math.max(0, Math.min(databases.length - 1, index + step))
        selectedName = databases[index].name
        revealPassword = false
    }

    function copy(text, what) {
        backend.copy(text)
        toast.show(qsTr("Copied %1").arg(what), false)
    }

    function openCreate() {
        if (backend.dockerProblem === "")
            createDialog.open()
    }

    Connections {
        target: backend
        function onNotice(message, error) { toast.show(message, error) }
        function onCreated(name) {
            win.selectedName = name
            toast.show(qsTr("%1 is ready").arg(name), false)
        }
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; enabled: win.helpVisible; onActivated: win.helpVisible = false }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: win.shortcutsOn; onActivated: Qt.quit() }
    Shortcut { sequence: "N"; context: Qt.ApplicationShortcut; enabled: win.shortcutsOn; onActivated: win.openCreate() }
    Shortcut { sequences: ["Down", "J"]; context: Qt.ApplicationShortcut; enabled: win.shortcutsOn; onActivated: win.move(1) }
    Shortcut { sequences: ["Up", "K"]; context: Qt.ApplicationShortcut; enabled: win.shortcutsOn; onActivated: win.move(-1) }
    Shortcut {
        sequence: "Space"; context: Qt.ApplicationShortcut; enabled: win.shortcutsOn && win.selected !== null
        onActivated: backend.setRunning(win.selected.name, !win.selected.running)
    }
    Shortcut {
        sequence: "T"; context: Qt.ApplicationShortcut; enabled: win.shortcutsOn && win.selected !== null
        onActivated: backend.setExposed(win.selected.name, !win.selected.exposed)
    }
    Shortcut {
        sequence: "C"; context: Qt.ApplicationShortcut; enabled: win.shortcutsOn && win.selected !== null && !!(win.selected.exposed ? win.selected.url : win.selected.localUrl)
        onActivated: win.copy(win.selected.exposed ? win.selected.url : win.selected.localUrl, qsTr("the connection string"))
    }
    Shortcut {
        sequence: "P"; context: Qt.ApplicationShortcut; enabled: win.shortcutsOn && win.selected !== null
        onActivated: win.revealPassword = !win.revealPassword
    }
    Shortcut {
        sequences: ["Delete", "Backspace"]; context: Qt.ApplicationShortcut
        enabled: win.shortcutsOn && win.selected !== null && win.selected.state !== "creating"
        onActivated: removeDialog.openFor(win.selected)
    }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; enabled: win.shortcutsOn; onActivated: backend.refresh() }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // Header: the app, this machine on the tailnet, and the main action.
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 20
                Layout.bottomMargin: 12
                spacing: 16

                Label {
                    text: "Depot"
                    font.pixelSize: 24
                    font.weight: Font.Bold
                    color: "#fafafa"
                }

                Rectangle {
                    Layout.preferredHeight: 28
                    Layout.preferredWidth: tailnetLabel.implicitWidth + 32
                    radius: Math.min(14, theme.radius)
                    color: "#18181b"
                    Rectangle {
                        x: 12; anchors.verticalCenter: parent.verticalCenter
                        width: 8; height: 8; radius: 4
                        color: backend.tailscaleProblem === "" && backend.tailnetName !== "" ? "#4ade80" : "#f87171"
                    }
                    Label {
                        id: tailnetLabel
                        x: 26; anchors.verticalCenter: parent.verticalCenter
                        font.pixelSize: 13
                        color: "#d4d4d8"
                        text: backend.tailnetName !== ""
                              ? qsTr("%1 on the tailnet · %2").arg(backend.tailnetHost).arg(backend.tailnetIp)
                              : qsTr("Not on a tailnet")
                    }
                }

                Item { Layout.fillWidth: true }

                AccentButton {
                    text: qsTr("New database")
                    enabled: backend.dockerProblem === ""
                    onClicked: win.openCreate()
                }
            }

            // Problems are shown, never worked around.
            Rectangle {
                visible: backend.dockerProblem !== ""
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.bottomMargin: 12
                implicitHeight: dockerRow.implicitHeight + 24
                radius: theme.radius
                color: "#2a1215"
                border.color: "#7f1d1d"
                RowLayout {
                    id: dockerRow
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12
                    Label {
                        Layout.fillWidth: true
                        text: backend.dockerProblem
                        wrapMode: Text.Wrap
                        color: "#fecaca"
                    }
                    Button {
                        Material.roundedScale: theme.radius
                        visible: backend.dockerNeedsPermission
                        text: qsTr("Turn on sudoless Docker")
                        onClicked: backend.openDockerSetup()
                    }
                }
            }

            Rectangle {
                visible: backend.tailscaleProblem !== "" && backend.loaded
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.bottomMargin: 12
                implicitHeight: tailscaleText.implicitHeight + 24
                radius: theme.radius
                color: "#2a2410"
                border.color: "#78350f"
                Label {
                    id: tailscaleText
                    anchors.fill: parent
                    anchors.margins: 12
                    text: backend.tailscaleProblem
                    wrapMode: Text.Wrap
                    color: "#fde68a"
                }
            }

            // The list.
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth

                Column {
                    width: parent.width
                    spacing: 4
                    Repeater {
                        model: win.databases
                        delegate: DatabaseRow {
                            width: parent ? parent.width : 0
                            db: modelData
                            selected: win.selected !== null && win.selected.name === modelData.name
                            onClicked: {
                                win.selectedName = modelData.name
                                win.revealPassword = false
                            }
                        }
                    }
                }

                Label {
                    visible: backend.loaded && win.databases.length === 0 && backend.dockerProblem === ""
                    anchors.centerIn: parent
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("No databases yet.\nPress N to create one.")
                    font.pixelSize: 16
                    color: "#71717a"
                }
            }

            // How to reach the selected database from another machine.
            Rectangle {
                visible: win.selected !== null && win.selected.state !== "creating"
                Layout.fillWidth: true
                Layout.margins: 16
                implicitHeight: details.implicitHeight + 32
                radius: theme.radius
                color: "#141417"
                border.color: "#27272a"

                ColumnLayout {
                    id: details
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10

                    Label {
                        text: win.selected && win.selected.exposed
                              ? qsTr("Connect from another device on your tailnet")
                              : qsTr("Connect from this machine")
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        color: "#e4e4e7"
                    }

                    Label {
                        visible: win.selected !== null && !win.selected.exposed
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("Turn on Tailnet (T) to reach it from your other devices.")
                        font.pixelSize: 13
                        color: "#a1a1aa"
                    }

                    Label {
                        visible: win.selected !== null && win.selected.exposed && win.selected.password === ""
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("This database has no password, so every device on your tailnet can log in. Databases made in Depot get one.")
                        font.pixelSize: 13
                        color: "#fbbf24"
                    }

                    Label {
                        visible: win.selected !== null && win.selected.publishedEverywhere === true
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("Docker publishes this port on every network interface, so your local network can reach it too.")
                        font.pixelSize: 13
                        color: "#fbbf24"
                    }

                    GridLayout {
                        columns: 3
                        columnSpacing: 12
                        rowSpacing: 4
                        Layout.fillWidth: true

                        Repeater {
                            model: win.selected ? [
                                { label: qsTr("Host"), value: win.selected.exposed ? win.selected.host : "127.0.0.1", secret: false },
                                { label: qsTr("Port"), value: String(win.selected.port), secret: false },
                                { label: qsTr("User"), value: win.selected.user, secret: false },
                                { label: qsTr("Password"), value: win.selected.password, secret: true },
                                { label: qsTr("URL"), value: win.selected.exposed ? win.selected.url : win.selected.localUrl, secret: true }
                            ] : []

                            delegate: Item {
                                Layout.columnSpan: 3
                                Layout.fillWidth: true
                                implicitHeight: 34
                                visible: modelData.value !== "" || modelData.label === qsTr("Password")

                                RowLayout {
                                    anchors.fill: parent
                                    spacing: 12
                                    Label {
                                        text: modelData.label
                                        Layout.preferredWidth: 90
                                        color: "#71717a"
                                        font.pixelSize: 13
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.value === "" ? qsTr("none")
                                            : modelData.secret && !win.revealPassword && win.selected.password !== ""
                                              ? modelData.value.replace(win.selected.password, "••••••••")
                                              : modelData.value
                                        font.family: "monospace"
                                        font.pixelSize: 13
                                        color: modelData.value === "" ? "#71717a" : "#f4f4f5"
                                        elide: Text.ElideMiddle
                                    }
                                    ToolButton {
                                        Material.roundedScale: theme.radius
                                        visible: modelData.label === qsTr("Password") && modelData.value !== ""
                                        text: win.revealPassword ? qsTr("Hide") : qsTr("Show")
                                        font.pixelSize: 12
                                        onClicked: win.revealPassword = !win.revealPassword
                                    }
                                    ToolButton {
                                        Material.roundedScale: theme.radius
                                        visible: modelData.value !== ""
                                        text: qsTr("Copy")
                                        font.pixelSize: 12
                                        onClicked: win.copy(modelData.value, modelData.label.toLowerCase())
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.bottomMargin: 12
                text: qsTr("N new · Space start/stop · T tailnet · C copy · ? all shortcuts")
                font.pixelSize: 12
                color: "#52525b"
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
            anchors.bottomMargin: 40
            width: Math.min(parent.width - 80, toastText.implicitWidth + 32)
            height: toastText.implicitHeight + 20
            radius: theme.radius
            color: error ? "#7f1d1d" : "#27272a"
            opacity: 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 180 } }
            Label {
                id: toastText
                anchors.centerIn: parent
                width: Math.min(implicitWidth, win.width - 112)
                wrapMode: Text.Wrap
                color: "#fafafa"
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
