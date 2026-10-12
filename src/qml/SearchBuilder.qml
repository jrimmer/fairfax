import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// the reference app's "Search Options", as a visual builder for the search language. Chips
// and pickers write a query string (shown live, and editable); opening it with
// an existing search reads back what it recognises and keeps the rest as text.
// Chip states: off -> include -> exclude (written as !value) -> off.
Popup {
    id: root

    // ---- builder state -------------------------------------------------
    property string freeText: ""
    property var types: ({})                  // preset value -> 1 include, 2 exclude
    property string typeMode: "or"            // "or" | "and"
    property var names: ({ tags: {}, lists: {}, taggroups: {}, groups: {}, locations: {} })
    property var dates: ({ modified: { n: 0, unit: "d" }, created: { n: 0, unit: "d" }, completed: { n: 0, unit: "d" } })
    property int pickerTab: 0
    property string query: ""
    property bool loading: false              // Suppresses rebuilds while state is being set.
    property bool editingPreview: false

    readonly property var typeGroups: [
        { title: "Priority", items: [["hiprio", "High"], ["medprio", "Medium"], ["lowprio", "Low"], ["prio", "Any priority"], ["noprio", "No priority"], ["starred", "Starred"]] },
        { title: "Dates", items: [["overdue", "Overdue"], ["duetoday", "Due today"], ["tomorrow", "Due tomorrow"], ["duethisweek", "Due this week"],
                                  ["starttoday", "Starts today"], ["startthisweek", "Starts this week"], ["anythisweek", "Any this week"],
                                  ["due", "Has due date"], ["nodue", "No due date"], ["nostart", "No start date"]] },
        { title: "Status", items: [["inprogress", "In progress"], ["scheduled", "Scheduled"], ["paused", "Paused"], ["repeat", "Repeating"],
                                   ["duration", "Has duration"], ["donetoday", "Done today"], ["doneyesterday", "Done yesterday"], ["donethisweek", "Done this week"]] },
        { title: "Kind", items: [["task", "Task"], ["subtasks", "Subtask"], ["project", "Project"], ["checklist", "Checklist"], ["emptyproj", "Empty project"], ["note", "Has note"]] },
        { title: "Has", items: [["action", "Action"], ["call", "Call"], ["sms", "SMS"], ["email", "Email"], ["url", "URL"], ["visit", "Visit"],
                                ["google", "Google"], ["anytag", "Any tag"], ["notag", "No tag"]] }
    ]
    readonly property var knownTypes: {
        const s = {}
        for (const g of typeGroups) for (const it of g.items) s[it[0]] = true
        for (const a of ["today", "tomorrow", "thisweek", "anytoday", "anytomorrow", "starttomorrow", "duetomorrow", "emptychck", "proj", "chck",
                         "mail", "message", "browse", "tasksoverdue", "alltasks"]) s[a] = true
        return s
    }
    readonly property var tabs: [
        { key: "tags", title: "Tags", kw: "tags:" }, { key: "lists", title: "Lists", kw: "lists:" },
        { key: "taggroups", title: "Tag groups", kw: "taggroups:" }, { key: "groups", title: "List groups", kw: "groups:" },
        { key: "locations", title: "Locations", kw: "locations:" }
    ]
    readonly property var dateRows: [
        { key: "modified", title: "Modified" }, { key: "created", title: "Created" }, { key: "completed", title: "Completed" }
    ]

    // ---- popup chrome --------------------------------------------------
    parent: Overlay.overlay
    x: (parent.width - width) / 2
    y: Math.max(20, parent.height * 0.08)
    width: Math.min(680, parent.width - 40)
    height: Math.min(760, parent.height - 40)
    padding: 0
    modal: true
    focus: true
    dim: false
    background: Rectangle { color: theme.raised; radius: 12; border.color: theme.divider }

    function openAt(item) {
        loadFrom(backend.search)
        open()
        if (item) {
            const p = item.mapToItem(parent, 0, item.height)
            x = Math.max(20, Math.min(parent.width - width - 20, p.x + item.width - width))
            y = Math.max(20, Math.min(parent.height - height - 20, p.y + 8))
        } else {
            x = (parent.width - width) / 2
            y = Math.max(20, parent.height * 0.08)
        }
        freeInput.forceActiveFocus()
    }

    // ---- query generation ---------------------------------------------
    function quote(name) { return /[\s,()"]/.test(name) ? "\"" + name.replace(/"/g, "") + "\"" : name }

    function stateOf(map, key) { const v = map[key]; return v === 1 || v === 2 ? v : 0 }

    function keysWith(map, st) { return Object.keys(map).filter(k => map[k] === st) }

    function build() {
        const parts = []
        if (freeText.trim()) parts.push(freeText.trim())
        const inc = keysWith(types, 1), exc = keysWith(types, 2)
        if (inc.length || exc.length) {
            const op = typeMode === "and" ? "AND" : "OR"
            const pieces = []
            if (inc.length) pieces.push(inc.length > 1 && exc.length ? "(" + inc.join(" " + op + " ") + ")" : inc.join(" " + op + " "))
            for (const e of exc) pieces.push("!" + e)
            parts.push("type: " + pieces.join(" AND "))
        }
        for (const t of tabs) {
            const m = names[t.key]
            const i = keysWith(m, 1), e = keysWith(m, 2)
            if (i.length) parts.push(t.kw + " " + i.map(quote).join(", "))
            for (const n of e) parts.push(t.kw + " !" + quote(n))
        }
        for (const d of dateRows) {
            const v = dates[d.key]
            if (v.n > 0) parts.push(d.key + ": " + v.n + v.unit)
        }
        return parts.join(" ")
    }

    function rebuild() {
        if (loading) return
        query = build()
        if (!editingPreview) preview.text = query
    }

    onFreeTextChanged: rebuild()
    onTypesChanged: rebuild()
    onTypeModeChanged: rebuild()
    onNamesChanged: rebuild()
    onDatesChanged: rebuild()

    function cycled(map, key, toExclude) {
        const c = Object.assign({}, map)
        const cur = stateOf(map, key)
        const next = toExclude ? (cur === 2 ? 0 : 2) : (cur + 1) % 3
        if (next === 0) delete c[key]; else c[key] = next
        return c
    }

    function cycleType(key, toExclude) { types = cycled(types, key, toExclude) }

    function cycleName(tab, key, toExclude) {
        const c = Object.assign({}, names)
        c[tab] = cycled(names[tab], key, toExclude)
        names = c
    }

    function setDate(key, n, unit) {
        const c = Object.assign({}, dates)
        c[key] = { n: n, unit: unit }
        dates = c
    }

    function reset() {
        loading = true
        freeText = ""
        types = ({})
        typeMode = "or"
        names = { tags: {}, lists: {}, taggroups: {}, groups: {}, locations: {} }
        dates = { modified: { n: 0, unit: "d" }, created: { n: 0, unit: "d" }, completed: { n: 0, unit: "d" } }
        loading = false
    }

    // ---- parsing an existing query back into state --------------------
    function splitTop(s) {
        const out = []
        let cur = "", q = false
        for (const ch of s) {
            if (ch === "\"") { q = !q; cur += ch }
            else if (ch === "," && !q) { out.push(cur); cur = "" }
            else cur += ch
        }
        out.push(cur)
        return out.map(x => x.trim()).filter(x => x.length)
    }

    function unquote(s) { return s.length > 1 && s[0] === "\"" && s[s.length - 1] === "\"" ? s.slice(1, -1) : s }

    // Returns {inc, exc, mode} or null when the type: value isn't ours to edit.
    function parseTypeValue(val) {
        let raw = val.trim()
        let incPart = raw, excPart = ""
        const paren = raw.match(/^\(([^()]*)\)\s*(?:AND\s+(.*))?$/i)
        if (paren) { incPart = paren[1]; excPart = paren[2] || "" }
        const inc = [], exc = []
        let mode = ""
        const take = (text, allowOr) => {
            const toks = text.split(/\s*,\s*|\s+/).filter(x => x.length)
            for (const tk of toks) {
                const up = tk.toUpperCase()
                if (up === "OR") { if (!allowOr || mode === "and") return false; mode = "or"; continue }
                if (up === "AND") { if (mode === "or") return false; mode = "and"; continue }
                const neg = tk[0] === "!"
                const name = (neg ? tk.slice(1) : tk).toLowerCase()
                if (!knownTypes[name]) return false
                if (neg) exc.push(name); else inc.push(name)
            }
            return true
        }
        if (paren) {
            if (!take(incPart, true) || exc.length) return null
            const m = mode
            mode = ""
            if (!take(excPart, false) || inc.length === 0 && exc.length === 0) return null
            if (!incPart.trim() || !exc.length && excPart.trim()) return null
            mode = m
        } else {
            if (!take(raw, true)) return null
            if (mode === "or" && exc.length) return null
        }
        if (!inc.length && !exc.length) return null
        if (mode === "or" && inc.length > 1 && !paren) { /* plain OR list */ }
        return { inc: inc, exc: exc, mode: mode }
    }

    function loadFrom(text) {
        loading = true
        reset()
        loading = true
        const st = { types: {}, typeMode: "or", names: { tags: {}, lists: {}, taggroups: {}, groups: {}, locations: {} },
                     dates: { modified: { n: 0, unit: "d" }, created: { n: 0, unit: "d" }, completed: { n: 0, unit: "d" } } }
        const extras = []
        let haveTypes = false
        const re = /(^|\s)(type:|tags:|taggroups:|locations:|lists:|groups:|modified:|created:|completed:)/gi
        const hits = []
        let m
        const src = text || ""
        while ((m = re.exec(src)) !== null) {
            const start = m.index + m[1].length
            hits.push({ start: start, end: start + m[2].length, kw: m[2].toLowerCase() })
            re.lastIndex = start + m[2].length
        }
        let free = src.slice(0, hits.length ? hits[0].start : src.length)
        let nextOr = false
        const orTail = /(^|\s)or\s*$/i
        if (hits.length && orTail.test(free)) { free = free.replace(orTail, ""); nextOr = true }
        for (let i = 0; i < hits.length; ++i) {
            let value = src.slice(hits[i].end, i + 1 < hits.length ? hits[i + 1].start : src.length)
            const thisOr = nextOr
            nextOr = false
            if (i + 1 < hits.length && orTail.test(value)) { value = value.replace(orTail, ""); nextOr = true }
            const kw = hits[i].kw
            const verbatim = (thisOr ? "OR " : "") + kw + value.replace(/\s+$/, "")
            let ok = false
            if (!thisOr && value.trim()) {
                if (kw === "type:") {
                    const r = haveTypes ? null : parseTypeValue(value)
                    if (r) {
                        haveTypes = true
                        for (const n of r.inc) st.types[n] = 1
                        for (const n of r.exc) st.types[n] = 2
                        if (r.mode) st.typeMode = r.mode
                        ok = true
                    }
                } else if (kw === "modified:" || kw === "created:" || kw === "completed:") {
                    const dm = value.trim().match(/^(\d+)\s*(d|days?|w|weeks?)?$/i)
                    const key = kw.slice(0, -1)
                    if (dm && st.dates[key].n === 0 && parseInt(dm[1]) > 0) {
                        st.dates[key] = { n: parseInt(dm[1]), unit: (dm[2] || "d")[0].toLowerCase() === "w" ? "w" : "d" }
                        ok = true
                    }
                } else {
                    const key = tabs.find(t => t.kw === kw).key
                    const items = splitTop(value)
                    const neg = items.map(x => x[0] === "!")
                    const hasInc = Object.keys(st.names[key]).some(k => st.names[key][k] === 1)
                    if (items.length === 1 && neg[0]) {
                        const n = unquote(items[0].slice(1))
                        if (n && n !== "?" && n !== "~") { st.names[key][n] = 2; ok = true }
                    } else if (items.length && !neg.includes(true) && !hasInc && !items.some(x => x === "?" || x === "~")) {
                        for (const it of items) st.names[key][unquote(it)] = 1
                        ok = true
                    }
                }
            }
            if (!ok) extras.push(verbatim.trim())
        }
        freeText = [free.trim()].concat(extras).filter(x => x.length).join(" ")
        types = st.types
        typeMode = st.typeMode
        names = st.names
        dates = st.dates
        loading = false
        rebuild()
        freeInput.text = freeText
    }

    // Preview typed by hand: re-read it, but leave the text the user is typing alone.
    function previewEdited(text) {
        editingPreview = true
        loadFrom(text)
        query = text.trim()
        editingPreview = false
        freeInput.text = freeText
    }

    // ---- pickers -------------------------------------------------------
    function pickerItems(tab) {
        const key = tabs[tab].key
        let base = []
        switch (key) {
        case "tags": base = backend.tagNames.map(n => ({ name: n })); break
        case "lists": base = backend.listChoices.map(l => ({ name: l.name, color: l.color })); break
        case "taggroups": base = backend.tagGroupChoices().map(g => ({ name: g.name })); break
        case "groups": {
            const seen = {}
            for (const l of backend.listChoices) if (l.group && !seen[l.group]) { seen[l.group] = true; base.push({ name: l.group }) }
            break
        }
        case "locations": base = backend.locationChoices().map(g => ({ name: g.name })); break
        }
        const have = {}
        for (const b of base) have[b.name] = true
        for (const k of Object.keys(names[key])) if (!have[k]) base.push({ name: k })   // Selected but unknown.
        return base
    }

    function pickerCount(tab) { return Object.keys(names[tabs[tab].key]).length }

    // ---- reusable pieces ----------------------------------------------
    component TriChip: Rectangle {
        id: chip
        property string label
        property int state: 0
        property color dot: "transparent"
        property bool showDot: false
        signal clicked()
        signal excluded()

        implicitWidth: chipRow.implicitWidth + 20
        implicitHeight: 26
        radius: 13
        color: state === 1 ? Qt.alpha(theme.accent, 0.22)
             : state === 2 ? Qt.alpha(theme.overdue, 0.16)
             : chipMouse.containsMouse ? theme.selection : theme.mix(theme.raised, theme.foreground, 0.07)
        border.width: 1
        border.color: state === 1 ? theme.accent : state === 2 ? theme.overdue : "transparent"

        Row {
            id: chipRow
            anchors.centerIn: parent
            spacing: 5
            Rectangle {
                visible: chip.showDot
                width: 8; height: 8; radius: 4
                anchors.verticalCenter: parent.verticalCenter
                color: chip.dot
            }
            Text {
                visible: chip.state !== 0
                text: chip.state === 1 ? "✓" : "−"
                color: chip.state === 1 ? theme.accent : theme.overdue
                font.pixelSize: backend.fontSize - 2
                font.bold: true
            }
            Text {
                text: chip.label
                color: chip.state === 2 ? theme.overdue : theme.foreground
                font.pixelSize: backend.fontSize - 2
                font.strikeout: chip.state === 2
            }
        }
        MouseArea {
            id: chipMouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: (e) => { if (e.button === Qt.RightButton) chip.excluded(); else chip.clicked() }
        }
    }

    component Heading: Text {
        color: theme.muted
        font.pixelSize: backend.fontSize - 2
        font.weight: Font.Bold
        font.letterSpacing: 0.5
    }

    component Segment: Rectangle {
        id: seg
        property string label
        property bool active: false
        property bool tall: false
        signal clicked()
        implicitWidth: segText.implicitWidth + 20
        implicitHeight: 26
        radius: 13
        opacity: enabled ? 1 : 0.4
        color: active ? theme.accent : segMouse.containsMouse ? theme.selection : theme.mix(theme.raised, theme.foreground, 0.07)
        Text {
            id: segText
            anchors.centerIn: parent
            text: seg.label
            color: seg.active ? theme.accentForeground : theme.foreground
            font.pixelSize: backend.fontSize - 2
        }
        MouseArea { id: segMouse; anchors.fill: parent; hoverEnabled: true; onClicked: seg.clicked() }
    }

    // ---- layout --------------------------------------------------------
    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Layout.bottomMargin: 8
            Text {
                text: "Search Options"
                color: theme.foreground
                font.pixelSize: backend.fontSize + 3
                font.bold: true
                Layout.fillWidth: true
            }
            CheckBox {
                id: soundexBox
                text: "Sounds like"
                checked: backend.soundex
                onToggled: backend.soundex = checked
                padding: 0
                contentItem: Text {
                    leftPadding: soundexBox.indicator.width + 8
                    verticalAlignment: Text.AlignVCenter
                    text: soundexBox.text
                    color: theme.foreground
                    font.pixelSize: backend.fontSize
                }
            }
        }

        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 14
            Layout.rightMargin: 4
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: scroll.availableWidth - 10
                spacing: 8

                Heading { text: "CONTAINS WORDS" }
                TextField {
                    id: freeInput
                    Layout.fillWidth: true
                    font.pixelSize: backend.fontSize + 1
                    color: theme.foreground
                    Text {
                        x: freeInput.leftPadding
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !freeInput.text
                        text: "Words, \"phrases\", -exclude, OR, wild*"
                        color: theme.faint
                        font: freeInput.font
                    }
                    background: Rectangle { radius: 8; color: theme.background; border.color: freeInput.activeFocus ? theme.accent : theme.divider }
                    onTextEdited: root.freeText = text
                    Keys.onReturnPressed: root.doSearch()
                    Keys.onEnterPressed: root.doSearch()
                }

                RowLayout {
                    Layout.topMargin: 6
                    spacing: 6
                    Heading { text: "TASK TYPE"; Layout.rightMargin: 6 }
                    Segment { label: "Any of these (OR)"; active: root.typeMode === "or"; onClicked: root.typeMode = "or" }
                    Segment { label: "All of these (AND)"; active: root.typeMode === "and"; onClicked: root.typeMode = "and" }
                }

                Repeater {
                    model: root.typeGroups
                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            Layout.preferredWidth: 64
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 5
                            text: modelData.title
                            color: theme.muted
                            font.pixelSize: backend.fontSize - 2
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: modelData.items
                                TriChip {
                                    required property var modelData
                                    label: modelData[1]
                                    state: root.stateOf(root.types, modelData[0])
                                    onClicked: root.cycleType(modelData[0], false)
                                    onExcluded: root.cycleType(modelData[0], true)
                                }
                            }
                        }
                    }
                }

                Text {
                    text: "Click to include, click again to exclude, again to clear. Right-click excludes directly."
                    color: theme.faint
                    font.pixelSize: backend.fontSize - 3
                }

                Heading { text: "NAMED"; Layout.topMargin: 6 }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: root.tabs
                        Segment {
                            required property var modelData
                            required property int index
                            label: modelData.title + (root.pickerCount(index) ? " (" + root.pickerCount(index) + ")" : "")
                            active: root.pickerTab === index
                            onClicked: root.pickerTab = index
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 118
                    radius: 8
                    color: theme.background
                    border.color: theme.divider
                    ScrollView {
                        id: pickScroll
                        anchors.fill: parent
                        anchors.margins: 6
                        clip: true
                        contentWidth: availableWidth
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        Flow {
                            width: pickScroll.availableWidth
                            spacing: 6
                            Repeater {
                                model: root.pickerItems(root.pickerTab)
                                TriChip {
                                    required property var modelData
                                    label: modelData.name
                                    showDot: modelData.color !== undefined && modelData.color >= 0
                                    dot: showDot ? theme.listColor(modelData.color) : "transparent"
                                    state: root.stateOf(root.names[root.tabs[root.pickerTab].key], modelData.name)
                                    onClicked: root.cycleName(root.tabs[root.pickerTab].key, modelData.name, false)
                                    onExcluded: root.cycleName(root.tabs[root.pickerTab].key, modelData.name, true)
                                }
                            }
                        }
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: root.pickerItems(root.pickerTab).length === 0
                        text: "Nothing to choose from yet"
                        color: theme.faint
                        font.pixelSize: backend.fontSize - 2
                    }
                }

                Heading { text: "WITHIN THE LAST"; Layout.topMargin: 6 }
                Repeater {
                    model: root.dateRows
                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 6
                        Text {
                            Layout.preferredWidth: 72
                            text: modelData.title
                            color: theme.foreground
                            font.pixelSize: backend.fontSize
                        }
                        TextField {
                            id: nField
                            Layout.preferredWidth: 60
                            horizontalAlignment: Text.AlignHCenter
                            font.pixelSize: backend.fontSize
                            color: theme.foreground
                            Text {
                                anchors.centerIn: parent
                                visible: !nField.text
                                text: "\u2013"
                                color: theme.faint
                                font: nField.font
                            }
                            validator: IntValidator { bottom: 0; top: 9999 }
                            inputMethodHints: Qt.ImhDigitsOnly
                            text: root.dates[modelData.key].n > 0 ? String(root.dates[modelData.key].n) : ""
                            background: Rectangle { radius: 8; color: theme.background; border.color: nField.activeFocus ? theme.accent : theme.divider }
                            onTextEdited: root.setDate(modelData.key, parseInt(text) || 0, root.dates[modelData.key].unit)
                        }
                        Segment {
                            label: "days"
                            active: root.dates[modelData.key].unit === "d"
                            onClicked: root.setDate(modelData.key, root.dates[modelData.key].n, "d")
                        }
                        Segment {
                            label: "weeks"
                            active: root.dates[modelData.key].unit === "w"
                            onClicked: root.setDate(modelData.key, root.dates[modelData.key].n, "w")
                        }
                    }
                }
                Item { implicitHeight: 4 }
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.divider }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Layout.topMargin: 10
            spacing: 8

            Heading { text: "QUERY" }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 62
                radius: 8
                color: theme.background
                border.color: preview.activeFocus ? theme.accent : theme.divider
                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 2
                    clip: true
                    TextArea {
                        id: preview
                        objectName: "preview"
                        wrapMode: TextEdit.Wrap
                        font.family: theme.monoFont
                        font.pixelSize: backend.fontSize
                        color: theme.foreground
                        Text {
                            x: preview.leftPadding
                            y: preview.topPadding
                            visible: !preview.text
                            text: "Choose options above, or type a query"
                            color: theme.faint
                            font: preview.font
                        }
                        background: null
                        onTextChanged: if (activeFocus) root.previewEdited(text)
                        Keys.onPressed: (e) => {
                            if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter) && !(e.modifiers & Qt.ShiftModifier)) {
                                root.doSearch()
                                e.accepted = true
                            }
                        }
                    }
                }
            }

            RowLayout {
                spacing: 8
                Segment {
                    objectName: "clearButton"
                    label: "Clear"
                    onClicked: { root.reset(); freeInput.text = ""; backend.search = "" }
                }
                Item { Layout.fillWidth: true }
                Segment {
                    objectName: "saveButton"
                    label: "Save as Smart List\u2026"
                    enabled: root.query.length > 0
                    onClicked: { nameInput.text = ""; nameDialog.open(); nameInput.forceActiveFocus() }
                }
                Segment {
                    objectName: "searchButton"
                    label: "Search"
                    active: true
                    onClicked: root.doSearch()
                }
            }
        }
    }

    function doSearch() {
        backend.search = query
        close()
    }

    function saveAs(name) {
        const n = name.trim()
        if (!n || !query) return
        backend.saveSmartList(n, query)
        nameDialog.close()
        close()
    }

    Popup {
        id: nameDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 340
        modal: true
        focus: true
        padding: 14
        background: Rectangle { color: theme.raised; radius: 12; border.color: theme.divider }
        contentItem: ColumnLayout {
            spacing: 10
            Text { text: "Save as Smart List"; color: theme.foreground; font.pixelSize: backend.fontSize + 1; font.bold: true }
            TextField {
                id: nameInput
                objectName: "nameInput"
                Layout.fillWidth: true
                Text {
                    x: nameInput.leftPadding
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !nameInput.text
                    text: "Name"
                    color: theme.faint
                    font: nameInput.font
                }
                color: theme.foreground
                font.pixelSize: backend.fontSize + 1
                background: Rectangle { radius: 8; color: theme.background; border.color: nameInput.activeFocus ? theme.accent : theme.divider }
                Keys.onReturnPressed: root.saveAs(text)
                Keys.onEnterPressed: root.saveAs(text)
            }
            RowLayout {
                Item { Layout.fillWidth: true }
                Segment { label: "Cancel"; onClicked: nameDialog.close() }
                Segment { objectName: "nameSave"; label: "Save"; active: true; enabled: nameInput.text.trim().length > 0; onClicked: root.saveAs(nameInput.text) }
            }
        }
    }
}
