import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The centre panel: toolbar, the list title with its sort bar, and the
// sectioned task list. The bare-key commands (t, y, d, ], [, 0–3 …) act
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
    signal tagSearch()
    signal proposalsRequested()
    property bool altHeld: false

    // The sequence -> command id map the bare-key handler looks a press up in,
    // rebuilt whenever a binding changes. Fixed keys are handled below.
    property var shortcutMap: shortcutsOf(backend.shortcutMap, backend.shortcutCommands())
    function shortcutsOf(snapshot, commands) {
        const map = ({})
        for (const c of commands) {
            if (c.fixed) continue
            for (const s of c.current) map[s] = c.id
        }
        return map
    }

    function focusList() { list.forceActiveFocus() }

    // "6h 30m" from a minute count, for the My Day capacity line.
    function formatMinutes(total) {
        const h = Math.floor(total / 60), m = total % 60
        if (h === 0) return m + "m"
        if (m === 0) return h + "h"
        return h + "h " + m + "m"
    }
    function openSearchBuilder() {
        if (backend.sourceIsSmart) editSmartQuery()
        else searchBuilder.openAt(search)
    }
    // The plain-English ask: only while the agent is on, so the entry and the
    // shortcut are inert otherwise.
    function openAskSheet() {
        if (!backend.agentEnabled) return
        askSheet.openSheet()
    }
    function saveCompiledAsSmartList() {
        searchBuilder.openAt(search)
    }
    function editSmartQuery() { searchBuilder.openForList(Number(backend.sourceKey.split(":")[1])) }
    function focusSearch() { search.opened = true; search.field.forceActiveFocus() }
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

    readonly property real contentMargin: Math.max(28, (width - backend.contentWidth) / 2)

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

            IconButton { objectName: "showSidebar"; visible: !root.sidebarVisible; icon: "sidebar"; tip: "Show Lists (Ctrl+Alt+←)"; onClicked: root.toggleSidebar() }
            IconButton {
                objectName: "newTaskButton"
                icon: "plus"
                size: 18
                tip: "New Task (Ctrl+N)"
                onClicked: backend.newTask(0)
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: newMenu.popup()
                }
                ScrollingMenu {
                    id: newMenu
                    // A closing menu hands the keyboard back to what had it, which would take it
                    // from the title of the task the menu just made.
                    onClosed: if (backend.editingTaskId) Qt.callLater(() => backend.editField("title"))
                    MenuItem { text: "New Task"; onTriggered: backend.newTask(0) }
                    MenuItem { text: "New Project"; onTriggered: backend.newTask(1) }
                    MenuItem { text: "New Checklist"; onTriggered: backend.newTask(2) }
                    MenuItem { text: "New Sub-Task"; enabled: backend.currentTaskId > 0; onTriggered: backend.newTask(0, true) }
                    MenuItem {
                        text: "New Section"
                        enabled: backend.sourceKey.startsWith("list:") && !backend.sourceIsSmart
                        onTriggered: backend.newSection(Number(backend.sourceKey.split(":")[1]), "")
                    }
                }
            }
            Item { Layout.fillWidth: true }
            SearchBox { id: search; onFinished: root.focusList() }
            // The search menu: ready-made searches, and the full options.
            IconButton {
                id: presetsButton
                objectName: "searchPresets"
                icon: "chevronDown"
                size: 10
                tip: "Search Presets"
                onClicked: presets.popup(0, presetsButton.height)
                ScrollingMenu {
                    id: presets
                    Repeater {
                        model: [["Overdue", "type: overdue"], ["Due Today", "type: duetoday"], ["Due This Week", "type: duethisweek"],
                                ["In Progress", "type: inprogress"], ["High Priority", "type: hiprio"], ["Starred", "type: starred"],
                                ["Without Due Date", "type: nodue"], ["Repeating", "type: repeat"], ["With Alerts", "type: alarms"],
                                ["Empty Projects and Checklists", "type: emptyproj OR emptychck"], ["Recently Completed", "completed: 7d"],
                                ["Completed Only", "type: completed"], ["Abandoned", "type: abandoned"]]
                        MenuItem {
                            required property var modelData
                            text: modelData[0]
                            onTriggered: { backend.search = modelData[1]; root.focusList() }
                        }
                    }
                    MenuSeparator {}
                    MenuItem {
                        objectName: "searchAskEnglish"
                        visible: backend.agentEnabled
                        height: visible ? implicitHeight : 0
                        text: "Ask in plain English…\t" + backend.shortcutText("view.askEnglish")
                        onTriggered: root.openAskSheet()
                    }
                    MenuItem { text: "Search Options…\tCtrl+Shift+Alt+F"; onTriggered: root.openSearchBuilder() }
                    MenuItem { text: backend.searchAllLists ? "Search This List Only" : "Search All Lists"; onTriggered: backend.toggleSearchScope() }
                }
            }
            IconButton { objectName: "toggleUtility"; icon: "panel"; tip: root.utilityVisible ? "Hide Tags (Ctrl+Alt+→)" : "Show Tags (Ctrl+Alt+→)"; onClicked: root.toggleUtility() }
            // Inbox triage: only while the agent is on and the Inbox is open.
            IconButton {
                objectName: "triageInboxButton"
                visible: backend.agentEnabled && backend.sourceIsInbox
                icon: "inbox"
                tip: "Triage Inbox"
                onClicked: backend.triageInbox()
            }
            // The proposal engine's review sheet, only while the agent is on
            // and something is waiting.
            IconButton {
                objectName: "suggestionsButton"
                visible: backend.agentEnabled
                icon: "checklist"
                tip: backend.proposalCount > 0 ? backend.proposalCount + " Suggested Change(s)" : "Suggested Changes"
                checked: backend.proposalCount > 0
                onClicked: root.proposalsRequested()
            }
        }

        // Title and sort bar.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: root.contentMargin
            Layout.rightMargin: root.contentMargin
            Layout.topMargin: 4
            Layout.bottomMargin: backend.sourceIsMyDay ? 2 : 10
            spacing: 8

            // In a project the list name becomes an arrow-shaped badge.
            ListBadge {
                objectName: "projectBadge"
                visible: backend.sourceKey.startsWith("project:")
                text: backend.sourceSubtitle
                color: theme.listColor(backend.sourceColor)
                onClicked: root.toggleProjectView()
            }
            Text {
                objectName: "paneTitle"
                text: backend.sourceTitle
                color: backend.sourceKey.startsWith("project:") ? theme.muted : theme.foreground
                font.pixelSize: backend.fontSize + 9
                font.weight: Font.Bold
                elide: Text.ElideRight
                Layout.alignment: Qt.AlignBaseline
                Layout.maximumWidth: root.width * 0.45
                Accessible.role: Accessible.Button
                Accessible.name: backend.sourceTitle
                Accessible.onPressAction: root.command("jump")
                MouseArea { anchors.fill: parent; onClicked: root.command("jump") }
            }
            Text {
                objectName: "paneDate"
                visible: backend.sourceKey === "today" || backend.sourceIsMyDay
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
                objectName: "hideScheduledButton"
                visible: backend.hasNotStarted
                icon: "clock"
                checked: backend.hideScheduled
                tip: backend.hideScheduled ? "Show not-started tasks (Ctrl+K)" : "Hide not-started tasks (Ctrl+K)"
                onClicked: backend.hideScheduled = !backend.hideScheduled
            }
            Rectangle { visible: backend.hasNotStarted; implicitWidth: 1; implicitHeight: 16; color: theme.divider }
            FocusButton {}
            IconButton {
                objectName: "sortReverse"
                visible: !backend.sourceIsMyDay
                icon: backend.sortAscending ? "arrowDown" : "arrowUp"
                size: 15
                tip: "Reverse Sort"
                onClicked: backend.toggleSortDirection()
            }
            // My Day has its own section order, so it offers no sort control.
            Loader { active: !backend.sourceIsMyDay; sourceComponent: SortButton {} }
        }

        // My Day's numbers sit on their own line under the title, so the
        // title row keeps room for the date and the button at any width.
        RowLayout {
            objectName: "myDayLine"
            visible: backend.sourceIsMyDay
            Layout.fillWidth: true
            Layout.leftMargin: root.contentMargin
            Layout.rightMargin: root.contentMargin
            Layout.topMargin: -6
            Layout.bottomMargin: 10
            spacing: 8

            // My Day's capacity line: the day's tasks against the work day left.
            RowLayout {
                id: myDayCapacity
                objectName: "myDayCapacity"
                visible: backend.sourceIsMyDay && !backend.myDayShutdown && (planned > 0 || without > 0)
                spacing: 6
                Layout.alignment: Qt.AlignBaseline
                // A RowLayout has no baseline of its own; report the first
                // text's so the row's texts share one baseline.
                baselineOffset: plannedLabel.y + plannedLabel.baselineOffset
                readonly property int planned: Number(backend.myDayCapacityMap.plannedMinutes || 0)
                readonly property int free: Number(backend.myDayCapacityMap.freeMinutes || 0)
                readonly property bool dayOver: Number(backend.myDayCapacityMap.windowMinutes || 0) <= 0
                readonly property int without: Number(backend.myDayCapacityMap.withoutDuration || 0)
                readonly property bool over: planned > free
                readonly property bool amber: !over && free > 0 && planned > free * 0.85
                Text {
                    id: plannedLabel
                    text: root.formatMinutes(parent.planned) + " of tasks"
                    color: parent.over ? theme.overdue : parent.amber ? theme.priorityMedium : theme.muted
                    font.pixelSize: backend.fontSize + 1
                }
                Text {
                    text: "·"
                    color: theme.faint
                    font.pixelSize: backend.fontSize + 1
                }
                Text {
                    text: parent.dayOver ? "work day over" : root.formatMinutes(parent.free) + " left in work day"
                    color: parent.dayOver ? theme.muted : parent.over ? theme.overdue : parent.amber ? theme.priorityMedium : theme.muted
                    font.pixelSize: backend.fontSize + 1
                }
                Text {
                    visible: parent.without > 0
                    text: "· " + parent.without + " without a duration"
                    color: theme.faint
                    font.pixelSize: backend.fontSize - 1
                }
            }
            // The shutdown review's summary line: tasks done today, and the minutes
            // done out of the minutes planned.
            RowLayout {
                objectName: "myDaySummary"
                visible: backend.sourceIsMyDay && backend.myDayShutdown
                spacing: 6
                Layout.alignment: Qt.AlignBaseline
                // As with the capacity row: report the first text's baseline.
                baselineOffset: completedLabel.y + completedLabel.baselineOffset
                readonly property int completed: Number(backend.myDaySummaryMap.completedCount || 0)
                readonly property int planned: Number(backend.myDaySummaryMap.plannedMinutes || 0)
                readonly property int done: Number(backend.myDaySummaryMap.doneMinutes || 0)
                Text {
                    id: completedLabel
                    text: "Done today: " + parent.completed + (parent.completed === 1 ? " task" : " tasks")
                    color: theme.muted
                    font.pixelSize: backend.fontSize + 1
                }
                Text { text: "·"; color: theme.faint; font.pixelSize: backend.fontSize + 1 }
                Text {
                    text: root.formatMinutes(parent.done) + " of " + root.formatMinutes(parent.planned)
                    color: theme.muted
                    font.pixelSize: backend.fontSize + 1
                }
            }
            // The way into the end-of-day review (it also opens by itself once the
            // work day is over) and back out of it ("Back to plan", or Return).
            // The separator shows only after numbers; with nothing planned the
            // link stands alone.
            Text {
                text: "·"
                visible: backend.sourceIsMyDay && (backend.myDayShutdown || myDayCapacity.visible)
                color: theme.faint
                font.pixelSize: backend.fontSize + 1
                Layout.alignment: Qt.AlignBaseline
            }
            MutedLink {
                objectName: "myDayReview"
                visible: backend.sourceIsMyDay && !backend.myDayShutdown
                text: "Wrap up day"
                font.pixelSize: backend.fontSize + 1
                Layout.alignment: Qt.AlignBaseline
                onClicked: backend.myDayReview()
            }
            MutedLink {
                objectName: "myDayFinish"
                visible: backend.sourceIsMyDay && backend.myDayShutdown
                text: "Back to plan"
                font.pixelSize: backend.fontSize + 1
                Layout.alignment: Qt.AlignBaseline
                onClicked: backend.myDayFinishReview()
            }
            Item { Layout.fillWidth: true }
        }

        // A Smart List shows its saved search, editable in place; a typed
        // search offers to become one.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: root.contentMargin
            Layout.rightMargin: root.contentMargin
            Layout.bottomMargin: 6
            visible: backend.sourceIsSmart || backend.search !== ""
            spacing: 8
            Icon { name: "search"; size: 14; color: theme.muted }
            TextInput {
                id: smartQuery
                objectName: "smartQueryField"
                Layout.fillWidth: true
                visible: backend.sourceIsSmart
                text: backend.smartQuery
                color: theme.foreground
                selectionColor: theme.selection
                font.family: theme.monoFont
                font.pixelSize: backend.fontSize - 2
                clip: true
                onEditingFinished: backend.setSmartQuery(Number(backend.sourceKey.split(":")[1]), text)
                Keys.onEscapePressed: { text = backend.smartQuery; root.focusList() }
            }
            Text {
                Layout.fillWidth: true
                visible: !backend.sourceIsSmart
                text: backend.search
                color: theme.muted
                font.family: theme.monoFont
                font.pixelSize: backend.fontSize - 2
                elide: Text.ElideRight
            }
            Text {
                visible: backend.smartRangeText !== ""
                text: backend.smartRangeText
                color: theme.today
                font.pixelSize: backend.fontSize - 2
            }
            CheckBox {
                objectName: "searchSoundex"
                text: "Sounds like"
                checked: backend.soundex
                font.pixelSize: backend.fontSize - 2
                onToggled: backend.soundex = checked
                padding: 0
            }
            Button {
                objectName: "saveSearchAsSmart"
                visible: backend.search !== "" && !backend.sourceIsSmart
                text: "Save as Smart List"
                flat: true
                font.pixelSize: backend.fontSize - 2
                onClicked: backend.saveSmartList("", backend.search)
            }
        }

        // Today's calendar events, above Today (or every list, by setting).
        // While in My Day mode this compact bar is only for a narrow window;
        // the wide window shows the expanded timeline in the utility panel.
        DayBar {
            objectName: "dayBar"
            Layout.fillWidth: true
            Layout.leftMargin: root.contentMargin
            Layout.rightMargin: root.contentMargin
            Layout.bottomMargin: 6
            visible: !!calendar && calendar.sources.length > 0 && backend.search === ""
                     && (backend.sourceKey === "today"
                         || (backend.sourceIsMyDay && (root.Window.width < 900 || !root.utilityVisible))
                         || ((backend.settingsRevision, backend.settingFlag("calendar.inLists", false)) && !backend.sourceIsSmart && !backend.sourceKey.startsWith("project:") && !backend.sourceIsMyDay))
        }

        ListView {
            id: list
            objectName: "taskList"
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
                sourceComponent: model.kind === "section" ? sectionComponent
                               : model.kind === "myDayMore" ? moreComponent
                               : model.editing ? editorComponent : rowComponent
            }

            Component {
                id: moreComponent
                Item {
                    objectName: "myDayMore:" + (model.row && model.row.sectionKey ? model.row.sectionKey : "")
                    height: 30
                    Accessible.role: Accessible.Button
                    Accessible.name: model.row && model.row.title ? model.row.title : "More"
                    Accessible.onPressAction: backend.myDayExpand(model.row.sectionKey)
                    Text {
                        x: root.contentMargin + 32
                        anchors.verticalCenter: parent.verticalCenter
                        text: model.row && model.row.title ? model.row.title : ""
                        color: theme.accent
                        font.pixelSize: backend.fontSize - 1
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: backend.myDayExpand(model.row.sectionKey)
                    }
                }
            }

            Component {
                id: sectionComponent
                SectionHeader { row: parent.model ? parent.model.row : null; margin: root.contentMargin }
            }
            Component {
                id: rowComponent
                TaskRow { row: parent.model ? parent.model.row : null; margin: root.contentMargin; revealNames: root.altHeld }
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
                // A refilter brings focus back to the list, except from a text
                // field such as search, where the next keystroke belongs.
                function onViewChanged() {
                    const f = root.Window.activeFocusItem
                    if (f && "cursorPosition" in f) return
                    if (!list.activeFocus && !backend.editingTaskId) list.forceActiveFocus()
                }
            }

            // Clicking empty space clears the selection.
            MouseArea {
                z: -1
                anchors.fill: parent
                onClicked: { backend.clearSelection(); list.forceActiveFocus() }
                onDoubleClicked: backend.newTask(0)
            }

            Text {
                objectName: "paneEmptyState"
                anchors.centerIn: parent
                visible: backend.tasks.count === 0
                text: backend.search ? "No tasks match “" + backend.search + "”"
                    : backend.sourceIsMyDay ? "Nothing planned for today"
                    : backend.sourceKey === "today" ? "Nothing due today"
                    : backend.sourceKey === "done" ? "Nothing completed yet" : "No tasks"
                color: theme.faint
                font.pixelSize: backend.fontSize + 2
            }

            Keys.onPressed: (event) => root.handleKey(event)
            Keys.onReleased: (event) => { if (event.key === Qt.Key_Alt) root.altHeld = false }
            onActiveFocusChanged: if (!activeFocus) root.altHeld = false

            // Files dropped below the tasks make a new task named after them.
            DropArea {
                z: -2
                anchors.fill: parent
                keys: ["text/uri-list"]
                onDropped: (drop) => { if (drop.hasUrls) { backend.dropFiles(0, drop.urls); drop.accept() } }
            }

            // Section bars: one centred dash per section, longer for
            // fuller sections, with the one in view drawn darker. Click or drag
            // along the rail to jump. Only shown when the list scrolls, so it
            // isn't mistaken for a row's priority mark.
            Column {
                id: rail
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                width: 14
                spacing: 5
                visible: sections.length > 1 && list.contentHeight > list.height
                // The last section starting at or above the top of the view; above
                // the first row (the list's header) that's the first section.
                readonly property int current: {
                    const top = list.indexAt(list.width / 2, list.contentY + 1)
                    let c = 0
                    for (let i = 0; i < sections.length; ++i)
                        if (top >= 0 && sections[i].index <= top) c = i
                    return c
                }
                readonly property var sections: {
                    const out = []
                    for (let i = 0; i < backend.tasks.count; ++i) {
                        const r = backend.tasks.get(i)
                        if (r.kind === "section") out.push({ index: i, title: r.title, count: r.count || 0, tone: r.tone })
                    }
                    return out
                }
                readonly property int most: Math.max(1, ...sections.map(s => s.count))
                Repeater {
                    model: rail.sections
                    Rectangle {
                        required property var modelData
                        required property int index
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 4 + 10 * modelData.count / rail.most
                        height: 3
                        radius: 1.5
                        color: index === rail.current ? theme.foreground : theme.faint
                    }
                }
            }
            // One area over the whole rail, so a press scrubs along it and the
            // list follows (bars are 3 px high with 5 px between them).
            MouseArea {
                anchors.fill: rail
                anchors.margins: -6
                visible: rail.visible
                hoverEnabled: true
                property int hovered: -1
                function indexAt(y) { return Math.max(0, Math.min(rail.sections.length - 1, Math.floor((y - 6) / 8))) }
                function jump(y) {
                    hovered = indexAt(y)
                    list.positionViewAtIndex(rail.sections[hovered].index, ListView.Beginning)
                }
                onPressed: (m) => jump(m.y)
                onPositionChanged: (m) => { if (pressed) jump(m.y); else hovered = indexAt(m.y) }
                ToolTip.visible: containsMouse && hovered >= 0 && hovered < rail.sections.length
                ToolTip.text: hovered >= 0 && hovered < rail.sections.length ? rail.sections[hovered].title + " · " + rail.sections[hovered].count : ""
            }
        }
    }

    SearchBuilder { id: searchBuilder; objectName: "searchBuilder"; onClosed: Qt.callLater(root.focusList) }
    AskSheet { id: askSheet; objectName: "askSheet"; onSaveRequested: root.saveCompiledAsSmartList() }

    Confetti { id: confetti; objectName: "confetti"; anchors.fill: parent; enabled: (backend.settingsRevision, backend.settingFlag("general.celebrate", true)) }
    Connections {
        target: backend
        function onCelebrate() { if (confetti.enabled) confetti.burst(root.width / 2, root.height / 3) }
    }

    function handleKey(e) {
        const shift = e.modifiers & Qt.ShiftModifier
        const ctrl = e.modifiers & Qt.ControlModifier
        const k = e.key
        const done = () => { e.accepted = true }

        // The fixed keys: navigation, editing, clipboard and the Alt reveal.
        if (k === Qt.Key_Down) { backend.moveCursor(1, shift); return done() }
        if (k === Qt.Key_Up) { backend.moveCursor(-1, shift); return done() }
        if (k === Qt.Key_Return || k === Qt.Key_Enter) {
            // The shutdown review finishes on Return, before the editor's
            // Return; the Back to plan button does the same when it has the focus.
            if (backend.sourceIsMyDay && backend.myDayShutdown) { backend.myDayFinishReview(); return done() }
            // With the setting on, Return creates a new task and Ctrl+Return edits.
            const returnCreates = backend.settingFlag("tasks.returnCreates", false)
            if (returnCreates !== !!ctrl) backend.newTask(0); else backend.beginEdit()
            return done()
        }
        if (k === Qt.Key_Escape) { backend.clearSelection(); return done() }
        if (k === Qt.Key_Alt) { root.altHeld = true; return }
        if (ctrl && k === Qt.Key_A) { backend.selectAll(); return done() }
        if (ctrl && k === Qt.Key_C) { backend.copySelection(); return done() }
        if (ctrl && k === Qt.Key_X) { backend.cutSelection(); return done() }
        if (ctrl && k === Qt.Key_V) { backend.pasteTasks(); return done() }

        // Everything else is a remappable command: look the key up in the
        // registry. A Ctrl+Shift+[ sometimes arrives as Ctrl+Shift+{ (the
        // shifted bracket); both spell the one binding.
        let seq = backend.shortcutFor(e.key, e.modifiers)
        if (seq === "Ctrl+Shift+{") seq = "Ctrl+Shift+["
        const id = root.shortcutMap[seq]
        if (!id) {
            if (ctrl) return
            return
        }
        if (root.runCommand(id)) return done()
    }

    // One table for every remappable list command.
    function runCommand(id) {
        switch (id) {
        case "list.quickLook": if (backend.quickLookEnabled) root.quickLook(); return true
        case "list.delete": backend.deleteSelection(); return true
        case "list.startToday": backend.setStartInDays(0); return true
        case "list.clearDueTime": backend.setSelectionDueTime(-1); return true
        case "list.dueToday": backend.setDueInDays(0); return true
        case "list.dueTomorrow": backend.setDueInDays(1); return true
        case "list.star": backend.setStarred(1); return true
        case "list.priorityNone": backend.setPriority(0); return true
        case "list.priorityLow": backend.setPriority(1); return true
        case "list.priorityMedium": backend.setPriority(2); return true
        case "list.priorityHigh": backend.setPriority(3); return true
        case "list.priorityRaise": backend.bumpPriority(1); return true
        case "list.priorityLower": backend.bumpPriority(-1); return true
        case "list.nudgeDueDay": backend.nudgeDue(1); return true
        case "list.nudgeDueWeek": backend.nudgeDue(7); return true
        case "list.nudgeDueBackDay": backend.nudgeDue(-1); return true
        case "list.nudgeDueBackWeek": backend.nudgeDue(-7); return true
        case "list.clearDue": backend.clearDue(); return true
        case "list.nudgeStartDay": backend.nudgeStart(1); return true
        case "list.nudgeStartWeek": backend.nudgeStart(7); return true
        case "list.nudgeStartBackDay": backend.nudgeStart(-1); return true
        case "list.nudgeStartBackWeek": backend.nudgeStart(-7); return true
        case "list.clearStart": backend.clearStart(); return true
        case "list.collapse": backend.toggleCollapsed(); return true
        case "list.performAction": backend.performAction(); return true
        case "list.openLinks": backend.openLinks(); return true
        case "list.abandon": backend.toggleAbandon(); return true
        case "list.defer": root.command("defer"); return true
        case "list.schedule": root.command("schedule"); return true
        case "list.time": root.command("time"); return true
        case "list.duration": root.command("duration"); return true
        case "list.tag": root.command("tag"); return true
        case "list.untag": root.command("untag"); return true
        case "list.move": root.command("move"); return true
        case "list.jump": root.command("jump"); return true
        case "list.find": root.command("find"); return true
        case "list.tagSearch": root.tagSearch(); return true
        case "list.overlay": root.help(); return true
        case "list.quit": Qt.quit(); return true
        // My Day's plan keys act only while the My Day screen is open; in an
        // ordinary list they keep doing nothing.
        case "myday.add": if (backend.sourceIsMyDay) backend.myDayAddSelection(); return backend.sourceIsMyDay
        case "myday.block": if (backend.sourceIsMyDay) backend.myDayBlockSelection(); return backend.sourceIsMyDay
        case "myday.remove": if (backend.sourceIsMyDay) backend.myDayRemoveSelection(); return backend.sourceIsMyDay
        case "myday.hide": if (backend.sourceIsMyDay) backend.myDayHideSelection(); return backend.sourceIsMyDay
        }
        return false
    }
}
