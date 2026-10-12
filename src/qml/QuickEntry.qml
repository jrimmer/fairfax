import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Controls.Basic as Plain
import QtQuick.Layouts

// Quick Entry: a small floating window for capturing a task from anywhere,
// summoned with `precrime --quick` from a Hyprland bind. Its layout follows
// the reference app's own Quick Entry: the title (priority and star at the right), a ribbon
// in the list's colour with the list, the due summary and the attribute
// buttons, tags beside the Start and Due fields, notes, and a footer.
//
// Natural language stays primary: dates, #tags, /list/, !!, *, repeats and
// [durations] typed in the title are recognised as you type, drawn as capsules
// in the title and filled into the fields. Anything you set by hand is
// "pinned" and no longer overwritten by what the title says.
//
// Return saves; Shift+Return saves and starts the next one (as does pressing
// the bind again); Ctrl+Return saves and opens the task in the main window;
// Escape closes a menu or the inline editor first, then cancels.
Window {
    id: root

    title: "Precrime Quick Entry"
    width: 600
    height: body.implicitHeight
    minimumWidth: 480
    minimumHeight: body.implicitHeight
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
    property bool notesOpen: false

    // "", repeat or duration: the compact inline editor under the ribbon.
    property string customMode: ""
    property int durationUnit: 0   // 0 = minutes, 1 = hours (custom duration editor)

    // Read by the app's Escape filter: with a menu or the inline editor open,
    // Escape closes just that (handled there) rather than the window.
    readonly property bool anyMenuOpen: listMenu.visible || typeMenu.visible || priorityMenu.visible || placeMenu.visible
        || repeatMenu.visible || alarmMenu.visible || durationMenu.visible || attachMenu.visible
    readonly property bool escapeCloses: customMode === "" && !anyMenuOpen && !tagPopup.visible
        && !startField.dirty && !dueField.dirty && !startTimeField.dirty && !dueTimeField.dirty
    // A pasted link stays whole: no dates are read out of it.
    readonly property bool bareUrl: /^(https?|ftp):\/\/\S+$/i.test(title_.text.trim())
    readonly property bool detect: (backend.settingsRevision, backend.settingFlag("nl.quickEntry", true)) && !bareUrl

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
        { label: "Every 2 Weeks", frequency: 2, interval: 2 },
        { label: "Monthly", frequency: 3, interval: 1 },
        { label: "Quarterly", frequency: 3, interval: 3 },
        { label: "Yearly", frequency: 4, interval: 1 }
    ]
    readonly property var alertPresets: [0, 5, 15, 30, 60, 1440]
    readonly property var durationPresets: [15, 30, 45, 60, 120]
    readonly property var currentList: listById(listId)
    readonly property color listColor: currentList ? theme.listColor(currentList.color) : theme.accent
    readonly property var repeatUnits: ["days", "weeks", "months", "years"]

    // ---------------------------------------------------------------- helpers

    // Tag matching ignores case and accents, so "cafe" finds "Café".
    function fold(s) { return String(s).normalize("NFD").replace(/[\u0300-\u036f]/g, "").toLowerCase() }

    function blankRule() {
        return { frequency: 0, interval: 1, from: 0, weekdays: 0, monthOrdinal: 0, monthWeekday: 1, endType: 0, endCount: 1, endDate: "" }
    }
    function parseIso(s) {
        const m = /^(\d{4})-(\d{2})-(\d{2})/.exec(s || "")
        return m ? new Date(+m[1], +m[2] - 1, +m[3]) : null
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
        else if (diff > 1 && diff < 7) s = Qt.locale().dayName(d.getDay(), Locale.ShortFormat)
        else s = Qt.locale().monthName(d.getMonth(), Locale.ShortFormat) + " " + d.getDate() + (d.getFullYear() !== t.getFullYear() ? ", " + d.getFullYear() : "")
        return minutes >= 0 ? s + ", " + timeText(minutes) : s
    }
    function fieldDate(iso) {
        const d = parseIso(iso)
        if (!d) return ""
        const t = parseIso(backend.today) || new Date()
        return Qt.locale().dayName(d.getDay(), Locale.ShortFormat) + ", " + Qt.locale().monthName(d.getMonth(), Locale.ShortFormat) + " " + d.getDate()
            + (d.getFullYear() !== t.getFullYear() ? " " + d.getFullYear() : "")
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
        return m + " minutes before"
    }
    function listById(id) {
        return backend.listChoices.find(l => l.id === id) || backend.listChoices[0] || null
    }
    function fileName(path) {
        return decodeURIComponent(String(path).split("/").pop())
    }
    function anchorWeekday() {
        const d = parseIso(dueIso || startIso || backend.today) || new Date()
        return (d.getDay() + 6) % 7  // 0 = Monday, as in the bitmask.
    }
    function presetIndex() {
        const r = recurrence
        return repeatPresets.findIndex(p => r.frequency === p.frequency && r.interval === p.interval)
    }
    function repeatTip() {
        if (!recurrence.frequency) return "Repeat"
        const d = backend.describeRecurrence(recurrence)
        return "Repeats " + d.charAt(0).toLowerCase() + d.slice(1) + (recurrence.from === 1 ? " (after completion)" : "")
    }
    function alarmTip() {
        return alerts.length ? "Alert " + alerts.map(alertText).join(", ").toLowerCase() : "Alert"
    }
    function placeName() {
        const l = backend.locationChoices().find(p => p.id === locationId)
        return l ? l.name : ""
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
    function setTime(kind, minutes) {
        pin(kind)
        if (kind === "due") dueMinutes = dueIso || minutes < 0 ? minutes : (dueIso = backend.today, minutes)
        else startMinutes = startIso || minutes < 0 ? minutes : (startIso = backend.today, minutes)
    }
    function typedDate(kind, text) {
        if (!text.trim()) { setDate(kind, "", -1); return }
        const s = backend.dateSuggestions(text)
        if (s.length && s[0].date) setDate(kind, s[0].date, s[0].minutes >= 0 ? s[0].minutes : undefined)
    }
    function typedTime(kind, text) {
        if (!text.trim()) { setTime(kind, -1); return }
        const s = backend.dateSuggestions(text)
        const hit = s.find(x => x.minutes >= 0)
        if (hit) setTime(kind, hit.minutes)
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
    function toggleAlert(m) {
        pin("alerts")
        alerts = alerts.includes(m) ? alerts.filter(a => a !== m) : alerts.concat([m]).sort((a, b) => a - b)
    }
    function setDuration(m) { pin("duration"); duration = Math.max(0, m) }
    function setPriority(p) { pin("priority"); priority = p }
    function setList(id) { pin("listId"); listId = id }
    function setPlace(id) { pin("locationId"); locationId = id }
    function addTag(text) {
        const name = text.trim().replace(/^#+/, "").replace(/\s+/g, "-")
        if (!name) return
        pin("tags")
        if (!tags.includes(name)) tags = tags.concat([name])
    }
    function removeTag(name) {
        pin("tags")
        tags = tags.filter(t => t !== name)
    }
    function openCustom(kind) {
        customMode = kind
        if (kind === "repeat") {
            if (!recurrence.frequency) setRule({ frequency: 2, interval: 1 })
            else if (recurrence.frequency === 5) setRule({ frequency: 2, interval: 1, weekdays: 31 })
            numField.text = String(recurrence.interval)
        } else {
            durationUnit = duration >= 60 && duration % 60 === 0 ? 1 : 0
            numField.text = duration ? String(durationUnit ? duration / 60 : duration) : ""
        }
        Qt.callLater(() => { numField.forceActiveFocus(); numField.selectAll() })
    }
    function closeCustom() {
        customMode = ""
        title_.forceActiveFocus()
    }
    function applyCustom() {
        if (customMode === "duration") {
            const n = parseInt(numField.text)
            if (n > 0) setDuration(n * (durationUnit ? 60 : 1))
        }
        closeCustom()
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
        customMode = ""
        defaults = backend.quickEntryDefaults()
        type = 0
        files = []
        title_.text = ""
        listId = defaults.listId
        applyParsed()
        // Like the reference app, the clipboard's text lands in the notes, ready to keep or clear.
        notes.text = ""
        if (backend.settingFlag("quickEntry.clipboard", true)) {
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

    // Return on a field: save and hide; Shift keeps going; Ctrl opens the task.
    function submit(modifiers) {
        if (modifiers & Qt.ControlModifier) save(true)
        else if (modifiers & Qt.ShiftModifier) save(false)
        else { save(false); hide() }
    }

    TextEdit { id: clip; visible: false }

    Connections {
        target: filePicker
        function onPicked(path, purpose) {
            if (purpose === "quickentry-attach" && path && !root.files.includes(path)) root.files = root.files.concat([path])
        }
    }

    Item {
        anchors.fill: parent
        Shortcut { sequence: "Ctrl+W"; context: Qt.WindowShortcut; onActivated: root.hide() }
        Shortcut { sequence: "Ctrl+Return"; context: Qt.WindowShortcut; onActivated: root.save(true) }
        Shortcut { sequence: "Shift+Return"; context: Qt.WindowShortcut; onActivated: root.save(false) }
    }

    // ------------------------------------------------------------ small parts

    // A menu entry that stays truthful to its model after being clicked.
    component CheckItem: MenuItem {
        property bool on: false
        checkable: true
        checked: on
        onTriggered: checked = Qt.binding(() => on)
    }

    // An icon button in the ribbon, tinted when it holds a value.
    // Opens its menu just below itself and stays highlighted while the menu
    // is open, so it's clear what's being changed.
    component RibbonButton: IconButton {
        id: rb
        property bool active: false
        property color tint: theme.today
        property var menu: null
        size: 16
        color: active || (menu && menu.visible) ? tint : theme.muted
        onClicked: if (menu) menu.popup(rb, 0, rb.height + 4)
        Rectangle {
            anchors.fill: parent
            radius: 6
            visible: !!rb.menu && rb.menu.visible
            color: Qt.alpha(rb.tint, 0.18)
            border.color: rb.tint
            border.width: 1
        }
    }

    // A compact text toggle in the inline editors.
    component Seg: Rectangle {
        id: seg
        property string text
        property bool on: false
        signal clicked()
        implicitWidth: segText.implicitWidth + 16
        implicitHeight: 24
        radius: 6
        color: on ? Qt.alpha(theme.accent, 0.28) : segHover.hovered ? theme.mix(theme.raised, theme.foreground, 0.1) : theme.mix(theme.raised, theme.foreground, 0.05)
        Text { id: segText; anchors.centerIn: parent; text: seg.text; color: seg.on ? theme.foreground : theme.muted; font.pixelSize: backend.fontSize - 1 }
        HoverHandler { id: segHover }
        TapHandler { onTapped: seg.clicked() }
    }

    component ClearButton: Item {
        property bool shown: false
        signal clicked()
        implicitWidth: 16
        implicitHeight: 16
        Icon { anchors.centerIn: parent; visible: parent.shown; name: "clear"; size: 13; color: theme.faint }
        MouseArea { anchors.fill: parent; enabled: parent.shown; onClicked: parent.clicked() }
    }

    // ------------------------------------------------------------ menus

    Menu {

        // A real popup window, so it can reach past the dialog's edges.

        popupType: Popup.Window
        id: priorityMenu
        Repeater {
            model: [["None", 0], ["High", 3], ["Medium", 2], ["Low", 1]]
            CheckItem {
                required property var modelData
                text: modelData[0]
                on: root.priority === modelData[1]
                onTriggered: root.setPriority(modelData[1])
            }
        }
        MenuSeparator {}
        CheckItem { text: "Star"; on: root.starred; onTriggered: { root.pin("starred"); root.starred = !root.starred } }
    }

    Menu {

        // A real popup window, so it can reach past the dialog's edges.

        popupType: Popup.Window
        id: listMenu
        Repeater {
            model: backend.listChoices
            CheckItem {
                required property var modelData
                text: modelData.name
                on: modelData.id === root.listId
                onTriggered: root.setList(modelData.id)
            }
        }
    }

    Menu {

        // A real popup window, so it can reach past the dialog's edges.

        popupType: Popup.Window
        id: typeMenu
        Repeater {
            model: ["Task", "Project", "Checklist"]
            CheckItem {
                required property int index
                required property string modelData
                text: modelData
                on: root.type === index
                onTriggered: root.type = index
            }
        }
    }

    Menu {

        // A real popup window, so it can reach past the dialog's edges.

        popupType: Popup.Window
        id: attachMenu
        MenuItem { text: "Add File…"; onTriggered: filePicker.open("Attach a file", "quickentry-attach") }
        Repeater {
            model: root.files
            MenuItem {
                required property int index
                required property string modelData
                text: "Remove " + root.fileName(modelData)
                onTriggered: root.files = root.files.filter((_, j) => j !== index)
            }
        }
    }

    Menu {

        // A real popup window, so it can reach past the dialog's edges.

        popupType: Popup.Window
        id: placeMenu
        property var places: []
        onAboutToShow: places = backend.locationChoices()
        CheckItem { text: "None"; on: !root.locationId; onTriggered: root.setPlace(0) }
        MenuSeparator { visible: placeMenu.places.length > 0 }
        Repeater {
            model: placeMenu.places
            CheckItem {
                required property var modelData
                text: modelData.name
                on: root.locationId === modelData.id
                onTriggered: root.setPlace(root.locationId === modelData.id ? 0 : modelData.id)
            }
        }
    }

    Menu {

        // A real popup window, so it can reach past the dialog's edges.

        popupType: Popup.Window
        id: repeatMenu
        CheckItem { text: "Never"; on: !root.recurrence.frequency; onTriggered: { root.pin("recurrence"); root.recurrence = root.blankRule(); if (root.customMode === "repeat") root.closeCustom() } }
        Repeater {
            model: root.repeatPresets
            CheckItem {
                required property var modelData
                required property int index
                text: modelData.label
                on: root.presetIndex() === index
                onTriggered: { root.setPreset(modelData); if (root.customMode === "repeat") root.closeCustom() }
            }
        }
        MenuSeparator {}
        CheckItem {
            text: "After Completion"
            enabled: !!root.recurrence.frequency
            on: root.recurrence.from === 1
            onTriggered: root.setRule({ from: root.recurrence.from === 1 ? 0 : 1 })
        }
        MenuItem { text: "Custom…"; onTriggered: root.openCustom("repeat") }
    }

    Menu {

        // A real popup window, so it can reach past the dialog's edges.

        popupType: Popup.Window
        id: alarmMenu
        Repeater {
            model: root.alertPresets
            CheckItem {
                required property int modelData
                text: root.alertText(modelData)
                on: root.alerts.includes(modelData)
                onTriggered: root.toggleAlert(modelData)
            }
        }
        MenuSeparator {}
        MenuItem { text: "Remove All"; enabled: root.alerts.length > 0; onTriggered: { root.pin("alerts"); root.alerts = [] } }
    }

    Menu {

        // A real popup window, so it can reach past the dialog's edges.

        popupType: Popup.Window
        id: durationMenu
        CheckItem { text: "None"; on: !root.duration; onTriggered: root.setDuration(0) }
        Repeater {
            model: root.durationPresets
            CheckItem {
                required property int modelData
                text: root.durationText(modelData)
                on: root.duration === modelData
                onTriggered: root.setDuration(modelData)
            }
        }
        CheckItem { text: "Custom…"; on: root.duration > 0 && !root.durationPresets.includes(root.duration); onTriggered: root.openCustom("duration") }
    }

    // ------------------------------------------------------------ the window

    ColumnLayout {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 0

        // ---- title: circle, the line itself, priority and star
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.topMargin: 14
            Layout.bottomMargin: 10
            spacing: 12
            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: 20; implicitHeight: 20; radius: root.type === 2 ? 5 : 10
                color: "transparent"
                border.width: 1.5
                border.color: root.priority > 0 ? root.kindColors.priority : theme.faint
                Icon {
                    anchors.centerIn: parent
                    visible: root.type !== 0
                    name: root.type === 1 ? "project" : "checklist"
                    size: 11
                    color: theme.faint
                }
            }
            // Recognised phrases are drawn as capsules behind the text.
            Item {
                Layout.fillWidth: true
                implicitHeight: 28
                Repeater {
                    model: root.preview.spans || []
                    Rectangle {
                        required property var modelData
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
                    padding: 0
                    background: Item {}
                    color: theme.foreground
                    placeholderText: root.type === 1 ? "New Project" : root.type === 2 ? "New Checklist" : "New Task"
                    placeholderTextColor: theme.faint
                    font.pixelSize: backend.fontSize + 4
                    verticalAlignment: TextInput.AlignVCenter
                    selectByMouse: true
                    onTextChanged: root.applyParsed()
                    // Multi-line text keeps its first line as the title and moves
                    // the rest into the notes.
                    Keys.onPressed: (e) => {
                        if (!e.matches(StandardKey.Paste)) return
                        clip.text = ""
                        clip.paste()
                        if (clip.text.trim().indexOf("\n") < 0) return
                        const parts = backend.splitPaste(clip.text)
                        remove(selectionStart, selectionEnd)
                        insert(cursorPosition, parts.title)
                        if (parts.notes) {
                            notes.text = notes.text ? notes.text + "\n" + parts.notes : parts.notes
                            root.notesOpen = true
                        }
                        e.accepted = true
                    }
                    Keys.onReturnPressed: (e) => root.submit(e.modifiers)
                    Keys.onEnterPressed: (e) => root.submit(e.modifiers)
                    Keys.onEscapePressed: { if (root.customMode !== "") root.closeCustom(); else root.hide() }
                }
            }
            // One star: filled when starred, coloured by priority when one is
            // set; it opens the priority and star menu.
            IconButton {
                id: prio
                Layout.alignment: Qt.AlignVCenter
                icon: "starred"
                size: 18
                color: root.priority === 3 ? theme.priorityHigh : root.priority === 2 ? theme.priorityMedium
                     : root.priority === 1 ? theme.priorityLow : root.starred ? theme.star : theme.faint
                tip: (["No priority", "Low priority", "Medium priority", "High priority"][root.priority]) + (root.starred ? " · starred" : "")
                onClicked: priorityMenu.popup(prio, 0, prio.height + 4)
                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    visible: priorityMenu.visible
                    color: Qt.alpha(theme.accent, 0.18)
                    border.color: theme.accent
                }
                // IconButton draws outlines; fill the star when starred.
                Icon {
                    anchors.centerIn: parent
                    visible: root.starred
                    name: "starred"
                    size: 18
                    filled: true
                    color: parent.color
                }
            }
        }

        // ---- the list ribbon
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 34
            color: Qt.alpha(root.listColor, theme.dark ? 0.22 : 0.16)
            RowLayout {
                anchors.fill: parent
                anchors.rightMargin: 12
                spacing: 8
                ListBadge {
                    Layout.leftMargin: 10
                    text: root.currentList ? root.currentList.name : "No list"
                    color: root.listColor
                    chevrons: true
                    implicitHeight: 34
                    pressedDown: listMenu.opened
                    onClicked: listMenu.popup(this, 0, height + 2)
                }
                Item { Layout.fillWidth: true }
                Text {
                    readonly property bool late: root.isOverdue(root.dueIso)
                    text: root.dueIso ? "Due " + root.dateText(root.dueIso, root.dueMinutes) : ""
                    visible: text !== ""
                    color: late ? theme.overdue : theme.today
                    font.pixelSize: backend.fontSize
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Layout.maximumWidth: 200
                }
                Row {
                    spacing: 0
                    RibbonButton {
                        id: attachButton
                        icon: "image"; active: root.files.length > 0
                        tip: root.files.length ? root.files.map(root.fileName).join(", ") : "Attach a File"
                        menu: attachMenu
                    }
                    RibbonButton {
                        id: placeButton
                        icon: "pin"; active: root.locationId > 0; tint: theme.start
                        tip: root.locationId ? "Location: " + root.placeName() : "Location"
                        menu: placeMenu
                    }
                    RibbonButton {
                        id: repeatButton
                        objectName: "quickRepeat"
                        icon: "repeat"; active: !!root.recurrence.frequency; tint: theme.duration
                        tip: root.repeatTip()
                        menu: repeatMenu
                    }
                    RibbonButton {
                        id: alarmButton
                        icon: "alarm"; active: root.alerts.length > 0; tint: theme.priorityMedium
                        tip: root.alarmTip()
                        menu: alarmMenu
                    }
                    RibbonButton {
                        id: durationButton
                        icon: "duration"; active: root.duration > 0; tint: theme.duration
                        tip: root.duration ? "Duration " + root.durationText(root.duration) : "Duration"
                        menu: durationMenu
                    }
                }
            }
        }

        // ---- compact custom editor for repeat / duration
        Rectangle {
            Layout.fillWidth: true
            visible: root.customMode !== ""
            implicitHeight: customColumn.implicitHeight + 12
            color: theme.mix(theme.raised, theme.foreground, 0.04)
            // Two lines so it fits the panel: the interval and unit with Done,
            // then the weekdays for a weekly repeat.
            ColumnLayout {
                id: customColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 6
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    id: everyLabel
                    visible: root.customMode === "repeat"
                    text: "Every"
                    color: theme.muted
                    font.pixelSize: backend.fontSize - 1
                }
                Plain.TextField {
                    id: numField
                    implicitWidth: 52
                    implicitHeight: 26
                    padding: 4
                    horizontalAlignment: TextInput.AlignHCenter
                    color: theme.foreground
                    font.pixelSize: backend.fontSize - 1
                    placeholderText: "1"
                    placeholderTextColor: theme.faint
                    validator: IntValidator { bottom: 1; top: 999 }
                    inputMethodHints: Qt.ImhDigitsOnly
                    selectByMouse: true
                    background: Rectangle { radius: 6; color: theme.background; border.color: numField.activeFocus ? theme.accent : theme.divider }
                    onTextEdited: if (root.customMode === "repeat" && text) root.setRule({ interval: parseInt(text) })
                    Keys.onReturnPressed: root.applyCustom()
                    Keys.onEnterPressed: root.applyCustom()
                    Keys.onEscapePressed: root.closeCustom()
                }
                Repeater {
                    model: root.customMode === "repeat" ? root.repeatUnits : ["min", "hours"]
                    Seg {
                        required property string modelData
                        required property int index
                        objectName: "quickUnit:" + modelData
                        text: modelData
                        on: root.customMode === "repeat" ? root.recurrence.frequency === index + 1 || (root.recurrence.frequency === 5 && index === 1) : root.durationUnit === index
                        onClicked: {
                            if (root.customMode === "repeat") root.setRule({ frequency: index + 1, weekdays: 0 })
                            else root.durationUnit = index
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                Seg { text: "Done"; on: true; onClicked: root.applyCustom() }
            }
            RowLayout {
                visible: root.customMode === "repeat" && root.recurrence.frequency === 2
                spacing: 6
                Text { text: "On"; color: theme.muted; font.pixelSize: backend.fontSize - 1; Layout.preferredWidth: everyLabel.implicitWidth }
                Repeater {
                    model: root.customMode === "repeat" && root.recurrence.frequency === 2 ? 7 : 0
                    Seg {
                        required property int index
                        objectName: "quickWeekday:" + index
                        text: root.weekdayNames[index].charAt(0)
                        implicitWidth: 24
                        on: (root.recurrence.weekdays >> index) & 1
                        onClicked: root.setRule({ weekdays: (root.recurrence.weekdays ^ (1 << index)) || (1 << index) })
                    }
                }
            }
            }
        }

        // ---- tags on the left; start and due on the right, as in the reference app: the
        // date and time sit right next to their icon at the far right.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 14
            Layout.topMargin: 10
            Layout.bottomMargin: 8
            spacing: 16

            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 10
                Icon {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 6
                    name: "tag"; size: 16; color: theme.faint
                }
                // The tag box: chips for what's set, then where you type.
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: tagFlow.implicitHeight + 8
                    radius: 6
                    color: theme.background
                    border.color: tagInput.activeFocus ? theme.accent : theme.divider
                    MouseArea { anchors.fill: parent; onClicked: tagInput.forceActiveFocus() }
                    Flow {
                        id: tagFlow
                        x: 6; y: 4
                        width: parent.width - 12
                        spacing: 5
                        Repeater {
                            model: root.tags
                            Rectangle {
                                required property string modelData
                                height: 22
                                width: chipText.implicitWidth + 30
                                radius: 11
                                color: theme.mix(theme.raised, theme.foreground, 0.28)
                                Text { id: chipText; x: 9; anchors.verticalCenter: parent.verticalCenter; text: modelData; color: theme.background; font.pixelSize: backend.fontSize - 2; font.weight: Font.DemiBold }
                                Icon {
                                    anchors.right: parent.right; anchors.rightMargin: 5; anchors.verticalCenter: parent.verticalCenter
                                    name: "close"; size: 9; stroke: 2.4; color: theme.background
                                    MouseArea { anchors.fill: parent; anchors.margins: -3; onClicked: root.removeTag(modelData) }
                                }
                            }
                        }
                TextInput {
                            id: tagInput
                            property var matches: []
                            width: Math.max(70, contentWidth + 10)
                            height: 24
                            verticalAlignment: TextInput.AlignVCenter
                            color: theme.foreground
                            selectionColor: theme.selection
                            font.pixelSize: backend.fontSize - 1
                            clip: true
                            Text { visible: !tagInput.text; text: root.tags.length ? "Add another…" : "Add tags…"; color: theme.faint; font: tagInput.font; anchors.verticalCenter: parent.verticalCenter }
                            function accept(name) {
                                root.addTag(name)
                                text = ""
                                matches = []
                                tagPopup.close()
                            }
                            onTextEdited: {
                                if (text.endsWith(",")) { accept(text.slice(0, -1)); return }
                                const q = text.toLowerCase()
                                matches = q ? backend.tagNames.filter(n => root.fold(n).startsWith(root.fold(q)) && !root.tags.includes(n)).slice(0, 6) : []
                                if (matches.length) tagPopup.open(); else tagPopup.close()
                            }
                            Keys.onReturnPressed: (e) => {
                                if (text.trim()) accept(matches.length && tagPopup.visible ? matches[0] : text)
                                else root.submit(e.modifiers)
                            }
                            Keys.onTabPressed: (e) => { if (text.trim()) { accept(matches.length && tagPopup.visible ? matches[0] : text); e.accepted = true } else e.accepted = false }
                            Keys.onPressed: (e) => {
                                if (e.key === Qt.Key_Backspace && !text && root.tags.length) {
                                    root.removeTag(root.tags[root.tags.length - 1])
                                    e.accepted = true
                                }
                            }
                            Keys.onEscapePressed: (e) => { if (tagPopup.visible) { tagPopup.close(); e.accepted = true } else root.hide() }
                            onActiveFocusChanged: if (!activeFocus && text.trim()) accept(text)

                            Popup {
                                id: tagPopup
                                popupType: Popup.Window
                                y: tagInput.height + 2
                                padding: 4
                                background: Rectangle { color: theme.raised; radius: 8; border.color: theme.divider }
                                contentItem: Column {
                                    Repeater {
                                        model: tagInput.matches
                                        Text {
                                            required property string modelData
                                            text: modelData
                                            color: theme.foreground
                                            padding: 5
                                            font.pixelSize: backend.fontSize - 1
                                            MouseArea { anchors.fill: parent; onClicked: tagInput.accept(modelData) }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            GridLayout {
                Layout.alignment: Qt.AlignTop
                columns: 3
                columnSpacing: 4
                rowSpacing: 2
                DateField {
                    id: startField
                    implicitWidth: 112
                    iso: root.startIso
                    value: root.fieldDate(root.startIso)
                    placeholderText: "Start"
                    clearable: !!root.startIso
                    onCleared: root.setDate("start", "", -1)
                    onPicked: (iso, minutes) => root.setDate("start", iso, minutes >= 0 ? minutes : undefined)
                    onTyped: (text) => root.typedDate("start", text)
                }
                DateField {
                    id: startTimeField
                    timeOnly: true
                    implicitWidth: 82
                    value: root.startMinutes >= 0 ? root.timeText(root.startMinutes) : ""
                    placeholderText: "Time"
                    clearable: root.startMinutes >= 0
                    onCleared: root.setTime("start", -1)
                    onTyped: (text) => root.typedTime("start", text)
                }
                Icon { Layout.preferredWidth: 14; Layout.preferredHeight: 14; name: "play"; size: 12; filled: true; color: root.startIso ? theme.start : theme.faint }

                DateField {
                    id: dueField
                    implicitWidth: 112
                    iso: root.dueIso
                    value: root.fieldDate(root.dueIso)
                    placeholderText: "Due"
                    color: root.dueIso ? theme.today : theme.foreground
                    clearable: !!root.dueIso
                    onCleared: root.setDate("due", "", -1)
                    onPicked: (iso, minutes) => root.setDate("due", iso, minutes >= 0 ? minutes : undefined)
                    onTyped: (text) => root.typedDate("due", text)
                }
                DateField {
                    id: dueTimeField
                    timeOnly: true
                    implicitWidth: 82
                    value: root.dueMinutes >= 0 ? root.timeText(root.dueMinutes) : ""
                    placeholderText: "Time"
                    clearable: root.dueMinutes >= 0
                    onCleared: root.setTime("due", -1)
                    onTyped: (text) => root.typedTime("due", text)
                }
                Icon { Layout.preferredWidth: 14; Layout.preferredHeight: 14; name: "check"; size: 13; stroke: 2.2; color: root.dueIso ? theme.today : theme.faint }
            }
        }

        // ---- notes: one line until you click or Tab into it
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: root.notesOpen ? Math.ceil(notes.font.pixelSize * 1.4 * 4) + 24 : 40
            color: theme.notes
            Icon { x: 16; y: 12; name: "note"; size: 16; color: theme.faint }
            Plain.TextArea {
                id: notes
                anchors.fill: parent
                anchors.leftMargin: 44
                anchors.rightMargin: 16
                topPadding: 10
                bottomPadding: 10
                leftPadding: 0
                wrapMode: TextEdit.Wrap
                placeholderText: "Notes"
                placeholderTextColor: theme.faint
                color: theme.foreground
                font.pixelSize: backend.fontSize
                verticalAlignment: TextEdit.AlignTop
                background: Item {}
                onActiveFocusChanged: {
                    if (activeFocus) root.notesOpen = true
                    else if (text === "") root.notesOpen = false
                }
                Keys.onEscapePressed: root.hide()
            }
        }

        // ---- footer
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.topMargin: 12
            spacing: 10

            Rectangle {
                id: typeButton
                implicitWidth: typeRow.implicitWidth + 24; implicitHeight: 32; radius: 6
                color: typeMenu.opened ? Qt.alpha(theme.accent, 0.22) : typeHover.hovered ? theme.mix(theme.raised, theme.foreground, 0.14) : theme.mix(theme.raised, theme.foreground, 0.08)
                border.width: 2
                border.color: typeFocus.activeFocus || typeMenu.opened ? theme.accent : "transparent"
                Row {
                    id: typeRow
                    anchors.centerIn: parent
                    spacing: 14
                    Text { text: ["Task", "Project", "Checklist"][root.type]; color: theme.foreground; font.pixelSize: backend.fontSize; anchors.verticalCenter: parent.verticalCenter }
                    Icon { name: "updown"; size: 11; stroke: 2; color: theme.muted; anchors.verticalCenter: parent.verticalCenter }
                }
                HoverHandler { id: typeHover }
                TapHandler { onTapped: typeMenu.popup(typeButton, 0, typeButton.height + 2) }
                FocusScope { id: typeFocus; anchors.fill: parent; activeFocusOnTab: true
                    Keys.onPressed: (e) => { if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Space) { typeMenu.popup(typeButton, 0, typeButton.height + 2); e.accepted = true } } }
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                implicitWidth: cancelText.implicitWidth + 40; implicitHeight: 32; radius: 6
                color: cancelHover.hovered ? theme.mix(theme.raised, theme.foreground, 0.14) : theme.mix(theme.raised, theme.foreground, 0.08)
                border.width: 2
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
                implicitWidth: saveText.implicitWidth + 48; implicitHeight: 32; radius: 6
                color: theme.accent
                opacity: ready ? 1 : 0.4
                border.width: 2
                border.color: saveFocus.activeFocus ? theme.foreground : "transparent"
                Text { id: saveText; anchors.centerIn: parent; text: "Save"; color: theme.accentForeground; font.pixelSize: backend.fontSize; font.weight: Font.DemiBold }
                HoverHandler { id: saveHover }
                TapHandler { enabled: saveButton.ready; onTapped: { root.save(false); root.hide() } }
                ToolTip.visible: saveHover.hovered
                ToolTip.delay: 600
                ToolTip.text: "Return saves · Shift+Return adds another · Ctrl+Return opens"
                FocusScope { id: saveFocus; anchors.fill: parent; activeFocusOnTab: true
                    Keys.onPressed: (e) => { if (saveButton.ready && (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Space) && !(e.modifiers & (Qt.ShiftModifier | Qt.ControlModifier))) { root.save(false); root.hide(); e.accepted = true } } }
            }
        }
    }
}
