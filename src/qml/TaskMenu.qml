import QtQuick
import QtQuick.Controls

// The right-click menu on tasks; it acts on the whole selection.
Menu {
    id: root
    property var row: ({})

    MenuItem { text: root.row.completed ? "Mark as Not Completed" : "Mark as Completed"; onTriggered: backend.toggleComplete() }
    MenuItem { text: "Edit"; onTriggered: backend.beginEdit(root.row.taskId) }
    MenuSeparator {}
    MenuItem { text: "Due Today"; onTriggered: backend.setDueInDays(0) }
    MenuItem { text: "Due Tomorrow"; onTriggered: backend.setDueInDays(1) }
    MenuItem { text: "Start Today"; onTriggered: backend.setStartInDays(0) }
    Menu {
        title: "Priority"
        MenuItem { text: "None"; onTriggered: backend.setPriority(0) }
        MenuItem { text: "Low"; onTriggered: backend.setPriority(1) }
        MenuItem { text: "Medium"; onTriggered: backend.setPriority(2) }
        MenuItem { text: "High"; onTriggered: backend.setPriority(3) }
        MenuSeparator {}
        MenuItem { text: "Increase Priority"; onTriggered: backend.bumpPriority(1) }
        MenuItem { text: "Decrease Priority"; onTriggered: backend.bumpPriority(-1) }
    }
    MenuItem { text: root.row.starred ? "Unstar" : "Star"; onTriggered: backend.setStarred(root.row.starred ? 0 : 1) }
    MenuSeparator {}
    Menu {
        title: "Convert"
        MenuItem { text: "To Task"; onTriggered: backend.convertSelection(0) }
        MenuItem { text: "To Project"; onTriggered: backend.convertSelection(1) }
        MenuItem { text: "To Checklist"; onTriggered: backend.convertSelection(2) }
    }
    MenuItem {
        text: "Open in List"
        visible: !backend.sourceKey.startsWith("list:")
        height: visible ? implicitHeight : 0
        onTriggered: {
            const info = backend.taskInfo(root.row.taskId)
            backend.selectSource("list:" + info.listId)
            backend.selectTask(root.row.taskId)
        }
    }
    MenuItem { text: "Perform Action"; enabled: (root.row.actionType || 0) > 0; onTriggered: backend.performAction(root.row.taskId) }
    MenuItem { text: "Duplicate"; onTriggered: backend.duplicateSelection() }
    MenuSeparator {}
    MenuItem { text: "Delete"; onTriggered: backend.deleteSelection() }
}
