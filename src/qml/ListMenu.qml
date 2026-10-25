import QtQuick
import QtQuick.Controls

// The right-click menu on a list: edit, rename, colour, focus-list exclusions,
// archive, hide and delete, as in the Lists panel.
Menu {
    id: root

    property int listId: 0
    signal rename()
    signal editList()
    signal customColor()
    // Read each time the menu opens, so its entries describe the list as it is now.
    property var info: listId ? backend.listInfo(listId) : ({})
    onAboutToShow: info = listId ? backend.listInfo(listId) : ({})

    MenuItem { text: "Edit List…"; onTriggered: root.editList() }
    MenuItem { text: "Rename…"; onTriggered: root.rename() }
    MenuItem { text: "New Section"; onTriggered: { backend.selectSource("list:" + root.listId); backend.newSection(root.listId, "") } }
    ColorMenu {
        current: root.info.color === undefined ? -1 : root.info.color
        onChosen: (color) => backend.setListColor(root.listId, color)
        onCustomRequested: root.customColor()
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
    MenuItem {
        text: root.info.completedMode === 2 ? "Include Completed" : "Exclude Completed"
        onTriggered: backend.setListCompletedMode(root.listId, root.info.completedMode === 2 ? 1 : 2)
    }
    MenuItem {
        text: "Mute Alerts"
        checkable: true
        checked: !!root.info.muteAlerts
        onTriggered: backend.setListFlag(root.listId, "muteAlerts", !root.info.muteAlerts)
    }
    MenuSeparator {}
    MenuItem { text: "Archive"; onTriggered: backend.setListFlag(root.listId, "archived", true) }
    MenuItem { text: "Hide List"; onTriggered: backend.setListHidden(root.listId, true) }
    MenuItem { text: "Delete all Tasks…"; onTriggered: confirmTasks.open() }
    MenuItem { text: "Delete List…"; onTriggered: confirm.open() }
    MenuSeparator {}
    MenuItem {
        text: "Don't Sync This List"
        checkable: true
        checked: !!root.info.syncExcluded
        onTriggered: backend.setListSyncExcluded(root.listId, !root.info.syncExcluded)
    }

    ThemedDialog {
        id: confirmTasks
        width: Math.min(440, (parent ? parent.width : 480) - 40)
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: "Delete all tasks in “" + (root.info.name || "") + "”?"
        Label { text: "The list stays. Undo (Ctrl+Z) brings the tasks back." }
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: backend.deleteAllTasks(root.listId)
    }

    ThemedDialog {

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
