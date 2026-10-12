import QtQuick
import QtQuick.Controls

// A Dialog drawn in the theme's colours. The Material style's own dialog
// background comes out see-through behind the content, so every dialog uses
// this instead of a bare Dialog.
Dialog {
    id: root
    padding: 20
    topPadding: 4
    background: Rectangle { color: theme.raised; radius: 12; border.color: theme.divider }
    header: Label {
        visible: root.title !== ""
        text: root.title
        padding: 20
        bottomPadding: 8
        wrapMode: Text.Wrap
        color: theme.foreground
        font.pixelSize: backend.fontSize + 4
        font.weight: Font.DemiBold
    }
}
