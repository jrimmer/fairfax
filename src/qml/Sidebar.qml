import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The Lists panel: focus lists, list groups with their colour-banded lists,
// projects revealed under a list, and the mini calendar at the bottom.
Rectangle {
    id: root

    signal hideRequested()

    color: theme.sidebar
    property int renamingListId: 0
    property int renamingGroupId: 0

    function selectInbox() {
        for (let i = 0; i < backend.sidebar.count; ++i) {
            const row = backend.sidebar.get(i)
            if (row.icon === "inbox") { backend.selectSource(row.sourceKey); return }
        }
    }

    function renameSelected() {
        for (let i = 0; i < backend.sidebar.count; ++i) {
            const row = backend.sidebar.get(i)
            if (row.selected && row.kind === "list") { renamingListId = row.listId; return }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header strip, level with the task pane's toolbar.
        Item {
            Layout.fillWidth: true
            implicitHeight: 46
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                text: "Precrime"
                color: theme.muted
                font.pixelSize: backend.fontSize - 1
                font.weight: Font.DemiBold
                font.letterSpacing: 0.4
            }
            IconButton {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                icon: "sidebar"
                tip: "Hide Lists (Ctrl+Alt+←)"
                onClicked: root.hideRequested()
            }
        }

        ListView {
            id: lists
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: backend.sidebar
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Loader {
                id: rowLoader
                required property var model
                required property int index
                width: ListView.view.width
                sourceComponent: model.kind === "header" ? headerRow
                               : model.kind === "gap" ? gapRow
                               : listRow
            }
        }

        MiniCalendar {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            visible: root.height > 660
        }

        // Footer: new list or group.
        Item {
            Layout.fillWidth: true
            implicitHeight: 38
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: theme.divider }
            IconButton {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                icon: "plus"
                tip: "New List or Group"
                onClicked: addMenu.popup()
                Menu {
                    id: addMenu
                    MenuItem { text: "New List"; onTriggered: { backend.newList(); root.renameSelected() } }
                    MenuItem { text: "New List Group"; onTriggered: root.renamingGroupId = backend.newGroup() }
                }
            }
        }
    }

    Component {
        id: gapRow
        Item { height: 10 }
    }

    Component {
        id: headerRow
        Item {
            id: header
            height: 34
            property var row: parent ? parent.model : ({})
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 6
                visible: root.renamingGroupId !== header.row.groupId
                text: header.row.title || ""
                color: theme.muted
                font.pixelSize: backend.fontSize - 1
                font.weight: Font.Bold
            }
            Icon {
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 8
                name: header.row.collapsed ? "chevronRight" : "chevronDown"
                size: 11
                color: theme.faint
                visible: groupMouse.containsMouse || header.row.collapsed
            }
            MouseArea {
                id: groupMouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: (mouse) => {
                    if (mouse.button === Qt.RightButton) groupMenu.popup()
                    else backend.toggleGroupCollapsed(header.row.groupId)
                }
                onDoubleClicked: root.renamingGroupId = header.row.groupId
            }
            Menu {
                id: groupMenu
                MenuItem { text: "Rename Group…"; onTriggered: root.renamingGroupId = header.row.groupId }
                MenuItem { text: header.row.collapsed ? "Expand" : "Collapse"; onTriggered: backend.toggleGroupCollapsed(header.row.groupId) }
            }
            InlineEditor {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.bottom: parent.bottom
                active: root.renamingGroupId === header.row.groupId && header.row.groupId > 0
                initial: header.row.title || ""
                onCommitted: (text) => { backend.renameGroup(header.row.groupId, text); root.renamingGroupId = 0 }
                onCancelled: root.renamingGroupId = 0
            }
        }
    }

    Component {
        id: listRow
        Item {
            id: item
            height: row.kind === "project" ? 28 : 32
            property var row: parent ? parent.model : ({})
            property int rowIndex: parent ? parent.index : 0
            readonly property bool isList: row.kind === "list"
            readonly property color band: theme.listColor(row.color || 0)
            // Consecutive lists' colour pills join into one rounded column.
            readonly property bool runStart: {
                const prev = backend.sidebar.get(rowIndex - 1)
                return !prev || prev.kind !== "list" && prev.kind !== "project"
            }
            readonly property bool runEnd: {
                const next = backend.sidebar.get(rowIndex + 1)
                return !next || next.kind !== "list" && next.kind !== "project"
            }

            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                radius: 6
                color: item.row.selected ? theme.selection : (mouse.containsMouse ? theme.mix(theme.sidebar, theme.foreground, 0.05) : "transparent")
            }

            // The list's colour band.
            Rectangle {
                visible: item.isList || item.row.kind === "project"
                x: 30
                width: 7
                y: item.runStart ? 3 : 0
                height: parent.height - (item.runStart ? 3 : 0) - (item.runEnd ? 3 : 0)
                radius: 3.5
                color: item.band
                // Square off the joins so stacked pills read as one column.
                Rectangle { visible: !item.runStart; width: parent.width; height: parent.radius; color: parent.color }
                Rectangle { visible: !item.runEnd; width: parent.width; height: parent.radius; color: parent.color; anchors.bottom: parent.bottom }
                Icon {
                    visible: !!item.row.excluded && item.isList
                    anchors.centerIn: parent
                    name: "eyeOff"
                    size: 7
                    stroke: 2.4
                    color: theme.background
                }
            }

            Icon {
                visible: item.row.expandable === true
                x: 13
                anchors.verticalCenter: parent.verticalCenter
                name: item.row.expanded ? "chevronDown" : "chevronRight"
                size: 11
                stroke: 2
                color: theme.muted
                MouseArea { anchors.fill: parent; anchors.margins: -5; onClicked: backend.toggleListExpanded(item.row.listId) }
            }

            // Focus list glyphs; Today shows the day of the month like the reference app.
            Item {
                visible: item.row.kind === "focus" || item.row.kind === "smart"
                x: 22
                width: 20
                height: 20
                anchors.verticalCenter: parent.verticalCenter
                Icon {
                    anchors.centerIn: parent
                    visible: item.row.icon !== "today"
                    name: item.row.icon || ""
                    size: 19
                    color: item.row.icon === "starred" ? theme.star : theme.muted
                }
                Rectangle {
                    anchors.centerIn: parent
                    visible: item.row.icon === "today"
                    width: 18
                    height: 18
                    radius: 4
                    color: theme.overdue
                    Text {
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: 1
                        text: new Date().getDate()
                        color: theme.background
                        font.pixelSize: 10
                        font.bold: true
                    }
                }
            }

            Icon {
                visible: item.row.kind === "project"
                x: 48
                anchors.verticalCenter: parent.verticalCenter
                name: item.row.icon || "project"
                size: 14
                color: theme.muted
            }

            // Due-today (white) or overdue (red) glow at the name's edge.
            Rectangle {
                visible: !!item.row.dot && item.row.kind !== "focus"
                x: 41
                anchors.verticalCenter: parent.verticalCenter
                width: 5
                height: 5
                radius: 2.5
                color: item.row.dot === "overdue" ? theme.overdue : theme.foreground
            }

            Text {
                id: label
                visible: root.renamingListId !== item.row.listId || !item.isList
                anchors.verticalCenter: parent.verticalCenter
                x: item.row.kind === "focus" || item.row.kind === "smart" ? 52 : item.row.kind === "project" ? 68 : 50
                width: counts.x - x - 6
                text: item.row.title || ""
                elide: Text.ElideRight
                color: item.row.excluded && item.row.kind === "project" ? theme.muted : theme.foreground
                font.pixelSize: item.row.kind === "project" ? backend.fontSize - 1 : backend.fontSize
            }

            Row {
                id: counts
                anchors.right: parent.right
                anchors.rightMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3
                Text {
                    visible: (item.row.overdue || 0) > 0
                    text: item.row.overdue > 999 ? "999+" : item.row.overdue || ""
                    color: theme.overdue
                    font.pixelSize: backend.fontSize - 2
                }
                Text {
                    visible: (item.row.overdue || 0) > 0 && (item.row.undone || 0) > 0
                    text: "|"
                    color: theme.faint
                    font.pixelSize: backend.fontSize - 2
                }
                Text {
                    visible: (item.row.undone || 0) > 0
                    text: item.row.undone > 999 ? "999+" : item.row.undone || ""
                    color: theme.muted
                    font.pixelSize: backend.fontSize - 2
                }
            }

            MouseArea {
                id: mouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: (m) => {
                    if (item.row.sourceKey) backend.selectSource(item.row.sourceKey)
                    if (m.button === Qt.RightButton && item.isList) listMenu.popup()
                    if (m.button === Qt.RightButton && item.row.kind === "smart") smartMenu.popup()
                }
                onDoubleClicked: if (item.isList || item.row.kind === "smart") root.renamingListId = item.row.listId
            }

            InlineEditor {
                anchors.left: label.left
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                active: (item.isList || item.row.kind === "smart") && root.renamingListId === item.row.listId
                initial: item.row.title || ""
                onCommitted: (text) => { backend.renameList(item.row.listId, text); root.renamingListId = 0 }
                onCancelled: root.renamingListId = 0
            }

            ListMenu { id: listMenu; listId: item.row.listId || 0; onRename: root.renamingListId = item.row.listId }
            Menu {
                id: smartMenu
                MenuItem { text: "Rename…"; onTriggered: root.renamingListId = item.row.listId }
                MenuItem { text: "Delete Smart List"; onTriggered: backend.deleteList(item.row.listId) }
            }
        }
    }
}
