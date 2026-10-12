import QtQuick
import QtQuick.Controls

// The Focus Filter: the sun toggles it, the chevron picks its conditions.
// Conditions combine with "or", and each list keeps its own.
Row {
    id: root
    spacing: 0

    readonly property var choices: [
        ["overdue", "Overdue"], ["nodue", "No Due Date"], ["inprogress", "In Progress"], ["today", "Today"],
        ["tomorrow", "Tomorrow"], ["thisweek", "This Week"], ["next3", "Next 3 Days"], ["next7", "Next 7 Days"],
        ["next30", "Next 30 Days"], ["next6m", "Next 6 Months"], ["next12m", "Next 12 Months"],
        ["hiprio", "High Priority"], ["starred", "Starred"]
    ]

    IconButton {
        icon: "sun"
        size: 16
        checked: backend.focusOn
        tip: backend.focusOn ? "Focus Filter on" : "Focus Filter"
        onClicked: backend.toggleFocus()
    }
    IconButton {
        icon: "chevronDown"
        size: 10
        tip: "Focus conditions"
        onClicked: menu.popup(0, height)
        Menu {
            id: menu
            Repeater {
                model: root.choices
                MenuItem {
                    required property var modelData
                    text: modelData[1]
                    checkable: true
                    checked: backend.focusConditions.indexOf(modelData[0]) >= 0
                    onTriggered: backend.setFocusCondition(modelData[0], checked)
                }
            }
        }
    }
}
