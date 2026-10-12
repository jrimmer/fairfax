import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Settings, in the reference app's panes: General, Appearance, Tasks, Today & Lists,
// Alerts, Backups, and Import & Export. Everything applies at once.
Window {
    id: root

    title: "Precrime Settings"
    width: 720
    height: 560
    minimumWidth: 560
    minimumHeight: 420
    color: theme.background

    Material.theme: theme.dark ? Material.Dark : Material.Light
    Material.accent: theme.accent
    Material.background: theme.raised
    Material.foreground: theme.foreground

    property int pane: 0
    readonly property var panes: ["General", "Appearance", "Tasks", "Today & Lists", "Alerts", "Sync", "Backups", "Import & Export", "Calendar", "Automation"]

    function value(key, fallback) {
        const v = backend.setting(key).value
        return v === undefined || v === null || v === "" ? fallback : v
    }
    function bool(key, fallback) {
        const v = value(key, fallback)
        return v === true || v === "true" || v === 1 || v === "1"
    }

    // Omarchy windows have no title bar, so Settings closes itself: Escape,
    // Ctrl+W, or the ✕ in the corner (Super+W works too, as for any window).
    onVisibleChanged: if (visible) { requestActivate(); keyCatcher.forceActiveFocus() }

    Connections {
        target: filePicker
        function onPicked(path, purpose) {
            if (purpose === "export-csv") backend.exportTo(path, "csv")
            else if (purpose === "export-ics") backend.exportTo(path, "ics")
            else if (purpose === "import-ics") backend.importFrom(path, "ics")
            else if (purpose === "import-taskdb") backend.importFrom(path, "taskapp")
            else if (purpose === "import-ticktick") backend.importFrom(path, "ticktick")
            else if (purpose === "restore") backend.restoreBackup(path)
        }
        function onFailed(message) { status.text = message }
    }

    Item {
        id: keyCatcher
        anchors.fill: parent
        focus: true
        Keys.onEscapePressed: root.close()
        Shortcut { sequences: ["Escape", "Ctrl+W"]; context: Qt.WindowShortcut; onActivated: root.close() }
    }

    IconButton {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 10
        z: 10
        icon: "close"
        size: 14
        tip: "Close (Esc)"
        onClicked: root.close()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 180
            color: theme.sidebar

            // About, in the corner: what this is, which version, whose, and
            // under what licence.
            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 14
                spacing: 3
                Row {
                    spacing: 8
                    Image { source: "qrc:/icon.svg"; width: 28; height: 28; sourceSize: Qt.size(56, 56); anchors.verticalCenter: parent.verticalCenter }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        Text { text: "Precrime"; color: theme.foreground; font.pixelSize: backend.fontSize; font.bold: true }
                        Text { text: "Version " + Qt.application.version; color: theme.muted; font.pixelSize: backend.fontSize - 3 }
                    }
                }
                Text {
                    topPadding: 4
                    text: "github.com/jrimmer/precrime"
                    color: theme.today
                    font.pixelSize: backend.fontSize - 3
                    font.underline: linkMouse.containsMouse
                    MouseArea {
                        id: linkMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Qt.openUrlExternally("https://github.com/jrimmer/precrime")
                    }
                }
                Text { text: "© 2026 Jason Rimmer"; color: theme.muted; font.pixelSize: backend.fontSize - 3 }
                Text {
                    text: "MIT License"
                    color: theme.today
                    font.pixelSize: backend.fontSize - 3
                    font.underline: licenseMouse.containsMouse
                    MouseArea {
                        id: licenseMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: licenseDialog.open()
                    }
                }
            }

            Column {
                anchors.fill: parent
                anchors.topMargin: 14
                spacing: 2
                Repeater {
                    model: root.panes
                    Rectangle {
                        required property int index
                        required property string modelData
                        x: 8
                        width: parent.width - 16
                        height: 32
                        radius: 6
                        color: root.pane === index ? theme.selection : "transparent"
                        Text { x: 12; anchors.verticalCenter: parent.verticalCenter; text: modelData; color: theme.foreground; font.pixelSize: backend.fontSize }
                        MouseArea { anchors.fill: parent; onClicked: root.pane = index }
                    }
                }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width - 48
                x: 24
                y: 20
                spacing: 6

                Text {
                    text: root.panes[root.pane]
                    color: theme.foreground
                    font.pixelSize: backend.fontSize + 6
                    font.weight: Font.Bold
                    Layout.bottomMargin: 10
                }

                // General
                ColumnLayout {
                    visible: root.pane === 0
                    Layout.fillWidth: true
                    Toggle { key: "general.keepRunning"; fallback: true; label: "Keep running in the background when the window closes"; note: "Alarms and Quick Entry need Precrime running. Q or Ctrl+Q quits for real." }
                    Toggle { key: "quickEntry.clipboard"; fallback: true; label: "Auto-fill Quick Entry notes with text from the clipboard" }
                    Toggle { key: "omarchy.fitThirds"; fallback: true; label: "Open into a free third when windows are snapped to thirds"; note: "On Hyprland, when the other windows on the workspace float in thirds, Precrime takes the free one. With tiled windows Hyprland places it as usual." }
                    Toggle { key: "general.trayIcon"; fallback: true; label: "Show Precrime in the bar's tray"; note: "Its menu lists today's tasks, with New Task, Find, Sync and Quit." }
                    Toggle { key: "general.sounds"; fallback: true; label: "Play task completion sounds" }
                    Toggle { key: "general.celebrate"; fallback: true; label: "Celebrate when a list is cleared" }
                    Hint { text: "On Omarchy, run  precrime setup omarchy  in a terminal: it adds a Quick Entry key (Super+Shift+T), floating windows, a Precrime section in the Super+Space menu, and starts Precrime at sign-in so alarms arrive. Undo with  precrime setup omarchy --remove." }
                }

                // Appearance
                ColumnLayout {
                    visible: root.pane === 1
                    Layout.fillWidth: true
                    RowLayout {
                        Label { text: "Appearance"; Layout.preferredWidth: 160 }
                        // System follows the Omarchy theme; Dark and Light force one.
                        Repeater {
                            model: [["system", "System"], ["dark", "Dark"], ["light", "Light"]]
                            Button {
                                required property var modelData
                                text: modelData[1]
                                checkable: true
                                autoExclusive: true
                                checked: theme.scheme === modelData[0]
                                highlighted: checked
                                flat: !checked
                                onClicked: backend.setSetting("appearance.scheme", modelData[0])
                            }
                        }
                    }
                    RowLayout {
                        Label { text: "Text size"; Layout.preferredWidth: 160 }
                        Slider { from: 0; to: 8; stepSize: 1; snapMode: Slider.SnapAlways; value: backend.textSize; onMoved: backend.textSize = value; Layout.fillWidth: true }
                        Label { text: backend.textSizeName; Layout.preferredWidth: 70 }
                    }
                    RowLayout {
                        Label { text: "Content width"; Layout.preferredWidth: 160 }
                        Slider { from: 1400; to: 640; stepSize: 20; value: Number(root.value("appearance.contentWidth", 980)); onMoved: backend.setSetting("appearance.contentWidth", value); Layout.fillWidth: true }
                        Label { text: "Compact"; color: theme.muted }
                    }
                    RowLayout {
                        Label { text: "Task rows"; Layout.preferredWidth: 160 }
                        ComboBox {
                            model: ["Compressed", "Normal", "Extended"]
                            currentIndex: Number(root.value("appearance.density", 1))
                            onActivated: (i) => backend.setSetting("appearance.density", i)
                        }
                    }
                    RowLayout {
                        Label { text: "Mini calendar busy days"; Layout.preferredWidth: 160 }
                        ComboBox {
                            readonly property var styles: ["heat", "dots", "none"]
                            model: ["Heat Map", "Dots", "None"]
                            currentIndex: Math.max(0, styles.indexOf(String(root.value("appearance.calendarStyle", "heat"))))
                            onActivated: (i) => backend.setSetting("appearance.calendarStyle", styles[i])
                        }
                    }
                    Toggle { key: "appearance.friendlyDates"; fallback: true; label: "Use friendly dates (Today, Tomorrow, In 28 days)" }
                    Toggle { key: "appearance.notesPreview"; fallback: false; label: "Show notes under tasks" }
                    Toggle { key: "appearance.listNames"; fallback: false; label: "Always show list names"; note: "Otherwise hold Alt in the task list to reveal list and tag names." }
                    Hint { text: "Colours follow your Omarchy theme, light or dark, and change with it." }
                }

                // Tasks
                ColumnLayout {
                    visible: root.pane === 2
                    Layout.fillWidth: true
                    CheckBox {
                        text: "Getting Things Done® Inbox"
                        checked: backend.inboxEnabled()
                        onToggled: {
                            if (!checked && !confirmInbox.visible) { checked = true; confirmInbox.open(); return }
                            backend.setInboxEnabled(checked)
                        }
                    }
                    Toggle { key: "nl.quickEntry"; fallback: true; label: "Detect dates and details as you type in Quick Entry" }
                    Toggle { key: "nl.editor"; fallback: true; label: "Detect dates and details as you type in the task editor" }
                    RowLayout {
                        Label { text: "Show completed tasks for"; Layout.preferredWidth: 220 }
                        ComboBox {
                            readonly property var minutes: [0, 1, 60, 1440, 10080, 43200, -1]
                            model: ["Never", "1 minute", "1 hour", "1 day", "1 week", "1 month", "Always"]
                            currentIndex: Math.max(0, minutes.indexOf(Number(root.value("tasks.completedWindow", 1440))))
                            onActivated: (i) => backend.setSetting("tasks.completedWindow", minutes[i])
                        }
                    }
                    RowLayout {
                        Label { text: "Default list"; Layout.preferredWidth: 220 }
                        ComboBox {
                            // 0 is the first list and -1 the list used last.
                            model: [{ name: "First list", id: 0 }, { name: "Last used", id: -1 }].concat(backend.listChoices)
                            textRole: "name"
                            enabled: !backend.inboxEnabled()
                            currentIndex: Math.max(0, model.findIndex(l => l.id === Number(root.value("tasks.defaultList", 0))))
                            onActivated: (i) => backend.setSetting("tasks.defaultList", model[i].id)
                        }
                    }
                    Toggle { key: "tasks.alwaysDefaultList"; fallback: false; label: "Always use the default list for new tasks"; note: "Otherwise a task added from the main window goes to the list you are viewing." }
                    Toggle { key: "tasks.overdueUsesTime"; fallback: false; label: "Due time taken into account when marking a task overdue" }
                    RowLayout {
                        Label { text: "Default due date for new tasks"; Layout.preferredWidth: 220 }
                        ComboBox {
                            readonly property var days: [-1, 0, 1, 2, 3, 7, 14, 30]
                            model: ["None", "Same Day", "Day After", "2 Days Later", "3 Days Later", "1 Week Later", "2 Weeks Later", "Next Month"]
                            currentIndex: Math.max(0, days.indexOf(Number(root.value("tasks.defaultDueOffset", -1))))
                            onActivated: (i) => backend.setSetting("tasks.defaultDueOffset", days[i])
                        }
                    }
                    Toggle { key: "tasks.todayDue"; fallback: true; label: "Use today's date in the Today list" }
                    Toggle { key: "tasks.useDefaultDueTime"; fallback: false; label: "Give new due dates the default due time (Alerts pane)" }
                    RowLayout {
                        Label { text: "Auto-delete completed tasks"; Layout.preferredWidth: 220 }
                        ComboBox {
                            readonly property var days: [0, 1, 7, 30, 90, 180, 365]
                            model: ["Never", "After 1 day", "After 1 week", "After 1 month", "After 3 months", "After 6 months", "After 1 year"]
                            currentIndex: Math.max(0, days.indexOf(Number(root.value("tasks.autoDeleteDays", 0))))
                            onActivated: (i) => backend.setSetting("tasks.autoDeleteDays", days[i])
                        }
                    }
                    Toggle { key: "tasks.autoDeleteDelaySubtasks"; fallback: false; label: "Delay sub-tasks by 3 weeks" }
                    Toggle { key: "tasks.returnCreates"; fallback: false; label: "Hit Return to create a new task, Ctrl+Return to edit" }
                }

                // Today & Lists
                ColumnLayout {
                    visible: root.pane === 3
                    Layout.fillWidth: true
                    Toggle { key: "today.overdue"; fallback: true; label: "Include overdue items in Today" }
                    Toggle { key: "today.starred"; fallback: false; label: "Include starred items in Today" }
                    Toggle { key: "today.stillWorking"; fallback: true; label: "Include 'Still Working On' tasks in Today"; note: "Tasks whose start date has passed stay in Today until done." }
                    Toggle { key: "sort.groupedDates"; fallback: false; label: "Sorting by due date uses grouped dates"; note: "Today, Tomorrow, Next 7 Days and Future instead of one heading per day." }
                    Toggle { key: "sort.starredAboveHigh"; fallback: false; label: "Sort starred items above high priority" }
                }

                // Alerts
                ColumnLayout {
                    visible: root.pane === 4
                    Layout.fillWidth: true
                    RowLayout {
                        Label { text: "Alerts on date-only tasks fire at"; Layout.preferredWidth: 260 }
                        ComboBox {
                            readonly property var minutes: [360, 420, 480, 540, 600, 720]
                            model: ["6:00 AM", "7:00 AM", "8:00 AM", "9:00 AM", "10:00 AM", "12:00 PM"]
                            currentIndex: Math.max(0, minutes.indexOf(Number(root.value("tasks.defaultDueTime", 540))))
                            onActivated: (i) => backend.setSetting("tasks.defaultDueTime", minutes[i])
                        }
                    }
                    RowLayout {
                        Label { text: "Sound alerts repeat until dismissed"; Layout.preferredWidth: 260 }
                        ComboBox {
                            readonly property var minutes: [0, 2, 5, 10]
                            model: ["Never", "Every 2 mins", "Every 5 mins", "Every 10 mins"]
                            currentIndex: Math.max(0, minutes.indexOf(Number(root.value("alerts.nagMinutes", 0))))
                            onActivated: (i) => backend.setSetting("alerts.nagMinutes", minutes[i])
                        }
                    }
                    Hint { text: "Only alerts set to Message with Sound repeat; a plain Message is shown once." }

                    Label { text: "Default Reminders"; font.bold: true; Layout.topMargin: 12 }
                    RowLayout {
                        Label { text: "Remind me of tasks with a due time"; Layout.preferredWidth: 260 }
                        ComboBox {
                            readonly property var offsets: [-1, 0, 5, 10, 15, 30, 60, 120, 360, 720, 1440, 2880]
                            model: ["None", "At due time", "5 minutes before", "10 minutes before", "15 minutes before", "30 minutes before",
                                    "1 hour before", "2 hours before", "6 hours before", "12 hours before", "1 day before", "2 days before"]
                            currentIndex: Math.max(0, offsets.indexOf(Number(root.value("alerts.defaultTimed", -1))))
                            onActivated: (i) => backend.setSetting("alerts.defaultTimed", offsets[i])
                        }
                    }
                    RowLayout {
                        Label { text: "Remind me of tasks with a due date"; Layout.preferredWidth: 260 }
                        ComboBox {
                            readonly property var days: [-1, 0, 1, 2]
                            model: ["None", "On the day", "1 day before", "2 days before"]
                            currentIndex: Math.max(0, days.indexOf(Number(root.value("alerts.defaultDateOnly", -1))))
                            onActivated: (i) => backend.setSetting("alerts.defaultDateOnly", days[i])
                        }
                        Label { text: "at" }
                        ComboBox {
                            id: reminderTime
                            readonly property var minutes: [360, 420, 480, 540, 600, 660, 720, 780, 840, 900, 960, 1020, 1080, 1140, 1200, 1260]
                            model: minutes.map(m => ((m / 60) % 12 || 12) + ":00 " + (m < 720 ? "AM" : "PM"))
                            currentIndex: Math.max(0, minutes.indexOf(Number(root.value("alerts.defaultDateOnlyTime", root.value("tasks.defaultDueTime", 540)))))
                            onActivated: (i) => backend.setSetting("alerts.defaultDateOnlyTime", minutes[i])
                        }
                    }
                    Hint { text: "Added whenever a task gets a due date and has no alerts yet. You can still edit or remove alerts on any task." }
                    Hint { text: "Silence a single list from its right-click menu (Mute Alerts). Alerts appear as notifications with Complete and Snooze; Precrime has to be running to deliver them." }
                }

                // Sync
                ColumnLayout {
                    visible: root.pane === 5
                    Layout.fillWidth: true
                    Label { text: "Git"; font.bold: true }
                    Hint {
                        Layout.topMargin: 0
                        text: "Keep your tasks in a private git repository and every machine that points at it stays in step. "
                            + "Each task, list and tag is a small JSON file; git runs with your own SSH keys and never prompts."
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            id: remoteField
                            Layout.fillWidth: true
                            placeholderText: "git@github.com:you/tasks.git"
                            text: gitSync ? gitSync.remote : ""
                            enabled: !!gitSync
                        }
                        Button {
                            text: gitSync && gitSync.configured ? "Change" : "Connect"
                            enabled: !!gitSync && remoteField.text.trim() !== ""
                            onClicked: gitSync.setRemote(remoteField.text.trim())
                        }
                    }
                    RowLayout {
                        visible: !!gitSync && gitSync.configured
                        Button { text: gitSync && gitSync.busy ? "Syncing…" : "Sync Now"; enabled: !!gitSync && !gitSync.busy; onClicked: gitSync.syncNow() }
                        Button { text: "Disconnect"; flat: true; onClicked: gitSync.disconnectRemote() }
                    }
                    Text {
                        visible: !!gitSync
                        text: gitSync ? gitSync.status + (gitSync.lastSync && !isNaN(gitSync.lastSync.getTime()) ? "  ·  last synced " + Qt.formatDateTime(gitSync.lastSync, "ddd d MMM, h:mm AP") : "") : ""
                        color: theme.muted
                        font.pixelSize: backend.fontSize - 1
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Hint { visible: !!gitSync; text: gitSync ? "Working copy: " + gitSync.directory : "" }
                    Hint { visible: !gitSync; text: "Sync is off in demo mode." }
                    Label { text: "Todoist"; font.bold: true; Layout.topMargin: 16 }
                    Hint {
                        Layout.topMargin: 0
                        text: "Two-way sync with Todoist, so your phone sees your tasks. Paste your API token from Todoist's Settings → Integrations → Developer; it's kept in your keyring, not in Precrime's database. Lists, tags, dates, repeats, priority and sub-tasks sync; start dates, stars and task types ride along in a small metadata block in each task's description. List groups, smart lists and alerts stay local. Turn it on on one machine only if you also use git sync."
                    }
                    RowLayout {
                        visible: !!todoist && !todoist.connected
                        Layout.fillWidth: true
                        TextField { id: todoistToken; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: "Todoist API token" }
                        Button { text: "Connect"; enabled: todoistToken.text.trim() !== ""; onClicked: { todoist.connectAccount(todoistToken.text.trim()); todoistToken.text = "" } }
                    }
                    RowLayout {
                        visible: !!todoist && todoist.connected
                        Button { text: todoist && todoist.busy ? "Syncing…" : "Sync Now"; enabled: !!todoist && !todoist.busy; onClicked: todoist.syncNow() }
                        Button { text: "Disconnect"; flat: true; onClicked: todoist.disconnectAccount() }
                    }
                    Text { visible: !!todoist; text: todoist ? todoist.status : ""; color: theme.muted; font.pixelSize: backend.fontSize - 1; wrapMode: Text.Wrap; Layout.fillWidth: true }
                }

                // Backups
                ColumnLayout {
                    visible: root.pane === 6
                    Layout.fillWidth: true
                    RowLayout {
                        Label { text: "Back up automatically"; Layout.preferredWidth: 200 }
                        ComboBox {
                            readonly property var hours: [0, 24, 48]
                            model: ["Never", "Every 24 hours", "Every 48 hours"]
                            currentIndex: Math.max(0, hours.indexOf(Number(root.value("backup.intervalHours", 24))))
                            onActivated: (i) => backend.setSetting("backup.intervalHours", hours[i])
                        }
                    }
                    RowLayout {
                        Label { text: "Keep at most"; Layout.preferredWidth: 200 }
                        SpinBox { from: 1; to: 100; value: Number(root.value("backup.keep", 10)); onValueModified: backend.setSetting("backup.keep", value) }
                    }
                    RowLayout {
                        Button { text: "Back Up Now"; onClicked: { backend.backupNow(); backupList.model = backend.backups() } }
                        Button { text: "Restore from File…"; flat: true; onClicked: filePicker.open("Restore a Precrime backup", "restore") }
                    }
                    Hint { text: "Backups are kept in " + backend.backupFolder() }
                    Repeater {
                        id: backupList
                        model: root.pane === 6 ? backend.backups() : []
                        RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Label { text: modelData.date; Layout.fillWidth: true }
                            Label { text: modelData.size; color: theme.muted }
                            Button { text: "Restore"; flat: true; onClicked: { confirmRestore.path = modelData.path; confirmRestore.open() } }
                        }
                    }
                }

                // Import & Export
                ColumnLayout {
                    visible: root.pane === 7
                    Layout.fillWidth: true
                    Label { text: "Export"; font.bold: true }
                    RowLayout {
                        Button { text: "Choose Lists…"; onClicked: exportDialog.show() }
                        Button { text: "Everything as CSV…"; flat: true; onClicked: filePicker.save("Export tasks as CSV", "Precrime.csv", "export-csv") }
                        Button { text: "Everything as .ics…"; flat: true; onClicked: filePicker.save("Export tasks as iCalendar", "Precrime.ics", "export-ics") }
                    }
                    Label { text: "Import"; font.bold: true; Layout.topMargin: 12 }
                    RowLayout {
                        Button { text: "From TickTick…"; onClicked: filePicker.open("Choose a TickTick backup (.csv)", "import-ticktick") }
                        Button { text: "From the reference app…"; onClicked: filePicker.open("Choose a the reference app database (tasks.db or a .taskdb backup)", "import-taskdb") }
                        Button { text: "iCalendar (.ics)…"; onClicked: filePicker.open("Choose an iCalendar file", "import-ics") }
                    }
                    Hint { text: "From TickTick: in TickTick's web app, Settings → Account → Generate Backup, then choose the .csv here. Lists, tags, dates, repeats, reminders, checklists and completed history come across; importing the same backup again adds only what's new." }
                    Hint { text: "From the reference app: on the Mac, use a backup from the reference app > Settings > Backups (a .taskdb file). Lists, groups, tags, dates, repeats and alerts come across; one Ctrl+Z undoes the whole import, and a backup is made first." }
                }

                // Calendar
                ColumnLayout {
                    visible: root.pane === 8
                    Layout.fillWidth: true
                    Hint {
                        Layout.topMargin: 0
                        text: "Show your calendar's events in a day bar above Today. Add one iCalendar source per line: a file path, or an https:// or webcal:// address (Google, Fastmail and Nextcloud offer private .ics links). Drag a task onto the bar to block out time for it."
                    }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 120
                        TextArea {
                            id: calendarSources
                            text: String(root.value("calendar.sources", ""))
                            placeholderText: "https://calendar.example.com/private.ics"
                            wrapMode: TextEdit.NoWrap
                            font.family: theme.monoFont
                            font.pixelSize: backend.fontSize - 2
                        }
                    }
                    Button {
                        text: "Apply"
                        onClicked: {
                            backend.setSetting("calendar.sources", calendarSources.text.trim())
                            calendar.sources = calendarSources.text.split("\n").map(l => l.trim()).filter(l => l.length)
                        }
                    }
                    Toggle { key: "calendar.inLists"; fallback: false; label: "Show events above every list, not just Today" }
                    Text { visible: !!calendar && calendar.error !== ""; text: calendar ? calendar.error : ""; color: theme.overdue; wrapMode: Text.Wrap; Layout.fillWidth: true }
                }

                // Automation
                ColumnLayout {
                    visible: root.pane === 9
                    Layout.fillWidth: true
                    Hint {
                        Layout.topMargin: 0
                        text: "Let Claude work with your tasks: add Precrime as an MCP server. In Claude Code run:\n  claude mcp add precrime -- precrime mcp\n\nScripts can use the same commands from a terminal:\n  precrime ctl help\n  precrime ctl create_task title=\"Call Sam\" due_date=2026-10-12\n\nLinks like precrime://x-callback-url/add?task=Buy%20milk open in the app.\n\nWaybar: add a custom module that runs  precrime --waybar  every 60 seconds to show overdue and due-today counts."
                    }
                }

                Text { id: status; color: theme.overdue; font.pixelSize: backend.fontSize - 1; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
        }
    }

    Dialog {

        width: Math.min(440, (parent ? parent.width : 480) - 40)
        id: confirmInbox
        anchors.centerIn: parent
        modal: true
        title: "Turn off the Inbox?"
        Label { text: "The Inbox list and its tasks are deleted. Ctrl+Z brings them back." }
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: backend.setInboxEnabled(false)
    }

    Dialog {

        width: Math.min(440, (parent ? parent.width : 480) - 40)
        id: confirmRestore
        property string path
        anchors.centerIn: parent
        modal: true
        title: "Restore this backup?"
        Label { text: "Everything is replaced by the backup. Your current tasks are backed up first." }
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: backend.restoreBackup(path)
    }

    ExportDialog { id: exportDialog; owner: "settings" }

    component Toggle: ColumnLayout {
        property string key
        property bool fallback: false
        property string label
        property string note
        Layout.fillWidth: true
        spacing: 0
        CheckBox {
            text: parent.label
            checked: root.bool(parent.key, parent.fallback)
            onToggled: backend.setSetting(parent.key, checked)
        }
        Text {
            visible: parent.note !== ""
            text: parent.note
            color: theme.muted
            font.pixelSize: backend.fontSize - 2
            leftPadding: 44
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }

    component Hint: Text {
        color: theme.muted
        font.pixelSize: backend.fontSize - 2
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        Layout.topMargin: 8
    }
}
