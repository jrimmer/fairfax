import QtQuick
import QtQuick.Controls
import "Icons.js" as Icons

// One task in the list, laid out as checkbox, title, then tags,
// action, duration, date and priority on the right, with the parent project
// as a breadcrumb underneath when the task appears outside it.
Item {
    id: root

    objectName: "taskRow:" + (r.taskId || 0)
    property var row
    // The last real row, so bindings stay valid while the delegate is torn
    // down and its model entry is already gone.
    property var r: row || ({})
    onRowChanged: if (row) r = row
    property real margin: 40
    property bool revealNames: false  // Alt held: show list names.
    readonly property real lineHeight: Math.round(backend.fontSize * [2.05, 2.5, 2.8][backend.density])
    readonly property bool showNotes: !!r.notesPreview && (backend.showNotesPreview || backend.density === 2)

    // "Show overdue in red": off, overdue tasks use the ordinary colours.
    readonly property color overdueColor: backend.overdueInRed ? theme.overdue : theme.muted
    readonly property color overdueTitle: backend.overdueInRed ? theme.overdue : theme.foreground

    readonly property real indent: (r.depth || 0) * 28
    readonly property bool hasCrumb: !!r.breadcrumb
    height: lineHeight + (hasCrumb ? backend.fontSize + 2 : 0) + (showNotes ? backend.fontSize + 2 : 0)
    opacity: r.dimmed ? 0.55 : 1

    // What a screen reader reads for this row: the title, then the facts a
    // sighted user sees on it, comma-separated. The date uses the same
    // friendly text the row shows.
    readonly property string axDetails: {
        const parts = []
        if (root.r.type === 1 || root.r.type === 2) {
            parts.push(root.r.type === 1 ? "project" : "checklist")
            if ((root.r.childCount || 0) > 0)
                parts.push(((root.r.childCount || 0) - (root.r.childOpen || 0)) + " of " + root.r.childCount + " done")
            parts.push(root.r.collapsed ? "collapsed" : "expanded")
        }
        if (root.r.startText)
            parts.push("starts " + root.r.startText)
        if (root.r.dueText)
            parts.push((root.r.overdue ? "overdue since " : "due ") + root.r.dueText)
        if ((root.r.priority || 0) > 0)
            parts.push(["", "low", "medium", "high"][root.r.priority] + " priority")
        if (root.r.starred)
            parts.push("starred")
        // The list name shows in focus lists when chosen in Settings, or while
        // Alt is held; the reader hears it whenever the row would show it.
        if ((root.revealNames || backend.showListNames) && backend.sourceIsFocus && root.r.listName)
            parts.push("in " + root.r.listName)
        if ((root.r.tags || []).length > 0)
            parts.push("tagged " + (root.r.tags || []).join(", "))
        if (root.r.hasNotes)
            parts.push("has notes")
        if (root.r.completed)
            parts.push("completed")
        return parts.join(", ")
    }

    Accessible.role: Accessible.ListItem
    Accessible.name: [root.r.title || "", root.axDetails].filter(s => s !== "").join(", ")
    Accessible.selected: !!root.r.selected
    Accessible.checkable: true
    Accessible.checked: !!root.r.completed

    // The list's colour runs down the pane's leading edge, row by row.
    Rectangle {
        width: 4
        height: parent.height
        color: theme.listColor(root.r.listColor || 0)
        opacity: 0.9
    }

    Rectangle {
        x: root.margin - 10 + root.indent
        width: parent.width - x - root.margin + 10
        height: parent.height - 2
        y: 1
        radius: 6
        color: root.r.selected ? theme.selection
             : mouse.containsMouse ? theme.mix(theme.background, theme.foreground, 0.035) : "transparent"
    }

    Rectangle {
        x: root.margin + root.indent + 28
        width: parent.width - x - root.margin
        anchors.bottom: parent.bottom
        height: 1
        color: theme.divider
        visible: !root.r.selected
    }

    // Disclosure for projects and checklists.
    Icon {
        objectName: "rowDisclosure:" + (root.r.taskId || 0)
        visible: root.r.hasChildren && (root.r.depth || 0) === 0
        x: root.margin + root.indent - 20
        y: (root.lineHeight - height) / 2
        name: root.r.collapsed ? "chevronRight" : "chevronDown"
        size: 11
        stroke: 2
        color: theme.muted
        MouseArea { anchors.fill: parent; anchors.margins: -6; onClicked: backend.toggleCollapsed(root.r.taskId) }
    }

    // Checkbox: a circle, a repeat arrow for repeating tasks, or the
    // checklist's square badge with its open-item count.
    Item {
        id: check
        objectName: "taskCheck:" + (root.r.taskId || 0)
        x: root.margin + root.indent
        y: (root.lineHeight - height) / 2
        width: 18
        height: 18

        Rectangle {
            anchors.fill: parent
            visible: root.r.type !== 2 && !root.r.repeating
            radius: 9
            color: root.r.completed ? theme.muted : "transparent"
            border.width: 1.3
            border.color: root.r.overdue && backend.overdueInRed ? theme.overdue : theme.faint
            Icon { anchors.centerIn: parent; visible: root.r.completed; name: "check"; size: 12; stroke: 2.4; color: theme.background }
        }
        Icon {
            anchors.centerIn: parent
            objectName: "rowRepeat:" + (root.r.taskId || 0)
            visible: root.r.type !== 2 && root.r.repeating
            name: root.r.completed ? "done" : "repeat"
            size: 19
            color: root.r.overdue ? root.overdueColor : theme.muted
        }
        Rectangle {
            objectName: "rowChecklist:" + (root.r.taskId || 0)
            anchors.fill: parent
            visible: root.r.type === 2
            radius: 4
            color: root.r.completed ? theme.muted : "transparent"
            border.width: 1.3
            border.color: theme.today
            Text {
                anchors.centerIn: parent
                text: root.r.completed ? "✓" : (root.r.childOpen || "")
                color: root.r.completed ? theme.background : theme.today
                font.pixelSize: 10
                font.bold: true
            }
        }
        MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: backend.toggleComplete(root.r.taskId) }
    }

    Item {
        id: projectGlyph
        objectName: "rowProject:" + (root.r.taskId || 0)
        visible: root.r.type === 1
        anchors.left: check.right
        anchors.leftMargin: 8
        y: (root.lineHeight - height) / 2
        width: visible ? 16 : 0
        height: 16
        Icon { anchors.centerIn: parent; name: "project"; size: 15; color: theme.muted }
    }

    Text {
        id: title
        objectName: "taskTitle:" + (root.r.taskId || 0)
        anchors.left: projectGlyph.visible ? projectGlyph.right : check.right
        anchors.leftMargin: 10
        anchors.right: details.left
        anchors.rightMargin: 12
        y: (root.lineHeight - height) / 2
        text: root.r.title || "New Task"
        elide: Text.ElideRight
        font.pixelSize: backend.fontSize
        font.strikeout: root.r.completed
        color: !root.r.title ? theme.faint
             : root.r.completed ? theme.muted
             : root.r.overdue ? root.overdueTitle : theme.foreground
    }

    Text {
        id: notesLine
        visible: root.showNotes
        anchors.left: title.left
        anchors.right: title.right
        anchors.top: root.hasCrumb ? crumb.bottom : title.bottom
        anchors.topMargin: 1
        text: root.r.notesPreview || ""
        elide: Text.ElideRight
        color: theme.muted
        font.pixelSize: backend.fontSize - 2
    }

    Row {
        id: crumb
        visible: root.hasCrumb
        anchors.left: title.left
        anchors.top: title.bottom
        anchors.topMargin: 1
        spacing: 5
        Icon { name: "breadcrumb"; size: 11; color: theme.faint; anchors.verticalCenter: parent.verticalCenter }
        Icon { name: "project"; size: 11; color: theme.muted; anchors.verticalCenter: parent.verticalCenter }
        Text { text: root.r.breadcrumb || ""; color: theme.muted; font.pixelSize: backend.fontSize - 2 }
    }

    Row {
        id: details
        anchors.right: parent.right
        anchors.rightMargin: root.margin
        height: root.lineHeight
        spacing: 10

        // My Day's reason for a row in Overdue or Could do today: a muted chip.
        Rectangle {
            objectName: "myDayReason:" + (root.r.taskId || 0)
            visible: !!root.r.reason
            anchors.verticalCenter: parent.verticalCenter
            width: reasonText.implicitWidth + 12
            height: reasonText.implicitHeight + 4
            radius: height / 2
            color: theme.mix(theme.background, theme.foreground, 0.08)
            Text {
                id: reasonText
                anchors.centerIn: parent
                text: root.r.reason || ""
                color: theme.muted
                font.pixelSize: backend.fontSize - 3
            }
        }
        // List name, always in focus lists when chosen in Settings, or while
        // Alt is held.
        Text {
            visible: (root.revealNames || backend.showListNames) && backend.sourceIsFocus
            anchors.verticalCenter: parent.verticalCenter
            text: root.r.listName || ""
            color: theme.listColor(root.r.listColor || 0)
            font.pixelSize: backend.fontSize - 3
            font.weight: Font.DemiBold
        }
        // Project progress pill.
        Rectangle {
            objectName: "rowProgress:" + (root.r.taskId || 0)
            visible: root.r.type === 1 && root.r.childCount > 0
            anchors.verticalCenter: parent.verticalCenter
            width: pct.implicitWidth + 12
            height: pct.implicitHeight + 4
            radius: height / 2
            color: Qt.alpha(theme.start, 0.18)
            Text { id: pct; anchors.centerIn: parent; text: (root.r.progress || 0) + "%"; color: theme.start; font.pixelSize: backend.fontSize - 3; font.bold: true }
        }

        Repeater {
            // "Always show tag names": off, tags are an icon until Alt is held.
            model: (root.width > 760 && backend.showTagNames) || root.revealNames ? (root.r.tags || []).slice(0, 3) : []
            Rectangle {
                required property string modelData
                objectName: "rowTag:" + (root.r.taskId || 0) + ":" + modelData
                anchors.verticalCenter: parent.verticalCenter
                width: tagText.implicitWidth + 12
                height: tagText.implicitHeight + 4
                radius: height / 2
                color: theme.tagBubble
                Text { id: tagText; anchors.centerIn: parent; text: modelData; color: theme.muted; font.pixelSize: backend.fontSize - 3 }
            }
        }
        Icon {
            objectName: "rowTagIcon:" + (root.r.taskId || 0)
            visible: !((root.width > 760 && backend.showTagNames) || root.revealNames) && (root.r.tags || []).length > 0
            anchors.verticalCenter: parent.verticalCenter
            name: "tag"; size: 14; color: theme.faint
        }
        Icon {
            objectName: "rowLocation:" + (root.r.taskId || 0)
            visible: !!root.r.hasLocation
            anchors.verticalCenter: parent.verticalCenter
            name: "pin"
            size: 14
            color: theme.faint
        }
        Icon {
            visible: !!root.r.hasAttachment
            anchors.verticalCenter: parent.verticalCenter
            name: "attach"
            size: 14
            color: theme.faint
        }
        Icon {
            objectName: "rowAlarm:" + (root.r.taskId || 0)
            visible: !!root.r.hasAlarm
            anchors.verticalCenter: parent.verticalCenter
            name: "alarm"
            size: 14
            color: theme.faint
        }
        Icon {
            objectName: "rowAction:" + (root.r.taskId || 0)
            visible: (root.r.actionType || 0) > 0
            anchors.verticalCenter: parent.verticalCenter
            name: Icons.actionIcons[root.r.actionType || 0]
            size: 14
            color: theme.today
        }
        Row {
            objectName: "rowDuration:" + (root.r.taskId || 0)
            visible: !!root.r.durationText
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3
            Icon { name: "clock"; size: 13; color: theme.duration; anchors.verticalCenter: parent.verticalCenter }
            Text { text: root.r.durationText || ""; color: theme.duration; font.pixelSize: backend.fontSize - 2 }
        }
        Column {
            visible: !!root.r.dueText || !!root.r.startText
            anchors.verticalCenter: parent.verticalCenter
            Text {
                objectName: "rowStart:" + (root.r.taskId || 0)
                visible: !!root.r.startText
                anchors.right: parent.right
                text: root.r.startText || ""
                color: theme.start
                font.pixelSize: root.r.dueText ? backend.fontSize - 3 : backend.fontSize - 2
            }
            Text {
                objectName: "rowDue:" + (root.r.taskId || 0)
                visible: !!root.r.dueText
                anchors.right: parent.right
                text: root.r.dueText || ""
                font.pixelSize: root.r.startText ? backend.fontSize - 3 : backend.fontSize - 2
                color: root.r.completed ? theme.faint
                     : root.r.dueTone === "overdue" ? root.overdueColor
                     : root.r.dueTone === "today" ? theme.today : theme.muted
            }
        }
        // Priority bars and star; clicking toggles the star, right-click opens the priority menu.
        Item {
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            Column {
                anchors.centerIn: parent
                objectName: "rowPriority:" + (root.r.taskId || 0)
                spacing: 2.5
                visible: (root.r.priority || 0) > 0 && !root.r.starred
                Repeater {
                    model: 3
                    Rectangle {
                        required property int index
                        width: 13
                        height: 2
                        radius: 1
                        color: root.r.priority === 3 ? theme.priorityHigh : root.r.priority === 2 ? theme.priorityMedium : theme.priorityLow
                        opacity: root.r.priority === 1 && index > 0 ? 0.25 : root.r.priority === 2 && index > 1 ? 0.25 : 1
                    }
                }
            }
            Icon {
                objectName: "rowStar:" + (root.r.taskId || 0)
                anchors.centerIn: parent
                visible: !!root.r.starred
                name: "starred"
                size: 15
                filled: true
                color: theme.star
            }
            Rectangle {
                anchors.centerIn: parent
                visible: !root.r.starred && !root.r.priority && mouse.containsMouse
                width: 13; height: 2; radius: 1; color: theme.faint
            }
            // A click stars or unstars; the priority menu is for right-click
            // and long-press, so one stray tap no longer opens a menu.
            MouseArea {
                objectName: "rowStarArea:" + (root.r.taskId || 0)
                anchors.fill: parent
                anchors.margins: -4
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                property bool held: false
                onPressed: held = false
                onPressAndHold: { held = true; root.openMenu(priorityMenu) }
                onClicked: (mouse) => {
                    if (mouse.button === Qt.RightButton) root.openMenu(priorityMenu)
                    else if (!held) backend.setStarred(root.r.starred ? 0 : 1, root.r.taskId)
                }
            }
            // Built on first use: a menu per row made every row slow to create.
            Loader {
                id: priorityMenu
                active: false
                sourceComponent: Menu {
                    parent: root
                    MenuItem { text: "None"; onTriggered: backend.setPriority(0, root.r.taskId) }
                    MenuItem { text: "High"; onTriggered: backend.setPriority(3, root.r.taskId) }
                    MenuItem { text: "Medium"; onTriggered: backend.setPriority(2, root.r.taskId) }
                    MenuItem { text: "Low"; onTriggered: backend.setPriority(1, root.r.taskId) }
                    MenuSeparator {}
                    MenuItem { text: root.r.starred ? "Unstar" : "Star"; onTriggered: backend.setStarred(root.r.starred ? 0 : 1, root.r.taskId) }
                }
            }
        }
    }

    readonly property var proxy: ApplicationWindow.window ? ApplicationWindow.window.dragProxy : null

    // A tag dragged from the Tags panel tags this task; files dropped from
    // the file manager attach to it.
    DropArea {
        anchors.fill: parent
        keys: ["fairfax-tag"]
        onDropped: (drop) => backend.tagTask(root.r.taskId, drop.source.tagId)
        Rectangle { anchors.fill: parent; color: "transparent"; border.color: theme.accent; border.width: 2; radius: 6; visible: parent.containsDrag }
    }
    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: (drop) => { if (drop.hasUrls) { backend.dropFiles(root.r.taskId, drop.urls); drop.accept() } }
        Rectangle { anchors.fill: parent; color: "transparent"; border.color: theme.accent; border.width: 2; radius: 6; visible: parent.containsDrag }
    }

    // Dropping tasks onto this row moves them in front of it.
    DropArea {
        anchors.fill: parent
        keys: ["fairfax-tasks"]
        onDropped: backend.dropBefore(root.r.taskId)
        Rectangle {
            visible: parent.containsDrag
            x: root.margin + root.indent
            width: parent.width - x - root.margin
            height: 2
            color: theme.accent
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        z: -1
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        drag.target: root.proxy
        drag.threshold: 10
        drag.onActiveChanged: {
            if (!root.proxy) return
            if (drag.active) {
                const n = backend.selectionCount
                root.proxy.start(n > 1 ? n + " tasks" : root.r.title)
            } else {
                root.proxy.finish()
            }
        }
        onPressed: (m) => {
            if (root.proxy) root.proxy.place(mapToItem(null, m.x, m.y))
            const mode = m.modifiers & Qt.ShiftModifier ? 1 : m.modifiers & Qt.ControlModifier ? 2 : 0
            // Option (Alt) held as the drag starts copies instead of moving.
            backend.setDragCopy(!!(m.modifiers & Qt.AltModifier))
            if (m.button === Qt.RightButton && root.r.selected) return
            backend.selectTask(root.r.taskId, mode)
        }
        // Leaving the window turns the drag into a file drag-out.
        property bool dragOut: false
        onPositionChanged: (m) => {
            if (!drag.active || dragOut || !root.proxy) return
            const p = mapToItem(null, m.x, m.y)
            const w = ApplicationWindow.window
            if (w && (p.x < 0 || p.y < 0 || p.x > w.width || p.y > w.height)) {
                dragOut = true
                root.proxy.startExternal()
            }
        }
        onReleased: dragOut = false
        onClicked: (m) => { if (m.button === Qt.RightButton) root.openMenu(contextMenu) }
        onDoubleClicked: backend.beginEdit(root.r.taskId)
    }

    // The menus are built when first opened, not with every row.
    function openMenu(loader) {
        loader.active = true
        loader.item.popup()
    }
    Loader {
        id: contextMenu
        active: false
        sourceComponent: TaskMenu { parent: root; row: root.row }
    }
}
