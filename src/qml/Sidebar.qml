import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The Lists panel: focus lists, list groups with their colour-banded lists,
// projects revealed under a list, and the mini calendar at the bottom.
Rectangle {
    id: root

    signal hideRequested()
    signal settingsRequested()
    signal newSmartListRequested()
    signal editSmartListRequested()

    color: theme.sidebar
    property int renamingListId: 0
    property int renamingGroupId: 0
    property var archived: backend.archivedLists()
    property int dragListId: 0
    property int dragGroupId: 0
    property int deleteGroupId: 0
    readonly property bool calendarShown: backend.setting("appearance.showCalendar").value !== false
    property string deleteGroupName

    Connections {
        target: backend
        function onDataChanged() { root.archived = backend.archivedLists() }
    }

    // What follows the pointer while dragging a list or a group.
    Rectangle {
        id: dragPill
        property string label
        property bool grouping: false
        parent: Overlay.overlay
        visible: Drag.active
        z: 1000
        width: pillText.implicitWidth + 22
        height: 26
        radius: 13
        color: theme.accent
        opacity: 0.94
        Drag.keys: grouping ? ["precrime-group"] : ["precrime-list"]
        Drag.hotSpot.x: 10
        Drag.hotSpot.y: 13
        Text {
            id: pillText
            anchors.centerIn: parent
            text: dragPill.label
            color: theme.accentForeground
            font.pixelSize: backend.fontSize - 2
            font.weight: Font.DemiBold
        }
        function place(p) { if (!Drag.active) { x = p.x - 10; y = p.y - 13 } }
        function begin(text, group) { label = text; grouping = group; Drag.active = true }
        function end() {
            if (!Drag.active) return
            Drag.drop()
            Drag.active = false
            root.dragListId = 0
            root.dragGroupId = 0
        }
    }

    Dialog {

        width: Math.min(440, (parent ? parent.width : 480) - 40)
        id: deleteGroupDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: "Delete group “" + root.deleteGroupName + "”?"
        Label { text: "Its lists are kept and move out of the group." }
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: backend.deleteGroup(root.deleteGroupId)
    }

    ArchiveDialog { id: archiveDialog; lists: root.archived }

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
                color: theme.foreground
                font.pixelSize: backend.fontSize + 4
                font.weight: Font.Bold
                font.letterSpacing: 0.3
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

            // Below the last list: drop a list here to ungroup it, and open
            // the archive.
            footer: Item {
                width: lists.width
                height: root.dragListId ? 44 : (root.archived.length > 0 ? 44 : 12)
                DropArea {
                    id: tailDrop
                    anchors.fill: parent
                    keys: ["precrime-list"]
                    onDropped: if (root.dragListId) backend.moveList(root.dragListId, 0, 0)
                    Rectangle {
                        visible: tailDrop.containsDrag
                        x: 16; y: 2
                        width: parent.width - 32
                        height: 2
                        color: theme.accent
                    }
                }
                Rectangle {
                    visible: root.archived.length > 0
                    anchors.fill: parent
                    anchors.topMargin: 8
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    radius: 6
                    color: archiveMouse.containsMouse ? theme.mix(theme.sidebar, theme.foreground, 0.05) : "transparent"
                    Icon {
                        x: 14
                        anchors.verticalCenter: parent.verticalCenter
                        name: "done"
                        size: 15
                        color: theme.muted
                    }
                    Text {
                        x: 42
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Archived"
                        color: theme.muted
                        font.pixelSize: backend.fontSize
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.archived.length
                        color: theme.muted
                        font.pixelSize: backend.fontSize - 2
                    }
                    MouseArea {
                        id: archiveMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: archiveDialog.open()
                    }
                }
            }

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
            Layout.bottomMargin: 4
            // Shown or hidden from the footer; it also appears while tasks are
            // dragged, so there is always a date to drop them on.
            readonly property var proxy: ApplicationWindow.window ? ApplicationWindow.window.dragProxy : null
            visible: root.height > 560 && (root.calendarShown || (!!proxy && proxy.visible))
        }

        // Footer: new list or group, and settings.
        Item {
            Layout.fillWidth: true
            implicitHeight: 38
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: theme.divider }
            // Sync, when a git remote is set: spins while syncing, and the
            // tooltip says how it went.
            IconButton {
                id: syncButton
                visible: !!gitSync && gitSync.configured
                anchors.right: parent.right
                anchors.rightMargin: 44
                anchors.verticalCenter: parent.verticalCenter
                icon: "sync"
                tip: gitSync ? gitSync.status + " (Ctrl+S)" : ""
                color: gitSync && gitSync.status.startsWith("Synced") === false && !gitSync.busy ? theme.overdue : theme.muted
                onClicked: gitSync.syncNow()
                RotationAnimator on rotation { running: !!gitSync && gitSync.busy; from: 0; to: 360; duration: 900; loops: Animation.Infinite }
            }
            IconButton {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                icon: "settings"
                tip: "Settings (Ctrl+,)"
                onClicked: root.settingsRequested()
            }
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
                    MenuItem { text: "New Smart List…"; onTriggered: root.newSmartListRequested() }
                }
            }
            // Show or hide the mini calendar, and beside it its display style.
            Row {
                id: calendarControls
                x: 40
                anchors.verticalCenter: parent.verticalCenter
                spacing: 0
                IconButton {
                    icon: "calendar"
                    tip: root.calendarShown ? "Hide Calendar" : "Show Calendar"
                    checked: root.calendarShown
                    onClicked: backend.setSetting("appearance.showCalendar", !root.calendarShown)
                }
                IconButton {
                    id: styleButton
                    icon: "chevronDown"
                    size: 11
                    width: 18
                    tip: "Calendar Display Style"
                    checked: styleMenu.opened
                    onClicked: styleMenu.popup(styleButton, 0, -styleMenu.implicitHeight - 4)
                    Menu {
                        id: styleMenu
                        popupType: Popup.Window
                        readonly property string style: String(backend.setting("appearance.calendarStyle").value || "heat")
                        // Picking a style also shows a hidden calendar.
                        function pick(key) {
                            backend.setSetting("appearance.calendarStyle", key)
                            backend.setSetting("appearance.showCalendar", true)
                        }
                        MenuItem {
                            text: "Shade Weekends"
                            checkable: true
                            checked: backend.setting("appearance.calendarWeekends").value === true
                            onTriggered: backend.setSetting("appearance.calendarWeekends", checked)
                        }
                        MenuSeparator {}
                        MenuItem { text: "Display style:"; enabled: false }
                        MenuItem {
                            text: "None"
                            checkable: true
                            checked: styleMenu.style === "none"
                            onTriggered: styleMenu.pick("none")
                        }
                        MenuItem {
                            text: "Dots"
                            checkable: true
                            checked: styleMenu.style === "dots"
                            onTriggered: styleMenu.pick("dots")
                        }
                        MenuItem {
                            text: "Heat Map"
                            checkable: true
                            checked: styleMenu.style === "heat"
                            onTriggered: styleMenu.pick("heat")
                        }
                    }
                }
            }
        }
    }

    Component {
        id: gapRow
        Item {
            id: gap
            height: 10
            DropArea {
                id: gapDrop
                anchors.fill: parent
                keys: ["precrime-list"]
                onDropped: if (root.dragListId) backend.moveList(root.dragListId, 0, 0)
                Rectangle {
                    visible: gapDrop.containsDrag
                    x: 16; y: parent.height - 2
                    width: parent.width - 32
                    height: 2
                    color: theme.accent
                }
            }
        }
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
            readonly property bool isGroup: (header.row.groupId || 0) > 0

            // Lists dropped on a header go to the end of its group; groups
            // dropped on it land before it.
            DropArea {
                id: headerDrop
                anchors.fill: parent
                keys: ["precrime-list", "precrime-group"]
                enabled: header.isGroup
                onDropped: {
                    if (root.dragGroupId && root.dragGroupId !== header.row.groupId)
                        backend.moveGroup(root.dragGroupId, header.row.groupId)
                    else if (root.dragListId)
                        backend.moveList(root.dragListId, 0, header.row.groupId)
                }
                Rectangle {
                    visible: headerDrop.containsDrag && root.dragGroupId > 0 && root.dragGroupId !== header.row.groupId
                    x: 12; y: 0
                    width: parent.width - 24
                    height: 2
                    color: theme.accent
                }
                Rectangle {
                    visible: headerDrop.containsDrag && root.dragListId > 0
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    anchors.topMargin: 4
                    radius: 6
                    color: "transparent"
                    border.width: 2
                    border.color: theme.accent
                }
            }

            MouseArea {
                id: groupMouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                drag.target: header.isGroup ? dragPill : null
                drag.threshold: 10
                drag.onActiveChanged: {
                    if (drag.active) {
                        root.dragGroupId = header.row.groupId
                        dragPill.begin(header.row.title || "Group", true)
                    } else {
                        dragPill.end()
                    }
                }
                onPressed: (m) => dragPill.place(mapToItem(null, m.x, m.y))
                onClicked: (mouse) => {
                    if (mouse.button === Qt.RightButton) { if (header.isGroup) groupMenu.popup() }
                    else if (header.isGroup) backend.toggleGroupCollapsed(header.row.groupId)
                }
                onDoubleClicked: if (header.isGroup) root.renamingGroupId = header.row.groupId
            }
            Menu {
                id: groupMenu
                MenuItem { text: "Rename…"; onTriggered: root.renamingGroupId = header.row.groupId }
                MenuItem {
                    text: "New List in Group"
                    onTriggered: {
                        const id = backend.newList()
                        backend.moveList(id, 0, header.row.groupId)
                        root.renamingListId = id
                    }
                }
                MenuItem { text: header.row.collapsed ? "Expand" : "Collapse"; onTriggered: backend.toggleGroupCollapsed(header.row.groupId) }
                MenuSeparator {}
                MenuItem {
                    text: "Delete Group"
                    onTriggered: { root.deleteGroupId = header.row.groupId; root.deleteGroupName = header.row.title || ""; deleteGroupDialog.open() }
                }
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
                // the reference app's bands are wide pills that stack into one column.
                x: 24
                width: 20
                y: item.runStart ? 3 : 0
                height: parent.height - (item.runStart ? 3 : 0) - (item.runEnd ? 3 : 0)
                radius: 8
                color: item.band
                // Square off the joins so stacked pills read as one column.
                Rectangle { visible: !item.runStart; width: parent.width; height: parent.radius; color: parent.color }
                Rectangle { visible: !item.runEnd; width: parent.width; height: parent.radius; color: parent.color; anchors.bottom: parent.bottom }
                Icon {
                    visible: !!item.row.excluded && item.isList
                    anchors.centerIn: parent
                    name: "eyeOff"
                    size: 11
                    stroke: 2
                    color: theme.background
                }
            }

            Icon {
                visible: item.row.expandable === true
                x: 9
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
                // A calendar page showing today's date, as the reference app draws it.
                Rectangle {
                    anchors.centerIn: parent
                    visible: item.row.icon === "today"
                    width: 18
                    height: 18
                    radius: 4
                    color: theme.muted
                    Rectangle { width: parent.width; height: 4; radius: 2; color: theme.mix(theme.muted, theme.foreground, 0.35) }
                    Text {
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: 1.5
                        text: new Date().getDate()
                        color: theme.sidebar
                        font.pixelSize: 10
                        font.bold: true
                    }
                }
            }

            Icon {
                visible: item.row.kind === "project"
                x: 54
                anchors.verticalCenter: parent.verticalCenter
                name: item.row.icon || "project"
                size: 14
                color: theme.muted
            }

            // Due-today (white) or overdue (red) glow: on a list's band, or at
            // the far edge of Today.
            Rectangle {
                visible: !!item.row.dot
                x: item.row.kind === "focus" ? 3 : 21
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
                x: item.row.kind === "focus" || item.row.kind === "smart" ? 52 : item.row.kind === "project" ? 74 : 54
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
                RollingNumber {
                    visible: (item.row.overdue || 0) > 0
                    value: item.row.overdue || 0
                    color: theme.overdue
                    pixelSize: backend.fontSize - 2
                }
                Text {
                    visible: (item.row.overdue || 0) > 0 && (item.row.undone || 0) > 0
                    text: "|"
                    color: theme.faint
                    font.pixelSize: backend.fontSize - 2
                }
                RollingNumber {
                    visible: (item.row.undone || 0) > 0
                    value: item.row.undone || 0
                    color: theme.muted
                    pixelSize: backend.fontSize - 2
                }
            }

            DropArea {
                id: drop
                anchors.fill: parent
                keys: ["precrime-tasks"]
                enabled: !!item.row.sourceKey && backend.dropHint(item.row.sourceKey) !== ""
                readonly property var proxy: ApplicationWindow.window ? ApplicationWindow.window.dragProxy : null
                onEntered: if (proxy) proxy.hint = backend.dropHint(item.row.sourceKey)
                onExited: if (proxy) proxy.hint = ""
                onDropped: backend.dropOnSource(item.row.sourceKey)
                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    radius: 6
                    color: "transparent"
                    border.width: 2
                    border.color: theme.accent
                    visible: drop.containsDrag
                }
            }

            // Lists dropped on a list row land before it.
            DropArea {
                id: listDrop
                anchors.fill: parent
                keys: ["precrime-list"]
                enabled: item.isList
                onDropped: if (root.dragListId && root.dragListId !== item.row.listId)
                               backend.moveList(root.dragListId, item.row.listId, item.row.groupId || 0)
                Rectangle {
                    visible: listDrop.containsDrag && root.dragListId !== item.row.listId
                    x: 12; y: 0
                    width: parent.width - 24
                    height: 2
                    color: theme.accent
                }
            }

            MouseArea {
                id: mouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                drag.target: item.isList ? dragPill : null
                drag.threshold: 10
                drag.onActiveChanged: {
                    if (drag.active) {
                        root.dragListId = item.row.listId
                        dragPill.begin(item.row.title || "List", false)
                    } else {
                        dragPill.end()
                    }
                }
                onPressed: (m) => dragPill.place(mapToItem(null, m.x, m.y))
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
                MenuItem { text: "Edit Search…"; onTriggered: { backend.selectSource(item.row.sourceKey); root.editSmartListRequested() } }
                MenuItem { text: "Rename…"; onTriggered: root.renamingListId = item.row.listId }
                MenuItem { text: "Delete Smart List"; onTriggered: backend.deleteList(item.row.listId) }
            }
        }
    }
}
