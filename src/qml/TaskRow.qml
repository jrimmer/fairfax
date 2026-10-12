import QtQuick
import QtQuick.Controls
import "Icons.js" as Icons

// One task in the list, laid out like the reference app's row: checkbox, title, then tags,
// action, duration, date and priority on the right, with the parent project
// as a breadcrumb underneath when the task appears outside it.
Item {
    id: root

    property var row
    property real margin: 40

    readonly property real indent: (row.depth || 0) * 28
    readonly property bool hasCrumb: !!row.breadcrumb
    height: Math.round(backend.fontSize * 2.5) + (hasCrumb ? backend.fontSize + 2 : 0)
    opacity: row.dimmed ? 0.55 : 1

    // The list's colour runs down the pane's leading edge, row by row.
    Rectangle {
        width: 4
        height: parent.height
        color: theme.listColor(root.row.listColor || 0)
        opacity: 0.9
    }

    Rectangle {
        x: root.margin - 10 + root.indent
        width: parent.width - x - root.margin + 10
        height: parent.height - 2
        y: 1
        radius: 6
        color: root.row.selected ? theme.selection
             : mouse.containsMouse ? theme.mix(theme.background, theme.foreground, 0.035) : "transparent"
    }

    Rectangle {
        x: root.margin + root.indent + 28
        width: parent.width - x - root.margin
        anchors.bottom: parent.bottom
        height: 1
        color: theme.divider
        visible: !root.row.selected
    }

    // Disclosure for projects and checklists.
    Icon {
        visible: root.row.hasChildren && (root.row.depth || 0) === 0
        x: root.margin + root.indent - 20
        y: (Math.round(backend.fontSize * 2.5) - height) / 2
        name: root.row.collapsed ? "chevronRight" : "chevronDown"
        size: 11
        stroke: 2
        color: theme.muted
        MouseArea { anchors.fill: parent; anchors.margins: -6; onClicked: backend.toggleCollapsed(root.row.taskId) }
    }

    // Checkbox: a circle, a repeat arrow for repeating tasks, or the
    // checklist's square badge with its open-item count.
    Item {
        id: check
        x: root.margin + root.indent
        y: (Math.round(backend.fontSize * 2.5) - height) / 2
        width: 18
        height: 18

        Rectangle {
            anchors.fill: parent
            visible: root.row.type !== 2 && !root.row.repeating
            radius: 9
            color: root.row.completed ? theme.muted : "transparent"
            border.width: 1.3
            border.color: root.row.overdue ? theme.overdue : theme.faint
            Icon { anchors.centerIn: parent; visible: root.row.completed; name: "check"; size: 12; stroke: 2.4; color: theme.background }
        }
        Icon {
            anchors.centerIn: parent
            visible: root.row.type !== 2 && root.row.repeating
            name: root.row.completed ? "done" : "repeat"
            size: 19
            color: root.row.overdue ? theme.overdue : theme.muted
        }
        Rectangle {
            anchors.fill: parent
            visible: root.row.type === 2
            radius: 4
            color: root.row.completed ? theme.muted : "transparent"
            border.width: 1.3
            border.color: theme.today
            Text {
                anchors.centerIn: parent
                text: root.row.completed ? "✓" : (root.row.childOpen || "")
                color: root.row.completed ? theme.background : theme.today
                font.pixelSize: 10
                font.bold: true
            }
        }
        MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: backend.toggleComplete(root.row.taskId) }
    }

    Item {
        id: projectGlyph
        visible: root.row.type === 1
        anchors.left: check.right
        anchors.leftMargin: 8
        y: (Math.round(backend.fontSize * 2.5) - height) / 2
        width: visible ? 16 : 0
        height: 16
        Icon { anchors.centerIn: parent; name: "project"; size: 15; color: theme.muted }
    }

    Text {
        id: title
        anchors.left: projectGlyph.visible ? projectGlyph.right : check.right
        anchors.leftMargin: 10
        anchors.right: details.left
        anchors.rightMargin: 12
        y: (Math.round(backend.fontSize * 2.5) - height) / 2
        text: root.row.title || "New Task"
        elide: Text.ElideRight
        font.pixelSize: backend.fontSize
        font.strikeout: root.row.completed
        color: !root.row.title ? theme.faint
             : root.row.completed ? theme.muted
             : root.row.overdue ? theme.overdue : theme.foreground
    }

    Row {
        visible: root.hasCrumb
        anchors.left: title.left
        anchors.top: title.bottom
        anchors.topMargin: 1
        spacing: 5
        Icon { name: "breadcrumb"; size: 11; color: theme.faint; anchors.verticalCenter: parent.verticalCenter }
        Icon { name: "project"; size: 11; color: theme.muted; anchors.verticalCenter: parent.verticalCenter }
        Text { text: root.row.breadcrumb || ""; color: theme.muted; font.pixelSize: backend.fontSize - 2 }
    }

    Row {
        id: details
        anchors.right: parent.right
        anchors.rightMargin: root.margin
        height: Math.round(backend.fontSize * 2.5)
        spacing: 10

        // Project progress pill.
        Rectangle {
            visible: root.row.type === 1 && root.row.childCount > 0
            anchors.verticalCenter: parent.verticalCenter
            width: pct.implicitWidth + 12
            height: pct.implicitHeight + 4
            radius: height / 2
            color: Qt.alpha(theme.start, 0.18)
            Text { id: pct; anchors.centerIn: parent; text: (root.row.progress || 0) + "%"; color: theme.start; font.pixelSize: backend.fontSize - 3; font.bold: true }
        }

        Repeater {
            model: root.width > 760 ? (root.row.tags || []).slice(0, 3) : []
            Rectangle {
                required property string modelData
                anchors.verticalCenter: parent.verticalCenter
                width: tagText.implicitWidth + 12
                height: tagText.implicitHeight + 4
                radius: height / 2
                color: theme.tagBubble
                Text { id: tagText; anchors.centerIn: parent; text: modelData; color: theme.muted; font.pixelSize: backend.fontSize - 3 }
            }
        }
        Icon {
            visible: root.width <= 760 && (root.row.tags || []).length > 0
            anchors.verticalCenter: parent.verticalCenter
            name: "tag"; size: 14; color: theme.faint
        }
        Icon {
            visible: !!root.row.hasAlarm
            anchors.verticalCenter: parent.verticalCenter
            name: "alarm"
            size: 14
            color: theme.faint
        }
        Icon {
            visible: (root.row.actionType || 0) > 0
            anchors.verticalCenter: parent.verticalCenter
            name: Icons.actionIcons[root.row.actionType || 0]
            size: 14
            color: theme.today
        }
        Row {
            visible: !!root.row.durationText
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3
            Icon { name: "clock"; size: 13; color: theme.duration; anchors.verticalCenter: parent.verticalCenter }
            Text { text: root.row.durationText || ""; color: theme.duration; font.pixelSize: backend.fontSize - 2 }
        }
        Column {
            visible: !!root.row.dueText || !!root.row.startText
            anchors.verticalCenter: parent.verticalCenter
            Text {
                visible: !!root.row.startText
                anchors.right: parent.right
                text: root.row.startText || ""
                color: theme.start
                font.pixelSize: root.row.dueText ? backend.fontSize - 3 : backend.fontSize - 2
            }
            Text {
                visible: !!root.row.dueText
                anchors.right: parent.right
                text: root.row.dueText || ""
                font.pixelSize: root.row.startText ? backend.fontSize - 3 : backend.fontSize - 2
                color: root.row.completed ? theme.faint
                     : root.row.dueTone === "overdue" ? theme.overdue
                     : root.row.dueTone === "today" ? theme.today : theme.muted
            }
        }
        // Priority bars and star; clicking opens the priority menu.
        Item {
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            Column {
                anchors.centerIn: parent
                spacing: 2.5
                visible: (root.row.priority || 0) > 0 && !root.row.starred
                Repeater {
                    model: 3
                    Rectangle {
                        required property int index
                        width: 13
                        height: 2
                        radius: 1
                        color: root.row.priority === 3 ? theme.priorityHigh : root.row.priority === 2 ? theme.priorityMedium : theme.priorityLow
                        opacity: root.row.priority === 1 && index > 0 ? 0.25 : root.row.priority === 2 && index > 1 ? 0.25 : 1
                    }
                }
            }
            Icon {
                anchors.centerIn: parent
                visible: !!root.row.starred
                name: "starred"
                size: 15
                filled: true
                color: theme.star
            }
            Rectangle {
                anchors.centerIn: parent
                visible: !root.row.starred && !root.row.priority && mouse.containsMouse
                width: 13; height: 2; radius: 1; color: theme.faint
            }
            MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: priorityMenu.popup() }
            Menu {
                id: priorityMenu
                MenuItem { text: "None"; onTriggered: backend.setPriority(0, root.row.taskId) }
                MenuItem { text: "High"; onTriggered: backend.setPriority(3, root.row.taskId) }
                MenuItem { text: "Medium"; onTriggered: backend.setPriority(2, root.row.taskId) }
                MenuItem { text: "Low"; onTriggered: backend.setPriority(1, root.row.taskId) }
                MenuSeparator {}
                MenuItem { text: root.row.starred ? "Unstar" : "Star"; onTriggered: backend.setStarred(root.row.starred ? 0 : 1, root.row.taskId) }
            }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        z: -1
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: (m) => {
            const mode = m.modifiers & Qt.ShiftModifier ? 1 : m.modifiers & Qt.ControlModifier ? 2 : 0
            if (m.button === Qt.RightButton && root.row.selected) return
            backend.selectTask(root.row.taskId, mode)
        }
        onClicked: (m) => { if (m.button === Qt.RightButton) contextMenu.popup() }
        onDoubleClicked: backend.beginEdit(root.row.taskId)
    }

    TaskMenu { id: contextMenu; row: root.row }
}
