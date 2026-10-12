import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// the reference app's Edit List sheet: name, colour, group, which Focus Lists the list
// feeds, how completed tasks show, and muted alerts. Smart Lists only have a
// name and a colour here; their rules are edited in the search builder.
ThemedDialog {
    id: root

    property int listId: 0
    property int chosenColor: 0
    property var groups: []
    readonly property bool isSmart: info.kind === 2
    readonly property bool isInbox: info.kind === 1
    property var info: ({})
    // The sidebar owns the colour picker and sets chosenColor from it.
    signal customColorRequested(color initial)

    function edit(id) {
        listId = id
        info = backend.listInfo(id)
        groups = backend.groupChoices()
        chosenColor = info.color
        nameInput.text = info.name
        groupBox.currentIndex = Math.max(0, groups.findIndex(g => g.id === info.groupId) + 1)
        allBox.checked = info.showInAll
        todayBox.checked = info.showInToday
        doneBox.checked = info.showInDone
        completedBox.currentIndex = info.completedMode
        muteBox.checked = info.muteAlerts
        open()
        nameInput.forceActiveFocus()
        nameInput.selectAll()
    }

    function apply() {
        const name = nameInput.text.trim()
        if (name) backend.renameList(listId, name)
        if (chosenColor !== info.color) backend.setListColor(listId, chosenColor)
        if (isSmart)
            return
        if (!isInbox) {
            const groupId = groupBox.currentIndex > 0 ? groups[groupBox.currentIndex - 1].id : 0
            if (groupId !== info.groupId) backend.moveList(listId, 0, groupId)
        }
        if (allBox.checked !== info.showInAll) backend.setListFlag(listId, "all", allBox.checked)
        if (todayBox.checked !== info.showInToday) backend.setListFlag(listId, "today", todayBox.checked)
        if (doneBox.checked !== info.showInDone) backend.setListFlag(listId, "done", doneBox.checked)
        if (completedBox.currentIndex !== info.completedMode) backend.setListCompletedMode(listId, completedBox.currentIndex)
        if (muteBox.checked !== info.muteAlerts) backend.setListFlag(listId, "muteAlerts", muteBox.checked)
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    title: "Edit List"
    width: Math.min(400, (parent ? parent.width : 440) - 40)
    standardButtons: Dialog.Cancel | Dialog.Ok
    onAccepted: apply()

    contentItem: ColumnLayout {
        spacing: 10

        TextField {
            id: nameInput
            Layout.fillWidth: true
            placeholderText: "Name"
            maximumLength: 200
            onAccepted: { root.apply(); root.close() }
        }

        // The palette as a row of swatches, then a custom colour.
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: theme.listPalette
                Rectangle {
                    required property int index
                    width: 22; height: 22; radius: 11
                    color: theme.listColor(index)
                    border.width: root.chosenColor === index ? 3 : 0
                    border.color: theme.foreground
                    MouseArea { anchors.fill: parent; onClicked: root.chosenColor = index }
                }
            }
            Rectangle {
                readonly property bool custom: root.chosenColor >= 0x1000000
                width: 22; height: 22; radius: 11
                color: custom ? theme.listColor(root.chosenColor) : "transparent"
                border.width: custom ? 3 : 1
                border.color: custom ? theme.foreground : theme.muted
                Text {
                    anchors.centerIn: parent
                    visible: !parent.custom
                    text: "…"
                    color: theme.muted
                    font.pixelSize: backend.fontSize
                }
                MouseArea { anchors.fill: parent; onClicked: root.customColorRequested(theme.listColor(root.chosenColor)) }
            }
        }

        RowLayout {
            visible: !root.isSmart && !root.isInbox
            Label { text: "Group"; Layout.preferredWidth: 90 }
            ComboBox {
                id: groupBox
                Layout.fillWidth: true
                model: [{ name: "None" }].concat(root.groups)
                textRole: "name"
            }
        }
        RowLayout {
            visible: !root.isSmart
            Label { text: "Completed"; Layout.preferredWidth: 90 }
            ComboBox {
                id: completedBox
                Layout.fillWidth: true
                model: ["Follow the default", "Always include", "Always exclude"]
            }
        }
        ColumnLayout {
            visible: !root.isSmart
            spacing: 0
            CheckBox { id: allBox; text: "Include in All" }
            CheckBox { id: todayBox; text: "Include in Today" }
            CheckBox { id: doneBox; text: "Include in Done" }
            CheckBox { id: muteBox; text: "Mute alerts" }
        }
    }
}
