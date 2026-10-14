import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Every shortcut, on `?`. the reference app's ⌘ is Ctrl here, since Super belongs to
// Hyprland; the bare keys work while the task list has focus. The rows come
// from the command registry, so a remapped key shows its new binding.
Popup {
    id: root
    function toggle() { if (visible) close(); else open() }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(920, parent.width - 40)
    height: Math.min(640, parent.height - 40)
    modal: true
    focus: true
    padding: 22
    background: Rectangle { color: theme.raised; radius: 14; border.color: theme.divider }

    // The group headings, in registry order.
    readonly property var groupNames: (backend.shortcutMap, backend.shortcutCommands()).map(c => c.group).filter((g, i, a) => a.indexOf(g) === i)

    component GroupColumn: ColumnLayout {
        property string groupName
        Layout.alignment: Qt.AlignTop
        Layout.fillWidth: true
        spacing: 5
        Text { text: parent.groupName.toUpperCase(); color: theme.accent; font.pixelSize: backend.fontSize - 3; font.weight: Font.Bold; font.letterSpacing: 0.6; bottomPadding: 4 }
        Repeater {
            model: (backend.shortcutMap, backend.shortcutCommands()).filter(c => c.group === groupName)
            RowLayout {
                required property var modelData
                spacing: 10
                Text { Layout.preferredWidth: 130; text: (backend.shortcutMap, backend.shortcutText(modelData.id)); color: theme.foreground; font.family: theme.monoFont; font.pixelSize: backend.fontSize - 3 }
                Text { Layout.fillWidth: true; text: modelData.title; color: theme.muted; font.pixelSize: backend.fontSize - 2; wrapMode: Text.Wrap }
            }
        }
    }

    contentItem: Flickable {
        contentHeight: grid.implicitHeight
        clip: true
        focus: true
        Accessible.role: Accessible.Dialog
        Accessible.name: "Keyboard Shortcuts"
        Keys.onPressed: (e) => { root.close(); e.accepted = true }
        GridLayout {
            id: grid
            width: parent.width
            columns: width > 760 ? 3 : 1
            columnSpacing: 28
            rowSpacing: 18
            Repeater {
                model: root.groupNames
                GroupColumn { required property string modelData; groupName: modelData }
            }
        }
    }
}
