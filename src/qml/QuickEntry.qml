import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Controls.Basic as Plain
import QtQuick.Layouts

// Quick Entry: a small floating window for capturing a task from anywhere,
// summoned with `precrime --quick` from a Hyprland bind. Dates, #tags,
// /list/, !!, *, repeats and [durations] in the title are recognised as you
// type and shown as capsules. Return saves; Shift+Return saves and starts the
// next one (as does pressing the bind again); Ctrl+Return saves and opens the
// task in the main window; Escape cancels.
Window {
    id: root

    title: "Precrime Quick Entry"
    width: 600
    height: body.implicitHeight + 28
    minimumWidth: 420
    color: theme.raised
    flags: Qt.Window | Qt.WindowStaysOnTopHint

    Material.theme: theme.dark ? Material.Dark : Material.Light
    Material.accent: theme.accent
    Material.background: theme.raised
    Material.foreground: theme.foreground

    property int type: 0
    property var preview: ({ chips: [], spans: [] })
    readonly property var kindColors: ({
        date: theme.today, due: theme.today, start: theme.start, time: theme.today, repeat: theme.duration,
        alert: theme.priorityMedium, duration: theme.duration, priority: theme.priorityHigh, star: theme.star,
        tag: theme.muted, list: theme.accent, location: theme.start
    })

    function summon() {
        if (visible && title_.text.trim()) { save(false); return }
        reset()
        show()
        raise()
        requestActivate()
        title_.forceActiveFocus()
    }

    function reset() {
        title_.text = ""
        notes.text = ""
        type = 0
        listBox.currentIndex = Math.max(0, backend.listChoices.findIndex(l => l.id === backend.quickEntryListId()))
        preview = { chips: [], spans: [] }
        // Like the reference app, the clipboard's text lands in the notes, ready to keep or clear.
        if (backend.setting("quickEntry.clipboard").value !== "false") {
            clip.text = ""
            clip.paste()
            notes.text = clip.text.trim().length < 2000 ? clip.text.trim() : ""
        }
    }

    function save(openAfter) {
        if (!title_.text.trim()) return
        const list = backend.listChoices[listBox.currentIndex]
        const id = backend.quickAdd(title_.text, notes.text, type, list ? list.id : 0)
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

    TextEdit { id: clip; visible: false }

    Shortcut { sequence: "Escape"; onActivated: root.hide() }
    Shortcut { sequence: "Ctrl+Return"; onActivated: root.save(true) }
    Shortcut { sequence: "Shift+Return"; onActivated: root.save(false) }

    ColumnLayout {
        id: body
        x: 14
        y: 14
        width: parent.width - 28
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ComboBox {
                model: ["Task", "Project", "Checklist"]
                currentIndex: root.type
                onActivated: (i) => root.type = i
                implicitHeight: 34
                font.pixelSize: backend.fontSize - 1
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                implicitWidth: 7
                implicitHeight: 18
                radius: 3.5
                color: theme.listColor(backend.listChoices[listBox.currentIndex] ? backend.listChoices[listBox.currentIndex].color : 0)
            }
            ComboBox {
                id: listBox
                model: backend.listChoices
                textRole: "name"
                implicitHeight: 34
                implicitWidth: 180
                font.pixelSize: backend.fontSize - 1
            }
        }

        // The title, with recognised phrases drawn as capsules behind the text.
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
                onTextChanged: root.preview = text.trim() ? backend.previewEntry(text) : { chips: [], spans: [] }
                Keys.onReturnPressed: (e) => {
                    if (e.modifiers & Qt.ControlModifier) root.save(true)
                    else if (e.modifiers & Qt.ShiftModifier) root.save(false)
                    else { root.save(false); root.hide() }
                }
                Keys.onEnterPressed: { root.save(false); root.hide() }
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 6
            visible: (root.preview.chips || []).length > 0
            Repeater {
                model: root.preview.chips || []
                Rectangle {
                    required property var modelData
                    width: chipLabel.implicitWidth + 16
                    height: 22
                    radius: 11
                    color: Qt.alpha(root.kindColors[modelData.kind] || theme.accent, 0.18)
                    Text {
                        id: chipLabel
                        anchors.centerIn: parent
                        text: modelData.label
                        color: root.kindColors[modelData.kind] || theme.foreground
                        font.pixelSize: backend.fontSize - 2
                        font.weight: Font.DemiBold
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 80
            radius: 8
            color: theme.notes
            Plain.TextArea {
                id: notes
                anchors.fill: parent
                anchors.margins: 4
                wrapMode: TextEdit.Wrap
                placeholderText: "Notes"
                placeholderTextColor: theme.faint
                color: theme.foreground
                font.pixelSize: backend.fontSize - 1
                background: Item {}
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Return saves · Shift+Return saves and adds another · Ctrl+Return saves and opens"; color: theme.faint; font.pixelSize: backend.fontSize - 3; Layout.fillWidth: true; elide: Text.ElideRight }
            Button { text: "Cancel"; flat: true; onClicked: root.hide() }
            Button { text: "Save"; highlighted: true; enabled: title_.text.trim() !== ""; onClicked: { root.save(false); root.hide() } }
        }
    }
}
