import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The "Search Options", as a visual builder for the search language. The
// Search Presets rule rows, chips and pickers write a query string (shown live,
// and editable); opening it with an existing search reads back what it
// recognises and keeps the rest as text. Name chip states: off -> include ->
// exclude (written as !value) -> off.
Popup {
    id: root

    // ---- builder state -------------------------------------------------
    property string freeText: ""
    // The Search Presets rule tree (see searchrules.h): a group of clauses and
    // nested groups, written as the query's `type:` section.
    property var rules: ({ match: "all", items: [] })
    property var names: ({ tags: {}, lists: {}, taggroups: {}, groups: {}, locations: {} })
    property var dates: ({ modified: { n: 0, unit: "d" }, created: { n: 0, unit: "d" }, completed: { n: 0, unit: "d" } })
    property int pickerTab: 0
    // Parenthesised groups of whole filters, "(tags: a OR tags: b)": each has
    // its own text and how it joins the rest (and / or / not).
    property var groups: []
    property string query: ""
    property bool loading: false              // Suppresses rebuilds while state is being set.
    property bool editingPreview: false

    // Smart List options (the Search Options sheet): the date range stored in
    // TaskList.smartRange, and the per-list completed/scheduled switches.
    property int editingListId: 0             // The Smart List being edited, or 0 when building a plain search.
    property string rangeKind: "none"         // "none" | "fixed" | "next" | "nodue"
    property string rangeFrom: ""
    property string rangeTo: ""
    property int rangeN: 3
    property string rangeUnit: "d"
    property int completedMode: 0             // 0 follows the setting, 1 include, 2 exclude.
    property bool includeScheduled: false
    readonly property string range: rangeString()

    readonly property var ruleProps: backend.ruleProperties()
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

    function resetSmartOptions() {
        editingListId = 0
        rangeKind = "none"; rangeFrom = ""; rangeTo = ""; rangeN = 3; rangeUnit = "d"
        completedMode = 0
        includeScheduled = false
    }

    function isoOk(s) { return /^\d{4}-\d{2}-\d{2}$/.test(s) && !isNaN(Date.parse(s)) }

    // The smartRange string for the controls, in the format model.h documents.
    function rangeString() {
        if (rangeKind === "nodue") return "nodue"
        if (rangeKind === "next") return rangeN > 0 ? "next:" + rangeN + ":" + rangeUnit : ""
        if (rangeKind === "fixed" && (isoOk(rangeFrom) || isoOk(rangeTo)))
            return "fixed:" + (isoOk(rangeFrom) ? rangeFrom : "") + ":" + (isoOk(rangeTo) ? rangeTo : "")
        return ""
    }

    function loadRange(r) {
        const p = (r || "").split(":")
        rangeKind = "none"; rangeFrom = ""; rangeTo = ""; rangeN = 3; rangeUnit = "d"
        if (p[0] === "nodue") rangeKind = "nodue"
        else if (p[0] === "next") { rangeKind = "next"; rangeN = Math.max(1, parseInt(p[1]) || 1); rangeUnit = p[2] || "d" }
        else if (p[0] === "fixed") { rangeKind = "fixed"; rangeFrom = p[1] || ""; rangeTo = p[2] || "" }
    }

    // Opens on an existing Smart List: its rules, range and switches. The
    // Update button writes them back.
    function openForList(listId) {
        const info = backend.smartListInfo(listId)
        if (!info.id) return
        resetSmartOptions()
        editingListId = info.id
        loadRange(info.range)
        completedMode = info.completedMode
        includeScheduled = info.includeScheduled
        loadFrom(info.query)
        open()
        x = (parent.width - width) / 2
        y = Math.max(20, parent.height * 0.08)
        freeInput.forceActiveFocus()
    }

    function openAt(item) {
        resetSmartOptions()
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
        const typeText = backend.rulesToType(rules)
        if (typeText) parts.push("type: " + typeText)
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
        for (const g of groups) {
            const inner = g.text.trim()
            if (!inner) continue
            const group = (g.join === "not" ? "!" : "") + "(" + inner + ")"
            parts.push(g.join === "or" && parts.length ? "OR " + group : group)
        }
        return parts.join(" ")
    }

    function setGroup(i, change) {
        const c = groups.slice()
        c[i] = Object.assign({}, c[i], change)
        groups = c
    }
    function addGroup() { groups = groups.concat([{ text: "", join: "and" }]) }
    function removeGroup(i) { const c = groups.slice(); c.splice(i, 1); groups = c }

    // Lifts parenthesised groups that open with a keyword (as the query
    // language does) out of `src`; returns the rest and the groups found.
    function liftGroups(src) {
        const found = []
        let rest = ""
        const kw = /^\s*!?(type:|tags:|taggroups:|locations:|lists:|groups:|modified:|created:|completed:)/i
        let i = 0
        while (i < src.length) {
            if (src[i] !== "(") { rest += src[i++]; continue }
            let depth = 0, close = -1
            for (let j = i; j < src.length; ++j) {
                if (src[j] === "(") ++depth
                else if (src[j] === ")" && --depth === 0) { close = j; break }
            }
            const inner = close < 0 ? "" : src.slice(i + 1, close)
            if (close < 0 || !kw.test(inner)) { rest += src[i++]; continue }
            let join = "and"
            if (rest.endsWith("!")) { join = "not"; rest = rest.slice(0, -1) }
            const orTail = /(^|\s)or\s*$/i
            if (join === "and" && orTail.test(rest)) { join = "or"; rest = rest.replace(orTail, "") }
            found.push({ text: inner.trim(), join: join })
            i = close + 1
        }
        return { rest: rest, groups: found }
    }

    function rebuild() {
        if (loading) return
        query = build()
        if (!editingPreview) preview.text = query
    }

    onFreeTextChanged: rebuild()
    onRulesChanged: rebuild()
    onNamesChanged: rebuild()
    onDatesChanged: rebuild()
    onGroupsChanged: rebuild()

    function cycled(map, key, toExclude) {
        const c = Object.assign({}, map)
        const cur = stateOf(map, key)
        const next = toExclude ? (cur === 2 ? 0 : 2) : (cur + 1) % 3
        if (next === 0) delete c[key]; else c[key] = next
        return c
    }

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

    // ---- Search Presets rules --------------------------------------------
    // Rows are addressed by an index path: "2" is the third item of the top
    // group, "2.1" the second item of the sub-filter that is the third item.
    // The top group's path is "".
    function propOf(key) { return ruleProps.find(p => p.key === key) || ruleProps[0] }

    function newClause() { const p = ruleProps[0]; return { prop: p.key, neg: false, value: p.values[0].token } }

    function newGroup() { return { match: "all", items: [newClause()] } }

    function nodeAt(tree, path) {
        let n = tree
        if (path === "") return n
        for (const i of path.split(".")) n = n.items[parseInt(i)]
        return n
    }

    // Edits a copy of the tree, then swaps it in, so bindings see a change.
    function editRules(fn) {
        const c = JSON.parse(JSON.stringify(rules))
        fn(c)
        rules = c
    }

    function splitPath(path) {
        const parts = path.split(".")
        const index = parseInt(parts.pop())
        return { parent: parts.join("."), index: index }
    }

    function setMatch(path, match) { editRules(t => { nodeAt(t, path).match = match }) }

    function setProp(path, key) {
        editRules(t => {
            const c = nodeAt(t, path)
            c.prop = key
            c.value = propOf(key).values[0].token
        })
    }

    function setNeg(path, neg) { editRules(t => { nodeAt(t, path).neg = neg }) }

    function setValue(path, token) { editRules(t => { nodeAt(t, path).value = token }) }

    // A new clause, or with `sub` a sub-filter, after the row at `path` (or at
    // the end of the top group when the path is empty).
    function addRule(path, sub) {
        editRules(t => {
            const made = sub ? newGroup() : newClause()
            if (path === "") { t.items.push(made); return }
            const at = splitPath(path)
            nodeAt(t, at.parent).items.splice(at.index + 1, 0, made)
        })
    }

    // Removes a clause or a sub-filter; a sub-filter left empty goes too.
    function removeRule(path) {
        editRules(t => {
            let at = splitPath(path)
            nodeAt(t, at.parent).items.splice(at.index, 1)
            while (at.parent !== "" && nodeAt(t, at.parent).items.length === 0) {
                at = splitPath(at.parent)
                nodeAt(t, at.parent).items.splice(at.index, 1)
            }
        })
    }

    // A row's human label from its index path: "0" is "Rule 1", "1.0" is
    // "Rule 2.1". The top group has no number, so its own label is "Match".
    function ruleLabel(path) {
        if (path === "") return "Match"
        return "Rule " + path.split(".").map(n => parseInt(n) + 1).join(".")
    }

    function flatRules(group, prefix, depth, out) {
        out.push({ kind: "group", path: prefix, depth: depth, match: group.match })
        group.items.forEach((item, i) => {
            const path = prefix === "" ? String(i) : prefix + "." + i
            if (item.items) flatRules(item, path, depth + 1, out)
            else out.push({ kind: "clause", path: path, depth: depth, clause: item })
        })
        return out
    }

    readonly property var ruleRows: flatRules(rules, "", 0, [])

    function reset() {
        loading = true
        freeText = ""
        rules = { match: "all", items: [] }
        names = { tags: {}, lists: {}, taggroups: {}, groups: {}, locations: {} }
        dates = { modified: { n: 0, unit: "d" }, created: { n: 0, unit: "d" }, completed: { n: 0, unit: "d" } }
        groups = []
        loading = false
        rebuild()
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

    function loadFrom(text) {
        loading = true
        reset()
        loading = true
        const st = { rules: null, names: { tags: {}, lists: {}, taggroups: {}, groups: {}, locations: {} },
                     dates: { modified: { n: 0, unit: "d" }, created: { n: 0, unit: "d" }, completed: { n: 0, unit: "d" } } }
        const extras = []
        const lifted = liftGroups(text || "")
        text = lifted.rest
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
                    // Only the first type: section becomes rows, and only when
                    // the rows can say all of it; anything else stays text.
                    const r = haveTypes ? null : backend.rulesFromType(value)
                    if (r && r.ok) {
                        haveTypes = true
                        st.rules = { match: r.match, items: r.items }
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
        rules = st.rules || { match: "all", items: [] }
        names = st.names
        dates = st.dates
        groups = lifted.groups
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
        Accessible.role: Accessible.Button
        Accessible.name: chip.label
        Accessible.onPressAction: chip.clicked()
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
        // Overrides the displayed label for the spoken name, for a control
        // whose text is only clear beside its neighbours (the Match buttons).
        property string accessibleName: label
        // A radio-like button in a group, as Match All/Any are.
        property bool radio: false
        property bool active: false
        property bool tall: false
        signal clicked()
        implicitWidth: segText.implicitWidth + 20
        implicitHeight: 26
        radius: 13
        opacity: enabled ? 1 : 0.4
        Accessible.role: seg.radio ? Accessible.RadioButton : Accessible.Button
        Accessible.name: seg.accessibleName
        Accessible.checkable: seg.radio
        Accessible.checked: seg.radio && seg.active
        Accessible.onPressAction: seg.clicked()
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

    // A drop-down trimmed to the height of a rule row.
    component RuleCombo: ComboBox {
        implicitHeight: 34
        topInset: 0
        bottomInset: 0
        topPadding: 0
        bottomPadding: 0
        font.pixelSize: backend.fontSize - 1
    }

    // The round [-] and [+] buttons of a rule row. A plain click emits
    // clicked(false), an Alt+click clicked(true); right-click asks for a menu.
    component RuleButton: Rectangle {
        id: rb
        property string symbol
        property string tip
        // What a screen reader calls the button; the tip is a longer help.
        property string accessibleName: tip
        property bool menu: false
        signal clicked(bool alt)
        signal menuRequested()
        implicitWidth: 26
        implicitHeight: 26
        radius: 13
        Accessible.role: Accessible.Button
        Accessible.name: accessibleName
        Accessible.onPressAction: rb.clicked(false)
        color: rbMouse.containsMouse ? theme.selection : theme.mix(theme.raised, theme.foreground, 0.07)
        Text {
            anchors.centerIn: parent
            text: rb.symbol
            color: theme.foreground
            font.pixelSize: backend.fontSize + 2
        }
        MouseArea {
            id: rbMouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: rb.menu ? Qt.LeftButton | Qt.RightButton : Qt.LeftButton
            onClicked: (e) => { if (e.button === Qt.RightButton) rb.menuRequested(); else rb.clicked(!!(e.modifiers & Qt.AltModifier)) }
        }
        ToolTip.visible: rbMouse.containsMouse && rb.tip !== ""
        ToolTip.delay: 600
        ToolTip.text: rb.tip
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
                objectName: "soundexBox"
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
                    objectName: "freeInput"
                    Accessible.name: "Contains words"
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

                Heading { text: "SEARCH PRESETS"; Layout.topMargin: 6 }
                Column {
                    Layout.fillWidth: true
                    spacing: 0

                    Repeater {
                        model: root.ruleRows
                        Item {
                            id: ruleRow
                            required property var modelData
                            readonly property bool isGroup: modelData.kind === "group"
                            readonly property string path: modelData.path
                            readonly property int indent: 18 * modelData.depth
                            // A rule-less builder shows just the Add Rule button.
                            visible: !(isGroup && path === "" && root.rules.items.length === 0)
                            width: parent.width
                            height: visible ? 40 : 0

                            // A sub-filter is a lightly tinted block with a bar down its left edge.
                            Repeater {
                                model: ruleRow.modelData.depth
                                Rectangle {
                                    required property int index
                                    x: 18 * index + 2
                                    width: ruleRow.width - x
                                    height: ruleRow.height
                                    color: Qt.alpha(theme.foreground, 0.035)
                                    Rectangle { width: 2; height: parent.height; color: theme.divider }
                                }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: ruleRow.indent + 10
                                anchors.rightMargin: 4
                                spacing: 6

                                // Group header: Match [All] [Any] of the following.
                                RowLayout {
                                    visible: ruleRow.isGroup
                                    Layout.fillWidth: true
                                    spacing: 6
                                    objectName: "ruleMatch:" + ruleRow.path
                                    Accessible.role: Accessible.Grouping
                                    Accessible.name: ruleRow.path === "" ? "Match" : "Match, " + root.ruleLabel(ruleRow.path).toLowerCase()
                                    Text {
                                        visible: ruleRow.isGroup
                                        text: "Match"
                                        color: theme.foreground
                                        font.pixelSize: backend.fontSize
                                    }
                                    Segment {
                                        visible: ruleRow.isGroup
                                        objectName: "ruleMatchAll:" + ruleRow.path
                                        label: "All"
                                        radio: true
                                        accessibleName: "All"
                                        active: ruleRow.modelData.match === "all"
                                        onClicked: root.setMatch(ruleRow.path, "all")
                                    }
                                    Segment {
                                        visible: ruleRow.isGroup
                                        objectName: "ruleMatchAny:" + ruleRow.path
                                        label: "Any"
                                        radio: true
                                        accessibleName: "Any"
                                        active: ruleRow.modelData.match === "any"
                                        onClicked: root.setMatch(ruleRow.path, "any")
                                    }
                                    Text {
                                        visible: ruleRow.isGroup
                                        text: "of the following"
                                        color: theme.foreground
                                        font.pixelSize: backend.fontSize
                                    }
                                    Item { visible: ruleRow.isGroup; Layout.fillWidth: true }
                                }
                                RuleButton {
                                    visible: ruleRow.isGroup && ruleRow.path !== ""
                                    objectName: "ruleGroupRemove:" + ruleRow.path
                                    symbol: "\u2212"
                                    tip: "Remove this sub-filter"
                                    accessibleName: "Remove " + root.ruleLabel(ruleRow.path).toLowerCase()
                                    onClicked: root.removeRule(ruleRow.path)
                                }

                                // Clause: Property, operator, value, and the [-] [+] buttons.
                                RuleCombo {
                                    id: propBox
                                    visible: !ruleRow.isGroup
                                    objectName: "ruleProp:" + ruleRow.path
                                    Accessible.name: root.ruleLabel(ruleRow.path) + " property"
                                    Layout.preferredWidth: 150
                                    Layout.fillWidth: true
                                    model: root.ruleProps
                                    textRole: "title"
                                    currentIndex: ruleRow.isGroup ? 0 : root.ruleProps.indexOf(root.propOf(ruleRow.modelData.clause.prop))
                                    onActivated: (i) => root.setProp(ruleRow.path, root.ruleProps[i].key)
                                }
                                RuleCombo {
                                    id: opBox
                                    visible: !ruleRow.isGroup
                                    objectName: "ruleOp:" + ruleRow.path
                                    Accessible.name: root.ruleLabel(ruleRow.path) + " operator"
                                    Layout.preferredWidth: 130
                                    Layout.fillWidth: true
                                    model: ruleRow.isGroup ? [] : root.propOf(ruleRow.modelData.clause.prop).ops
                                    currentIndex: !ruleRow.isGroup && ruleRow.modelData.clause.neg ? 1 : 0
                                    onActivated: (i) => root.setNeg(ruleRow.path, i === 1)
                                }
                                RuleCombo {
                                    id: valueBox
                                    // A single-valued property reads in full from its operator ("Note has a note").
                                    visible: !ruleRow.isGroup && !root.propOf(ruleRow.modelData.clause.prop).single
                                    objectName: "ruleValue:" + ruleRow.path
                                    Accessible.name: root.ruleLabel(ruleRow.path) + " value"
                                    Layout.preferredWidth: 150
                                    Layout.fillWidth: true
                                    model: ruleRow.isGroup ? [] : root.propOf(ruleRow.modelData.clause.prop).values
                                    textRole: "label"
                                    currentIndex: {
                                        if (ruleRow.isGroup) return 0
                                        const vs = root.propOf(ruleRow.modelData.clause.prop).values
                                        return Math.max(0, vs.findIndex(v => v.token === ruleRow.modelData.clause.value))
                                    }
                                    onActivated: (i) => root.setValue(ruleRow.path, root.propOf(ruleRow.modelData.clause.prop).values[i].token)
                                }
                                Item { visible: !ruleRow.isGroup && !valueBox.visible; Layout.fillWidth: true; Layout.preferredWidth: 150 }
                                RuleButton {
                                    visible: !ruleRow.isGroup
                                    objectName: "ruleRemove:" + ruleRow.path
                                    symbol: "\u2212"
                                    tip: "Remove this rule"
                                    accessibleName: "Remove " + root.ruleLabel(ruleRow.path).toLowerCase()
                                    onClicked: root.removeRule(ruleRow.path)
                                }
                                RuleButton {
                                    id: plusButton
                                    visible: !ruleRow.isGroup
                                    objectName: "ruleAdd:" + ruleRow.path
                                    symbol: "+"
                                    tip: "Add a rule below. Alt+click adds a sub-filter; right-click for both."
                                    accessibleName: "Add rule after " + root.ruleLabel(ruleRow.path).toLowerCase()
                                    menu: true
                                    onClicked: (alt) => root.addRule(ruleRow.path, alt)
                                    onMenuRequested: root.addMenuAt(plusButton, ruleRow.path)
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.topMargin: 4
                    Segment { objectName: "ruleAdd"; label: "+  Add Rule"; accessibleName: "Add rule"; onClicked: root.addRule("", false) }
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
                            objectName: "pickerTab:" + modelData.key
                            label: modelData.title + (root.pickerCount(index) ? " (" + root.pickerCount(index) + ")" : "")
                            accessibleName: modelData.title
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
                                    objectName: "nameChip:" + root.tabs[root.pickerTab].key + ":" + modelData.name
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

                Heading { text: "GROUPS"; Layout.topMargin: 6 }
                Text {
                    Layout.fillWidth: true
                    text: "A group combines whole filters, such as tags: home OR type: starred, inside parentheses."
                    color: theme.faint
                    font.pixelSize: backend.fontSize - 3
                    wrapMode: Text.Wrap
                }
                Repeater {
                    // One row per group; the rows read their group by index, so
                    // typing in one does not rebuild it and lose the cursor.
                    model: root.groups.length
                    RowLayout {
                        required property int index
                        readonly property var modelData: root.groups[index] || ({ join: "and", text: "" })
                        Layout.fillWidth: true
                        spacing: 6
                        Segment { objectName: "groupJoin:" + index + ":and"; label: "And"; active: modelData.join === "and"; onClicked: root.setGroup(index, { join: "and" }) }
                        Segment { objectName: "groupJoin:" + index + ":or"; label: "Or"; active: modelData.join === "or"; onClicked: root.setGroup(index, { join: "or" }) }
                        Segment { objectName: "groupJoin:" + index + ":not"; label: "Not"; active: modelData.join === "not"; onClicked: root.setGroup(index, { join: "not" }) }
                        TextField {
                            id: groupField
                            objectName: "groupField"
                            Layout.fillWidth: true
                            font.pixelSize: backend.fontSize
                            color: theme.foreground
                            text: modelData.text
                            placeholderText: "tags: home OR type: starred"
                            placeholderTextColor: theme.faint
                            background: Rectangle { radius: 8; color: theme.background; border.color: groupField.activeFocus ? theme.accent : theme.divider }
                            onTextEdited: root.setGroup(index, { text: text })
                            Keys.onReturnPressed: root.doSearch()
                        }
                        Segment { objectName: "removeGroup:" + index; label: "\u00d7"; accessibleName: "Remove group " + (index + 1); onClicked: root.removeGroup(index) }
                    }
                }
                RowLayout {
                    Segment { objectName: "addGroup"; label: "Add group"; onClicked: root.addGroup() }
                }

                Heading { text: "SEARCH SCOPE"; Layout.topMargin: 6 }
                RowLayout {
                    spacing: 6
                    Text { Layout.preferredWidth: 72; text: "Search in"; color: theme.foreground; font.pixelSize: backend.fontSize }
                    Segment { objectName: "scopeThis"; label: "This list"; accessibleName: "Search this list"; radio: true; active: !backend.searchAllLists; onClicked: backend.setSearchAllLists(false) }
                    Segment { objectName: "scopeAll"; label: "All lists"; accessibleName: "Search all lists"; radio: true; active: backend.searchAllLists; onClicked: backend.setSearchAllLists(true) }
                }
                RowLayout {
                    spacing: 6
                    Text { Layout.preferredWidth: 72; text: "Scheduled"; color: theme.foreground; font.pixelSize: backend.fontSize }
                    Segment {
                        objectName: "searchScheduledHide"
                        label: "Hide"
                        accessibleName: "Hide scheduled"
                        radio: true
                        active: root.editingListId ? !root.includeScheduled : !backend.showScheduled
                        onClicked: { if (root.editingListId) root.includeScheduled = false; else backend.showScheduled = false }
                    }
                    Segment {
                        objectName: "searchScheduledInclude"
                        label: "Include"
                        accessibleName: "Include scheduled"
                        radio: true
                        active: root.editingListId ? root.includeScheduled : backend.showScheduled
                        onClicked: { if (root.editingListId) root.includeScheduled = true; else backend.showScheduled = true }
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
                            objectName: "dateN:" + modelData.key
                            Accessible.name: "Within the last, " + modelData.title
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
                            objectName: "dateUnit:" + modelData.key + ":d"
                            label: "days"
                            accessibleName: modelData.title + " unit, days"
                            radio: true
                            active: root.dates[modelData.key].unit === "d"
                            onClicked: root.setDate(modelData.key, root.dates[modelData.key].n, "d")
                        }
                        Segment {
                            objectName: "dateUnit:" + modelData.key + ":w"
                            label: "weeks"
                            accessibleName: modelData.title + " unit, weeks"
                            radio: true
                            active: root.dates[modelData.key].unit === "w"
                            onClicked: root.setDate(modelData.key, root.dates[modelData.key].n, "w")
                        }
                    }
                }

                Heading { text: "SMART LIST OPTIONS"; Layout.topMargin: 6 }
                RowLayout {
                    spacing: 6
                    Text { Layout.preferredWidth: 72; text: "Due date"; color: theme.foreground; font.pixelSize: backend.fontSize }
                    Segment { objectName: "rangeNone"; label: "Any"; accessibleName: "Due date, any"; radio: true; active: root.rangeKind === "none"; onClicked: root.rangeKind = "none" }
                    Segment { objectName: "rangeFixed"; label: "Between"; accessibleName: "Due date, between"; radio: true; active: root.rangeKind === "fixed"; onClicked: root.rangeKind = "fixed" }
                    Segment { objectName: "rangeNext"; label: "In the next"; accessibleName: "Due date, in the next"; radio: true; active: root.rangeKind === "next"; onClicked: root.rangeKind = "next" }
                    Segment { objectName: "rangeNoDue"; label: "No due date"; accessibleName: "Due date, no due date"; radio: true; active: root.rangeKind === "nodue"; onClicked: root.rangeKind = "nodue" }
                }
                RowLayout {
                    visible: root.rangeKind === "fixed"
                    spacing: 6
                    Item { Layout.preferredWidth: 72 }
                    TextField {
                        id: fromField
                        objectName: "rangeFrom"
                        Accessible.name: "Due date, from"
                        Layout.preferredWidth: 120
                        placeholderText: "yyyy-mm-dd"
                        text: root.rangeFrom
                        font.pixelSize: backend.fontSize
                        color: root.rangeFrom && !root.isoOk(root.rangeFrom) ? theme.overdue : theme.foreground
                        background: Rectangle { radius: 8; color: theme.background; border.color: fromField.activeFocus ? theme.accent : theme.divider }
                        onTextEdited: root.rangeFrom = text.trim()
                    }
                    Text { text: "to"; color: theme.muted; font.pixelSize: backend.fontSize }
                    TextField {
                        id: toField
                        objectName: "rangeTo"
                        Accessible.name: "Due date, to"
                        Layout.preferredWidth: 120
                        placeholderText: "yyyy-mm-dd"
                        text: root.rangeTo
                        font.pixelSize: backend.fontSize
                        color: root.rangeTo && !root.isoOk(root.rangeTo) ? theme.overdue : theme.foreground
                        background: Rectangle { radius: 8; color: theme.background; border.color: toField.activeFocus ? theme.accent : theme.divider }
                        onTextEdited: root.rangeTo = text.trim()
                    }
                }
                RowLayout {
                    visible: root.rangeKind === "next"
                    spacing: 6
                    Item { Layout.preferredWidth: 72 }
                    TextField {
                        id: nextField
                        objectName: "rangeN"
                        Accessible.name: "Due date, number"
                        Layout.preferredWidth: 60
                        horizontalAlignment: Text.AlignHCenter
                        font.pixelSize: backend.fontSize
                        color: theme.foreground
                        validator: IntValidator { bottom: 1; top: 999 }
                        inputMethodHints: Qt.ImhDigitsOnly
                        text: String(root.rangeN)
                        background: Rectangle { radius: 8; color: theme.background; border.color: nextField.activeFocus ? theme.accent : theme.divider }
                        onTextEdited: root.rangeN = parseInt(text) || 0
                    }
                    Repeater {
                        model: [["d", "days"], ["w", "weeks"], ["m", "months"], ["y", "years"]]
                        Segment {
                            required property var modelData
                            objectName: "rangeUnit:" + modelData[0]
                            label: modelData[1]
                            accessibleName: "Due date unit, " + modelData[1]
                            radio: true
                            active: root.rangeUnit === modelData[0]
                            onClicked: root.rangeUnit = modelData[0]
                        }
                    }
                }
                RowLayout {
                    spacing: 6
                    Text { Layout.preferredWidth: 72; text: "Completed"; color: theme.foreground; font.pixelSize: backend.fontSize }
                    Segment { objectName: "completedDefault"; label: "Default"; accessibleName: "Completed, default"; radio: true; active: root.completedMode === 0; onClicked: root.completedMode = 0 }
                    Segment { objectName: "completedInclude"; label: "Include"; accessibleName: "Completed, include"; radio: true; active: root.completedMode === 1; onClicked: root.completedMode = 1 }
                    Segment { objectName: "completedExclude"; label: "Exclude"; accessibleName: "Completed, exclude"; radio: true; active: root.completedMode === 2; onClicked: root.completedMode = 2 }
                }
                Text {
                    Layout.fillWidth: true
                    text: "The due date range and Completed switch apply to the Smart List you save or update; a plain search ignores them."
                    color: theme.faint
                    font.pixelSize: backend.fontSize - 3
                    wrapMode: Text.Wrap
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
                        Accessible.name: "Search query"
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
                    label: root.editingListId ? "Save as New\u2026" : "Save as Smart List\u2026"
                    enabled: root.query.length > 0 || root.range !== ""
                    onClicked: { nameInput.text = ""; nameDialog.open(); nameInput.forceActiveFocus() }
                }
                Segment {
                    objectName: "updateButton"
                    visible: root.editingListId > 0
                    label: "Update Smart List"
                    active: true
                    enabled: root.query.length > 0 || root.range !== ""
                    onClicked: root.updateList()
                }
                Segment {
                    objectName: "searchButton"
                    visible: !root.editingListId
                    label: "Search"
                    active: true
                    onClicked: root.doSearch()
                }
            }
        }
    }

    // Right-click on a [+]: the two things click and Alt+click do.
    function addMenuAt(button, path) {
        addMenu.path = path
        addMenu.popup(button, 0, button.height)
    }

    ScrollingMenu {
        id: addMenu
        property string path: ""
        MenuItem { objectName: "ruleAddMenuRule"; text: "Add Rule"; onTriggered: root.addRule(addMenu.path, false) }
        MenuItem { objectName: "ruleAddMenuSub"; text: "Add Sub-filter"; onTriggered: root.addRule(addMenu.path, true) }
    }

    function doSearch() {
        backend.search = query
        close()
    }

    function saveAs(name) {
        const n = name.trim()
        if (!n || (!query && !range)) return
        backend.saveSmartList(n, query, range, completedMode, includeScheduled)
        nameDialog.close()
        close()
    }

    function updateList() {
        if (!editingListId || (!query && !range)) return
        backend.updateSmartList(editingListId, "", query, range, completedMode, includeScheduled)
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
