import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Pick an engine, a name, and a port. The password is generated, never typed.
Dialog {
    id: dialog
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(680, Overlay.overlay ? Overlay.overlay.width - 48 : 680)
    title: qsTr("New database")
    padding: 24
    Material.roundedScale: theme.radius
    Material.background: "#18181b"

    property string engine: "postgres"
    readonly property string nameError: backend.nameProblem(nameField.text)
    readonly property string portError: backend.portProblem(parseInt(portField.text) || 0)
    readonly property bool valid: nameError === "" && portError === ""

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

    Shortcut { sequences: ["Return", "Enter"]; enabled: dialog.visible; onActivated: dialog.submit() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 16

        Flow {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: backend.engineList
                delegate: AccentButton {
                    text: modelData.label
                    highlighted: dialog.engine === modelData.id
                    flat: !highlighted
                    onClicked: dialog.pick(modelData.id)
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
                Material.accent: theme.accent
            }
            TextField {
                id: portField
                Layout.preferredWidth: 120
                placeholderText: qsTr("Port")
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 1; top: 65535 }
                Material.accent: theme.accent
            }
        }

        Label {
            visible: !dialog.valid && nameField.text !== ""
            text: dialog.nameError !== "" ? dialog.nameError : dialog.portError
            color: "#f87171"
            font.pixelSize: 13
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }

        CheckBox {
            id: shareBox
            text: qsTr("Share on the tailnet right away")
            enabled: backend.tailscaleProblem === ""
            Material.accent: theme.accent
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: 13
            color: "#a1a1aa"
            text: qsTr("Depot generates a password and keeps the data in the Docker volume %1. The port only listens on this machine; other devices reach it through Tailscale.")
                  .arg("depot-" + (nameField.text || "…"))
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                Material.roundedScale: theme.radius
                text: qsTr("Cancel")
                flat: true
                onClicked: dialog.close()
            }
            AccentButton {
                text: qsTr("Create")
                enabled: dialog.valid
                onClicked: dialog.submit()
            }
        }
    }
}
