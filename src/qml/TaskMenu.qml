import QtQuick
import QtQuick.Controls

// The right-click menu on tasks; it acts on the whole selection.
Menu {
    id: root
    property var row: ({})
    // What the menu needs to know about the task, read as it opens.
    property var info: ({})
    property var untagChoices: []
    property bool sortDescending: false
    readonly property var window: ApplicationWindow.window
    // A project's sort applies to the project itself, or the one a sub-task is in.
    readonly property int projectId: root.row.type === 1 || root.row.type === 2 ? root.row.taskId : (root.info.parentId || 0)

    // Edit opens the editor while the menu is still up; the menu then returns the keyboard
    // to the list, so the title is given it again.
    onClosed: if (backend.editingTaskId) Qt.callLater(() => backend.editField("title"))
    onAboutToShow: {
        info = backend.taskInfo(row.taskId || 0)
        untagChoices = backend.selectionTags()
    }

    // The style's own list, with its height held at the window's: it follows the list's
    // contentHeight, which ListView re-estimates as the menu scrolls, and the menu
    // then resized (and sprang back to the top) on every step. The cap leaves the
    // height alone once the menu is taller than the window.
    contentItem: ListView {
        implicitHeight: Math.min(contentHeight, root.window ? root.window.height - root.topPadding - root.bottomPadding : contentHeight)
        model: root.contentModel
        interactive: root.window ? contentHeight + root.topPadding + root.bottomPadding > root.height : false
        clip: true
        currentIndex: root.currentIndex
        ScrollIndicator.vertical: ScrollIndicator {}
    }

    function isoIn(days) {
        const d = new Date()
        d.setDate(d.getDate() + days)
        return d.getFullYear() + "-" + String(d.getMonth() + 1).padStart(2, "0") + "-" + String(d.getDate()).padStart(2, "0")
    }
    function pickDate(kind) { if (window) window.openCommand(kind) }

    // The relative dates Defer and Schedule both offer.
    readonly property var dateChoices: [["Today", 0], ["Tomorrow", 1], ["In 2 Days", 2], ["In 3 Days", 3], ["Next Week", 7],
                                        ["In 2 Weeks", 14], ["In a Month", 30]]

    MenuItem { text: root.row.completed ? "Mark as Not Completed" : "Mark as Completed"; onTriggered: backend.toggleComplete() }
    MenuItem { text: "Edit"; onTriggered: backend.beginEdit(root.row.taskId) }
    MenuSeparator {}
    MenuItem { text: "Due Today"; onTriggered: backend.setDueInDays(0) }
    MenuItem { text: "Due Tomorrow"; onTriggered: backend.setDueInDays(1) }
    MenuItem { text: "Start Today"; onTriggered: backend.setStartInDays(0) }
    Menu {
        title: "Defer"
        Repeater {
            model: root.dateChoices
            MenuItem {
                required property var modelData
                text: modelData[0]
                onTriggered: backend.deferTo(root.isoIn(modelData[1]))
            }
        }
        MenuSeparator {}
        MenuItem { text: "Pick Date or Offset (+3d)…"; onTriggered: root.pickDate("defer") }
        MenuItem { text: "No Due Date"; onTriggered: backend.clearDue() }
    }
    Menu {
        title: "Schedule"
        Repeater {
            model: root.dateChoices
            MenuItem {
                required property var modelData
                text: modelData[0]
                onTriggered: backend.scheduleTo(root.isoIn(modelData[1]))
            }
        }
        MenuSeparator {}
        MenuItem { text: "Pick Date…"; onTriggered: root.pickDate("schedule") }
        MenuItem { text: "No Start Date"; onTriggered: backend.clearStart() }
    }
    Menu {
        id: timeMenu
        title: "Due Time"
        // The Due Time Shortcuts setting, read again each time the menu opens.
        property var chips: backend.dueTimeChips()
        onAboutToShow: chips = backend.dueTimeChips()
        Repeater {
            model: timeMenu.chips
            MenuItem {
                required property var modelData
                text: modelData.label
                onTriggered: backend.setSelectionDueTime(modelData.minutes)
            }
        }
        MenuSeparator {}
        MenuItem { text: "Other…"; onTriggered: root.pickDate("time") }
        MenuItem { text: "No Time"; onTriggered: backend.setSelectionDueTime(-1) }
    }
    Menu {
        title: "Duration"
        Repeater {
            model: [["15 mins", 15], ["30 mins", 30], ["45 mins", 45], ["1 hour", 60], ["2 hours", 120], ["4 hours", 240], ["1 day", 1440]]
            MenuItem {
                required property var modelData
                text: modelData[0]
                onTriggered: backend.setSelectionDuration(modelData[1])
            }
        }
        MenuSeparator {}
        MenuItem { text: "Other…"; onTriggered: root.pickDate("duration") }
        MenuItem { text: "No Duration"; onTriggered: backend.setSelectionDuration(0) }
    }
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
    // The agent's priority suggestion, only while the feature is on and more
    // than one task is selected; it proposes, it changes nothing itself. The
    // review sheet opens on Backend.reviewRequested when the answer lands.
    MenuItem {
        objectName: "suggestPriorities"
        visible: backend.agentEnabled && backend.selectionCount > 1
        height: visible ? implicitHeight : 0
        text: "Suggest Priorities"
        onTriggered: backend.suggestPriorities()
    }
    // Break the right-clicked task (or project) into steps. It acts on that one
    // task only, never the selection, and changes nothing until applied.
    MenuItem {
        objectName: "suggestSteps"
        visible: backend.agentEnabled
        height: visible ? implicitHeight : 0
        text: "Suggest Steps…"
        onTriggered: backend.suggestSteps(root.row.taskId)
    }
    MenuItem { text: root.row.starred ? "Unstar" : "Star"; onTriggered: backend.setStarred(root.row.starred ? 0 : 1) }
    MenuSeparator {}
    Menu {
        id: tagMenu
        title: "Tag"
        // Only the first dozen tags fit a menu; the palette finds the rest.
        Repeater {
            model: backend.tagNames.slice(0, 12)
            MenuItem {
                required property string modelData
                text: modelData
                onTriggered: backend.tagSelection(modelData, false)
            }
        }
        MenuSeparator {}
        MenuItem { text: "Choose Tags…"; onTriggered: root.pickDate("tag") }
    }
    Menu {
        title: "Untag"
        enabled: root.untagChoices.length > 0
        Repeater {
            model: root.untagChoices
            MenuItem {
                required property var modelData
                text: modelData.name
                onTriggered: backend.untagSelection([modelData.id])
            }
        }
        MenuSeparator {}
        MenuItem { text: "All Tags"; onTriggered: backend.untagSelection() }
    }
    MenuSeparator {}
    Menu {
        title: "Move To"
        Repeater {
            model: backend.listChoices
            MenuItem {
                required property var modelData
                text: modelData.name
                onTriggered: backend.moveSelectionToList(modelData.id)
            }
        }
    }
    Menu {
        title: "Copy To"
        Repeater {
            model: backend.listChoices
            MenuItem {
                required property var modelData
                text: modelData.name
                onTriggered: backend.copySelectionToList(modelData.id)
            }
        }
    }
    Menu {
        id: sectionMenu
        title: "Move to Section"
        // Read as the menu opens (root.info), so a section added since the row was
        // built is offered; a binding on the row alone never looked again.
        readonly property var choices: root.info.listId ? backend.sectionChoices(root.info.listId) : []
        enabled: choices.length > 0
        Repeater {
            model: sectionMenu.choices
            MenuItem {
                required property var modelData
                text: modelData.name
                onTriggered: backend.moveSelectionToSection(modelData.id)
            }
        }
        MenuSeparator {}
        MenuItem { text: "No Section"; onTriggered: backend.moveSelectionToSection(0) }
    }
    MenuSeparator {}
    Menu {
        title: "Convert"
        MenuItem { text: "To Task"; onTriggered: backend.convertSelection(0) }
        MenuItem { text: "To Project"; onTriggered: backend.convertSelection(1) }
        MenuItem { text: "To Checklist"; onTriggered: backend.convertSelection(2) }
    }
    Menu {
        title: "New"
        MenuItem { text: "Project from Selection"; onTriggered: backend.newFromSelection(1) }
        MenuItem { text: "Checklist from Selection"; onTriggered: backend.newFromSelection(2) }
    }
    Menu {
        title: root.row.type === 2 || root.info.parentType === 2 ? "Sort Checklist" : "Sort Project"
        enabled: root.projectId > 0
        Repeater {
            model: [[8, "Manually"], [11, "Status"], [2, "Priority"], [1, "Start Date"], [0, "Due Date"], [5, "Creation"],
                    [13, "Note"], [4, "Title"], [9, "Tag"], [14, "Location"], [7, "Duration"]]
            MenuItem {
                required property var modelData
                text: modelData[1]
                onTriggered: backend.setProjectSort(root.projectId, modelData[0], !root.sortDescending)
            }
        }
        MenuSeparator {}
        MenuItem { text: "Descending"; checkable: true; checked: root.sortDescending; onToggled: root.sortDescending = checked }
    }
    MenuItem {
        text: "Goto Project"
        enabled: !!root.info.parentId
        onTriggered: backend.gotoProject(root.row.taskId)
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
    MenuSeparator {}
    MenuItem { text: "Perform Action"; enabled: (root.row.actionType || 0) > 0; onTriggered: backend.performAction(root.row.taskId) }
    MenuItem { text: "Link File…"; onTriggered: filePicker.open("Link a file in “" + root.row.title + "”", "link:" + root.row.taskId) }
    MenuItem { text: "Duplicate"; onTriggered: backend.duplicateSelection() }
    MenuSeparator {}
    MenuItem { text: "Delete"; onTriggered: backend.deleteSelection() }
    MenuSeparator {}
    MenuItem { text: "Add to Calendar"; onTriggered: backend.addToCalendar(root.row.taskId) }
    MenuItem { text: "Save as Event (.ics)…"; onTriggered: filePicker.save("Save the task as a calendar event", "Event.ics", "event:" + root.row.taskId) }
    MenuItem { text: "Email These Tasks…"; onTriggered: backend.emailTasks() }
    MenuItem { text: "Export Selection…"; onTriggered: filePicker.save("Export the selected tasks", "Fairfax.csv", "export-selection") }
}
