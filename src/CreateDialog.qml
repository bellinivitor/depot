import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Pick an engine, a name, and a port. The password is generated, never typed.
Dialog {
    id: dialog
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(640, Overlay.overlay ? Overlay.overlay.width - 48 : 640)
    title: qsTr("New database")
    padding: 24
    topPadding: 8
    font.family: theme.font
    Material.roundedScale: theme.radius
    Material.background: theme.surface
    Material.accent: theme.accent

    property string engine: "postgres"
    readonly property string nameError: backend.nameProblem(nameField.text)
    readonly property string portError: backend.portProblem(parseInt(portField.text) || 0)
    readonly property bool valid: nameError === "" && portError === ""

    function cycle(step) {
        const list = backend.engineList
        let i = 0
        for (let j = 0; j < list.length; ++j)
            if (list[j].id === engine)
                i = j
        i = (i + step + list.length) % list.length
        pick(list[i].id)
    }

    function pick(id) {
        engine = id
        nameField.text = backend.suggestName(id)
        portField.text = String(backend.suggestPort(id))
    }

    function submit() {
        if (!valid)
            return
        backend.create(engine, nameField.text, parseInt(portField.text), shareBox.checked)
        close()
    }

    onAboutToShow: {
        pick(engine)
        shareBox.checked = backend.tailscaleProblem === ""
        nameField.forceActiveFocus()
        nameField.selectAll()
    }

    // Enter creates, unless a button has focus: then Enter presses it.
    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: dialog.visible && !(win.activeFocusItem && win.activeFocusItem.depotControl === true)
        onActivated: dialog.submit()
    }
    // Text fields are one line, so up and down are free to pick the engine.
    Shortcut { sequences: ["Up", "Ctrl+K"]; enabled: dialog.visible; onActivated: dialog.cycle(-1) }
    Shortcut { sequences: ["Down", "Ctrl+J"]; enabled: dialog.visible; onActivated: dialog.cycle(1) }
    Shortcut { sequence: "Ctrl+T"; enabled: dialog.visible && shareBox.enabled; onActivated: shareBox.checked = !shareBox.checked }

    ColumnLayout {
        anchors.fill: parent
        spacing: 16

        // The engines, picked with the mouse or ↑↓; one Tab stop less.
        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 8
            rowSpacing: 8
            Repeater {
                model: backend.engineList
                delegate: Rectangle {
                    readonly property bool picked: dialog.engine === modelData.id
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: 50
                    radius: theme.radius
                    color: picked ? theme.tint(theme.accent, 0.12) : engineHover.hovered ? theme.selection : theme.background
                    border.width: picked ? 2 : 1
                    border.color: picked ? theme.accent : theme.selection

                    HoverHandler { id: engineHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: dialog.pick(modelData.id) }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 10
                        EngineBadge { engine: modelData.id; size: 30 }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label {
                                text: modelData.label
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                color: theme.textBright
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: ":" + modelData.port
                                font.pixelSize: 11
                                color: theme.textDim
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            TextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: qsTr("Name")
                font.family: theme.font
                Material.accent: theme.accent
            }
            TextField {
                id: portField
                Layout.preferredWidth: 120
                placeholderText: qsTr("Port")
                font.family: theme.font
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 1; top: 65535 }
                Material.accent: theme.accent
            }
        }

        Label {
            visible: !dialog.valid && nameField.text !== ""
            text: dialog.nameError !== "" ? dialog.nameError : dialog.portError
            color: theme.red
            font.pixelSize: 12
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }

        CheckBox {
            id: shareBox
            readonly property bool depotControl: true
            text: qsTr("Share on the tailnet right away")
            font.family: theme.font
            enabled: backend.tailscaleProblem === ""
            Material.accent: theme.accent
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: 12
            color: theme.textDim
            text: qsTr("Depot generates a password and keeps the data in the Docker volume %1. The port only listens on this machine; other devices reach it through Tailscale.")
                  .arg("depot-" + (nameField.text || "…"))
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 10
            Label {
                Layout.fillWidth: true
                text: qsTr("↑↓ engine · Tab next · Enter create · Esc cancel")
                font.pixelSize: 11
                color: theme.textDim
                elide: Text.ElideRight
            }
            DepotButton {
                kind: "quiet"
                text: qsTr("Cancel")
                onClicked: dialog.close()
            }
            DepotButton {
                kind: "primary"
                glyph: "󰐕"
                text: qsTr("Create")
                enabled: dialog.valid
                onClicked: dialog.submit()
            }
        }
    }
}
