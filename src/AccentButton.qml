import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A button filled with the theme accent when highlighted, with text in
// whichever of black or white reads on it. Material's own highlighted text is
// always white, which disappears on light accents.
Button {
    id: control
    highlighted: true
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    contentItem: Label {
        text: control.text
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        color: !control.enabled ? "#71717a" : control.highlighted ? theme.accentForeground : "#e4e4e7"
    }
}
