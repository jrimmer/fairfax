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
            IconButton {
                icon: "copy"; size: 14; tip: "Copy"
                onClicked: copyMenu.popup()
                Menu {
                    id: copyMenu
                    MenuItem { text: "Copy"; onTriggered: backend.copySelection() }
                    MenuItem { text: "Copy Title"; onTriggered: { clip.text = root.e.title; clip.selectAll(); clip.copy() } }
                    MenuItem { text: "Copy Notes"; enabled: !!root.e.notes; onTriggered: { clip.text = root.e.notes; clip.selectAll(); clip.copy() } }
                    MenuItem { text: "Copy Embeddable Link"; onTriggered: { clip.text = "@{" + root.e.title + "}"; clip.selectAll(); clip.copy() } }
                    MenuItem { text: "Copy Shareable Link"; onTriggered: { clip.text = root.e.link; clip.selectAll(); clip.copy() } }
                }
            }
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
                        RibbonButton { icon: "image"; tip: "Attach a File"; active: (root.e.attachments || []).length > 0; onClicked: filePicker.open("Attach a file to “" + root.e.title + "”", "attach:" + root.e.id) }
                        RibbonButton { icon: "pin"; tip: "Location"; active: (root.e.locations || []).length > 0; onClicked: locationMenu.popup() }
                        RibbonButton { id: repeatButton; icon: "repeat"; tip: "Repeat (Ctrl+;)"; active: !!root.e.repeat; enabled: !root.e.isChecklistItem; onClicked: repeatEditor.openAt(repeatButton) }
                        RibbonButton { icon: "alarm"; tip: "Alerts (Ctrl+')"; active: (root.e.alarmOffsets || []).length > 0; enabled: !root.e.isChecklistItem; onClicked: alarmMenu.popup() }
                        RibbonButton { icon: "duration"; tip: "Duration (x)"; active: (root.e.duration || 0) > 0; onClicked: durationMenu.popup() }

                        Menu {
                            id: alarmMenu
                            Repeater {
                                model: [[0, "At due time"], [5, "5 minutes before"], [15, "15 minutes before"], [30, "30 minutes before"],
                                        [60, "1 hour before"], [120, "2 hours before"], [360, "6 hours before"], [720, "12 hours before"],
                                        [1440, "1 day before"], [2880, "2 days before"]]
                                MenuItem {
                                    required property var modelData
                                    text: modelData[1]
                                    checkable: true
                                    checked: (root.e.alarmOffsets || []).indexOf(modelData[0]) >= 0
                                    onTriggered: backend.toggleAlarm(root.e.id, modelData[0])
                                }
                            }
                            MenuSeparator {}
                            MenuItem { text: "At a Date and Time…"; onTriggered: absoluteAlarm.open() }
                            MenuItem { text: "Remove All Alerts"; enabled: (root.e.alarms || []).length > 0; onTriggered: backend.clearAlarms(root.e.id) }
                        }
                        Menu {
                            id: locationMenu
                            Repeater {
                                model: backend.locationChoices()
                                MenuItem {
                                    required property var modelData
                                    text: modelData.name
                                    checkable: true
                                    checked: (root.e.locations || []).some(l => l.id === modelData.id)
                                    onTriggered: backend.toggleTaskLocation(root.e.id, modelData.id)
                                }
                            }
                            MenuSeparator {}
                            MenuItem { text: "New Location…"; onTriggered: newLocation.open() }
                        }

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
                            iso: root.e.startIso || ""
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
                            iso: root.e.dueIso || ""
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

                // Sub-tasks: a quick inline checklist, or a project's tasks.
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    Layout.bottomMargin: 6
                    spacing: 2
                    visible: !root.e.isChecklistItem
                    Repeater {
                        model: root.e.subtasks || []
                        RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            Rectangle {
                                implicitWidth: 14
                                implicitHeight: 14
                                radius: root.e.type === 2 ? 3 : 7
                                color: modelData.completed ? theme.muted : "transparent"
                                border.width: 1.2
                                border.color: theme.faint
                                Icon { anchors.centerIn: parent; visible: modelData.completed; name: "check"; size: 10; stroke: 2.4; color: theme.surface }
                                MouseArea { anchors.fill: parent; anchors.margins: -3; onClicked: backend.toggleComplete(modelData.id) }
                            }
                            Plain.TextField {
                                Layout.fillWidth: true
                                padding: 1
                                background: Item {}
                                text: modelData.title
                                color: modelData.completed ? theme.muted : theme.foreground
                                font.pixelSize: backend.fontSize - 1
                                font.strikeout: modelData.completed
                                selectByMouse: true
                                onEditingFinished: if (text !== modelData.title && text.trim() !== "") backend.setTitle(modelData.id, text)
                                // Return on a sub-task starts the next one, as the reference app's setting does.
                                Keys.onReturnPressed: { if (text !== modelData.title) backend.setTitle(modelData.id, text); newSubtask.forceActiveFocus() }
                                Keys.onEscapePressed: root.finish()
                            }
                            Icon {
                                name: "close"; size: 9; stroke: 2.2; color: theme.faint
                                MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: { backend.selectTask(modelData.id); backend.deleteSelection(); backend.beginEdit(root.e.id) } }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Icon { name: "plus"; size: 13; color: theme.faint }
                        Plain.TextField {
                            id: newSubtask
                            Layout.fillWidth: true
                            padding: 1
                            background: Item {}
                            placeholderText: root.e.type === 1 ? "Add a task to this project" : root.e.type === 2 ? "Add a checklist item" : "Add a sub-task"
                            placeholderTextColor: theme.faint
                            color: theme.foreground
                            font.pixelSize: backend.fontSize - 1
                            Keys.onReturnPressed: { if (text.trim()) { backend.addSubtask(root.e.id, text); text = "" } }
                            Keys.onEscapePressed: root.finish()
                        }
                    }
                }

                // Attachments: one picture, audio notes and files.
                Flow {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    Layout.bottomMargin: 8
                    spacing: 8
                    visible: (root.e.attachments || []).length > 0 || (root.e.alarms || []).length > 0 || (root.e.locations || []).length > 0
                    Repeater {
                        model: root.e.attachments || []
                        Rectangle {
                            required property var modelData
                            width: modelData.kind === 0 ? 84 : attachLabel.implicitWidth + 44
                            height: modelData.kind === 0 ? 60 : 26
                            radius: 6
                            color: theme.raised
                            clip: true
                            Image {
                                anchors.fill: parent
                                visible: modelData.kind === 0
                                source: modelData.kind === 0 ? modelData.url : ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                sourceSize.width: 168
                            }
                            Row {
                                visible: modelData.kind !== 0
                                anchors.verticalCenter: parent.verticalCenter
                                x: 8
                                spacing: 6
                                Icon { name: modelData.kind === 1 ? "mic" : "note"; size: 13; color: theme.muted }
                                Text { id: attachLabel; text: modelData.name; color: theme.foreground; font.pixelSize: backend.fontSize - 2 }
                            }
                            MouseArea { anchors.fill: parent; onClicked: backend.openAttachment(modelData.id) }
                            Rectangle {
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 3
                                width: 14; height: 14; radius: 7
                                color: Qt.alpha(theme.background, 0.8)
                                Icon { anchors.centerIn: parent; name: "close"; size: 7; stroke: 2.4; color: theme.foreground }
                                MouseArea { anchors.fill: parent; onClicked: backend.removeAttachment(modelData.id) }
                            }
                        }
                    }
                    Repeater {
                        model: root.e.alarms || []
                        Rectangle {
                            required property var modelData
                            width: alarmLabel.implicitWidth + 40
                            height: 24
                            radius: 12
                            color: Qt.alpha(theme.priorityMedium, 0.16)
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                x: 8
                                spacing: 5
                                Icon { name: "alarm"; size: 12; color: theme.priorityMedium }
                                Text { id: alarmLabel; text: modelData.label; color: theme.priorityMedium; font.pixelSize: backend.fontSize - 2 }
                            }
                            Icon {
                                anchors.right: parent.right; anchors.rightMargin: 7; anchors.verticalCenter: parent.verticalCenter
                                name: "close"; size: 8; stroke: 2.4; color: theme.priorityMedium
                                MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: backend.removeAlarm(modelData.id) }
                            }
                        }
                    }
                    Repeater {
                        model: root.e.locations || []
                        Rectangle {
                            required property var modelData
                            width: placeLabel.implicitWidth + 40
                            height: 24
                            radius: 12
                            color: Qt.alpha(theme.start, 0.16)
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                x: 8
                                spacing: 5
                                Icon { name: "pin"; size: 12; color: theme.start }
                                Text { id: placeLabel; text: modelData.name; color: theme.start; font.pixelSize: backend.fontSize - 2 }
                            }
                            Icon {
                                anchors.right: parent.right; anchors.rightMargin: 7; anchors.verticalCenter: parent.verticalCenter
                                name: "close"; size: 8; stroke: 2.4; color: theme.start
                                MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: backend.toggleTaskLocation(root.e.id, modelData.id) }
                            }
                        }
                    }
                }

                // Notes, with live Markdown and task links.
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: Math.max(96, notesArea.implicitHeight + 8)
                    color: theme.notes
                    NotesArea {
                        id: notesArea
                        anchors.fill: parent
                        anchors.margins: 4
                        text: root.e.notes || ""
                        fontSize: backend.fontSize - 1
                        onCommitted: (t) => backend.setNotes(root.e.id, t)
                        onEscaped: root.finish()
                        onLinkActivated: (target, kind) => {
                            if (kind !== "task") { Qt.openUrlExternally(target); return }
                            const hits = backend.findTasks(target, 1)
                            if (hits.length) backend.revealInList(hits[0].id)
                        }
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

    RepeatEditor { id: repeatEditor; taskId: root.e.id }

    Dialog {
        id: absoluteAlarm
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: "Alert at a date and time"
        standardButtons: Dialog.Cancel | Dialog.Ok
        property string iso: root.e.dueIso || backend.today
        property int minutes: 540
        Row {
            spacing: 8
            DateField { id: alarmDate; iso: absoluteAlarm.iso; value: absoluteAlarm.iso; placeholderText: "Date"; onPicked: (iso) => { absoluteAlarm.iso = iso; value = iso } ; onTyped: (t) => { const s = backend.dateSuggestions(t); if (s.length && s[0].date) { absoluteAlarm.iso = s[0].date; value = s[0].label; if (s[0].minutes >= 0) absoluteAlarm.minutes = s[0].minutes } } }
            DateField { timeOnly: true; value: "9:00 AM"; placeholderText: "Time"; onTyped: (t) => { const s = backend.dateSuggestions(t); if (s.length && s[0].minutes >= 0) { absoluteAlarm.minutes = s[0].minutes; value = s[0].label } } }
        }
        onAccepted: backend.addAbsoluteAlarm(root.e.id, iso, minutes)
    }

    Dialog {
        id: newLocation
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: "New Location"
        standardButtons: Dialog.Cancel | Dialog.Ok
        Column {
            spacing: 8
            TextField { id: locationName; width: 300; placeholderText: "Name (e.g. Home, Office)" }
            TextField { id: locationAddress; width: 300; placeholderText: "Address (optional)" }
        }
        onOpened: { locationName.text = ""; locationAddress.text = ""; locationName.forceActiveFocus() }
        onAccepted: {
            const id = backend.newLocation(locationName.text, locationAddress.text)
            if (id) backend.toggleTaskLocation(root.e.id, id)
        }
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
