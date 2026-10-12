import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One palette behind the reference app's quick commands: Defer (d), Schedule (k), Due Time
// (e), Duration (x), Tag (l), Untag (Alt+L), Move (m), List Jump (j), Find
// Task (f), Tag Search (/), Repeat and Quick Open (Ctrl+Shift+O). Each kind
// supplies preset chips (Alt+1…), a filtered item list, and what accepting
// does.
Popup {
    id: root

    property string kind
    property var items: []
    property int highlighted: 0
    // The exact-match switch of the Find, Jump, Tag Search and Quick Open bars;
    // each bar remembers its own.
    property bool exact: false
    readonly property bool hasExact: ["find", "jump", "tagsearch", "open"].includes(kind)

    parent: Overlay.overlay
    x: (parent.width - width) / 2
    y: parent.height * 0.14
    width: Math.min(520, parent.width - 40)
    padding: 0
    modal: true
    focus: true
    dim: false
    background: Rectangle { color: theme.raised; radius: 12; border.color: theme.divider }

    readonly property var titles: ({
        defer: "Defer", schedule: "Schedule", time: "Due Time", duration: "Duration", tag: "Tag", untag: "Untag",
        move: "Move to List", jump: "Jump to List", find: "Find Task", tagsearch: "Find Tag",
        open: "Quick Open", repeat: "Repeat"
    })

    readonly property var chips: {
        switch (kind) {
        case "defer": case "schedule":
            return [{ label: "Today", days: 0 }, { label: "Tomorrow", days: 1 }, { label: "2 Days", days: 2 }, { label: "Week", days: 7 }, { label: "None", days: -1 }]
        case "time":
            return backend.dueTimeChips().concat([{ label: "None", minutes: -1 }])
        case "duration":
            return [{ label: "30 mins", minutes: 30 }, { label: "1 hour", minutes: 60 }, { label: "2 hours", minutes: 120 }, { label: "None", minutes: 0 }]
        case "repeat":
            return [{ label: "Daily", preset: 1 }, { label: "Weekly", preset: 2 }, { label: "2 Weeks", preset: 3 }, { label: "Monthly", preset: 4 }, { label: "Quarterly", preset: 5 }, { label: "Yearly", preset: 6 }, { label: "Never", preset: 0 }]
        }
        return []
    }

    function openFor(k) {
        kind = k
        exact = backend.settingFlag("bar.exact." + k, false)
        input.text = ""
        refresh()
        open()
        input.forceActiveFocus()
    }

    function isoIn(days) {
        const d = new Date()
        d.setDate(d.getDate() + days)
        return d.getFullYear() + "-" + String(d.getMonth() + 1).padStart(2, "0") + "-" + String(d.getDate()).padStart(2, "0")
    }

    function parseDuration(text) {
        const m = text.trim().toLowerCase().match(/^(\d+(?:\.\d+)?)\s*(m|min|mins|minutes?|h|hr|hrs|hours?|d|days?)?$/)
        if (!m) return -1
        const n = parseFloat(m[1])
        const unit = (m[2] || "m")[0]
        return Math.round(unit === "h" ? n * 60 : unit === "d" ? n * 1440 : n)
    }

    function refresh() {
        const q = input.text.trim().toLowerCase()
        const like = (s) => backend.textMatches(s, q, hasExact && exact)
        let list = []
        switch (kind) {
        case "defer": case "schedule": case "time":
            // A typed offset such as +3d or -1w moves the due dates instead.
            if (kind === "defer" && /^[+-]\s*\d+\s*[dwmy]/i.test(q)) {
                list = [{ label: "Move due dates by " + input.text.trim(), offset: input.text.trim() }]
                break
            }
            list = q ? backend.dateSuggestions(input.text).map(s => ({ label: s.label, date: s.date, minutes: s.minutes })) : []
            if (kind === "time") list = list.filter(s => s.minutes >= 0)
            break
        case "duration": {
            const minutes = parseDuration(input.text)
            if (minutes >= 0) list = [{ label: minutes + " minutes", minutes: minutes }]
            break
        }
        case "move":
            list = backend.listChoices.filter(l => like(l.name)).map(l => ({ label: l.name, sub: l.group, color: l.color, id: l.id }))
            break
        case "jump":
            // Lists in collapsed groups count; the panel only hides them.
            list = backend.jumpTargets(input.text, exact)
            break
        case "find":
            // Tasks folded away inside collapsed projects count too.
            list = backend.findInView(input.text, 12, exact).map(t => ({ label: t.title, sub: t.listName, color: t.listColor, id: t.id }))
            break
        case "untag": {
            list = backend.selectionTags().filter(t => like(t.name)).map(t => ({ label: t.name, sub: t.count > 1 ? t.count + " tasks" : "", id: t.id }))
            if (!q && list.length > 1) list.push({ label: "All Tags", sub: "Remove every tag", all: true })
            break
        }
        case "tagsearch": case "tag":
            list = backend.tagNames.filter(like).map(n => ({ label: n }))
            if (kind === "tag" && q && !backend.tagNames.some(n => n.toLowerCase() === q)) list.unshift({ label: input.text.trim(), sub: "New tag" })
            break
        case "open":
            if (q) {
                for (let i = 0; i < backend.sidebar.count; ++i) {
                    const r = backend.sidebar.get(i)
                    if (r.sourceKey && like(r.title)) list.push({ label: r.title, sub: "List", color: r.kind === "focus" ? -1 : r.color, key: r.sourceKey })
                }
                list = list.slice(0, 4).concat(backend.findTasks(input.text, 10, exact).map(t => ({ label: t.title, sub: t.listName, color: t.listColor, id: t.id, task: true })))
            } else {
                // Nothing typed yet: where you have been lately.
                list = backend.recentItems(8)
            }
            break
        }
        items = list.slice(0, 12)
        highlighted = 0
    }

    function toggleExact() {
        exact = !exact
        backend.setSetting("bar.exact." + kind, exact)
        refresh()
        input.forceActiveFocus()
    }

    function acceptChip(i) {
        const c = chips[i]
        if (!c) return
        switch (kind) {
        case "defer": if (c.days < 0) backend.clearDue(); else backend.deferTo(isoIn(c.days)); break
        case "schedule": if (c.days < 0) backend.clearStart(); else backend.scheduleTo(isoIn(c.days)); break
        case "time": backend.setSelectionDueTime(c.minutes); break
        case "duration": backend.setSelectionDuration(c.minutes); break
        case "repeat": if (backend.currentTaskId) backend.setRepeatPreset(backend.currentTaskId, c.preset); break
        }
        close()
    }

    function accept(i, replace) {
        const it = items[i]
        if (kind === "tag") { backend.tagSelection(it ? it.label : input.text, replace); close(); return }
        if (!it) return
        switch (kind) {
        case "defer":
            if (it.offset) backend.deferBy(it.offset)
            else backend.deferTo(it.date, it.minutes >= 0 ? it.minutes : -2)
            break
        case "schedule": backend.scheduleTo(it.date, it.minutes >= 0 ? it.minutes : -2); break
        case "time": backend.setSelectionDueTime(it.minutes); break
        case "duration": backend.setSelectionDuration(it.minutes); break
        case "move": backend.moveSelectionToList(it.id); break
        case "jump": backend.selectSource(it.key); break
        case "find": backend.revealHere(it.id); break
        case "untag": if (it.all) backend.untagSelection(); else backend.untagSelection([it.id]); break
        case "tagsearch": {
            for (let r = 0; r < backend.tags.count; ++r) {
                const row = backend.tags.get(r)
                if (row.kind === "tag" && row.title === it.label) backend.toggleTagFilter(row.tagId)
            }
            break
        }
        case "open": if (it.task) backend.openTask(it.id); else backend.selectSource(it.key); break
        }
        close()
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 10
            Layout.topMargin: 12
            Text {
                Layout.fillWidth: true
                text: root.titles[root.kind] || ""
                color: theme.muted
                font.pixelSize: backend.fontSize - 2
                font.weight: Font.Bold
                font.letterSpacing: 0.5
            }
            Rectangle {
                visible: root.hasExact
                implicitWidth: exactText.implicitWidth + 16
                implicitHeight: 20
                radius: 10
                color: root.exact ? theme.accent : (exactMouse.containsMouse ? theme.selection : theme.mix(theme.raised, theme.foreground, 0.07))
                Text {
                    id: exactText
                    anchors.centerIn: parent
                    text: "Exact words  ⌥E"
                    color: root.exact ? theme.background : theme.muted
                    font.pixelSize: backend.fontSize - 3
                }
                MouseArea { id: exactMouse; anchors.fill: parent; hoverEnabled: true; onClicked: root.toggleExact() }
            }
        }

        TextField {
            id: input
            Layout.fillWidth: true
            Layout.margins: 10
            Layout.topMargin: 4
            font.pixelSize: backend.fontSize + 2
            color: theme.foreground
            placeholderTextColor: theme.faint
            placeholderText: ({ defer: "Type a date (tom 3pm, fri, in 2w) or an offset (+3d, +1w, -2d)", schedule: "Start on…", time: "Type a time: 9a, 14:30, 742",
                                duration: "45m, 2h, 1d", tag: "Tag name (Shift+Return replaces existing tags)", move: "List name",
                                jump: "List or project", find: "Task title", untag: "Tag to remove", tagsearch: "Tag", open: "Find any task or list",
                                repeat: "" })[root.kind] || ""
            background: Rectangle { radius: 8; color: theme.background; border.color: input.activeFocus ? theme.accent : theme.divider }
            onTextEdited: root.refresh()
            Keys.onDownPressed: root.highlighted = Math.min(root.highlighted + 1, root.items.length - 1)
            Keys.onUpPressed: root.highlighted = Math.max(root.highlighted - 1, 0)
            Keys.onReturnPressed: (e) => root.accept(root.highlighted, e.modifiers & Qt.ShiftModifier)
            Keys.onEnterPressed: (e) => root.accept(root.highlighted, e.modifiers & Qt.ShiftModifier)
            Keys.onEscapePressed: root.close()
            Keys.onPressed: (e) => {
                if ((e.modifiers & Qt.AltModifier) && e.key === Qt.Key_E && root.hasExact) {
                    root.toggleExact()
                    e.accepted = true
                } else if ((e.modifiers & Qt.AltModifier) && e.key >= Qt.Key_0 && e.key <= Qt.Key_9) {
                    const n = e.key - Qt.Key_0
                    root.acceptChip(n === 0 ? root.chips.length - 1 : n - 1)
                    e.accepted = true
                }
            }
        }

        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.bottomMargin: 8
            visible: root.chips.length > 0
            spacing: 6
            Repeater {
                model: root.chips
                Rectangle {
                    required property var modelData
                    required property int index
                    width: chipText.implicitWidth + 20
                    height: 26
                    radius: 13
                    color: chipMouse.containsMouse ? theme.selection : theme.mix(theme.raised, theme.foreground, 0.07)
                    Text {
                        id: chipText
                        anchors.centerIn: parent
                        text: modelData.label + "  " + (index === root.chips.length - 1 && ["defer", "schedule", "time", "repeat"].includes(root.kind) ? "⌥0" : "⌥" + (index + 1))
                        color: theme.foreground
                        font.pixelSize: backend.fontSize - 2
                    }
                    MouseArea { id: chipMouse; anchors.fill: parent; hoverEnabled: true; onClicked: root.acceptChip(index) }
                }
            }
        }

        Repeater {
            model: root.items
            Rectangle {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                Layout.leftMargin: 6
                Layout.rightMargin: 6
                implicitHeight: 32
                radius: 6
                color: index === root.highlighted ? theme.selection : "transparent"
                Rectangle {
                    visible: modelData.color !== undefined && modelData.color >= 0
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: 6; height: 16; radius: 3
                    color: theme.listColor(modelData.color || 0)
                }
                Text {
                    x: 26
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 140
                    text: modelData.label
                    elide: Text.ElideRight
                    color: theme.foreground
                    font.pixelSize: backend.fontSize
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.sub || ""
                    color: theme.muted
                    font.pixelSize: backend.fontSize - 2
                }
                MouseArea { anchors.fill: parent; hoverEnabled: true; onEntered: root.highlighted = index; onClicked: root.accept(index, false) }
            }
        }

        Item { implicitHeight: 6 }
    }
}
