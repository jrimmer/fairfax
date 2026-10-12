import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The centre panel: toolbar, the list title with its sort bar, and the
// sectioned task list. the reference app's bare-key commands (t, y, d, ], [, 0–3 …) act
// here, only while the list itself has focus, so typing elsewhere is safe.
Item {
    id: root

    property bool sidebarVisible: true
    property bool utilityVisible: true
    signal toggleSidebar()
    signal toggleUtility()
    signal command(string kind)
    signal quickLook()
    signal help()

    function focusList() { list.forceActiveFocus() }
    function focusSearch() { search.expanded = true; search.field.forceActiveFocus() }
    function toggleProjectView() {
        if (backend.sourceKey.startsWith("project:")) {
            backend.selectSource("list:" + backend.taskInfo(Number(backend.sourceKey.split(":")[1])).listId)
            return
        }
        const info = backend.taskInfo(backend.currentTaskId)
        if (!info.id) return
        const target = info.type === 1 ? info.id : 0
        if (target) backend.selectSource("project:" + target)
    }

    readonly property real contentMargin: Math.max(28, (width - 980) / 2)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Toolbar.
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            spacing: 2

            IconButton { visible: !root.sidebarVisible; icon: "sidebar"; tip: "Show Lists (Ctrl+Alt+←)"; onClicked: root.toggleSidebar() }
            IconButton {
                icon: "plus"
                size: 18
                tip: "New Task (Ctrl+N)"
                onClicked: backend.newTask(0)
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: newMenu.popup()
                }
                Menu {
                    id: newMenu
                    MenuItem { text: "New Task"; onTriggered: backend.newTask(0) }
                    MenuItem { text: "New Project"; onTriggered: backend.newTask(1) }
                    MenuItem { text: "New Checklist"; onTriggered: backend.newTask(2) }
                    MenuItem { text: "New Sub-Task"; enabled: backend.currentTaskId > 0; onTriggered: backend.newTask(0, true) }
                }
            }
            Item { Layout.fillWidth: true }
            SearchBox { id: search; onFinished: root.focusList() }
            IconButton { icon: "panel"; tip: root.utilityVisible ? "Hide Tags (Ctrl+Alt+→)" : "Show Tags (Ctrl+Alt+→)"; onClicked: root.toggleUtility() }
        }

        // Title and sort bar.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: root.contentMargin
            Layout.rightMargin: root.contentMargin
            Layout.topMargin: 4
            Layout.bottomMargin: 10
            spacing: 8

            // In a project the list name becomes an arrow-shaped badge.
            ListBadge {
                visible: backend.sourceKey.startsWith("project:")
                text: backend.sourceSubtitle
                color: theme.listColor(backend.sourceColor)
                onClicked: root.toggleProjectView()
            }
            Text {
                text: backend.sourceTitle
                color: backend.sourceKey.startsWith("project:") ? theme.muted : theme.foreground
                font.pixelSize: backend.fontSize + 9
                font.weight: Font.Bold
                elide: Text.ElideRight
                Layout.maximumWidth: root.width * 0.45
                MouseArea { anchors.fill: parent; onClicked: root.command("jump") }
            }
            Text {
                visible: backend.sourceKey === "today"
                text: backend.sourceSubtitle
                color: theme.muted
                font.pixelSize: backend.fontSize + 5
                Layout.alignment: Qt.AlignBaseline
            }
            Item { Layout.fillWidth: true }
            Text {
                visible: backend.selectionSummary !== "" && root.width > 640
                text: backend.selectionSummary
                color: theme.muted
                font.pixelSize: backend.fontSize - 1
            }
            IconButton {
                visible: backend.hasHidden
                icon: "clock"
                checked: backend.showScheduled || backend.showPaused
                tip: backend.showScheduled ? "Hide Scheduled (Ctrl+K)" : "Show Scheduled (Ctrl+K)"
                onClicked: backend.showScheduled = !backend.showScheduled
            }
            Rectangle { visible: backend.hasHidden; implicitWidth: 1; implicitHeight: 16; color: theme.divider }
            IconButton {
                icon: backend.sortAscending ? "arrowDown" : "arrowUp"
                size: 15
                tip: "Reverse Sort"
                onClicked: backend.toggleSortDirection()
            }
            SortButton {}
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
            model: backend.tasks
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: false
            cacheBuffer: 800
            bottomMargin: 80
            keyNavigationEnabled: false
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            add: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 } }
            displaced: Transition { NumberAnimation { property: "y"; duration: 140; easing.type: Easing.OutCubic } }

            delegate: Loader {
                required property var model
                required property int index
                width: ListView.view.width
                sourceComponent: model.kind === "section" ? sectionComponent : model.editing ? editorComponent : rowComponent
            }

            Component {
                id: sectionComponent
                SectionHeader { row: parent.model; margin: root.contentMargin }
            }
            Component {
                id: rowComponent
                TaskRow { row: parent.model; margin: root.contentMargin }
            }
            Component {
                id: editorComponent
                EditorCard { margin: root.contentMargin; onDone: root.focusList() }
            }

            Connections {
                target: backend
                function onRevealTask(id) {
                    const i = backend.tasks.indexOfKey("t:" + id)
                    if (i >= 0) list.positionViewAtIndex(i, ListView.Contain)
                }
                function onViewChanged() { if (!list.activeFocus && !backend.editingTaskId) list.forceActiveFocus() }
            }

            // Clicking empty space clears the selection, as in the reference app.
            MouseArea {
                z: -1
                anchors.fill: parent
                onClicked: { backend.clearSelection(); list.forceActiveFocus() }
                onDoubleClicked: backend.newTask(0)
            }

            Text {
                anchors.centerIn: parent
                visible: backend.tasks.count === 0
                text: backend.search ? "No tasks match “" + backend.search + "”"
                    : backend.sourceKey === "today" ? "Nothing due today"
                    : backend.sourceKey === "done" ? "Nothing completed yet" : "No tasks"
                color: theme.faint
                font.pixelSize: backend.fontSize + 2
            }

            Keys.onPressed: (event) => root.handleKey(event)
        }
    }

    function handleKey(e) {
        const shift = e.modifiers & Qt.ShiftModifier
        const ctrl = e.modifiers & Qt.ControlModifier
        const alt = e.modifiers & Qt.AltModifier
        const k = e.key
        const done = () => { e.accepted = true }

        if (k === Qt.Key_Down) { backend.moveCursor(1, shift); return done() }
        if (k === Qt.Key_Up) { backend.moveCursor(-1, shift); return done() }
        if (k === Qt.Key_Return || k === Qt.Key_Enter) { backend.beginEdit(); return done() }
        if (k === Qt.Key_Escape) { backend.clearSelection(); return done() }
        if (k === Qt.Key_Space) { root.quickLook(); return done() }
        if (k === Qt.Key_Delete || (k === Qt.Key_Backspace && !ctrl)) { backend.deleteSelection(); return done() }
        if (ctrl && k === Qt.Key_A) { backend.selectAll(); return done() }
        if (ctrl && k === Qt.Key_T) { backend.setStartInDays(0); return done() }
        if (ctrl && k === Qt.Key_S) { backend.setStarred(0); return done() }
        if (ctrl) return

        switch (k) {
        case Qt.Key_T: backend.setDueInDays(0); return done()
        case Qt.Key_Y: backend.setDueInDays(1); return done()
        case Qt.Key_S: backend.setStarred(1); return done()
        case Qt.Key_0: case Qt.Key_1: case Qt.Key_2: case Qt.Key_3:
            backend.setPriority(k - Qt.Key_0); return done()
        case Qt.Key_Equal: case Qt.Key_Plus: backend.bumpPriority(1); return done()
        case Qt.Key_Minus: backend.bumpPriority(-1); return done()
        case Qt.Key_BracketRight: backend.nudgeDue(1); return done()
        case Qt.Key_BraceRight: backend.nudgeDue(7); return done()
        case Qt.Key_BracketLeft: if (alt) backend.clearDue(); else backend.nudgeDue(-1); return done()
        case Qt.Key_BraceLeft: backend.nudgeDue(-7); return done()
        case Qt.Key_Period: backend.nudgeStart(1); return done()
        case Qt.Key_Greater: backend.nudgeStart(7); return done()
        case Qt.Key_Comma: if (alt) backend.clearStart(); else backend.nudgeStart(-1); return done()
        case Qt.Key_Less: backend.nudgeStart(-7); return done()
        case Qt.Key_C: backend.toggleCollapsed(); return done()
        case Qt.Key_P: backend.performAction(); return done()
        case Qt.Key_U: backend.openLinks(); return done()
        case Qt.Key_D: root.command("defer"); return done()
        case Qt.Key_K: root.command("schedule"); return done()
        case Qt.Key_E: root.command("time"); return done()
        case Qt.Key_X: root.command("duration"); return done()
        case Qt.Key_L: if (alt) backend.untagSelection(); else root.command("tag"); return done()
        case Qt.Key_M: root.command("move"); return done()
        case Qt.Key_J: root.command("jump"); return done()
        case Qt.Key_F: root.command("find"); return done()
        case Qt.Key_Slash: root.command("tagsearch"); return done()
        case Qt.Key_Question: root.help(); return done()
        case Qt.Key_Q: Qt.quit(); return done()
        }
    }
}
