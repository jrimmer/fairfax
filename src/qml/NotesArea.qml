import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Plain
import Precrime 1.0

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

    color: theme.notes
    implicitHeight: Math.max(84, (preview ? previewText.implicitHeight : area.implicitHeight) + 16)

    function commit() { if (area.text !== root.text) committed(area.text) }
    function insert(position, string) { area.insert(position, string) }
    function forceEditorFocus() { preview = false; area.forceActiveFocus() }

    onTextChanged: if (area.text !== text) area.text = text
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
    }

    Column {
        x: 12
        y: 10
        spacing: 10
        Icon { name: "note"; size: 15; color: theme.faint }
        Icon {
            name: "markdown"
            size: 15
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
            name: "insertDate"
            size: 15
            color: theme.faint
            opacity: root.preview ? 0.35 : 1
            MouseArea {
                anchors.fill: parent
                anchors.margins: -3
                enabled: !root.preview
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    area.insert(area.cursorPosition, Qt.formatDateTime(new Date(), "ddd d MMM yyyy, h:mm AP") + " ")
                    area.forceActiveFocus()
                }
            }
        }
        Icon {
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
        background: Item {}
        selectionColor: theme.selection

        onActiveFocusChanged: if (!activeFocus) { linkPicker.close(); root.commit() }
        Keys.onEscapePressed: {
            if (linkPicker.opened) { linkPicker.close(); return }
            root.commit()
            root.escaped()
        }

        // Typing "@{" starts a task link: the picker follows what comes
        // after it, and the link is completed with the chosen title.
        onTextChanged: linkPicker.sync()
        onCursorPositionChanged: linkPicker.sync()
        Keys.onPressed: (e) => {
            if (!linkPicker.opened) return
            if (e.key === Qt.Key_Down) linkPicker.move(1)
            else if (e.key === Qt.Key_Up) linkPicker.move(-1)
            else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Tab) {
                if (!linkPicker.choose()) return
            } else return
            e.accepted = true
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
        text: area.text.length ? area.text : " "
        color: theme.foreground
        linkColor: theme.today
        font.pixelSize: root.fontSize
        onLinkActivated: (link) => root.linkActivated(link, link.indexOf("@") > 0 && link.indexOf(":") < 0 ? "email" : "url")
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
