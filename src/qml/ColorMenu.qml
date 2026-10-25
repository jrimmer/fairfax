import QtQuick
import QtQuick.Controls

// The "Color" submenu shared by lists, Smart Lists and list groups: the theme
// palette plus a custom colour. `current` is a TaskList.color value, so a
// custom colour (0x1000000 | 0xRRGGBB) shows its own swatch.
ScrollingMenu {
    id: root

    property int current: 0
    signal chosen(int color)
    signal customRequested()

    title: "Color"
    Repeater {
        model: theme.listPalette
        MenuItem {
            required property int index
            required property string modelData
            text: ["Blue", "Purple", "Green", "Red", "Orange", "Cyan", "Yellow", "Brown",
                   "Light Blue", "Lavender", "Mint", "Coral", "Aqua"][index]
            icon.color: modelData
            checkable: true
            checked: root.current === index
            onTriggered: root.chosen(index)
        }
    }
    MenuSeparator {}
    MenuItem {
        text: "Custom…"
        icon.color: root.current >= 0x1000000 ? theme.listColor(root.current) : theme.muted
        checkable: true
        checked: root.current >= 0x1000000
        onTriggered: root.customRequested()
    }
}
