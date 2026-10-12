import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Basic as Plain
import "Icons.js" as Icons

// The inline task editor, opened in place of the row (Return or a
// double-click). Its anatomy follows the reference app: title row; a ribbon in the list's
// colour with the list picker, the due hint and the attribute buttons; tags
// beside the Start and Due fields; the notes area; and a meta line above.
Item {
    id: root

    property real margin: 40
    signal done()

    readonly property var e: backend.editor
    readonly property color listColor: theme.listColor(e.listColor || 0)
    height: column.implicitHeight + 12

    function finish() {
        titleField.commit()
        notesArea.commit()
        backend.endEdit()
        root.done()
    }

    Component.onCompleted: {
        titleField.text = e.title || ""
        notesArea.text = e.notes || ""
        titleField.forceActiveFocus()
        if (e.isNew) titleField.selectAll()
    }

    Rectangle { width: 4; height: parent.height; color: root.listColor }

    ColumnLayout {
        id: column
        x: root.margin - 12
        width: parent.width - 2 * root.margin + 24
        y: 6
        spacing: 4

        // Meta line.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Text { text: root.e.created || ""; color: theme.muted; font.pixelSize: backend.fontSize - 2; font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            IconButton { icon: "copy"; size: 14; tip: "Copy Title"; onClicked: { clip.text = root.e.title; clip.selectAll(); clip.copy() } }
            IconButton { icon: "keyboard"; size: 14; tip: "Shortcuts (?)"; onClicked: shortcutsMenu.popup() }
            IconButton { icon: "trash"; size: 14; color: theme.overdue; tip: "Delete"; onClicked: backend.deleteSelection() }
            TextEdit { id: clip; visible: false }
            Menu {
                id: shortcutsMenu
                MenuItem { text: "Priority None\tCtrl+Alt+0"; onTriggered: backend.setPriority(0) }
                MenuItem { text: "Priority Low\tCtrl+Alt+1"; onTriggered: backend.setPriority(1) }
                MenuItem { text: "Priority Medium\tCtrl+Alt+2"; onTriggered: backend.setPriority(2) }
                MenuItem { text: "Priority High\tCtrl+Alt+3"; onTriggered: backend.setPriority(3) }
                MenuItem { text: "Toggle Starred\tCtrl+Alt+S"; onTriggered: backend.setStarred(-1) }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: card.implicitHeight
            radius: 8
            color: theme.surface
            border.color: theme.divider
            clip: true

            ColumnLayout {
                id: card
                width: parent.width
                spacing: 0

                // Title.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 10
                    Layout.bottomMargin: 6
                    spacing: 10
                    Rectangle {
                        implicitWidth: 18
                        implicitHeight: 18
                        radius: root.e.type === 2 ? 4 : 9
                        color: "transparent"
                        border.width: 1.3
                        border.color: theme.faint
                        MouseArea { anchors.fill: parent; onClicked: backend.toggleComplete(root.e.id) }
                    }
                    Plain.TextField {
                        id: titleField
                        Layout.fillWidth: true
                        padding: 0
                        font.pixelSize: backend.fontSize + 1
                        color: theme.foreground
                        placeholderText: root.e.type === 1 ? "New Project" : root.e.type === 2 ? "New Checklist" : "New Task"
                        placeholderTextColor: theme.faint
                        selectByMouse: true
                        background: Item {}
                        function commit() { if (text !== (root.e.title || "")) backend.setTitle(root.e.id, text) }
                        onEditingFinished: commit()
                        Keys.onReturnPressed: root.finish()
                        Keys.onEscapePressed: root.finish()
                    }
                    Icon {
                        name: "starred"
                        size: 16
                        filled: !!root.e.starred
                        color: root.e.starred ? theme.star : theme.faint
                        MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: backend.setStarred(root.e.starred ? 0 : 1, root.e.id) }
                    }
                    PriorityPicker { value: root.e.priority || 0; onPicked: (p) => backend.setPriority(p, root.e.id) }
                }

                // The list ribbon.
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 32
                    color: Qt.alpha(root.listColor, theme.dark ? 0.22 : 0.16)
                    RowLayout {
                        anchors.fill: parent
                        anchors.rightMargin: 10
                        spacing: 8
                        ListBadge {
                            text: root.e.listName || ""
                            color: root.listColor
                            chevrons: true
                            implicitHeight: 32
                            onClicked: listMenu.popup()
                            Menu {
                                id: listMenu
                                Repeater {
                                    model: backend.listChoices
                                    MenuItem {
                                        required property var modelData
                                        text: modelData.name
                                        checkable: true
                                        checked: modelData.id === root.e.listId
                                        onTriggered: backend.setTaskList(root.e.id, modelData.id)
                                    }
                                }
                            }
                        }
                        Icon {
                            visible: (root.e.actionType || 0) > 0
                            name: Icons.actionIcons[root.e.actionType || 0]
                            size: 15
                            color: theme.today
                            MouseArea { anchors.fill: parent; onClicked: backend.performAction(root.e.id) }
                        }
                        Text {
                            text: root.e.hint || ""
                            color: (root.e.hint || "").startsWith("Overdue") ? theme.overdue : theme.today
                            font.pixelSize: backend.fontSize - 1
                            font.weight: Font.DemiBold
                        }
                        Text {
                            visible: !!root.e.repeat
                            text: "· " + (root.e.repeat || "")
                            color: theme.muted
                            font.pixelSize: backend.fontSize - 2
                        }
                        Item { Layout.fillWidth: true }
                        RibbonButton { icon: "url"; tip: "Action (Ctrl+B)"; active: (root.e.actionType || 0) > 0; onClicked: actionMenu.popup() }
                        RibbonButton { icon: "repeat"; tip: "Repeat (Ctrl+;)"; active: !!root.e.repeat; enabled: !root.e.isChecklistItem; onClicked: repeatMenu.popup() }
                        RibbonButton { icon: "duration"; tip: "Duration (x)"; active: (root.e.duration || 0) > 0; onClicked: durationMenu.popup() }

                        Menu {
                            id: actionMenu
                            Repeater {
                                model: Icons.actionNames
                                MenuItem {
                                    required property int index
                                    required property string modelData
                                    text: modelData
                                    checkable: true
                                    checked: (root.e.actionType || 0) === index
                                    onTriggered: {
                                        if (index === 0) { backend.setAction(root.e.id, 0, ""); return }
                                        actionDialog.kind = index
                                        actionDialog.open()
                                    }
                                }
                            }
                        }
                        Menu {
                            id: repeatMenu
                            Repeater {
                                model: ["Never", "Daily", "Weekly", "Every 2 Weeks", "Monthly", "Quarterly", "Yearly"]
                                MenuItem {
                                    required property int index
                                    required property string modelData
                                    text: modelData + (index > 0 ? "\tAlt+" + index : "")
                                    onTriggered: backend.setRepeatPreset(root.e.id, index)
                                }
                            }
                            MenuSeparator {}
                            MenuItem {
                                text: "After Completion"
                                checkable: true
                                checked: !!root.e.repeatFromCompletion
                                onTriggered: backend.setRepeatFromCompletion(root.e.id, !root.e.repeatFromCompletion)
                            }
                        }
                        Menu {
                            id: durationMenu
                            Repeater {
                                model: [[30, "30 mins"], [60, "1 hour"], [120, "2 hours"], [15, "15 mins"], [45, "45 mins"], [240, "4 hours"], [1440, "1 day"], [0, "None"]]
                                MenuItem {
                                    required property var modelData
                                    text: modelData[1]
                                    checkable: true
                                    checked: (root.e.duration || 0) === modelData[0]
                                    onTriggered: backend.setDuration(root.e.id, modelData[0])
                                }
                            }
                        }
                    }
                }

                // Tags and dates.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 10
                    spacing: 12
                    TagField {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        taskId: root.e.id || 0
                        tags: root.e.tags || []
                    }
                    GridLayout {
                        Layout.alignment: Qt.AlignTop
                        visible: !root.e.isChecklistItem
                        columns: 5
                        columnSpacing: 2
                        rowSpacing: 2
                        Icon { name: "play"; size: 11; filled: true; color: root.e.hasStart ? theme.start : theme.faint }
                        DateField {
                            value: root.e.startDate || ""
                            placeholderText: "Start"
                            onPicked: (iso, minutes) => backend.setStart(root.e.id, iso, minutes >= 0 ? minutes : -2)
                            onTyped: (text) => backend.setStartFromText(root.e.id, text)
                        }
                        ClearButton { shown: !!root.e.hasStart; onClicked: backend.setStart(root.e.id, "", -1) }
                        DateField {
                            timeOnly: true
                            value: root.e.startTime || ""
                            placeholderText: "Time"
                            onTyped: (text) => backend.setTimeFromText(root.e.id, false, text)
                        }
                        ClearButton { shown: !!root.e.startTime; onClicked: backend.setTimeFromText(root.e.id, false, "") }

                        Icon { name: "check"; size: 12; stroke: 2.2; color: root.e.hasDue ? theme.foreground : theme.faint }
                        DateField {
                            value: root.e.dueDate || ""
                            placeholderText: "Due"
                            onPicked: (iso, minutes) => backend.setDue(root.e.id, iso, minutes >= 0 ? minutes : -2)
                            onTyped: (text) => backend.setDueFromText(root.e.id, text)
                        }
                        ClearButton { shown: !!root.e.hasDue; onClicked: backend.setDue(root.e.id, "", -1) }
                        DateField {
                            timeOnly: true
                            value: root.e.dueTime || ""
                            placeholderText: "Time"
                            onTyped: (text) => backend.setTimeFromText(root.e.id, true, text)
                        }
                        ClearButton { shown: !!root.e.dueTime; onClicked: backend.setTimeFromText(root.e.id, true, "") }
                    }
                }

                // Notes.
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: Math.max(84, notesArea.implicitHeight + 16)
                    color: theme.notes
                    Column {
                        x: 12
                        y: 10
                        spacing: 10
                        Icon { name: "note"; size: 15; color: theme.faint }
                        Icon { name: "markdown"; size: 15; color: theme.faint }
                        Icon {
                            name: "insertDate"
                            size: 15
                            color: theme.faint
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    notesArea.insert(notesArea.cursorPosition, Qt.formatDateTime(new Date(), "ddd d MMM yyyy, h:mm AP") + " ")
                                    notesArea.forceActiveFocus()
                                }
                            }
                        }
                    }
                    Plain.TextArea {
                        id: notesArea
                        x: 40
                        width: parent.width - 52
                        y: 2
                        wrapMode: TextEdit.Wrap
                        placeholderText: "Notes"
                        placeholderTextColor: theme.faint
                        color: theme.foreground
                        font.pixelSize: backend.fontSize - 1
                        selectByMouse: true
                        background: Item {}
                        function commit() { if (text !== (root.e.notes || "")) backend.setNotes(root.e.id, text) }
                        onActiveFocusChanged: if (!activeFocus) commit()
                        Keys.onEscapePressed: root.finish()
                    }
                }
            }
        }
    }

    Dialog {
        id: actionDialog
        property int kind: 1
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: Icons.actionNames[kind]
        standardButtons: Dialog.Cancel | Dialog.Ok
        TextField {
            id: actionValue
            width: 320
            placeholderText: ["", "Phone number", "Phone number", "Email address", "Address", "https://…", "Search for…"][actionDialog.kind]
            onAccepted: actionDialog.accept()
        }
        onOpened: { actionValue.text = root.e.actionType === kind ? root.e.actionValue : ""; actionValue.forceActiveFocus() }
        onAccepted: backend.setAction(root.e.id, kind, actionValue.text)
    }

    component RibbonButton: IconButton {
        property bool active: false
        size: 15
        color: active ? theme.today : theme.muted
        opacity: enabled ? 1 : 0.35
    }

    component ClearButton: Item {
        property bool shown: false
        signal clicked()
        implicitWidth: 16
        implicitHeight: 16
        Icon { anchors.centerIn: parent; visible: parent.shown; name: "clear"; size: 13; color: theme.faint }
        MouseArea { anchors.fill: parent; enabled: parent.shown; onClicked: parent.clicked() }
    }
}
