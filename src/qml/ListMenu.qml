import QtQuick
import QtQuick.Controls

// The right-click menu on a list: rename, colour, focus-list exclusions,
// archive and delete, as in the reference app's Lists panel.
Menu {
    id: root

    property int listId: 0
    signal rename()
    readonly property var info: listId ? backend.listInfo(listId) : ({})

    MenuItem { text: "Rename…"; onTriggered: root.rename() }
    Menu {
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
                checked: root.info.color === index
                onTriggered: backend.setListColor(root.listId, index)
            }
        }
    }
    MenuSeparator {}
    MenuItem {
        text: root.info.showInAll ? "Exclude from 'All'" : "Include in 'All'"
        onTriggered: backend.setListFlag(root.listId, "all", !root.info.showInAll)
    }
    MenuItem {
        text: root.info.showInToday ? "Exclude from 'Today'" : "Include in 'Today'"
        onTriggered: backend.setListFlag(root.listId, "today", !root.info.showInToday)
    }
    MenuItem {
        text: root.info.showInDone ? "Exclude from 'Done'" : "Include in 'Done'"
        onTriggered: backend.setListFlag(root.listId, "done", !root.info.showInDone)
    }
    MenuSeparator {}
    MenuItem { text: "Archive"; onTriggered: backend.setListFlag(root.listId, "archived", true) }
    MenuItem { text: "Delete List…"; onTriggered: confirm.open() }

    Dialog {

        width: Math.min(440, (parent ? parent.width : 480) - 40)
        id: confirm
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: "Delete “" + (root.info.name || "") + "”?"
        Label { text: "Its tasks are deleted with it. Undo (Ctrl+Z) brings them back." }
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: backend.deleteList(root.listId)
    }
}
