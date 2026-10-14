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

    objectName: "editorCard"
    property real margin: 40
    signal done()

    readonly property var e: backend.editor
    // Natural-language reading of a new task's title, which the "Stop
    // detecting" chip turns off for this edit only.
    property bool detecting: true
    readonly property bool canDetect: !!e.isNew && (backend.settingsRevision, backend.settingFlag("nl.editor", true))
    readonly property var chipColors: ({
        due: theme.today, start: theme.start, repeat: theme.duration, alert: theme.priorityMedium,
        duration: theme.duration, priority: theme.priorityHigh, star: theme.star, tag: theme.muted,
        list: theme.accent, location: theme.start
    })
    readonly property color listColor: theme.listColor(e.listColor || 0)
    // The priority's colour, or null with none, for the circle and the star.
    readonly property var priorityColor: [null, theme.priorityLow, theme.priorityMedium, theme.priorityHigh][e.priority || 0]
    height: column.implicitHeight + 12

    function finish() {
        titleField.commit()
        notesArea.commit()
        backend.endEdit()
        root.done()
    }

    // Goes to one field when the editor was opened by a quick-jump (Ctrl+E
    // for the notes, Ctrl+G for the location, and so on).
    function goTo(kind) {
        switch (kind) {
        case "title": titleField.forceActiveFocus(); break
        case "notes": notesArea.forceEditorFocus(); break
        case "action": actionMenu.popup(actionButton, 0, actionButton.height + 4); break
        case "location": locationMenu.popup(locationButton, 0, locationButton.height + 4); break
        case "alerts": if (!root.e.isChecklistItem) alarmMenu.popup(alarmButton, 0, alarmButton.height + 4); break
        case "dates": if (!root.e.isChecklistItem) dueField.forceActiveFocus(); break
        case "repeat": if (!root.e.isChecklistItem) repeatEditor.openAt(repeatButton); break
        }
    }

    // The task whose title is in the field, so a late-arriving editor update
    // for a different task refills it rather than leaving it blank.
    property var loadedId: 0
    // Renaming or deleting a sub-task rebuilds the list and with it the delegate
    // that called, so what follows has to run from here, not from the delegate.
    function renameSubtask(id, oldTitle, text) {
        if (text !== oldTitle) backend.setTitle(id, text)
        newSubtask.forceActiveFocus()
    }
    function deleteSubtask(id) {
        const parentId = e.id
        backend.selectTask(id)
        backend.deleteSelection()
        backend.beginEdit(parentId)
    }

    function loadTitle() {
        if (loadedId === e.id) return
        loadedId = e.id
        titleField.text = e.title || ""
    }
    onEChanged: loadTitle()

    Component.onCompleted: {
        loadTitle()
        // The card is built before the list has placed its row, and focus
        // asked for then is lost, so a new task's title wouldn't take typing.
        Qt.callLater(() => {
            titleField.forceActiveFocus()
            if (root.e.isNew) titleField.selectAll()
        })
        const pending = backend.takePendingField()
        if (pending) Qt.callLater(root.goTo, pending)
    }

    // Closing the editor by any route (Ctrl+Return, a click elsewhere, another
    // task) must keep what was typed in the title and the notes.
    Connections {
        target: backend
        function onEditEnding() { titleField.commit(); notesArea.commit() }
    }

    Connections {
        target: backend
        function onEditFieldRequested() { root.goTo(backend.takePendingField()) }
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
                objectName: "editorCopy"; icon: "copy"; size: 14; tip: "Copy"
                onClicked: copyMenu.popup()
                Menu {
                    id: copyMenu
                    MenuItem { text: "Copy"; onTriggered: backend.copySelection() }
                    MenuItem { text: "Copy Title"; onTriggered: { clip.text = root.e.title; clip.selectAll(); clip.copy() } }
                    MenuItem { text: "Copy Notes"; enabled: !!root.e.notes; onTriggered: { clip.text = root.e.notes; clip.selectAll(); clip.copy() } }
                    MenuItem { text: "Copy Links from Notes"; enabled: notesArea.detectedLinks.length > 0; onTriggered: notesArea.copyLinks() }
                    MenuItem { text: "Copy Embeddable Link"; onTriggered: { clip.text = "@{" + root.e.title + "}"; clip.selectAll(); clip.copy() } }
                    MenuItem { text: "Copy Shareable Link"; onTriggered: { clip.text = root.e.link; clip.selectAll(); clip.copy() } }
                }
            }
            IconButton { objectName: "editorKeyboard"; icon: "keyboard"; size: 14; tip: "Shortcuts (?)"; onClicked: shortcutsMenu.popup() }
            IconButton { objectName: "editorDelete"; icon: "trash"; size: 14; color: theme.overdue; tip: "Delete"; onClicked: backend.deleteSelection() }
            TextEdit { id: clip; visible: false }
            Menu {
                id: shortcutsMenu
                MenuItem { text: "Priority None\t" + (backend.shortcutMap, backend.shortcutText("edit.priorityNone")); onTriggered: backend.setPriority(0) }
                MenuItem { text: "Priority Low\t" + (backend.shortcutMap, backend.shortcutText("edit.priorityLow")); onTriggered: backend.setPriority(1) }
                MenuItem { text: "Priority Medium\t" + (backend.shortcutMap, backend.shortcutText("edit.priorityMedium")); onTriggered: backend.setPriority(2) }
                MenuItem { text: "Priority High\t" + (backend.shortcutMap, backend.shortcutText("edit.priorityHigh")); onTriggered: backend.setPriority(3) }
                MenuItem { text: "Toggle Starred\t" + (backend.shortcutMap, backend.shortcutText("edit.star")); onTriggered: backend.setStarred(-1) }
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
                        border.color: root.priorityColor || theme.faint
                        Accessible.role: Accessible.Button
                        Accessible.name: "Toggle completed"
                        Accessible.onPressAction: backend.toggleComplete(root.e.id)
                        MouseArea { anchors.fill: parent; onClicked: backend.toggleComplete(root.e.id) }
                    }
                    Plain.TextField {
                        id: titleField
                        objectName: "editorTitle"
                        // A screen reader reads the field by its label; the
                        // placeholder is not a name.
                        Accessible.name: "Title"
                        Layout.fillWidth: true
                        // While a new task's title is typed, recognised dates,
                        // tags and the rest are drawn as capsules, as in Quick Entry.
                        readonly property var preview: root.canDetect && root.detecting && text.trim() ? backend.previewEntry(text) : null
                        property var spans: preview ? preview.spans : []
                        Repeater {
                            model: titleField.spans
                            Rectangle {
                                required property var modelData
                                readonly property rect a: { titleField.contentWidth; titleField.text; return titleField.positionToRectangle(modelData.start) }
                                readonly property rect b: { titleField.contentWidth; titleField.text; return titleField.positionToRectangle(modelData.start + modelData.length) }
                                x: titleField.leftPadding + a.x - 3
                                width: b.x - a.x + 6
                                y: (titleField.height - height) / 2
                                height: titleField.height + 2
                                radius: 5
                                z: -1
                                color: Qt.alpha(theme.accent, 0.20)
                            }
                        }
                        padding: 0
                        font.pixelSize: backend.fontSize + 1
                        color: theme.foreground
                        placeholderText: root.e.type === 1 ? "New Project" : root.e.type === 2 ? "New Checklist" : "New Task"
                        placeholderTextColor: theme.faint
                        selectByMouse: true
                        background: Item {}
                        // An existing task never loses its name to an empty field.
                        function commit() {
                            if (text === (root.e.title || "") || (text.trim() === "" && !root.e.isNew)) return
                            backend.setTitle(root.e.id, text)
                        }
                        onEditingFinished: commit()
                        Keys.onReturnPressed: root.finish()
                        Keys.onEscapePressed: root.finish()
                    }
                    // One star: filled when starred, coloured by priority when one is
                    // set; it opens the priority and star menu.
                    IconButton {
                        id: prio
                        objectName: "editorPriority"
                        icon: "starred"
                        size: 17
                        accessibleName: "Priority"
                        Accessible.description: tip
                        color: root.priorityColor || (root.e.starred ? theme.star : theme.faint)
                        tip: (["No priority", "Low priority", "Medium priority", "High priority"][root.e.priority || 0]) + (root.e.starred ? " · starred" : "")
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
                            visible: !!root.e.starred
                            name: "starred"
                            size: 17
                            filled: true
                            color: parent.color
                        }
                        Menu {
                            id: priorityMenu
                            Repeater {
                                model: [["None", 0], ["High", 3], ["Medium", 2], ["Low", 1]]
                                MenuItem {
                                    required property var modelData
                                    text: modelData[0]
                                    checkable: true
                                    checked: (root.e.priority || 0) === modelData[1]
                                    onTriggered: backend.setPriority(modelData[1], root.e.id)
                                }
                            }
                            MenuSeparator {}
                            MenuItem {
                                objectName: "editorStarred"
                                text: "Star"
                                Accessible.name: "Starred"
                                checkable: true
                                checked: !!root.e.starred
                                onTriggered: backend.setStarred(root.e.starred ? 0 : 1, root.e.id)
                            }
                        }
                    }
                }

                // What was recognised in a new task's title, with the switch to
                // stop reading it.
                Flow {
                    Layout.fillWidth: true
                    Layout.leftMargin: 38
                    Layout.rightMargin: 10
                    Layout.bottomMargin: visible ? 6 : 0
                    spacing: 6
                    visible: root.canDetect && titleField.text.trim() !== ""
                    Repeater {
                        model: titleField.preview ? titleField.preview.chips : []
                        Rectangle {
                            required property var modelData
                            height: 22
                            width: chipLabel.implicitWidth + 16
                            radius: 6
                            color: Qt.alpha(root.chipColors[modelData.kind] || theme.accent, 0.22)
                            Text { id: chipLabel; anchors.centerIn: parent; text: parent.modelData.label; color: theme.foreground; font.pixelSize: backend.fontSize - 2 }
                        }
                    }
                    Rectangle {
                        height: 22
                        width: stopLabel.implicitWidth + 16
                        radius: 11
                        color: "transparent"
                        border.width: 1
                        border.color: theme.faint
                        Text {
                            id: stopLabel
                            anchors.centerIn: parent
                            text: root.detecting ? "Stop detecting" : "Detect dates and details"
                            color: theme.muted
                            font.pixelSize: backend.fontSize - 2
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.detecting = !root.detecting
                                backend.setDetecting(root.e.id, root.detecting)
                                titleField.forceActiveFocus()
                            }
                        }
                    }
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
                            objectName: "editorList"
                            accessibleName: "List"
                            Accessible.description: root.e.listName || ""
                            text: root.e.listName || ""
                            color: root.listColor
                            chevrons: true
                            implicitHeight: 32
                            pressedDown: listMenu.opened
                            onClicked: listMenu.popup(this, 0, height + 2)
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
                        Item { Layout.fillWidth: true }
                        Icon {
                            visible: (root.e.actionType || 0) > 0
                            name: Icons.actionIcons[root.e.actionType || 0]
                            size: 15
                            color: theme.today
                            MouseArea { objectName: "editorActionRun"; anchors.fill: parent; onClicked: backend.performAction(root.e.id) }
                        }
                        Text {
                            visible: (root.e.actionType || 0) > 0 && !!root.e.hint
                            text: "·"
                            color: theme.today
                            font.pixelSize: backend.fontSize
                        }
                        Text {
                            Layout.maximumWidth: 220
                            text: root.e.hint || ""
                            visible: text !== ""
                            color: (root.e.hint || "").startsWith("Overdue") ? theme.overdue : theme.today
                            font.pixelSize: backend.fontSize
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: !!root.e.repeat
                            text: "· " + (root.e.repeat || "")
                            color: theme.muted
                            font.pixelSize: backend.fontSize - 2
                            Layout.rightMargin: 4
                        }
                        RibbonButton { id: actionButton; objectName: "editorAction"; icon: "url"; tip: "Action (Ctrl+B)"; accessibleName: "Action"; active: (root.e.actionType || 0) > 0; menu: actionMenu }
                        RibbonButton {
                            objectName: "editorRecord"
                            icon: "mic"
                            // PipeWire's pw-record is Linux only; hide the button elsewhere.
                            visible: backend.platformIsLinux
                            tip: backend.recordingTaskId === root.e.id ? "Stop Recording" : "Record an Audio Note"
                            active: backend.recordingTaskId === root.e.id
                            color: backend.recordingTaskId === root.e.id ? theme.overdue : theme.muted
                            onClicked: backend.toggleRecording(root.e.id)
                        }
                        RibbonButton { icon: "image"; tip: "Attach a File"; active: (root.e.attachments || []).length > 0; onClicked: filePicker.open("Attach a file to “" + root.e.title + "”", "attach:" + root.e.id) }
                        RibbonButton { id: locationButton; objectName: "editorLocation"; icon: "pin"; tint: theme.start; tip: "Location (Ctrl+G)"; accessibleName: "Location"; active: (root.e.locations || []).length > 0; menu: locationMenu }
                        RibbonButton { id: repeatButton; objectName: "editorRepeat"; icon: "repeat"; tint: theme.duration; tip: "Repeat (Ctrl+;)"; accessibleName: "Repeat"; active: !!root.e.repeat; enabled: !root.e.isChecklistItem; lit: repeatEditor.visible; onClicked: repeatEditor.openAt(repeatButton) }
                        RibbonButton { id: alarmButton; objectName: "editorAlarm"; icon: "alarm"; tint: theme.priorityMedium; tip: "Alerts (Ctrl+')"; accessibleName: "Alerts"; active: (root.e.alarmOffsets || []).length > 0; enabled: !root.e.isChecklistItem; menu: alarmMenu }
                        RibbonButton { id: durationButton; objectName: "editorDuration"; icon: "duration"; tint: theme.duration; tip: "Duration (x)"; accessibleName: "Duration"; active: (root.e.duration || 0) > 0; menu: durationMenu }

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
                            MenuItem { text: "Days Before at a Time…"; onTriggered: daysBeforeAlarm.open() }
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
                        objectName: "editorTags"
                        accessibleName: "Tags"
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        taskId: root.e.id || 0
                        tags: root.e.tags || []
                    }
                    // Start and due, each a date and a time, with the row's icon at the
                    // right as in Quick Entry and the reference app.
                    GridLayout {
                        Layout.alignment: Qt.AlignTop
                        visible: !root.e.isChecklistItem
                        columns: 3
                        columnSpacing: 4
                        rowSpacing: 2
                        DateField {
                            id: startField
                            objectName: "editorStart"
                            Accessible.name: "Start date"
                            implicitWidth: 112
                            iso: root.e.startIso || ""
                            value: root.e.startDate || ""
                            placeholderText: "Start"
                            clearable: !!root.e.hasStart
                            onCleared: backend.setStart(root.e.id, "", -1)
                            onPicked: (iso, minutes) => backend.setStart(root.e.id, iso, minutes >= 0 ? minutes : -2)
                            onTyped: (text) => backend.setStartFromText(root.e.id, text)
                        }
                        DateField {
                            objectName: "editorStartTime"
                            Accessible.name: "Start time"
                            timeOnly: true
                            implicitWidth: 82
                            value: root.e.startTime || ""
                            placeholderText: "Time"
                            clearable: !!root.e.startTime
                            onCleared: backend.setTimeFromText(root.e.id, false, "")
                            onTyped: (text) => backend.setTimeFromText(root.e.id, false, text)
                        }
                        Icon { Layout.preferredWidth: 14; Layout.preferredHeight: 14; name: "play"; size: 12; filled: true; color: root.e.hasStart ? theme.start : theme.faint }

                        DateField {
                            id: dueField
                            objectName: "editorDue"
                            Accessible.name: "Due date"
                            implicitWidth: 112
                            iso: root.e.dueIso || ""
                            value: root.e.dueDate || ""
                            placeholderText: "Due"
                            color: root.e.hasDue ? ((root.e.hint || "").startsWith("Overdue") ? theme.overdue : theme.today) : theme.foreground
                            clearable: !!root.e.hasDue
                            onCleared: backend.setDue(root.e.id, "", -1)
                            onPicked: (iso, minutes) => backend.setDue(root.e.id, iso, minutes >= 0 ? minutes : -2)
                            onTyped: (text) => backend.setDueFromText(root.e.id, text)
                        }
                        DateField {
                            objectName: "editorDueTime"
                            Accessible.name: "Due time"
                            timeOnly: true
                            implicitWidth: 82
                            value: root.e.dueTime || ""
                            placeholderText: "Time"
                            clearable: !!root.e.dueTime
                            onCleared: backend.setTimeFromText(root.e.id, true, "")
                            onTyped: (text) => backend.setTimeFromText(root.e.id, true, text)
                        }
                        Icon { Layout.preferredWidth: 14; Layout.preferredHeight: 14; name: "check"; size: 13; stroke: 2.2; color: root.e.hasDue ? theme.today : theme.faint }
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
                                objectName: "editorSubtaskCheck:" + modelData.id
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
                                objectName: "editorSubtask:" + modelData.id
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
                                Keys.onReturnPressed: root.renameSubtask(modelData.id, modelData.title, text)
                                Keys.onEscapePressed: root.finish()
                            }
                            Icon {
                                objectName: "editorSubtaskDelete:" + modelData.id
                                name: "close"; size: 9; stroke: 2.2; color: theme.faint
                                MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: root.deleteSubtask(modelData.id) }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Icon { name: "plus"; size: 13; color: theme.faint }
                        Plain.TextField {
                            id: newSubtask
                            objectName: "editorNewSubtask"
                            Accessible.name: root.e.type === 1 ? "Add a task to this project" : root.e.type === 2 ? "Add a checklist item" : "Add a sub-task"
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
                            objectName: "editorAttachment:" + modelData.id
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
                                objectName: "editorAttachmentRemove:" + modelData.id
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
                            objectName: "editorAlarmChip:" + modelData.id
                            width: alarmLabel.implicitWidth + 40
                            height: 24
                            radius: 12
                            color: Qt.alpha(theme.priorityMedium, 0.16)
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                x: 8
                                spacing: 5
                                Icon { name: "alarm"; size: 12; color: theme.priorityMedium; opacity: modelData.sound ? 1 : 0.5 }
                                Text { id: alarmLabel; text: modelData.label + (modelData.sound ? "" : " (message)"); color: theme.priorityMedium; font.pixelSize: backend.fontSize - 2 }
                            }
                            // Clicking the chip chooses its style, as in the reference app's alert rows.
                            MouseArea {
                                anchors.fill: parent; anchors.rightMargin: 22
                                onClicked: { alarmStyleMenu.alarmId = modelData.id; alarmStyleMenu.sound = modelData.sound; alarmStyleMenu.popup() }
                            }
                            Icon {
                                objectName: "editorAlarmRemove:" + modelData.id
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
                            objectName: "editorLocationChip:" + modelData.name
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
                        objectName: "editorNotes"
                        accessibleName: "Notes"
                        anchors.fill: parent
                        anchors.margins: 4
                        text: root.e.notes || ""
                        fontSize: backend.fontSize - 1
                        onCommitted: (t) => backend.setNotes(root.e.id, t)
                        onEscaped: root.finish()
                        taskId: root.e.id || 0
                        onLinkActivated: (target, kind) => {
                            if (kind !== "task") { Qt.openUrlExternally(target); return }
                            backend.openTaskLink(target, root.e.id)
                        }
                    }
                }
            }
        }
    }

    ThemedDialog {

        width: Math.min(440, (parent ? parent.width : 480) - 40)
        id: actionDialog
        property int kind: 1
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: Icons.actionNames[kind]
        standardButtons: Dialog.Cancel | Dialog.Ok
        TextField {
            id: actionValue
            objectName: "actionValue"
            width: 320
            placeholderText: ["", "Phone number", "Phone number", "Email address", "Address", "https://…", "Search for…"][actionDialog.kind]
            onAccepted: actionDialog.accept()
        }
        onOpened: { actionValue.text = root.e.actionType === kind ? root.e.actionValue : ""; actionValue.forceActiveFocus() }
        onAccepted: backend.setAction(root.e.id, kind, actionValue.text)
    }

    RepeatEditor { id: repeatEditor; objectName: "repeatEditor"; taskId: root.e.id }

    Menu {
        id: alarmStyleMenu
        property int alarmId: 0
        property bool sound: true
        MenuItem { text: "Message with Sound"; checkable: true; checked: alarmStyleMenu.sound; onTriggered: backend.setAlarmSound(alarmStyleMenu.alarmId, true) }
        MenuItem { text: "Message"; checkable: true; checked: !alarmStyleMenu.sound; onTriggered: backend.setAlarmSound(alarmStyleMenu.alarmId, false) }
    }

    ThemedDialog {

        width: Math.min(440, (parent ? parent.width : 480) - 40)
        id: daysBeforeAlarm
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: "Alert days before at a time"
        standardButtons: Dialog.Cancel | Dialog.Ok
        property int minutes: 540
        Row {
            spacing: 8
            SpinBox { id: daysBox; objectName: "alarmDays"; Accessible.name: "Days before"; Component.onCompleted: contentItem.Accessible.name = "Days before"; from: 0; to: 365; value: 1; editable: true }
            Label { text: daysBox.value === 1 ? "day before at" : "days before at"; anchors.verticalCenter: parent.verticalCenter }
            DateField { timeOnly: true; Accessible.name: "Alert time"; value: "9:00 AM"; placeholderText: "Time"; onTyped: (t) => { const s = backend.dateSuggestions(t); if (s.length && s[0].minutes >= 0) { daysBeforeAlarm.minutes = s[0].minutes; value = s[0].label } } }
        }
        onAccepted: backend.addDaysBeforeAlarm(root.e.id, daysBox.value, minutes)
    }

    ThemedDialog {

        width: Math.min(440, (parent ? parent.width : 480) - 40)
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
            DateField { id: alarmDate; Accessible.name: "Alert date"; iso: absoluteAlarm.iso; value: absoluteAlarm.iso; placeholderText: "Date"; onPicked: (iso) => { absoluteAlarm.iso = iso; value = iso } ; onTyped: (t) => { const s = backend.dateSuggestions(t); if (s.length && s[0].date) { absoluteAlarm.iso = s[0].date; value = s[0].label; if (s[0].minutes >= 0) absoluteAlarm.minutes = s[0].minutes } } }
            DateField { timeOnly: true; Accessible.name: "Alert time"; value: "9:00 AM"; placeholderText: "Time"; onTyped: (t) => { const s = backend.dateSuggestions(t); if (s.length && s[0].minutes >= 0) { absoluteAlarm.minutes = s[0].minutes; value = s[0].label } } }
        }
        onAccepted: backend.addAbsoluteAlarm(root.e.id, iso, minutes)
    }

    ThemedDialog {

        width: Math.min(440, (parent ? parent.width : 480) - 40)
        id: newLocation
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: "New Location"
        standardButtons: Dialog.Cancel | Dialog.Ok
        Column {
            spacing: 8
            TextField { id: locationName; objectName: "locationName"; width: 300; placeholderText: "Name (e.g. Home, Office)" }
            TextField { id: locationAddress; objectName: "locationAddress"; width: 300; placeholderText: "Address (optional)" }
        }
        onOpened: { locationName.text = ""; locationAddress.text = ""; locationName.forceActiveFocus() }
        onAccepted: {
            const id = backend.newLocation(locationName.text, locationAddress.text)
            if (id) backend.toggleTaskLocation(root.e.id, id)
        }
    }

    // An icon button in the ribbon, tinted when it holds a value. It opens
    // its menu just below itself and stays lit while the menu is open, so
    // it's clear what's being changed.
    component RibbonButton: IconButton {
        id: rb
        property bool active: false
        property color tint: theme.today
        property var menu: null
        property bool lit: !!menu && menu.visible
        size: 16
        color: active || lit ? tint : theme.muted
        opacity: enabled ? 1 : 0.35
        onClicked: if (menu) menu.popup(rb, 0, rb.height + 4)
        Rectangle {
            anchors.fill: parent
            radius: 6
            visible: rb.lit
            color: Qt.alpha(rb.tint, 0.18)
            border.color: rb.tint
            border.width: 1
        }
    }
}
