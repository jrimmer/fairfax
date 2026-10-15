import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Plain
import Fairfax 1.0

// The notes editor: a Markdown-styled text area with the reference app's left chip column
// (note, Markdown preview, insert date, open links). The text is always the
// Markdown source; the preview is a separate read-only rendering.
Rectangle {
    id: root

    // The notes to edit. Setting it replaces the editor's content; edits flow
    // out through committed() rather than back into this property, so a parent
    // binding is never broken.
    property string text
    property real fontSize: 14
    // What a screen reader calls the notes field.
    property string accessibleName: "Notes"
    // The task being edited: a link never resolves to its own task, and a task
    // in the same list is preferred when two share a title.
    property real taskId: 0
    readonly property string currentText: area.text
    readonly property bool previewing: preview

    // Emitted with the edited text when focus leaves the area or Esc is pressed.
    signal committed(string text)
    signal escaped()
    // A link was Ctrl+clicked or chosen from the menu. kind is "url", "email",
    // "file" or "task"; for a task target is the task title.
    signal linkActivated(string target, string kind)

    property bool preview: false
    readonly property var detectedLinks: highlighter.links(area.text)
    readonly property alias cursorPosition: area.cursorPosition
    // Bumped when tasks change, so links re-check whether their target exists.
    property int linkRevision: 0
    // Titles of @{links} that match no task; the highlighter draws them struck through.
    readonly property var unresolvedTitles: {
        root.linkRevision
        const dead = []
        for (const l of root.detectedLinks)
            if (l.kind === "task" && !backend.resolveTaskLink(l.target, root.taskId) && dead.indexOf(l.target) < 0) dead.push(l.target)
        return dead
    }
    // Set when typing or pasting ran into the 500 KB notes limit.
    property bool limitHit: false

    color: theme.notes
    // The chip column sits below the first line, so it also sets the least height:
    // otherwise the open-links chip is cut off by the editor card on short notes.
    implicitHeight: Math.max(84, chips.y + chips.height + 8, (preview ? previewText.implicitHeight : area.implicitHeight) + 16)

    function commit() { if (area.text !== root.text) committed(area.text) }
    function insert(position, string) { area.insert(position, string) }
    function forceEditorFocus() { preview = false; area.forceActiveFocus() }
    // Copies every link in the notes, one per line.
    function copyLinks() {
        const n = root.detectedLinks.length
        backend.copyText(highlighter.linksAsText(area.text), n === 0 ? "No links in these notes." : n === 1 ? "Copied 1 link" : "Copied " + n + " links")
    }
    // Inserts a date or time stamp at the caret, replacing the selection.
    function insertStamp(format) {
        if (area.selectionStart !== area.selectionEnd) area.remove(area.selectionStart, area.selectionEnd)
        area.insert(area.cursorPosition, highlighter.stamp(format) + " ")
        area.forceActiveFocus()
    }

    onTextChanged: if (area.text !== text) area.text = text

    Connections {
        target: backend
        function onDataChanged() { root.linkRevision++ }
    }
    Timer { id: limitTimer; interval: 5000; onTriggered: root.limitHit = false }
    Component.onCompleted: area.text = text

    NotesHighlighter {
        id: highlighter
        document: area.textDocument
        text: theme.foreground
        muted: theme.faint
        accent: theme.accent
        code: theme.today
        link: theme.today
        heading: theme.foreground
        unresolved: root.unresolvedTitles
    }

    Column {
        id: chips
        x: 12
        y: 10
        spacing: 10
        Icon { name: "note"; size: 15; color: theme.faint }
        Icon {
            objectName: "notesPreviewToggle"
            name: "markdown"
            size: 15
            Accessible.role: Accessible.Button
            Accessible.name: root.preview ? "Edit notes" : "Preview notes"
            Accessible.onPressAction: {
                if (!root.preview) root.commit()
                root.preview = !root.preview
            }
            color: root.preview ? theme.accent : theme.faint
            MouseArea {
                anchors.fill: parent
                anchors.margins: -3
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    // Commit on the way in: preview clears the edit session.
                    if (!root.preview) root.commit()
                    root.preview = !root.preview
                }
            }
        }
        Icon {
            objectName: "notesStamp"
            name: "insertDate"
            size: 15
            Accessible.role: Accessible.Button
            Accessible.name: "Insert date or time"
            Accessible.onPressAction: if (!root.preview) stampMenu.popup()
            color: theme.faint
            opacity: root.preview ? 0.35 : 1
            MouseArea {
                anchors.fill: parent
                anchors.margins: -3
                enabled: !root.preview
                cursorShape: Qt.PointingHandCursor
                onClicked: stampMenu.popup()
            }
        }
        Icon {
            objectName: "notesLinks"
            name: "url"
            size: 15
            color: theme.faint
            visible: root.detectedLinks.length > 0
            opacity: root.preview ? 0.35 : 1
            MouseArea {
                anchors.fill: parent
                anchors.margins: -3
                enabled: !root.preview
                cursorShape: Qt.PointingHandCursor
                onClicked: linksMenu.popup()
            }
        }
    }

    Plain.TextArea {
        id: area
        objectName: "notesText"
        Accessible.name: root.accessibleName
        x: 40
        y: 2
        width: parent.width - 52
        visible: !root.preview
        wrapMode: TextEdit.Wrap
        placeholderText: "Notes"
        placeholderTextColor: theme.faint
        color: theme.foreground
        font.pixelSize: root.fontSize
        selectByMouse: true
        // The selection survives the menus that take focus, so a stamp replaces it.
        persistentSelection: true
        background: Item {}
        selectionColor: theme.selection

        onActiveFocusChanged: if (!activeFocus) { linkPicker.close(); root.commit() }
        // Notes is the last field of the editor: Tab and Shift+Tab step out to
        // the next and previous control in the window's chain, so the editor
        // never traps the keyboard.
        Keys.onTabPressed: (e) => { e.accepted = true; const n = area.nextItemInFocusChain(true); if (n) n.forceActiveFocus(Qt.TabFocusReason) }
        Keys.onBacktabPressed: (e) => { e.accepted = true; const p = area.nextItemInFocusChain(false); if (p) p.forceActiveFocus(Qt.BacktabFocusReason) }
        Keys.onEscapePressed: {
            if (linkPicker.opened) { linkPicker.close(); return }
            root.commit()
            root.escaped()
        }

        // Typing "@{" starts a task link: the picker follows what comes
        // after it, and the link is completed with the chosen title.
        onTextChanged: {
            linkPicker.sync()
            // Notes stop at 500 KB. Short text can't be near that, so the exact
            // size is only worked out for long notes.
            if (length > 170000) {
                const kept = highlighter.clamp(text)
                if (kept.length < length) {
                    remove(kept.length, length)
                    root.limitHit = true
                    limitTimer.restart()
                }
            }
        }
        onCursorPositionChanged: linkPicker.sync()
        Keys.onPressed: (e) => {
            const enter = e.key === Qt.Key_Return || e.key === Qt.Key_Enter
            if (linkPicker.opened) {
                if (e.key === Qt.Key_Down) linkPicker.move(1)
                else if (e.key === Qt.Key_Up) linkPicker.move(-1)
                else if (enter || e.key === Qt.Key_Tab) {
                    if (!linkPicker.choose()) return
                } else return
                e.accepted = true
                return
            }
            // Return at the end of a list item starts the next one; on an empty
            // item it ends the list.
            if (enter && !(e.modifiers & (Qt.ControlModifier | Qt.ShiftModifier | Qt.AltModifier)) && selectionStart === selectionEnd) {
                const edit = highlighter.listContinuation(text, cursorPosition)
                if (edit.action === "continue") insert(cursorPosition, edit.insert)
                else if (edit.action === "end") { remove(edit.from, edit.to); cursorPosition = edit.from }
                else return
                e.accepted = true
            }
        }

        // The unresolved task link under the pointer, if any, for the tooltip.
        readonly property string deadLink: {
            if (!hover.hovered) return ""
            const l = highlighter.linkAt(text, positionAt(hover.point.position.x, hover.point.position.y))
            return l.kind === "task" && root.unresolvedTitles.indexOf(l.target) >= 0 ? l.target : ""
        }
        ToolTip.visible: deadLink !== ""
        ToolTip.delay: 300
        ToolTip.text: "No task named “" + deadLink + "”"

        // Right-click: the usual edit commands plus the notes extras.
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: contextMenu.popup()
        }

        // Ctrl+click follows a link. A TapHandler is passive, so ordinary
        // clicks and selection keep working underneath it.
        TapHandler {
            acceptedModifiers: Qt.ControlModifier
            onTapped: (point) => {
                const l = highlighter.linkAt(area.text, area.positionAt(point.position.x, point.position.y))
                if (l.kind) root.linkActivated(l.target, l.kind)
            }
        }
        HoverHandler {
            id: hover
            cursorShape: {
                if (!(point.modifiers & Qt.ControlModifier)) return Qt.IBeamCursor
                return highlighter.linkAt(area.text, area.positionAt(point.position.x, point.position.y)).kind
                    ? Qt.PointingHandCursor : Qt.IBeamCursor
            }
        }
    }

    // The task-link picker: recent tasks, narrowed by the title typed after "@{".
    Popup {
        id: linkPicker
        objectName: "linkPicker"
        property int anchorAt: -1
        property var hits: []
        property int index: 0
        readonly property rect caret: area.cursorRectangle

        parent: area
        x: Math.min(caret.x, area.width - width)
        y: caret.y + caret.height + 2
        width: Math.min(320, area.width)
        padding: 4
        focus: false
        closePolicy: Popup.NoAutoClose

        // Opens, refreshes or closes the picker to match the text before the caret.
        function sync() {
            const before = area.text.substring(0, area.cursorPosition)
            const at = before.lastIndexOf("@{")
            const query = at < 0 ? "" : before.substring(at + 2)
            if (at < 0 || query.indexOf("}") >= 0 || query.indexOf("\n") >= 0 || root.preview) { close(); anchorAt = -1; return }
            anchorAt = at
            hits = backend.findTasks(query, 8)
            index = 0
            if (hits.length) open(); else close()
        }
        function move(step) { index = (index + step + hits.length) % hits.length }
        // Replaces "@{query" with the finished link; false when nothing is chosen.
        function choose() {
            if (!hits.length) return false
            const title = hits[index].title.replace(/[{}\n]/g, " ").trim()
            const end = area.cursorPosition
            const start = anchorAt
            close()
            anchorAt = -1
            area.remove(start, end)
            area.insert(start, "@{" + title + "}")
            area.cursorPosition = start + title.length + 3
            return true
        }

        background: Rectangle { color: theme.raised; radius: 6; border.color: theme.divider }
        contentItem: ListView {
            implicitHeight: Math.min(contentHeight, 8 * 30)
            model: linkPicker.hits
            clip: true
            delegate: Rectangle {
                required property var modelData
                required property int index
                objectName: "linkPickerRow:" + index
                width: ListView.view.width
                height: 30
                radius: 4
                color: index === linkPicker.index ? Qt.alpha(theme.accent, 0.25) : "transparent"
                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    text: parent.modelData.title + "  ·  " + parent.modelData.listName
                    color: theme.foreground
                    font.pixelSize: root.fontSize - 1
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: { linkPicker.index = parent.index; linkPicker.choose(); area.forceActiveFocus() }
                }
            }
        }
    }

    // The rendered view. Links in it open through the same signal.
    Text {
        id: previewText
        x: 40
        y: 10
        width: parent.width - 52
        visible: root.preview
        wrapMode: Text.Wrap
        textFormat: Text.MarkdownText
        text: area.text.length ? highlighter.previewSource(area.text) : " "
        color: theme.foreground
        linkColor: theme.today
        font.pixelSize: root.fontSize
        onLinkActivated: (link) => {
            if (link.startsWith("fairfax-task:")) root.linkActivated(decodeURIComponent(link.substring(14)), "task")
            else root.linkActivated(link, link.indexOf("@") > 0 && link.indexOf(":") < 0 ? "email" : "url")
        }
    }

    // Shown when typing or pasting hit the notes size limit.
    Rectangle {
        visible: root.limitHit
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 6
        width: limitText.implicitWidth + 16
        height: limitText.implicitHeight + 8
        radius: 6
        color: theme.raised
        border.color: theme.overdue
        Text {
            id: limitText
            anchors.centerIn: parent
            text: "Notes are limited to 500 KB"
            color: theme.overdue
            font.pixelSize: root.fontSize - 2
        }
    }

    // Insert Date & Time: the stamp formats, each with today's example.
    Menu {
        id: stampMenu
        onAboutToShow: stamps.model = highlighter.stampFormats()
        Instantiator {
            id: stamps
            delegate: MenuItem {
                required property var modelData
                required property int index
                text: modelData.name + "\t" + modelData.example
                onTriggered: root.insertStamp(index)
            }
            onObjectAdded: (index, object) => stampMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => stampMenu.removeItem(object)
        }
    }

    Menu {
        id: contextMenu
        MenuItem { text: "Cut"; enabled: area.selectedText !== ""; onTriggered: area.cut() }
        MenuItem { text: "Copy"; enabled: area.selectedText !== ""; onTriggered: area.copy() }
        MenuItem { text: "Paste"; onTriggered: area.paste() }
        MenuItem { text: "Select All"; onTriggered: area.selectAll() }
        MenuSeparator {}
        MenuItem { text: "Insert Time Stamp…"; onTriggered: stampMenu.popup() }
        MenuItem { text: "Copy Links from Notes"; enabled: root.detectedLinks.length > 0; onTriggered: root.copyLinks() }
    }

    Menu {
        id: linksMenu
        MenuItem {
            text: "Open Links (" + root.detectedLinks.length + ")"
            enabled: false
        }
        Instantiator {
            model: root.detectedLinks
            delegate: MenuItem {
                required property var modelData
                text: (modelData.kind === "task" ? "Task: " : modelData.kind === "email" ? "Mail: " : modelData.kind === "file" ? "File: " : "")
                      + modelData.text
                // Hovering a row shows where the link sits in the notes.
                onHighlightedChanged: if (highlighted) area.select(modelData.start, modelData.start + modelData.length)
                onTriggered: root.linkActivated(modelData.target, modelData.kind)
            }
            onObjectAdded: (index, object) => linksMenu.insertItem(index + 1, object)
            onObjectRemoved: (index, object) => linksMenu.removeItem(object)
        }
        onClosed: area.deselect()
    }
}
