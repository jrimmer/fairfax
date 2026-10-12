import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// the reference app's export panel: tick the lists to write out, say which tasks (All, Not
// Done, Done) and pick the format; the save panel follows. Main and Settings
// each have one, so `owner` keeps them from both answering the file picker.
Dialog {
    id: root

    property string owner: "main"
    // The ids of the ticked lists.
    property var chosen: ({})
    readonly property var lists: backend.listChoices
    readonly property int count: lists.filter(l => chosen[l.id]).length

    function show() {
        const all = {}
        for (const l of lists) all[l.id] = true
        chosen = all
        open()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    title: "Export Tasks"
    width: Math.min(420, (parent ? parent.width : 460) - 40)
    standardButtons: Dialog.Cancel | Dialog.Ok
    padding: 20
    topPadding: 4
    background: Rectangle { color: theme.raised; radius: 12; border.color: theme.divider }
    header: Label {
        text: root.title
        padding: 20
        bottomPadding: 8
        color: theme.foreground
        font.pixelSize: backend.fontSize + 4
        font.weight: Font.DemiBold
    }
    Component.onCompleted: standardButton(Dialog.Ok).text = "Export…"
    onAboutToShow: standardButton(Dialog.Ok).enabled = Qt.binding(() => root.count > 0)

    onAccepted: {
        const ids = root.lists.filter(l => root.chosen[l.id]).map(l => l.id)
        const ext = format.currentIndex === 0 ? "csv" : "ics"
        filePicker.save(format.currentIndex === 0 ? "Export tasks as CSV" : "Export tasks as iCalendar",
                        "Precrime." + ext, "export:" + root.owner + ":" + ext + ":" + scope.currentIndex + ":" + ids.join(","))
    }

    Connections {
        target: filePicker
        function onPicked(path, purpose) {
            const parts = purpose.split(":")
            if (parts[0] !== "export" || parts[1] !== root.owner) return
            backend.exportLists(path, parts[2], parts[4] ? parts[4].split(",").map(Number) : [], Number(parts[3]))
        }
    }

    contentItem: ColumnLayout {
        spacing: 8
        Label { text: "Select lists to export:"; color: theme.muted }
        CheckBox {
            text: "Select all"
            tristate: false
            checked: root.count === root.lists.length
            onToggled: {
                const next = {}
                if (checked) for (const l of root.lists) next[l.id] = true
                root.chosen = next
            }
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(220, root.lists.length * 36 + 4)
            clip: true
            Column {
                width: parent.width
                Repeater {
                    model: root.lists
                    delegate: CheckBox {
                        required property var modelData
                        text: modelData.name
                        checked: !!root.chosen[modelData.id]
                        onToggled: {
                            const next = Object.assign({}, root.chosen)
                            next[modelData.id] = checked
                            root.chosen = next
                        }
                    }
                }
            }
        }
        RowLayout {
            Label { text: "Tasks to export"; Layout.preferredWidth: 130 }
            ComboBox { id: scope; model: ["All", "Not Done", "Done"]; Layout.fillWidth: true }
        }
        RowLayout {
            Label { text: "Format"; Layout.preferredWidth: 130 }
            ComboBox { id: format; model: ["Spreadsheet (.csv)", "Calendar file (.ics)"]; Layout.fillWidth: true }
        }
    }
}
