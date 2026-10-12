import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Controls.Basic as Plain
import QtQuick.Layouts

// Quick Entry: a small floating window for capturing a task from anywhere,
// summoned with `precrime --quick` from a Hyprland bind.
//
// Natural language stays primary: dates, #tags, /list/, !!, *, repeats and
// [durations] typed in the title are recognised as you type, shown as capsules
// in the title and filled into the pills below it. Any pill you set by hand is
// "pinned" and no longer overwritten by what the title says. Clicking a pill
// opens one row of choices right under the pills (no popovers); inside it
// Left/Right move, Return or 1-9 pick, Escape closes just the row.
//
// Return saves; Shift+Return saves and starts the next one (as does pressing
// the bind again); Ctrl+Return saves and opens the task in the main window;
// Escape (with no row open) cancels.
Window {
    id: root

    title: "Precrime Quick Entry"
    width: 640
    height: body.implicitHeight + 28
    minimumWidth: 480
    minimumHeight: body.implicitHeight + 28
    color: theme.raised
    flags: Qt.Window | Qt.WindowStaysOnTopHint

    Material.theme: theme.dark ? Material.Dark : Material.Light
    Material.accent: theme.accent
    Material.background: theme.raised
    Material.foreground: theme.foreground

    // ---- the task being entered
    property int type: 0
    property var preview: ({ chips: [], spans: [] })
    property var pinned: ({})
    property var defaults: ({ listId: 0, dueIso: "" })
    property int listId: 0
    property string dueIso: ""
    property int dueMinutes: -1
    property string startIso: ""
    property int startMinutes: -1
    property var recurrence: blankRule()
    property var alerts: []
    property int duration: 0
    property var tags: []
    property int locationId: 0
    property int priority: 0
    property bool starred: false
    property var files: []

    // ---- the option row
    property string openKind: ""   // "", list, due, start, repeat, alert, duration, tags, place, priority, attach
    // Read by the app's Escape filter: with an option row open, Escape closes
    // the row (handled here) rather than the window.
    readonly property bool escapeCloses: openKind === "" && rowMode === ""
    property string rowMode: ""    // "", repeatCustom, durationCustom
    readonly property var options: buildOptions()
    readonly property bool detect: backend.setting("nl.quickEntry").value !== "false"

    readonly property var kindColors: ({
        date: theme.today, due: theme.today, start: theme.start, time: theme.today, repeat: theme.duration,
        alert: theme.priorityMedium, duration: theme.duration, priority: theme.priorityHigh, star: theme.star,
        tag: theme.muted, list: theme.accent, location: theme.start
    })
    readonly property var weekdayNames: ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
    readonly property var repeatPresets: [
        { label: "Daily", frequency: 1, interval: 1 },
        { label: "Weekdays", frequency: 5, interval: 1 },
        { label: "Weekly", frequency: 2, interval: 1 },
        { label: "Every 2 weeks", frequency: 2, interval: 2 },
        { label: "Monthly", frequency: 3, interval: 1 },
        { label: "Quarterly", frequency: 3, interval: 3 },
        { label: "Yearly", frequency: 4, interval: 1 }
    ]
    readonly property var alertPresets: [0, 5, 15, 30, 60, 1440]
    readonly property var durationPresets: [15, 30, 45, 60, 120]
    readonly property var priorityNames: ["No priority", "! Low", "!! Medium", "!!! High"]

    // ---------------------------------------------------------------- helpers

    function blankRule() {
        return { frequency: 0, interval: 1, from: 0, weekdays: 0, monthOrdinal: 0, monthWeekday: 1, endType: 0, endCount: 1, endDate: "" }
    }
    function parseIso(s) {
        const m = /^(\d{4})-(\d{2})-(\d{2})/.exec(s || "")
        return m ? new Date(+m[1], +m[2] - 1, +m[3]) : null
    }
    function isoOf(d) {
        return d.getFullYear() + "-" + String(d.getMonth() + 1).padStart(2, "0") + "-" + String(d.getDate()).padStart(2, "0")
    }
    function addDays(iso, n) {
        const d = parseIso(iso) || new Date()
        return isoOf(new Date(d.getFullYear(), d.getMonth(), d.getDate() + n))
    }
    function timeText(m) {
        return new Date(2000, 0, 1, Math.floor(m / 60), m % 60).toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
    }
    function dateText(iso, minutes) {
        const d = parseIso(iso)
        if (!d) return ""
        const t = parseIso(backend.today) || new Date()
        const diff = Math.round((d - t) / 86400000)
        let s
        if (diff === 0) s = "Today"
        else if (diff === 1) s = "Tomorrow"
        else if (diff === -1) s = "Yesterday"
        else if (diff > 1 && diff < 7) s = Qt.locale().dayName(d.getDay(), Locale.LongFormat)
        else s = Qt.locale().monthName(d.getMonth(), Locale.ShortFormat) + " " + d.getDate() + (d.getFullYear() !== t.getFullYear() ? ", " + d.getFullYear() : "")
        return minutes >= 0 ? s + ", " + timeText(minutes) : s
    }
    function isOverdue(iso) {
        return !!iso && iso < (backend.today || "")
    }
    function durationText(m) {
        if (m <= 0) return "No duration"
        if (m < 60) return m + " min"
        const h = Math.floor(m / 60), r = m % 60
        if (r) return h + " h " + r + " min"
        return h === 1 ? "1 hour" : h + " hours"
    }
    function alertText(m) {
        if (m === 0) return "At due time"
        if (m % 1440 === 0) return m === 1440 ? "1 day before" : (m / 1440) + " days before"
        if (m % 60 === 0) return m === 60 ? "1 hour before" : (m / 60) + " hours before"
        return m + " min before"
    }
    function alertsText() {
        if (!alerts.length) return "No alert"
        return alertText(alerts[0]) + (alerts.length > 1 ? " +" + (alerts.length - 1) : "")
    }
    function priorityColor(p) {
        return p >= 3 ? theme.priorityHigh : p === 2 ? theme.priorityMedium : p === 1 ? theme.priorityLow : theme.faint
    }
    function listById(id) {
        return backend.listChoices.find(l => l.id === id) || backend.listChoices[0] || null
    }
    function fileName(url) {
        return decodeURIComponent(String(url).split("/").pop())
    }
    function anchorWeekday() {
        const d = parseIso(dueIso || startIso || backend.today) || new Date()
        return (d.getDay() + 6) % 7  // 0 = Monday, as in the bitmask.
    }

    function pin(key) {
        const p = Object.assign({}, pinned)
        p[key] = true
        pinned = p
    }
    function setDate(kind, iso, minutes) {
        pin(kind)
        if (kind === "due") {
            dueIso = iso
            dueMinutes = !iso ? -1 : (minutes === undefined ? dueMinutes : minutes)
        } else {
            startIso = iso
            startMinutes = !iso ? -1 : (minutes === undefined ? startMinutes : minutes)
        }
    }
    function setRule(patch) {
        pin("recurrence")
        const r = Object.assign(blankRule(), recurrence, patch)
        r.interval = Math.max(1, Math.min(999, r.interval || 1))
        if (r.frequency === 2 && !r.weekdays) r.weekdays = 1 << anchorWeekday()
        if (r.frequency !== 2) r.weekdays = 0
        recurrence = r
    }
    function setPreset(p) {
        pin("recurrence")
        const r = blankRule()
        r.frequency = p.frequency
        r.interval = p.interval
        if (p.frequency === 2) r.weekdays = 1 << anchorWeekday()
        recurrence = r
    }
    function setAlert(m) { pin("alerts"); alerts = m === null ? [] : [m] }
    function setDuration(m) { pin("duration"); duration = Math.max(0, m) }
    function setPriority(p) { pin("priority"); priority = p }
    function setList(id) { pin("listId"); listId = id }
    function setPlace(id) { pin("locationId"); locationId = id }
    function toggleTag(name) {
        pin("tags")
        tags = tags.includes(name) ? tags.filter(t => t !== name) : tags.concat([name])
    }
    function addTag(text) {
        const name = text.trim().replace(/^#+/, "").replace(/\s+/g, "-")
        if (!name) return
        pin("tags")
        if (!tags.includes(name)) tags = tags.concat([name])
    }

    // Seeds every field that has not been pinned: from what the title says, else from the defaults.
    function applyParsed() {
        const text = title_.text
        const p = detect && text.trim() ? backend.previewEntry(text) : null
        preview = p || { chips: [], spans: [] }
        const f = p ? p.fields : {}
        const pn = pinned
        if (!pn.due) {
            dueIso = f.dueIso || defaults.dueIso || ""
            dueMinutes = f.dueIso && f.dueMinutes !== undefined ? f.dueMinutes : -1
        }
        if (!pn.start) {
            startIso = f.startIso || ""
            startMinutes = f.startIso && f.startMinutes !== undefined ? f.startMinutes : -1
        }
        if (!pn.recurrence) recurrence = f.recurrence && f.recurrence.frequency ? f.recurrence : blankRule()
        if (!pn.alerts) alerts = f.alerts || []
        if (!pn.duration) duration = f.duration || 0
        if (!pn.tags) tags = f.tags || []
        if (!pn.listId) listId = f.listId > 0 ? f.listId : defaults.listId
        if (!pn.locationId) locationId = f.locationId || 0
        if (!pn.priority) priority = f.priority || 0
        if (!pn.starred) starred = !!f.starred
    }

    // ------------------------------------------------------------ option rows

    function opt(label, on, run, extra) {
        return Object.assign({ label: label, on: !!on, run: run, keep: false }, extra || {})
    }

    function dateOptions(kind) {
        const iso = kind === "due" ? dueIso : startIso
        const minutes = kind === "due" ? dueMinutes : startMinutes
        const t = backend.today
        const o = [
            opt("Today", iso === t, () => setDate(kind, t)),
            opt("Tomorrow", iso === addDays(t, 1), () => setDate(kind, addDays(t, 1))),
            opt("Next week", iso === addDays(t, 7), () => setDate(kind, addDays(t, 7))),
            opt("Pick date…", false, () => dateField.forceActiveFocus(), { keep: true })
        ]
        if (minutes >= 0) o.push(opt("No time", false, () => setDate(kind, iso, -1)))
        o.push(opt("No date", !iso, () => setDate(kind, "", -1)))
        return o
    }

    function repeatOptions() {
        const r = recurrence
        if (rowMode === "repeatCustom") {
            const o = []
            const units = ["days", "weeks", "months", "years"]
            for (let i = 0; i < 4; ++i)
                o.push(opt(units[i], r.frequency === i + 1, () => setRule({ frequency: i + 1, weekdays: 0 }), { keep: true }))
            if (r.frequency === 2)
                for (let d = 0; d < 7; ++d)
                    o.push(opt(weekdayNames[d], (r.weekdays >> d) & 1, () => {
                        const w = r.weekdays ^ (1 << d)
                        setRule({ weekdays: w || (1 << d) })
                    }, { keep: true }))
            o.push(opt("After completion", r.from === 1, () => setRule({ from: r.from === 1 ? 0 : 1 }), { keep: true }))
            o.push(opt("Presets", false, () => { rowMode = "" }, { keep: true }))
            return o
        }
        const o = [opt("Doesn't repeat", !r.frequency, () => { pin("recurrence"); recurrence = blankRule() })]
        for (const p of repeatPresets)
            o.push(opt(p.label, r.frequency === p.frequency && r.interval === p.interval, () => setPreset(p)))
        const isPreset = repeatPresets.some(p => r.frequency === p.frequency && r.interval === p.interval)
        o.push(opt("Custom…", r.frequency && !isPreset, () => {
            if (!r.frequency) setRule({ frequency: 2, interval: 1 })
            numField.text = String(recurrence.interval)
            rowMode = "repeatCustom"
            Qt.callLater(() => numField.forceActiveFocus())
        }, { keep: true }))
        return o
    }

    function buildOptions() {
        const k = openKind
        const o = []
        if (k === "due" || k === "start") return dateOptions(k)
        if (k === "repeat") return repeatOptions()
        if (k === "alert") {
            o.push(opt("No alert", !alerts.length, () => setAlert(null)))
            for (const m of alertPresets)
                o.push(opt(m === 0 ? "At due time" : m < 60 ? m + " min" : m === 60 ? "1 hour" : "1 day before", alerts.length === 1 && alerts[0] === m, () => setAlert(m)))
        } else if (k === "duration") {
            if (rowMode === "durationCustom") {
                o.push(opt("Back", false, () => { rowMode = "" }, { keep: true }))
                return o
            }
            o.push(opt("None", !duration, () => setDuration(0)))
            for (const m of durationPresets)
                o.push(opt(durationText(m), duration === m, () => setDuration(m)))
            o.push(opt("Custom…", duration && !durationPresets.includes(duration), () => {
                numField.text = duration ? String(duration) : ""
                rowMode = "durationCustom"
                Qt.callLater(() => numField.forceActiveFocus())
            }, { keep: true }))
        } else if (k === "priority") {
            for (let p = 0; p < 4; ++p) {
                const pr = p
                o.push(opt(pr === 0 ? "None" : priorityNames[pr], priority === pr, () => setPriority(pr), { tint: priorityColor(pr) }))
            }
        } else if (k === "list") {
            for (const l of backend.listChoices) {
                const id = l.id
                o.push(opt(l.name, listId === id, () => setList(id), { dot: theme.listColor(l.color), hasDot: true }))
            }
        } else if (k === "tags") {
            for (const name of backend.tagNames) {
                const n = name
                o.push(opt("#" + n, tags.includes(n), () => toggleTag(n), { keep: true }))
            }
            for (const name of tags)
                if (!backend.tagNames.includes(name)) {
                    const n = name
                    o.push(opt("#" + n, true, () => toggleTag(n), { keep: true }))
                }
        } else if (k === "place") {
            o.push(opt("No place", !locationId, () => setPlace(0)))
            for (const l of backend.locationChoices()) {
                const id = l.id
                o.push(opt(l.name, locationId === id, () => setPlace(id)))
            }
        } else if (k === "attach") {
            o.push(opt("Add file…", false, () => filePicker.open("Attach a file", "quickentry")))
            for (let i = 0; i < files.length; ++i) {
                const idx = i
                o.push(opt(fileName(files[i]) + "  ✕", true, () => { files = files.filter((_, j) => j !== idx) }, { keep: true }))
            }
        }
        return o
    }

    function selectedIndex() {
        const i = options.findIndex(o => o.on)
        return i < 0 ? 0 : i
    }

    function openRow(kind) {
        if (kind === "attach" && files.length === 0) {
            filePicker.open("Attach a file", "quickentry")
            return
        }
        if (openKind === kind) { closeRow(); return }
        openKind = kind
        rowMode = ""
        textField.text = ""
        optRow.index = selectedIndex()
        Qt.callLater(() => optRow.forceActiveFocus())
    }

    function closeRow() {
        openKind = ""
        rowMode = ""
        title_.forceActiveFocus()
    }

    function pick(i) {
        const o = options[i]
        if (!o) return
        o.run()
        if (!o.keep) closeRow()
        else if (openKind !== "") Qt.callLater(() => { if (!numField.activeFocus && !textField.activeFocus) optRow.forceActiveFocus() })
    }

    // ------------------------------------------------------------ lifecycle

    function summon() {
        if (visible && title_.text.trim()) { save(false); return }
        reset()
        show()
        raise()
        requestActivate()
        title_.forceActiveFocus()
    }

    function prefill(text) {
        title_.text = text
        title_.cursorPosition = text.length
    }

    function reset() {
        pinned = ({})
        openKind = ""
        rowMode = ""
        defaults = backend.quickEntryDefaults()
        type = 0
        files = []
        title_.text = ""
        listId = defaults.listId
        applyParsed()
        // Like the reference app, the clipboard's text lands in the notes, ready to keep or clear.
        notes.text = ""
        if (backend.setting("quickEntry.clipboard").value !== "false") {
            clip.text = ""
            clip.paste()
            notes.text = clip.text.trim().length < 2000 ? clip.text.trim() : ""
        }
        notesOpen = notes.text !== ""
    }

    function save(openAfter) {
        const raw = title_.text.trim()
        if (!raw) return
        const title = detect && preview.title && preview.title.trim() ? preview.title.trim() : raw
        const id = backend.quickAddFields({
            title: title, notes: notes.text, type: type, listId: listId,
            dueIso: dueIso, dueMinutes: dueMinutes, startIso: startIso, startMinutes: startMinutes,
            recurrence: recurrence, alerts: alerts, duration: duration, tags: tags,
            locationId: locationId, priority: priority, starred: starred, files: files
        })
        if (openAfter && id) {
            hide()
            backend.requestWindow()
            backend.revealInList(id)
            backend.beginEdit(id)
            return
        }
        reset()
        title_.forceActiveFocus()
    }

    property bool notesOpen: false

    TextEdit { id: clip; visible: false }

    Connections {
        target: filePicker
        function onPicked(path, purpose) {
            if (purpose === "quickentry" && path) root.files = root.files.concat(["file://" + path])
        }
    }

    // Escape is handled by an application-level filter (it closes this window
    // unless a "Popup"-named item has focus, which is why the option row and
    // its fields are named that way).
    Item {
        anchors.fill: parent
        Shortcut { sequence: "Ctrl+W"; context: Qt.WindowShortcut; onActivated: root.hide() }
        Shortcut { sequence: "Ctrl+Return"; context: Qt.WindowShortcut; onActivated: root.save(true) }
        Shortcut { sequence: "Shift+Return"; context: Qt.WindowShortcut; onActivated: root.save(false) }
    }

    // The layout fills whatever size the compositor gives the window; the
    // notes area takes up any extra height, so the footer stays at the bottom.
    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        // ---- title: a circle tinted by priority, then the line itself
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: 22; implicitHeight: 22; radius: 11
                color: "transparent"
                border.width: 2
                border.color: root.priority > 0 ? root.priorityColor(root.priority) : theme.faint
                Icon {
                    anchors.centerIn: parent
                    visible: root.type !== 0
                    name: root.type === 1 ? "project" : "checklist"
                    size: 12
                    color: theme.faint
                }
            }
            // Recognised phrases are drawn as capsules behind the text.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 42
                radius: 8
                color: theme.background
                border.color: title_.activeFocus ? theme.accent : theme.divider

                Repeater {
                    model: root.preview.spans || []
                    Rectangle {
                        required property var modelData
                        // Re-measured whenever the text or its layout changes.
                        readonly property rect a: { title_.contentWidth; title_.width; title_.text; return title_.positionToRectangle(modelData.start) }
                        readonly property rect b: { title_.contentWidth; title_.width; title_.text; return title_.positionToRectangle(modelData.start + modelData.length) }
                        x: title_.x + title_.leftPadding + a.x - 3
                        width: b.x - a.x + 6
                        y: (parent.height - height) / 2
                        height: 24
                        radius: 6
                        color: Qt.alpha(root.kindColors[modelData.kind] || theme.accent, 0.22)
                    }
                }
                Plain.TextField {
                    id: title_
                    objectName: "quickTitle"
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    background: Item {}
                    color: theme.foreground
                    placeholderText: root.type === 1 ? "New project" : root.type === 2 ? "New checklist" : "Call Sam tomorrow at 3pm #family !!"
                    placeholderTextColor: theme.faint
                    font.pixelSize: backend.fontSize + 2
                    selectByMouse: true
                    onTextChanged: root.applyParsed()
                    Keys.onReturnPressed: (e) => {
                        if (e.modifiers & Qt.ControlModifier) root.save(true)
                        else if (e.modifiers & Qt.ShiftModifier) root.save(false)
                        else { root.save(false); root.hide() }
                    }
                    Keys.onEnterPressed: { root.save(false); root.hide() }
                    Keys.onEscapePressed: { if (root.openKind !== "" || root.rowMode !== "") { root.openKind = ""; root.rowMode = "" } else root.hide() }
                }
            }
        }

        // ---- the pills
        Flow {
            Layout.fillWidth: true
            spacing: 6

            QuickEntryPill {
                readonly property var l: root.listById(root.listId)
                hasDot: true
                dot: l ? theme.listColor(l.color) : theme.faint
                text: l ? l.name : "No list"
                set: !!l
                open: root.openKind === "list"
                onClicked: root.openRow("list")
            }
            QuickEntryPill {
                icon: "calendar"
                text: root.dueIso ? root.dateText(root.dueIso, root.dueMinutes) : "No due date"
                set: !!root.dueIso
                tint: root.isOverdue(root.dueIso) ? theme.overdue : theme.today
                valueColor: root.isOverdue(root.dueIso) ? theme.overdue : theme.foreground
                open: root.openKind === "due"
                onClicked: root.openRow("due")
            }
            QuickEntryPill {
                icon: "play"
                text: root.startIso ? "Starts " + root.dateText(root.startIso, root.startMinutes) : "No start date"
                set: !!root.startIso
                tint: theme.start
                open: root.openKind === "start"
                onClicked: root.openRow("start")
            }
            QuickEntryPill {
                icon: "repeat"
                text: root.recurrence.frequency ? backend.describeRecurrence(root.recurrence) : "Doesn't repeat"
                set: !!root.recurrence.frequency
                tint: theme.duration
                open: root.openKind === "repeat"
                onClicked: root.openRow("repeat")
            }
            QuickEntryPill {
                icon: "alarm"
                text: root.alertsText()
                set: root.alerts.length > 0
                tint: theme.priorityMedium
                open: root.openKind === "alert"
                onClicked: root.openRow("alert")
            }
            QuickEntryPill {
                icon: "duration"
                text: root.durationText(root.duration)
                set: root.duration > 0
                tint: theme.duration
                open: root.openKind === "duration"
                onClicked: root.openRow("duration")
            }
            QuickEntryPill {
                icon: "tag"
                text: root.tags.length ? root.tags.map(t => "#" + t).join(" ") : "Add tags"
                set: root.tags.length > 0
                tint: theme.muted
                open: root.openKind === "tags"
                onClicked: root.openRow("tags")
            }
            QuickEntryPill {
                readonly property var loc: backend.locationChoices().find(l => l.id === root.locationId)
                icon: "pin"
                text: loc ? loc.name : "No place"
                set: !!loc
                tint: theme.start
                open: root.openKind === "place"
                onClicked: root.openRow("place")
            }
            QuickEntryPill {
                text: root.priorityNames[root.priority]
                set: root.priority > 0
                valueColor: root.priorityColor(root.priority)
                open: root.openKind === "priority"
                onClicked: root.openRow("priority")
            }
            QuickEntryPill {
                icon: "starred"
                filledIcon: root.starred
                text: root.starred ? "Starred" : "Star"
                set: root.starred
                tint: theme.star
                onClicked: { root.pin("starred"); root.starred = !root.starred }
            }
            QuickEntryPill {
                icon: "attach"
                text: root.files.length === 0 ? "Attach file" : root.files.length === 1 ? root.fileName(root.files[0]) : root.files.length + " files"
                set: root.files.length > 0
                tint: theme.muted
                open: root.openKind === "attach"
                onClicked: root.openRow("attach")
            }
        }

        // ---- the one open row of options
        QuickEntryPopupRow {
            id: optRow
            Layout.fillWidth: true
            visible: root.openKind !== ""
            implicitHeight: rowBox.implicitHeight
            count: root.options.length
            typeTarget: root.openKind === "due" || root.openKind === "start" || root.openKind === "tags" ? textField : null
            onActivated: (i) => root.pick(i)
            onCloseRequested: root.closeRow()

            Rectangle {
                id: rowBox
                width: parent.width
                implicitHeight: flow.implicitHeight + 20
                radius: 10
                color: theme.mix(theme.raised, theme.foreground, 0.04)
                border.color: optRow.activeFocus ? Qt.alpha(theme.accent, 0.5) : theme.divider

                Flow {
                    id: flow
                    x: 10; y: 10
                    width: parent.width - 20
                    spacing: 6

                    Text {
                        height: 26
                        verticalAlignment: Text.AlignVCenter
                        rightPadding: 4
                        text: ({ list: "List", due: "Due", start: "Start", repeat: root.rowMode === "repeatCustom" ? "Every" : "Repeat",
                                 alert: "Alert", duration: "Duration", tags: "Tags", place: "Place", priority: "Priority", attach: "Files" })[root.openKind] || ""
                        color: theme.faint
                        font.pixelSize: backend.fontSize - 2
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                    }
                    QuickEntryPopupField {
                        id: numField
                        visible: root.rowMode !== ""
                        implicitWidth: 64
                        horizontalAlignment: TextInput.AlignHCenter
                        placeholderText: root.rowMode === "durationCustom" ? "min" : "1"
                        validator: IntValidator { bottom: 1; top: 999 }
                        inputMethodHints: Qt.ImhDigitsOnly
                        onTextEdited: if (root.rowMode === "repeatCustom" && text) root.setRule({ interval: parseInt(text) })
                        onAccepted: {
                            if (root.rowMode === "durationCustom") { if (text) root.setDuration(parseInt(text)); root.closeRow() }
                            else optRow.forceActiveFocus()
                        }
                        onEscaped: root.closeRow()
                    }
                    Text {
                        visible: root.rowMode === "durationCustom"
                        height: 26
                        verticalAlignment: Text.AlignVCenter
                        text: "minutes"
                        color: theme.muted
                        font.pixelSize: backend.fontSize - 1
                    }
                    Repeater {
                        model: root.options
                        QuickEntryChip {
                            required property var modelData
                            required property int index
                            text: modelData.label
                            digit: index < 9 ? String(index + 1) : ""
                            on: modelData.on
                            tint: modelData.tint || theme.accent
                            dot: modelData.dot || "transparent"
                            hasDot: !!modelData.hasDot
                            current: optRow.activeFocus && optRow.index === index
                            onClicked: { optRow.index = index; root.pick(index) }
                        }
                    }
                    QuickEntryPopupField {
                        id: textField
                        visible: root.rowMode === "" && (root.openKind === "due" || root.openKind === "start" || root.openKind === "tags")
                        implicitWidth: 190
                        placeholderText: root.openKind === "tags" ? "New tag" : "Type a date, e.g. fri 3pm"
                        onAccepted: {
                            if (root.openKind === "tags") { root.addTag(text); text = "" }
                            else {
                                const s = backend.dateSuggestions(text)
                                if (s.length && s[0].date) {
                                    root.setDate(root.openKind, s[0].date, s[0].minutes >= 0 ? s[0].minutes : undefined)
                                    root.closeRow()
                                }
                            }
                        }
                        onEscaped: root.closeRow()
                    }
                    Text {
                        readonly property var s: textField.visible && root.openKind !== "tags" && textField.text ? backend.dateSuggestions(textField.text) : []
                        visible: s.length > 0
                        height: 26
                        verticalAlignment: Text.AlignVCenter
                        text: s.length ? "→ " + s[0].label : ""
                        color: theme.today
                        font.pixelSize: backend.fontSize - 1
                    }
                }
            }
        }

        // ---- notes: one line until you click or Tab into it
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: root.notesOpen ? 60 : 34
            implicitHeight: root.notesOpen ? Math.ceil(notes.font.pixelSize * 1.4 * 4) + 16 : 34
            radius: 8
            color: theme.notes
            Plain.TextArea {
                id: notes
                anchors.fill: parent
                anchors.margins: 4
                wrapMode: TextEdit.Wrap
                placeholderText: "Add notes…"
                placeholderTextColor: theme.faint
                color: theme.foreground
                font.pixelSize: backend.fontSize - 1
                verticalAlignment: root.notesOpen ? TextEdit.AlignTop : TextEdit.AlignVCenter
                background: Item {}
                onActiveFocusChanged: {
                    if (activeFocus) root.notesOpen = true
                    else if (text === "") root.notesOpen = false
                }
                Keys.onEscapePressed: { if (root.openKind !== "" || root.rowMode !== "") { root.openKind = ""; root.rowMode = "" } else root.hide() }
            }
        }

        // ---- footer
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Row {
                spacing: 4
                Repeater {
                    model: ["Task", "Project", "Checklist"]
                    QuickEntryChip {
                        required property string modelData
                        required property int index
                        focusable: true
                        text: modelData
                        on: root.type === index
                        onClicked: root.type = index
                    }
                }
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                implicitWidth: cancelText.implicitWidth + 28; implicitHeight: 32; radius: 6
                color: cancelHover.hovered ? theme.mix(theme.raised, theme.foreground, 0.1) : "transparent"
                border.color: cancelFocus.activeFocus ? theme.accent : "transparent"
                Text { id: cancelText; anchors.centerIn: parent; text: "Cancel"; color: theme.foreground; font.pixelSize: backend.fontSize }
                HoverHandler { id: cancelHover }
                TapHandler { onTapped: root.hide() }
                FocusScope { id: cancelFocus; anchors.fill: parent; activeFocusOnTab: true
                    Keys.onPressed: (e) => { if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Space) { root.hide(); e.accepted = true } } }
            }
            Rectangle {
                id: saveButton
                readonly property bool ready: title_.text.trim() !== ""
                implicitWidth: saveText.implicitWidth + 32; implicitHeight: 32; radius: 6
                color: theme.accent
                opacity: ready ? 1 : 0.4
                border.width: 2
                border.color: saveFocus.activeFocus ? theme.foreground : "transparent"
                Text { id: saveText; anchors.centerIn: parent; text: "Save"; color: theme.accentForeground; font.pixelSize: backend.fontSize; font.weight: Font.DemiBold }
                TapHandler { enabled: saveButton.ready; onTapped: { root.save(false); root.hide() } }
                FocusScope { id: saveFocus; anchors.fill: parent; activeFocusOnTab: true
                    Keys.onPressed: (e) => { if (saveButton.ready && (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Space) && !(e.modifiers & (Qt.ShiftModifier | Qt.ControlModifier))) { root.save(false); root.hide(); e.accepted = true } } }
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.topMargin: -4
            text: "Return saves · Shift+Return saves and adds another · Ctrl+Return saves and opens"
            color: theme.faint
            font.pixelSize: backend.fontSize - 3
            elide: Text.ElideRight
        }
    }

    // "Pick date…" focuses the typing field.
    readonly property alias dateField: textField
}
