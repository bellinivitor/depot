import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Pick an engine, a name, and a port. The password is generated, never typed.
Dialog {
    id: dialog
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(720, Overlay.overlay ? Overlay.overlay.width - 48 : 720)
    title: qsTr("New database")
    padding: 24
    topPadding: 8
    font.family: theme.font
    Material.roundedScale: theme.radius
    Material.background: theme.surface
    // Material dims the window with a pale wash in dark mode; darken it
    // instead, so the dialog stands out and the window stays in its colors.
    Overlay.modal: Rectangle {
        color: theme.tint("#000000", theme.dark ? 0.55 : 0.3)
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }
    Material.accent: theme.accent

    property string engine: "postgres"
    property string version: ""
    // The name follows the engine and version until someone types their own.
    property bool nameTouched: false
    readonly property var versions: {
        const list = backend.engineList
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === engine)
                return list[i].versions
        return []
    }
    readonly property string nameError: backend.nameProblem(nameField.text)
    readonly property string portError: backend.portProblem(parseInt(portField.text) || 0)
    readonly property string passwordError: backend.passwordProblem(engine, passwordField.text)
    // A removed database left its data under this name. Its password lives
    // in that data, so mounting it as is would quietly keep the old one:
    // the person has to choose between keeping it and starting over.
    readonly property bool leftover: backend.leftoverNames.indexOf(nameField.text) >= 0
    property string dataChoice: ""
    onLeftoverChanged: dataChoice = ""
    readonly property string reuseProblem: backend.reuseProblem(engine)
    readonly property bool dataChosen: !leftover || dataChoice === "fresh"
                                       || (dataChoice === "reuse" && reuseProblem === "")
    readonly property bool valid: nameError === "" && portError === "" && passwordError === "" && dataChosen

    function cycle(step) {
        const list = backend.engineList
        let i = 0
        for (let j = 0; j < list.length; ++j)
            if (list[j].id === engine)
                i = j
        i = (i + step + list.length) % list.length
        pick(list[i].id)
    }

    // Suggestions skip names in use and names with data left behind.
    function suggestedName() {
        const base = version === "" || version === versions[0] ? engine : engine + version.replace(/\./g, "")
        const free = name => backend.nameProblem(name) === "" && backend.leftoverNames.indexOf(name) < 0
        if (free(base))
            return base
        for (let i = 2; ; ++i)
            if (free(base + "-" + i))
                return base + "-" + i
    }

    function pick(id) {
        engine = id
        version = versions.length > 0 ? versions[0] : ""
        if (!nameTouched)
            nameField.text = suggestedName()
        portField.text = String(backend.suggestPort(id))
    }

    function pickVersion(label) {
        version = label
        if (!nameTouched)
            nameField.text = suggestedName()
    }

    function submit() {
        if (!valid)
            return
        if (leftover && dataChoice === "fresh") {
            freshConfirm.open()
            return
        }
        createNow(leftover ? dataChoice : "")
    }

    function createNow(existingData) {
        backend.create(engine, nameField.text, parseInt(portField.text), shareBox.checked, passwordField.text,
                       version, existingData)
        close()
    }

    ConfirmDialog {
        id: freshConfirm
        title: qsTr("Delete the old data of %1?").arg(nameField.text)
        message: qsTr("Every table and row in %1 is gone for good, and %2 starts empty.")
                 .arg("depot-" + nameField.text).arg(nameField.text)
        confirmText: qsTr("Delete and create")
        onConfirmed: dialog.createNow("fresh")
    }

    onAboutToShow: {
        nameTouched = false
        pick(engine)
        passwordField.text = ""
        passwordField.revealed = false
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
    Shortcut { sequences: ["Up", "Ctrl+K"]; enabled: dialog.visible && !freshConfirm.visible; onActivated: dialog.cycle(-1) }
    Shortcut { sequences: ["Down", "Ctrl+J"]; enabled: dialog.visible && !freshConfirm.visible; onActivated: dialog.cycle(1) }
    Shortcut { sequence: "Ctrl+T"; enabled: dialog.visible && shareBox.enabled; onActivated: shareBox.checked = !shareBox.checked }

    ColumnLayout {
        anchors.fill: parent
        spacing: 16

        // The engines, picked with the mouse or ↑↓; one Tab stop less.
        GridLayout {
            Layout.fillWidth: true
            columns: 4
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
            spacing: 8
            Label {
                text: qsTr("Version")
                font.pixelSize: 12
                color: theme.textDim
                Layout.rightMargin: 4
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: dialog.versions
                    delegate: DepotButton {
                        compact: true
                        kind: dialog.version === modelData ? "primary" : "ghost"
                        text: index === 0 ? qsTr("%1 (default)").arg(modelData) : modelData
                        onClicked: dialog.pickVersion(modelData)
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
                onTextEdited: dialog.nameTouched = true
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

        // Blank means Depot makes a strong one; typing one is for when a
        // project already expects a particular password.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                id: passwordField
                property bool revealed: false
                Layout.fillWidth: true
                placeholderText: backend.blankPassword(dialog.engine) !== ""
                                 ? qsTr("Password (blank: %1)").arg(backend.blankPassword(dialog.engine))
                                 : qsTr("Password (leave blank to generate one)")
                font.family: theme.font
                echoMode: revealed ? TextInput.Normal : TextInput.Password
                Material.accent: theme.accent
            }
            DepotButton {
                kind: "quiet"
                compact: true
                glyph: passwordField.revealed ? "󰈉" : "󰈈"
                text: passwordField.revealed ? qsTr("Hide") : qsTr("Show")
                onClicked: passwordField.revealed = !passwordField.revealed
            }
        }

        Label {
            // Only when there is something to say: an unchosen leftover keeps
            // Create off without being an error.
            visible: text !== "" && (nameField.text !== "" || passwordField.text !== "")
            text: dialog.nameError !== "" ? dialog.nameError : dialog.portError !== "" ? dialog.portError : dialog.passwordError
            color: theme.red
            font.pixelSize: 12
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }

        Rectangle {
            visible: dialog.leftover
            Layout.fillWidth: true
            implicitHeight: leftoverColumn.implicitHeight + 24
            radius: theme.radius
            color: theme.tint(theme.yellow, 0.08)
            border.color: theme.tint(theme.yellow, 0.4)

            ColumnLayout {
                id: leftoverColumn
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: theme.textBright
                    text: "󰀦  " + qsTr("A removed database left its data under this name, in %1. Its password is kept in that data, so choose what to do with it.")
                          .arg("depot-" + nameField.text)
                }
                RowLayout {
                    spacing: 8
                    DepotButton {
                        compact: true
                        kind: dialog.dataChoice === "reuse" ? "primary" : "ghost"
                        glyph: dialog.dataChoice === "reuse" ? "󰄬" : ""
                        text: qsTr("Keep the data")
                        enabled: dialog.reuseProblem === ""
                        onClicked: dialog.dataChoice = "reuse"
                    }
                    DepotButton {
                        compact: true
                        kind: "danger"
                        glyph: dialog.dataChoice === "fresh" ? "󰄬" : "󰩺"
                        text: qsTr("Delete it and start over")
                        onClicked: dialog.dataChoice = "fresh"
                    }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                    color: dialog.dataChoice === "fresh" ? theme.red : theme.textDim
                    text: dialog.reuseProblem !== "" && dialog.dataChoice !== "fresh" ? dialog.reuseProblem
                        : dialog.dataChoice === "reuse" ? qsTr("Keeps every table and sets the new password inside the database.")
                        : dialog.dataChoice === "fresh" ? qsTr("Deletes every table in that data before creating. It asks once more.")
                        : qsTr("Pick one to create it.")
                }
            }
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
            text: (passwordField.text === "" && backend.blankPassword(dialog.engine) === ""
                   ? qsTr("Depot generates a password and keeps the data in the Docker volume %1. The port only listens on this machine; other devices reach it through Tailscale.")
                   : qsTr("Depot keeps the data in the Docker volume %1. The port only listens on this machine; other devices reach it through Tailscale."))
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
