import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Settings, in the reference app's panes: General, Appearance, Tasks, Today & Lists,
// Alerts, Sync, Mail, Backups, and Import & Export. Everything applies at once.
Window {
    id: root

    title: "Fairfax Settings"
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
    property var hideable: backend.hideableLists()
    property bool inboxOn: backend.inboxEnabled()
    Connections {
        target: backend
        function onDataChanged() { root.hideable = backend.hideableLists(); root.inboxOn = backend.inboxEnabled() }
        function onSettingsChanged() { root.hideable = backend.hideableLists() }
    }
    property string copySource
    readonly property var panes: ["General", "Appearance", "Tasks", "Today & Lists", "Alerts", "Sync", "Mail", "Backups", "Import & Export", "Calendar", "Automation", "Shortcuts", "Diagnostics"]

    function value(key, fallback) {
        backend.settingsRevision  // Re-evaluate the caller's binding whenever any setting changes.
        const v = backend.setting(key).value
        return v === undefined || v === null || v === "" ? fallback : v
    }
    // Every half hour of the day, in minutes after midnight, for time pickers.
    readonly property var timeSteps: Array.from({ length: 48 }, (_, i) => i * 30)
    function timeLabel(minutes) {
        const m = minutes % 60
        return ((Math.floor(minutes / 60) % 12) || 12) + ":" + (m < 10 ? "0" : "") + m + " " + (minutes < 720 ? "AM" : "PM")
    }
    function bool(key, fallback) {
        const v = value(key, fallback)
        return v === true || v === "true" || v === 1 || v === "1"
    }
    // The five commands the tray menu can keep, as the one "tray.commands"
    // setting holds them.
    readonly property var trayCommandNames: ["new", "open", "find", "sync", "settings"]
    function commandOn(name) {
        return String(value("tray.commands", "new,open,find,sync,settings")).split(",")
            .map(c => c.trim()).indexOf(name) >= 0
    }
    function setCommand(name, on) {
        const kept = String(value("tray.commands", "new,open,find,sync,settings")).split(",")
            .map(c => c.trim()).filter(c => c.length && c !== name)
        if (on) kept.push(name)
        kept.sort((a, b) => trayCommandNames.indexOf(a) - trayCommandNames.indexOf(b))
        backend.setSetting("tray.commands", kept.join(","))
    }
    function pickTrayBadgeColour() {
        trayColourPicker.pick(String(value("tray.badgeColor", "#E5484D")))
    }

    // Omarchy windows have no title bar, so Settings closes itself: Escape,
    // Ctrl+W, or the ✕ in the corner (Super+W works too, as for any window).
    onVisibleChanged: { if (visible) { requestActivate(); keyCatcher.forceActiveFocus() } else shortcutPane.clearTransient() }

    Connections {
        target: filePicker
        function onPicked(path, purpose) {
            if (purpose === "export-csv") backend.exportTo(path, "csv")
            else if (purpose === "export-ics") backend.exportTo(path, "ics")
            else if (purpose === "import-ics") backend.importFrom(path, "ics")
            else if (purpose === "import-taskdb") backend.importFrom(path, "taskapp")
            else if (purpose === "import-ticktick") backend.importFrom(path, "ticktick")
            else if (purpose === "import-things") backend.importFrom(path, "things")
            else if (purpose === "import-omnifocus") backend.importFrom(path, "omnifocus")
            else if (purpose === "restore") backend.restoreBackup(path)
            else if (purpose === "sync-folder" && folderSync) folderSync.setFolder(path)
            else if (purpose === "mail-client-json") mailPane.clientError = mailFetch.importClientJson(path)
            else if (purpose === "backup-copy") backend.saveBackupCopy(copySource, path)
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

    // A click anywhere ends a shortcut recording; the key buttons start theirs
    // after this runs.
    TapHandler {
        parent: root.contentItem
        gesturePolicy: TapHandler.DragThreshold
        onPressedChanged: if (pressed && shortcutPane.recordingId !== "") shortcutPane.stopRecording()
    }

    IconButton {
        objectName: "settingsClose"
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
                        Text { objectName: "aboutName"; text: "Fairfax"; color: theme.foreground; font.pixelSize: backend.fontSize; font.bold: true }
                        Text { objectName: "aboutVersion"; text: "Version " + Qt.application.version; color: theme.muted; font.pixelSize: backend.fontSize - 3 }
                    }
                }
                Text {
                    objectName: "aboutLink"
                    topPadding: 4
                    text: "github.com/jrimmer/fairfax"
                    color: theme.today
                    font.pixelSize: backend.fontSize - 3
                    font.underline: linkMouse.containsMouse
                    MouseArea {
                        id: linkMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Qt.openUrlExternally("https://github.com/jrimmer/fairfax")
                    }
                }
                Text { text: "© 2026 Jason Rimmer"; color: theme.muted; font.pixelSize: backend.fontSize - 3 }
                Text {
                    objectName: "aboutLicense"
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
                        objectName: "settingsPane:" + modelData
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
            // The column sits 20 px down, so the scrolling height has to count that, or the last control of a long pane is clipped.
            contentHeight: paneColumn.implicitHeight + 40

            ColumnLayout {
                id: paneColumn
                width: parent.width - 48
                x: 24
                y: 20
                spacing: 6

                // The scrolling view takes presses first, so a click on the
                // pane's own empty space ends a shortcut recording here.
                TapHandler {
                    gesturePolicy: TapHandler.DragThreshold
                    onPressedChanged: if (pressed && shortcutPane.recordingId !== "") shortcutPane.stopRecording()
                }

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
                    Toggle { key: "general.keepRunning"; fallback: true; label: "Keep running in the background when the window closes"; note: "Alarms and Quick Entry need Fairfax running. " + backend.shortcutText("app.quit") + " quits for real." }
                    Toggle { key: "quickEntry.clipboard"; fallback: true; label: "Auto-fill Quick Entry notes with text from the clipboard" }
                    Toggle { key: "omarchy.fitThirds"; fallback: true; label: "Open into a free third when windows are snapped to thirds"; note: "On Hyprland, when the other windows on the workspace float in thirds, Fairfax takes the free one. With tiled windows Hyprland places it as usual." }
                    Toggle { key: "general.trayIcon"; fallback: true; label: "Show Fairfax in the bar's tray"; note: "Its menu shows a quick view and commands; choose them below." }
                    ColumnLayout {
                        // The tray menu's content, only while the tray icon is on.
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        spacing: 6
                        enabled: root.bool("general.trayIcon", true)
                        opacity: enabled ? 1 : 0.5
                        Label { text: "Tray menu"; font.bold: true }
                        RowLayout {
                            Label { text: "Quick view"; Layout.preferredWidth: 160 }
                            ComboBox {
                                objectName: "setting:tray.source"
                                implicitContentWidthPolicy: ComboBox.WidestText
                                textRole: "name"
                                valueRole: "id"
                                model: backend.traySourceChoices
                                currentIndex: Math.max(0, model.findIndex(c => c.id === String(root.value("tray.source", "today"))))
                                onActivated: (i) => backend.setSetting("tray.source", model[i].id)
                            }
                        }
                        RowLayout {
                            Label { text: "Tasks shown"; Layout.preferredWidth: 160 }
                            SpinBox {
                                objectName: "setting:tray.count"
                                from: 3; to: 30
                                value: Number(root.value("tray.count", 12))
                                onValueModified: backend.setSetting("tray.count", value)
                            }
                        }
                        RowLayout {
                            Label { text: "Clicking a task"; Layout.preferredWidth: 160 }
                            ComboBox {
                                objectName: "setting:tray.click"
                                implicitContentWidthPolicy: ComboBox.WidestText
                                readonly property var modes: ["open", "complete"]
                                model: ["Opens it in Fairfax", "Completes it"]
                                currentIndex: Math.max(0, modes.indexOf(String(root.value("tray.click", "open"))))
                                onActivated: (i) => backend.setSetting("tray.click", modes[i])
                            }
                        }
                        RowLayout {
                            Label { text: "Commands"; Layout.preferredWidth: 160; Layout.alignment: Qt.AlignTop }
                            ColumnLayout {
                                spacing: 0
                                CheckBox { objectName: "setting:tray.commands.new"; text: "New Task"; checked: root.commandOn("new"); onToggled: root.setCommand("new", checked) }
                                CheckBox { objectName: "setting:tray.commands.open"; text: "Open Fairfax"; checked: root.commandOn("open"); onToggled: root.setCommand("open", checked) }
                                CheckBox { objectName: "setting:tray.commands.find"; text: "Find a Task"; checked: root.commandOn("find"); onToggled: root.setCommand("find", checked) }
                                CheckBox { objectName: "setting:tray.commands.sync"; text: "Sync Now"; checked: root.commandOn("sync"); onToggled: root.setCommand("sync", checked) }
                                CheckBox { objectName: "setting:tray.commands.settings"; text: "Settings"; checked: root.commandOn("settings"); onToggled: root.setCommand("settings", checked) }
                            }
                        }
                        RowLayout {
                            Label { text: "Badge colour"; Layout.preferredWidth: 160 }
                            Button {
                                objectName: "setting:tray.badgeColor"
                                implicitWidth: 48
                                implicitHeight: 28
                                background: Rectangle {
                                    radius: 6
                                    color: String(root.value("tray.badgeColor", "#E5484D"))
                                    border.width: 1
                                    border.color: theme.divider
                                }
                                onClicked: root.pickTrayBadgeColour()
                            }
                            Button { objectName: "setting:tray.badgeColorReset"; text: "Reset"; flat: true; onClicked: backend.setSetting("tray.badgeColor", "#E5484D") }
                        }
                        Hint { text: "The number on the badge comes from Alerts > Badge shows." }
                    }
                    RowLayout {
                        Label { text: "Week starts on"; Layout.preferredWidth: 160 }
                        ComboBox {
                            objectName: "setting:general.weekStart"
                            // 0 follows the system locale; 1 to 7 are Qt's Monday to Sunday.
                            readonly property var days: [0, 1, 7, 6]
                            model: ["Automatic (" + Qt.locale().dayName(Qt.locale().firstDayOfWeek, Locale.LongFormat) + ")", "Monday", "Sunday", "Saturday"]
                            implicitContentWidthPolicy: ComboBox.WidestText
                            currentIndex: Math.max(0, days.indexOf(Number(root.value("general.weekStart", 0))))
                            onActivated: (i) => backend.setSetting("general.weekStart", days[i])
                        }
                    }
                    Hint { Layout.topMargin: 0; text: "Used by this-week searches and Focus Filter, the mini calendar, date pickers and weekly repeats. Fairfax is English only; month and day names are English and dates are laid out for the system locale (" + Qt.locale().name + ")." }
                    Toggle { key: "general.sounds"; fallback: true; label: "Play task completion sounds" }
                    Toggle { key: "general.celebrate"; fallback: true; label: "Celebrate when a list is cleared" }
                    Hint { text: "On Omarchy, run  fairfax setup omarchy  in a terminal: it adds a Quick Entry key (Super+Shift+T), floating windows, a Fairfax section in the Super+Space menu, and starts Fairfax at sign-in so alarms arrive. Undo with  fairfax setup omarchy --remove." }
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
                                objectName: "setting:appearance.scheme:" + modelData[0]
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
                        Slider { objectName: "setting:appearance.textSize"; from: 0; to: 8; stepSize: 1; snapMode: Slider.SnapAlways; value: backend.textSize; onMoved: backend.textSize = value; Layout.fillWidth: true }
                        Label { text: backend.textSizeName; Layout.preferredWidth: 70 }
                    }
                    RowLayout {
                        Label { text: "Content width"; Layout.preferredWidth: 160 }
                        Slider { objectName: "setting:appearance.contentWidth"; from: 1400; to: 640; stepSize: 20; value: Number(root.value("appearance.contentWidth", 980)); onMoved: backend.setSetting("appearance.contentWidth", value); Layout.fillWidth: true }
                        Label { text: "Compact"; color: theme.muted }
                    }
                    RowLayout {
                        Label { text: "Task rows"; Layout.preferredWidth: 160 }
                        ComboBox {
                            objectName: "setting:appearance.density"
                            implicitContentWidthPolicy: ComboBox.WidestText
                            model: ["Compressed", "Normal", "Extended"]
                            currentIndex: Number(root.value("appearance.density", 1))
                            onActivated: (i) => backend.setSetting("appearance.density", i)
                        }
                    }
                    RowLayout {
                        Label { text: "Mini calendar busy days"; Layout.preferredWidth: 160 }
                        ComboBox {
                            objectName: "setting:appearance.calendarStyle"
                            implicitContentWidthPolicy: ComboBox.WidestText
                            readonly property var styles: ["heat", "dots", "none"]
                            model: ["Heat Map", "Dots", "None"]
                            currentIndex: Math.max(0, styles.indexOf(String(root.value("appearance.calendarStyle", "heat"))))
                            onActivated: (i) => backend.setSetting("appearance.calendarStyle", styles[i])
                        }
                    }
                    Toggle { key: "appearance.monthDividers"; fallback: true; label: "Show month dividers in the mini calendar" }
                    Toggle { key: "appearance.friendlyDates"; fallback: true; label: "Use friendly dates (Today, Tomorrow, In 28 days)" }
                    Toggle { key: "appearance.notesPreview"; fallback: false; label: "Show notes under tasks" }
                    Toggle { key: "appearance.listNames"; fallback: false; label: "Always show list names"; note: "Otherwise hold Alt in the task list to reveal list and tag names." }
                    Toggle { key: "appearance.tagNames"; fallback: true; label: "Always show tag names"; note: "Off shows a tag icon instead; hold Alt to reveal the names. Narrow windows always show the icon." }
                    Toggle { key: "appearance.overdueRed"; fallback: true; label: "Show overdue tasks in red" }
                    Toggle { key: "appearance.headingDurations"; fallback: true; label: "Show durations in headings"; note: "The time still to do under each heading, such as 2h 45m." }
                    Hint { text: "Colours follow your Omarchy theme, light or dark, and change with it." }
                }

                // Tasks
                ColumnLayout {
                    visible: root.pane === 2
                    Layout.fillWidth: true
                    CheckBox {
                        objectName: "setting:inbox"
                        text: "Getting Things Done® Inbox"
                        checked: root.inboxOn
                        onToggled: {
                            if (!checked && !confirmInbox.visible) { checked = Qt.binding(() => root.inboxOn); confirmInbox.open(); return }
                            backend.setInboxEnabled(checked)
                        }
                    }
                    Toggle { key: "nl.quickEntry"; fallback: true; label: "Detect dates and details as you type in Quick Entry" }
                    Toggle { key: "nl.editor"; fallback: true; label: "Detect dates and details as you type in the task editor" }
                    RowLayout {
                        Label { text: "Show completed tasks for"; Layout.preferredWidth: 220 }
                        ComboBox {
                            objectName: "setting:tasks.completedWindow"
                            implicitContentWidthPolicy: ComboBox.WidestText
                            readonly property var minutes: [0, 1, 60, 1440, 10080, 43200, -1]
                            model: ["Never", "1 minute", "1 hour", "1 day", "1 week", "1 month", "Always"]
                            currentIndex: Math.max(0, minutes.indexOf(Number(root.value("tasks.completedWindow", 1440))))
                            onActivated: (i) => backend.setSetting("tasks.completedWindow", minutes[i])
                        }
                    }
                    Toggle { key: "tasks.completedRuleSubtasks"; fallback: true; label: "Apply the completed rule to sub-tasks"; note: "Off keeps completed project sub-tasks in view, whatever the setting above says." }
                    RowLayout {
                        Label { text: "Default list"; Layout.preferredWidth: 220 }
                        ComboBox {
                            objectName: "setting:tasks.defaultList"
                            implicitContentWidthPolicy: ComboBox.WidestText
                            // 0 is the first list and -1 the list used last.
                            model: [{ name: "First list", id: 0 }, { name: "Last used", id: -1 }].concat(backend.listChoices)
                            textRole: "name"
                            enabled: !root.inboxOn
                            currentIndex: Math.max(0, model.findIndex(l => l.id === Number(root.value("tasks.defaultList", 0))))
                            onActivated: (i) => backend.setSetting("tasks.defaultList", model[i].id)
                        }
                    }
                    Toggle { key: "tasks.alwaysDefaultList"; fallback: false; label: "Always use the default list for new tasks"; note: "Otherwise a task added from the main window goes to the list you are viewing." }
                    Toggle { key: "tasks.overdueUsesTime"; fallback: false; label: "Due time taken into account when marking a task overdue" }
                    RowLayout {
                        Label { text: "Default due date for new tasks"; Layout.preferredWidth: 220 }
                        ComboBox {
                            objectName: "setting:tasks.defaultDueOffset"
                            implicitContentWidthPolicy: ComboBox.WidestText
                            readonly property var days: [-1, 0, 1, 2, 3, 7, 14, 30]
                            model: ["None", "Same Day", "Day After", "2 Days Later", "3 Days Later", "1 Week Later", "2 Weeks Later", "Next Month"]
                            currentIndex: Math.max(0, days.indexOf(Number(root.value("tasks.defaultDueOffset", -1))))
                            onActivated: (i) => backend.setSetting("tasks.defaultDueOffset", days[i])
                        }
                    }
                    // Quick Entry only, or everywhere but Quick Entry: never both.
                    CheckBox {
                        objectName: "setting:tasks.defaultDueQuickEntryOnly"
                        enabled: Number(root.value("tasks.defaultDueOffset", -1)) >= 0
                        text: "Only use the default due date in Quick Entry"
                        checked: root.bool("tasks.defaultDueQuickEntryOnly", false)
                        onToggled: {
                            backend.setSetting("tasks.defaultDueQuickEntryOnly", checked)
                            if (checked) backend.setSetting("tasks.defaultDueNotQuickEntry", false)
                        }
                    }
                    CheckBox {
                        objectName: "setting:tasks.defaultDueNotQuickEntry"
                        enabled: Number(root.value("tasks.defaultDueOffset", -1)) >= 0
                        text: "Do not use the default due date in Quick Entry"
                        checked: root.bool("tasks.defaultDueNotQuickEntry", false)
                        onToggled: {
                            backend.setSetting("tasks.defaultDueNotQuickEntry", checked)
                            if (checked) backend.setSetting("tasks.defaultDueQuickEntryOnly", false)
                        }
                    }
                    Toggle { key: "tasks.todayDue"; fallback: true; label: "Use today's date in the Today list" }
                    RowLayout {
                        CheckBox {
                            id: useDueTime
                            objectName: "setting:tasks.useDefaultDueTime"
                            text: "Default due time"
                            checked: root.bool("tasks.useDefaultDueTime", false)
                            onToggled: backend.setSetting("tasks.useDefaultDueTime", checked)
                        }
                        ComboBox {
                            objectName: "setting:tasks.defaultDueTime"
                            enabled: useDueTime.checked
                            model: root.timeSteps.map(m => root.timeLabel(m))
                            implicitContentWidthPolicy: ComboBox.WidestText
                            currentIndex: Math.max(0, root.timeSteps.indexOf(Number(root.value("tasks.defaultDueTime", 540))))
                            onActivated: (i) => backend.setSetting("tasks.defaultDueTime", root.timeSteps[i])
                        }
                    }
                    Hint { Layout.topMargin: 0; text: "The time a task gets the first time you give it a due date without choosing one. Leave it off to keep dates without a clock time." }
                    RowLayout {
                        Label { text: "Auto-delete completed tasks"; Layout.preferredWidth: 220 }
                        ComboBox {
                            objectName: "setting:tasks.autoDeleteDays"
                            implicitContentWidthPolicy: ComboBox.WidestText
                            readonly property var days: [0, 1, 7, 30, 90, 180, 365]
                            model: ["Never", "After 1 day", "After 1 week", "After 1 month", "After 3 months", "After 6 months", "After 1 year"]
                            currentIndex: Math.max(0, days.indexOf(Number(root.value("tasks.autoDeleteDays", 0))))
                            onActivated: (i) => backend.setSetting("tasks.autoDeleteDays", days[i])
                        }
                    }
                    Toggle { key: "tasks.autoDeleteDelaySubtasks"; fallback: false; label: "Delay sub-tasks by 3 weeks" }
                    Toggle { key: "tasks.returnCreates"; fallback: false; label: "Hit Return to create a new task, Ctrl+Return to edit" }
                    Toggle { key: "quickopen.selectInPlace"; fallback: true; label: "Quick Open selects a task in place when the view shows it"; note: "Otherwise it always switches to the task's own list." }
                    RowLayout {
                        Label { text: "Due time shortcuts"; Layout.preferredWidth: 220 }
                        TextField {
                            objectName: "setting:tasks.dueTimeChips"
                            Layout.fillWidth: true
                            placeholderText: "9am, 12pm, 3pm, 6pm"
                            text: String(backend.setting("tasks.dueTimeChips").value || "")
                            onEditingFinished: backend.setSetting("tasks.dueTimeChips", text.trim())
                        }
                    }
                    Toggle { key: "tasks.quickLook"; fallback: true; label: "Enable Quick Look for tasks"; note: "Space on a task opens a zoomed-in view of it." }
                }

                // Today & Lists
                ColumnLayout {
                    visible: root.pane === 3
                    Layout.fillWidth: true
                    Toggle { key: "today.overdue"; fallback: true; label: "Include overdue items in Today" }
                    Toggle { key: "today.starred"; fallback: false; label: "Include starred items in Today" }
                    Toggle { key: "today.stillWorking"; fallback: true; label: "Include 'Still Working On' tasks in Today"; note: "Tasks whose start date has passed stay in Today until done." }
                    Toggle { key: "lists.startedVisible"; fallback: true; label: "Keep started tasks visible in All and lists"; note: "Off hides a task whose start date has passed unless it is due today or overdue. Today has its own setting above." }
                    Toggle { key: "focus.subtasksSeparately"; fallback: true; label: "Always list matched sub-tasks separately"; note: "In Focus and Smart Lists. Off hides a project's sub-tasks when the project is in the list too." }
                    Toggle { key: "sort.groupedDates"; fallback: false; label: "Sorting by due date uses grouped dates"; note: "Today, Tomorrow, Next 7 Days and Future instead of one heading per day." }
                    Toggle { key: "sort.starredAboveHigh"; fallback: false; label: "Sort starred items above high priority" }
                    Toggle { key: "sort.listOrder"; fallback: true; label: "Sorting by list uses your list order"; note: "Off sorts the sections A to Z by list name." }
                    RowLayout {
                        Label { text: "Counts on lists"; Layout.preferredWidth: 260 }
                        ComboBox {
                            objectName: "setting:lists.counts"
                            implicitContentWidthPolicy: ComboBox.WidestText
                            model: ["None", "Overdue", "Overdue + Undone"]
                            currentIndex: Math.max(0, Math.min(2, Number(root.value("lists.counts", 2))))
                            onActivated: (i) => backend.setSetting("lists.counts", i)
                        }
                    }
                    Toggle { key: "lists.undoneIncludesOverdue"; fallback: true; label: "Undone counts include overdue"; note: "Off shows only the tasks that are not yet late next to the red overdue number." }
                    Toggle { key: "search.inheritInProject"; fallback: false; label: "Inherit search in project view"; note: "Opening a project from a search or Smart List shows only the sub-tasks that match it." }
                    Label { text: "Hide lists"; font.bold: true; Layout.topMargin: 10 }
                    Hint { text: "Hidden lists vanish from the sidebar, All, Today, Smart Lists and searches. Reveal them for this session with Show Hidden Lists in the sidebar's + menu." }
                    Repeater {
                        model: root.pane === 3 ? root.hideable : []
                        CheckBox {
                            required property var modelData
                            objectName: "hideList:" + modelData.id
                            text: modelData.name
                            checked: modelData.hidden
                            onToggled: backend.setListHidden(modelData.id, checked)
                        }
                    }
                }

                // Alerts
                ColumnLayout {
                    visible: root.pane === 4
                    Layout.fillWidth: true
                    RowLayout {
                        Label { text: "Alerts on date-only tasks fire at"; Layout.preferredWidth: 260 }
                        ComboBox {
                            objectName: "setting:tasks.defaultDueTime"
                            readonly property var minutes: root.timeSteps
                            model: minutes.map(m => root.timeLabel(m))
                            implicitContentWidthPolicy: ComboBox.WidestText
                            currentIndex: Math.max(0, minutes.indexOf(Number(root.value("tasks.defaultDueTime", 540))))
                            onActivated: (i) => backend.setSetting("tasks.defaultDueTime", minutes[i])
                        }
                    }
                    RowLayout {
                        Label { text: "Sound alerts repeat until dismissed"; Layout.preferredWidth: 260 }
                        ComboBox {
                            objectName: "setting:alerts.nagMinutes"
                            implicitContentWidthPolicy: ComboBox.WidestText
                            readonly property var minutes: [0, 2, 5, 10]
                            model: ["Never", "Every 2 mins", "Every 5 mins", "Every 10 mins"]
                            currentIndex: Math.max(0, minutes.indexOf(Number(root.value("alerts.nagMinutes", 0))))
                            onActivated: (i) => backend.setSetting("alerts.nagMinutes", minutes[i])
                        }
                    }
                    Hint { text: "Only alerts set to Message with Sound repeat; a plain Message is shown once." }

                    RowLayout {
                        Label { text: "Badge shows"; Layout.preferredWidth: 260 }
                        ComboBox {
                            objectName: "setting:alerts.badge"
                            readonly property var metrics: ["both", "overdue", "today", "none"]
                            model: ["Overdue + Today", "Overdue", "Today", "None"]
                            implicitContentWidthPolicy: ComboBox.WidestText
                            currentIndex: Math.max(0, metrics.indexOf(String(root.value("alerts.badge", "both"))))
                            onActivated: (i) => backend.setSetting("alerts.badge", metrics[i])
                        }
                    }
                    Hint { Layout.topMargin: 0; text: "The number on the tray icon and in the Waybar module (fairfax --waybar). Today counts tasks due today, not overdue ones." }

                    Label { text: "Default Reminders"; font.bold: true; Layout.topMargin: 12 }
                    RowLayout {
                        Label { text: "Tasks with a due time"; Layout.preferredWidth: 170 }
                        ComboBox {
                            objectName: "setting:alerts.defaultTimed"
                            implicitContentWidthPolicy: ComboBox.WidestText
                            readonly property var offsets: [-1, 0, 5, 10, 15, 30, 60, 120, 360, 720, 1440, 2880]
                            model: ["None", "At due time", "5 minutes before", "10 minutes before", "15 minutes before", "30 minutes before",
                                    "1 hour before", "2 hours before", "6 hours before", "12 hours before", "1 day before", "2 days before"]
                            currentIndex: Math.max(0, offsets.indexOf(Number(root.value("alerts.defaultTimed", -1))))
                            onActivated: (i) => backend.setSetting("alerts.defaultTimed", offsets[i])
                        }
                    }
                    RowLayout {
                        Label { text: "Tasks with only a date"; Layout.preferredWidth: 170 }
                        ComboBox {
                            objectName: "setting:alerts.defaultDateOnly"
                            implicitContentWidthPolicy: ComboBox.WidestText
                            readonly property var days: [-1, 0, 1, 2]
                            model: ["None", "On the day", "1 day before", "2 days before"]
                            currentIndex: Math.max(0, days.indexOf(Number(root.value("alerts.defaultDateOnly", -1))))
                            onActivated: (i) => backend.setSetting("alerts.defaultDateOnly", days[i])
                        }
                        Label { text: "at" }
                        ComboBox {
                            objectName: "setting:alerts.defaultDateOnlyTime"
                            implicitContentWidthPolicy: ComboBox.WidestText
                            id: reminderTime
                            readonly property var minutes: [360, 420, 480, 540, 600, 660, 720, 780, 840, 900, 960, 1020, 1080, 1140, 1200, 1260]
                            model: minutes.map(m => ((m / 60) % 12 || 12) + ":00 " + (m < 720 ? "AM" : "PM"))
                            currentIndex: Math.max(0, minutes.indexOf(Number(root.value("alerts.defaultDateOnlyTime", root.value("tasks.defaultDueTime", 540)))))
                            onActivated: (i) => backend.setSetting("alerts.defaultDateOnlyTime", minutes[i])
                        }
                    }
                    Hint { text: "Added whenever a task gets a due date and has no alerts yet. You can still edit or remove alerts on any task." }
                    Hint { text: "Silence a single list from its right-click menu (Mute Alerts). Alerts appear as notifications with Complete and Snooze; Fairfax has to be running to deliver them." }
                }

                // Sync
                ColumnLayout {
                    visible: root.pane === 5
                    Layout.fillWidth: true
                    Toggle {
                        key: "sync.auto"; fallback: true; label: "Sync automatically"
                        note: "Syncs a few seconds after an edit and every few minutes. Turn it off to sync only with Ctrl+S or Sync Now."
                    }
                    Label { text: "Git"; font.bold: true; Layout.topMargin: 12 }
                    Hint {
                        Layout.topMargin: 0
                        text: "Keep your tasks in a private git repository and every machine that points at it stays in step. "
                            + "Each task, list and tag is a small JSON file; git runs with your own SSH keys and never prompts."
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            id: remoteField
                            objectName: "gitRemote"
                            Layout.fillWidth: true
                            placeholderText: "git@github.com:you/tasks.git"
                            text: gitSync ? gitSync.remote : ""
                            enabled: !!gitSync
                        }
                        Button {
                            objectName: "gitConnect"
                            text: gitSync && gitSync.configured ? "Change" : "Connect"
                            enabled: !!gitSync && remoteField.text.trim() !== ""
                            onClicked: gitSync.setRemote(remoteField.text.trim())
                        }
                    }
                    RowLayout {
                        visible: !!gitSync && gitSync.configured
                        Button { objectName: "gitSyncNow"; text: gitSync && gitSync.busy ? "Syncing…" : "Sync Now"; enabled: !!gitSync && !gitSync.busy; onClicked: gitSync.syncNow() }
                        Button { objectName: "gitDisconnect"; text: "Disconnect"; flat: true; onClicked: gitSync.disconnectRemote() }
                    }
                    Text {
                        visible: !!gitSync
                        text: gitSync ? gitSync.status + (gitSync.lastSync && !isNaN(gitSync.lastSync.getTime()) ? "  ·  last synced " + Qt.formatDateTime(gitSync.lastSync, "ddd d MMM, h:mm AP") : "") : ""
                        color: theme.muted
                        font.pixelSize: backend.fontSize - 1
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Hint { objectName: "gitWorkingCopy"; visible: !!gitSync; text: gitSync ? "Working copy: " + gitSync.directory : "" }
                    Hint { visible: !gitSync; text: "Sync is off in demo mode." }
                    Label { text: "Todoist"; font.bold: true; Layout.topMargin: 16 }
                    Hint {
                        Layout.topMargin: 0
                        text: "Two-way sync with Todoist, so your phone sees your tasks. Paste your API token from Todoist's Settings → Integrations → Developer; it's kept in your keyring, not in Fairfax's database. Lists, tags, dates, repeats, priority and sub-tasks sync; start dates, stars and task types ride along in a small metadata block in each task's description. List groups, smart lists and alerts stay local. Turn it on on one machine only if you also use git sync."
                    }
                    RowLayout {
                        visible: !!todoist && !todoist.connected
                        Layout.fillWidth: true
                        TextField { id: todoistToken; objectName: "todoistToken"; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: "Todoist API token" }
                        Button { objectName: "todoistConnect"; text: "Connect"; enabled: todoistToken.text.trim() !== ""; onClicked: { todoist.connectAccount(todoistToken.text.trim()); todoistToken.text = "" } }
                    }
                    RowLayout {
                        visible: !!todoist && todoist.connected
                        Button { objectName: "todoistSyncNow"; text: todoist && todoist.busy ? "Syncing…" : "Sync Now"; enabled: !!todoist && !todoist.busy; onClicked: todoist.syncNow() }
                        Button { objectName: "todoistDisconnect"; text: "Disconnect"; flat: true; onClicked: todoist.disconnectAccount() }
                    }
                    Text { visible: !!todoist; text: todoist ? todoist.status : ""; color: theme.muted; font.pixelSize: backend.fontSize - 1; wrapMode: Text.Wrap; Layout.fillWidth: true }

                    // Folder
                    Label { text: "Folder"; font.bold: true; Layout.topMargin: 16 }
                    Hint {
                        Layout.topMargin: 0
                        text: "Keep the same per-task files in a folder that Syncthing, Dropbox or Nextcloud's desktop client already shares. Several machines pointed at the same folder stay in step."
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: !!folderSync
                        Text { objectName: "folderPath"; Layout.fillWidth: true; elide: Text.ElideMiddle; color: theme.foreground; font.pixelSize: backend.fontSize - 1; text: folderSync && folderSync.configured ? folderSync.folder : "No folder chosen" }
                        Button { objectName: "folderChoose"; text: folderSync && folderSync.configured ? "Change…" : "Choose Folder…"; onClicked: filePicker.openFolder("Choose the sync folder", "sync-folder") }
                    }
                    RowLayout {
                        visible: !!folderSync && folderSync.configured
                        Button { objectName: "folderSyncNow"; text: folderSync && folderSync.busy ? "Syncing…" : "Sync Now"; enabled: !!folderSync && !folderSync.busy && !folderSync.needsChoice; onClicked: folderSync.syncNow() }
                        Button { objectName: "folderDisconnect"; text: "Disconnect"; flat: true; onClicked: folderSync.disconnectProvider() }
                    }
                    FirstSyncChoice { provider: folderSync }
                    Text { objectName: "folderStatus"; visible: !!folderSync && folderSync.configured; text: folderSync ? folderSync.status : ""; color: theme.muted; font.pixelSize: backend.fontSize - 1; wrapMode: Text.Wrap; Layout.fillWidth: true }

                    // WebDAV
                    Label { text: "WebDAV"; font.bold: true; Layout.topMargin: 16 }
                    Hint {
                        Layout.topMargin: 0
                        text: "The same files over WebDAV, for Nextcloud, ownCloud and other servers. Use the address of an existing folder, such as https://cloud.example.com/remote.php/dav/files/you/Fairfax/. The password is kept in your keyring."
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: !!webDavSync
                        TextField { id: davUrl; objectName: "davUrl"; Layout.fillWidth: true; placeholderText: "https://…/dav/files/you/Fairfax/"; text: webDavSync ? webDavSync.url : "" }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { id: davUser; objectName: "davUser"; Layout.fillWidth: true; placeholderText: "User name"; text: webDavSync ? webDavSync.user : "" }
                            TextField { id: davPassword; objectName: "davPassword"; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: "Password" }
                            Button {
                                objectName: "davConnect"
                                text: webDavSync && webDavSync.configured ? "Change" : "Connect"
                                enabled: davUrl.text.trim() !== "" && davPassword.text !== ""
                                onClicked: { webDavSync.setServer(davUrl.text.trim(), davUser.text.trim(), davPassword.text); davPassword.text = "" }
                            }
                        }
                    }
                    RowLayout {
                        visible: !!webDavSync && webDavSync.configured
                        Button { objectName: "davSyncNow"; text: webDavSync && webDavSync.busy ? "Syncing…" : "Sync Now"; enabled: !!webDavSync && !webDavSync.busy && !webDavSync.needsChoice; onClicked: webDavSync.syncNow() }
                        Button { objectName: "davDisconnect"; text: "Disconnect"; flat: true; onClicked: webDavSync.disconnectProvider() }
                    }
                    FirstSyncChoice { provider: webDavSync }
                    Text { objectName: "davStatus"; visible: !!webDavSync && webDavSync.configured; text: webDavSync ? webDavSync.status : ""; color: theme.muted; font.pixelSize: backend.fontSize - 1; wrapMode: Text.Wrap; Layout.fillWidth: true }

                    Label { text: "Exclusions"; font.bold: true; Layout.topMargin: 16 }
                    Hint { Layout.topMargin: 0; text: "Right-click a list and choose Don't Sync This List to keep it, and its tasks, on this machine only. Git, folder, WebDAV and Todoist sync all skip it." }
                }

                // Mail to Fairfax
                ColumnLayout {
                    id: mailPane
                    visible: root.pane === 6
                    Layout.fillWidth: true
                    // Re-read whenever any option changes.
                    readonly property string account: { mailFetch.revision; return mailFetch.option("mail.account") }
                    readonly property string after: { mailFetch.revision; return mailFetch.option("mail.after") }
                    readonly property string address: { mailFetch.revision; return mailFetch.option("mail.address") }
                    readonly property bool gmail: account === "gmail"
                    readonly property bool outlook: account === "outlook"
                    // The advanced sections start open once there is something in them.
                    property bool googleAdvanced: { mailFetch.revision; return mailFetch.hasGoogleClient || mailFetch.oauthState !== "none" }
                    property string clientError
                    property bool msAdvanced: { mailFetch.revision; return mailFetch.option("mail.msClientId") !== "" || mailFetch.oauthState !== "none" }

                    CheckBox {
                        objectName: "setting:mail.enabled"
                        text: "Enable Mail to Fairfax"
                        checked: { mailFetch.revision; return mailFetch.option("mail.enabled") === "true" }
                        onToggled: mailFetch.setOption("mail.enabled", checked)
                    }
                    Hint {
                        Layout.topMargin: 0
                        text: "Mail sent to a Fairfax address becomes tasks, as in the reference app's Mail to the reference app. Fairfax has no server of its own, so it watches a mailbox you own and picks out only the messages meant for it; the rest of that mailbox is left alone."
                    }
                    RowLayout {
                        Layout.topMargin: 8
                        Label { text: "Account"; Layout.preferredWidth: 160 }
                        ComboBox {
                            objectName: "setting:mail.account"
                            implicitContentWidthPolicy: ComboBox.WidestText
                            readonly property var kinds: ["gmail", "outlook", "other"]
                            model: ["Gmail", "Outlook", "Other"]
                            currentIndex: Math.max(0, kinds.indexOf(mailPane.account))
                            onActivated: (i) => mailFetch.setAccount(kinds[i])
                        }
                    }
                    MailField { key: "mail.address"; label: "Email address"; placeholder: "you@example.com" }

                    // Gmail: an app password, with your own OAuth client as the advanced route.
                    ColumnLayout {
                        visible: mailPane.gmail
                        Layout.fillWidth: true
                        MailPassword { label: "App password" }
                        Hint {
                            objectName: "mailAppPasswordHelp"
                            Layout.topMargin: 0
                            text: "Gmail wants an app password, not your Google password. Turn on 2-Step Verification, create one at myaccount.google.com/apppasswords, and make sure IMAP is on in Gmail's settings (Forwarding and POP/IMAP). The password is kept in your keyring."
                        }
                        Button {
                            objectName: "mailAppPasswordLink"
                            text: "Open Google app passwords"
                            flat: true
                            onClicked: Qt.openUrlExternally("https://myaccount.google.com/apppasswords")
                        }
                        Button {
                            objectName: "mailGoogleAdvanced"
                            text: (mailPane.googleAdvanced ? "▾ " : "▸ ") + "Advanced: use your own OAuth client"
                            flat: true
                            onClicked: mailPane.googleAdvanced = !mailPane.googleAdvanced
                        }
                        ColumnLayout {
                            visible: mailPane.googleAdvanced
                            Layout.fillWidth: true
                            Hint {
                                Layout.topMargin: 0
                                text: "Fairfax ships no Google client of its own. Create a Desktop app OAuth client in your own Google Cloud project, publish its consent screen to Production (otherwise Google expires the sign-in after 7 days), download its client_secret.json and import it here."
                            }
                            RowLayout {
                                Button { objectName: "mailImportClient"; text: "Import client JSON…"; onClicked: filePicker.open("Choose your Google client_secret.json", "mail-client-json") }
                                Text {
                                    objectName: "mailClientStatus"
                                    color: theme.muted
                                    font.pixelSize: backend.fontSize - 1
                                    text: { mailFetch.revision; return mailFetch.hasGoogleClient ? "Client " + mailFetch.option("mail.googleClientId").split("-")[0] + "… imported" : "No client imported" }
                                }
                            }
                            Text { objectName: "mailClientError"; visible: text !== ""; color: theme.overdue; font.pixelSize: backend.fontSize - 1; wrapMode: Text.Wrap; Layout.fillWidth: true; text: mailPane.clientError }
                            MailSignIn { provider: "Google" }
                        }
                        Label { text: "Plus-address"; font.bold: true; Layout.topMargin: 12 }
                        Text {
                            objectName: "mailPlusGuide"
                            Layout.fillWidth: true
                            textFormat: Text.StyledText
                            wrapMode: Text.Wrap
                            color: theme.foreground
                            font.pixelSize: backend.fontSize - 1
                            text: {
                                mailFetch.revision
                                const a = mailFetch.plusAddress || "you+fairfax@gmail.com"
                                return "Send or forward mail to <b>" + a + "</b>. Gmail delivers it to your normal inbox; Fairfax takes only mail sent to that address, files it under the "
                                    + mailFetch.option("mail.moveTo") + " label and out of your inbox, and leaves everything else alone."
                            }
                        }
                        Button {
                            objectName: "mailCopyAddress"
                            text: "Copy address"
                            enabled: { mailFetch.revision; return mailFetch.plusAddress !== "" }
                            onClicked: mailFetch.copyText(mailFetch.plusAddress)
                        }
                    }

                    // Outlook: guidance first; there is no project sign-in.
                    ColumnLayout {
                        visible: mailPane.outlook
                        Layout.fillWidth: true
                        Label { text: "1. Forward it"; font.bold: true; Layout.topMargin: 8 }
                        Hint {
                            objectName: "mailOutlookForward"
                            Layout.topMargin: 0
                            text: "Add an Outlook rule that redirects the mail you choose to your Gmail plus-address, or to any IMAP mailbox you connect here (choose Gmail or Other above). Nothing to register."
                        }
                        Label { text: "2. Use DavMail"; font.bold: true; Layout.topMargin: 8 }
                        Hint {
                            objectName: "mailOutlookDavmail"
                            Layout.topMargin: 0
                            text: "DavMail is an open-source gateway that makes Outlook.com and Microsoft 365 look like a local IMAP server (it is in the AUR). Install it, point it at your account, and Fairfax reads it on 127.0.0.1."
                        }
                        Button { objectName: "mailUseDavMail"; text: "Use DavMail"; onClicked: mailFetch.useDavMail() }
                        Label { text: "3. Advanced"; font.bold: true; Layout.topMargin: 8 }
                        Button {
                            objectName: "mailMsAdvanced"
                            text: (mailPane.msAdvanced ? "▾ " : "▸ ") + "Use your own client ID"
                            flat: true
                            onClicked: mailPane.msAdvanced = !mailPane.msAdvanced
                        }
                        ColumnLayout {
                            visible: mailPane.msAdvanced
                            Layout.fillWidth: true
                            Hint {
                                Layout.topMargin: 0
                                text: "Register an app in Microsoft Entra (portal.azure.com, App registrations) as a public client with the redirect http://localhost and the delegated IMAP.AccessAsUser.All permission, then paste its Application (client) ID. Work and school tenants may need an admin to allow it."
                            }
                            MailField { key: "mail.msClientId"; label: "Client ID"; placeholder: "Application (client) ID" }
                            MailSignIn { provider: "Microsoft" }
                        }
                    }

                    // Other: any IMAP server.
                    ColumnLayout {
                        visible: !mailPane.gmail && !mailPane.outlook
                        Layout.fillWidth: true
                        MailField { key: "mail.host"; label: "Server"; placeholder: "imap.example.com" }
                        MailField { key: "mail.port"; label: "Port"; placeholder: "993" }
                        RowLayout {
                            Label { text: "Security"; Layout.preferredWidth: 160 }
                            ComboBox {
                                objectName: "setting:mail.security"
                                implicitContentWidthPolicy: ComboBox.WidestText
                                readonly property var kinds: ["tls", "starttls", "none"]
                                model: ["TLS", "STARTTLS", "None"]
                                currentIndex: { mailFetch.revision; return Math.max(0, kinds.indexOf(mailFetch.option("mail.security"))) }
                                onActivated: (i) => mailFetch.setOption("mail.security", kinds[i])
                            }
                        }
                        MailField { key: "mail.user"; label: "User name"; placeholder: "you@example.com" }
                        MailPassword { label: "Password" }
                        Hint { Layout.topMargin: 0; text: "None is for a bridge on this machine, such as ProtonMail Bridge or DavMail on 127.0.0.1." }
                    }

                    Label { text: "Which mail"; font.bold: true; Layout.topMargin: 14 }
                    MailField { key: "mail.toFilter"; label: "Only mail sent to"; placeholder: "address@example.com" }
                    MailField { key: "mail.labelFilter"; label: "Only mail labelled"; placeholder: "Gmail label or IMAP keyword (optional)" }
                    MailField { key: "mail.folder"; label: "Folder"; placeholder: "INBOX" }
                    Text {
                        objectName: "mailUnfilteredWarning"
                        visible: { mailFetch.revision; return mailFetch.unfiltered }
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: theme.overdue
                        font.pixelSize: backend.fontSize - 1
                        text: { mailFetch.revision; return "Every message in " + mailFetch.option("mail.folder") + " will become a task." }
                    }

                    Label { text: "What happens"; font.bold: true; Layout.topMargin: 14 }
                    RowLayout {
                        Label { text: "After importing"; Layout.preferredWidth: 160 }
                        ComboBox {
                            objectName: "setting:mail.after"
                            implicitContentWidthPolicy: ComboBox.WidestText
                            readonly property var kinds: ["move", "read", "leave"]
                            model: ["Move to", "Mark as read", "Leave as is"]
                            currentIndex: Math.max(0, kinds.indexOf(mailPane.after))
                            onActivated: (i) => mailFetch.setOption("mail.after", kinds[i])
                        }
                        TextField {
                            objectName: "setting:mail.moveTo"
                            visible: mailPane.after === "move"
                            Layout.preferredWidth: 160
                            placeholderText: "Fairfax"
                            selectByMouse: true
                            text: { mailFetch.revision; return mailFetch.option("mail.moveTo") }
                            onEditingFinished: if (text !== mailFetch.option("mail.moveTo")) mailFetch.setOption("mail.moveTo", text.trim())
                        }
                    }
                    RowLayout {
                        Label { text: "New tasks go to"; Layout.preferredWidth: 160 }
                        ComboBox {
                            objectName: "setting:mail.listId"
                            implicitContentWidthPolicy: ComboBox.WidestText
                            model: [{ name: "Inbox", id: 0 }].concat(backend.listChoices.filter(l => l.name !== "Inbox"))
                            textRole: "name"
                            currentIndex: { mailFetch.revision; return Math.max(0, model.findIndex(l => l.id === Number(mailFetch.option("mail.listId")))) }
                            onActivated: (i) => mailFetch.setOption("mail.listId", model[i].id)
                        }
                    }
                    CheckBox {
                        objectName: "setting:mail.naturalLanguage"
                        text: "Read dates and tags in the subject (natural language)"
                        checked: { mailFetch.revision; return mailFetch.option("mail.naturalLanguage") === "true" }
                        onToggled: mailFetch.setOption("mail.naturalLanguage", checked)
                    }
                    Hint {
                        Layout.topMargin: 0
                        leftPadding: 44
                        text: "the reference app's due(), start(), list(), tag(), priority() and action() in the subject always work. This also reads “tomorrow 3pm #home !!” the way Quick Entry does."
                    }
                    RowLayout {
                        Label { text: "Check every"; Layout.preferredWidth: 160 }
                        SpinBox {
                            objectName: "setting:mail.interval"
                            from: 1; to: 240
                            value: { mailFetch.revision; return Number(mailFetch.option("mail.interval")) }
                            onValueModified: mailFetch.setOption("mail.interval", value)
                        }
                        Label { text: "minutes" }
                    }
                    Hint { Layout.topMargin: 0; text: "Used when the server can't push new mail (IDLE); otherwise Fairfax is told as soon as mail arrives." }

                    Label { text: "Status"; font.bold: true; Layout.topMargin: 14 }
                    RowLayout {
                        Button {
                            objectName: "mailFetchNow"
                            text: mailFetch.busy ? "Fetching…" : "Fetch Now"
                            enabled: !mailFetch.busy
                            onClicked: mailFetch.enabled ? mailFetch.fetchNow() : mailFetch.setOption("mail.enabled", true)
                        }
                        Text {
                            objectName: "mailStatus"
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: mailFetch.state === "error" ? theme.overdue : theme.muted
                            font.pixelSize: backend.fontSize - 1
                            text: mailFetch.statusLine
                        }
                    }

                    Label { text: "Or pipe it in"; font.bold: true; Layout.topMargin: 14 }
                    Hint { Layout.topMargin: 0; text: "For Sieve, procmail, fetchmail or a bridge, pass messages to Fairfax yourself:" }
                    TextEdit {
                        objectName: "mailPipe"
                        readOnly: true
                        selectByMouse: true
                        Layout.fillWidth: true
                        Layout.preferredWidth: 100   // Not the width of the unwrapped text, which would push the pane wider than the window.
                        wrapMode: TextEdit.Wrap
                        color: theme.foreground
                        selectedTextColor: theme.foreground
                        selectionColor: theme.selection
                        font.family: "monospace"
                        font.pixelSize: backend.fontSize - 1
                        text: "fairfax import-mail message.eml\nfairfax import-mail --maildir ~/Mail/fairfax"
                    }
                }

                // Backups
                ColumnLayout {
                    visible: root.pane === 7
                    Layout.fillWidth: true
                    RowLayout {
                        Label { text: "Back up automatically"; Layout.preferredWidth: 200 }
                        ComboBox {
                            objectName: "setting:backup.intervalHours"
                            readonly property var hours: [0, 24, 48]
                            model: ["Never", "Every 24 hours", "Every 48 hours"]
                            implicitContentWidthPolicy: ComboBox.WidestText
                            currentIndex: Math.max(0, hours.indexOf(Number(root.value("backup.intervalHours", 24))))
                            onActivated: (i) => backend.setSetting("backup.intervalHours", hours[i])
                        }
                    }
                    RowLayout {
                        Label { text: "Keep at most"; Layout.preferredWidth: 200 }
                        SpinBox { objectName: "setting:backup.keep"; from: 1; to: 100; value: Number(root.value("backup.keep", 10)); onValueModified: backend.setSetting("backup.keep", value) }
                    }
                    RowLayout {
                        Button { objectName: "backupNow"; text: "Back Up Now"; onClicked: { backend.backupNow(); backupList.model = backend.backups() } }
                        Button { text: "Restore from File…"; flat: true; onClicked: filePicker.open("Restore a Fairfax backup", "restore") }
                    }
                    Hint { text: "Backups are kept in " + backend.backupFolder() }
                    Repeater {
                        id: backupList
                        model: root.pane === 7 ? backend.backups() : []
                        RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Label { text: modelData.date; Layout.fillWidth: true }
                            Label { text: modelData.size; color: theme.muted }
                            Button { objectName: "backupRestore:" + modelData.name; text: "Restore"; flat: true; onClicked: { confirmRestore.path = modelData.path; confirmRestore.open() } }
                            Button { objectName: "backupLists:" + modelData.name; text: "Lists…"; flat: true; onClicked: restoreLists.show(modelData.path, modelData.date) }
                            Button { objectName: "backupCopy:" + modelData.name; text: "Save a Copy…"; flat: true; onClicked: { root.copySource = modelData.path; filePicker.save("Save a copy of this backup", modelData.name, "backup-copy") } }
                            Button { objectName: "backupDelete:" + modelData.name; text: "Delete"; flat: true; onClicked: { confirmDelete.path = modelData.path; confirmDelete.open() } }
                        }
                    }
                }

                // Import & Export
                ColumnLayout {
                    visible: root.pane === 8
                    Layout.fillWidth: true
                    Label { text: "Export"; font.bold: true }
                    RowLayout {
                        Button { objectName: "exportChoose"; text: "Choose Lists…"; onClicked: exportDialog.show() }
                        Button { objectName: "exportCsv"; text: "Everything as CSV…"; flat: true; onClicked: filePicker.save("Export tasks as CSV", "Fairfax.csv", "export-csv") }
                        Button { objectName: "exportIcs"; text: "Everything as .ics…"; flat: true; onClicked: filePicker.save("Export tasks as iCalendar", "Fairfax.ics", "export-ics") }
                    }
                    Label { text: "Import"; font.bold: true; Layout.topMargin: 12 }
                    RowLayout {
                        Button { objectName: "importTickTick"; text: "From TickTick…"; onClicked: filePicker.open("Choose a TickTick backup (.csv)", "import-ticktick") }
                        Button { objectName: "importTaskDb"; text: "From the reference app…"; onClicked: filePicker.open("Choose a the reference app database (tasks.db or a .taskdb backup)", "import-taskdb") }
                        Button { objectName: "importIcs"; text: "iCalendar (.ics)…"; onClicked: filePicker.open("Choose an iCalendar file", "import-ics") }
                    }
                    RowLayout {
                        Button { objectName: "importThings"; text: "From Things…"; onClicked: filePicker.open("Choose a Things 3 JSON export", "import-things") }
                        Button { objectName: "importOmnifocus"; text: "From OmniFocus…"; onClicked: filePicker.open("Choose an OmniFocus TaskPaper export", "import-omnifocus") }
                    }
                    Hint { text: "From Things 3 (a JSON export) and OmniFocus (File > Export > TaskPaper): areas, folders and projects become lists and projects; dates, tags, flags, notes and checklists come across." }
                    Hint { text: "From TickTick: in TickTick's web app, Settings → Account → Generate Backup, then choose the .csv here. Lists, tags, dates, repeats, reminders, checklists and completed history come across; importing the same backup again adds only what's new." }
                    Hint { text: "From the reference app: on the Mac, use a backup from the reference app > Settings > Backups (a .taskdb file). Lists, groups, tags, dates, repeats and alerts come across; one Ctrl+Z undoes the whole import, and a backup is made first." }
                }

                // Calendar
                ColumnLayout {
                    visible: root.pane === 9
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
                            objectName: "setting:calendar.sources"
                            text: String(backend.setting("calendar.sources").value || "")
                            placeholderText: "https://calendar.example.com/private.ics"
                            wrapMode: TextEdit.NoWrap
                            font.family: theme.monoFont
                            font.pixelSize: backend.fontSize - 2
                        }
                    }
                    Button {
                        objectName: "calendarApply"
                        text: "Apply"
                        onClicked: {
                            backend.setSetting("calendar.sources", calendarSources.text.trim())
                            calendar.sources = calendarSources.text.split("\n").map(l => l.trim()).filter(l => l.length)
                        }
                    }
                    Toggle { key: "calendar.inLists"; fallback: false; label: "Show events above every list, not just Today" }

                    Label { text: "Calendars"; font.weight: Font.DemiBold; Layout.topMargin: 8; visible: calendarRows.count > 0 }
                    Repeater {
                        id: calendarRows
                        model: calendar ? calendar.sourceInfo : []
                        delegate: RowLayout {
                            id: calendarRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            CheckBox {
                                objectName: "calendarEnabled:" + modelData.name
                                checked: modelData.enabled
                                text: modelData.name
                                Layout.fillWidth: true
                                onToggled: {
                                    // A plain array, so the list can be stored and then handed to the feed.
                                    const off = Array.from(calendar.disabledSources).filter(s => s !== modelData.source)
                                    if (!checked) off.push(modelData.source)
                                    backend.setSetting("calendar.disabled", off.join("\n"))
                                    calendar.disabledSources = off
                                }
                            }
                            Repeater {
                                model: calendar ? calendar.palette : []
                                delegate: Rectangle {
                                    required property string modelData
                                    readonly property var info: calendarRow.modelData
                                    objectName: "calendarColor:" + modelData
                                    width: 16; height: 16; radius: 8
                                    color: modelData
                                    opacity: info.enabled ? 1 : 0.4
                                    border.width: Qt.colorEqual(modelData, info.color) ? 2 : 0
                                    border.color: theme.foreground
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            const colors = Object.assign({}, calendar.sourceColors)
                                            colors[parent.info.source] = parent.modelData
                                            backend.setSetting("calendar.colors", JSON.stringify(colors))
                                            calendar.sourceColors = colors
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Label { text: "My email addresses"; Layout.topMargin: 8 }
                    TextField {
                        objectName: "setting:calendar.emails"
                        Layout.fillWidth: true
                        text: String(backend.setting("calendar.emails").value || "")
                        placeholderText: "me@example.com, me@work.example"
                        onEditingFinished: {
                            backend.setSetting("calendar.emails", text.trim())
                            calendar.myEmails = text.trim()
                        }
                    }
                    Hint { Layout.topMargin: 0; text: "Invitations you declined, matched by these comma-separated addresses, show struck through and leave their time free." }
                    Text { visible: !!calendar && calendar.error !== ""; text: calendar ? calendar.error : ""; color: theme.overdue; wrapMode: Text.Wrap; Layout.fillWidth: true }
                }

                // Automation
                ColumnLayout {
                    visible: root.pane === 10
                    Layout.fillWidth: true
                    Hint {
                        objectName: "automationHint"
                        Layout.topMargin: 0
                        text: "Let Claude work with your tasks: add Fairfax as an MCP server. In Claude Code run:\n  claude mcp add fairfax -- fairfax mcp\n\nScripts can use the same commands from a terminal:\n  fairfax ctl help\n  fairfax ctl create_task title=\"Call Sam\" due_date=2026-10-12\n\nLinks like fairfax://x-callback-url/add?task=Buy%20milk open in the app.\n\nWaybar: add a custom module that runs  fairfax --waybar  every 60 seconds to show overdue and due-today counts."
                    }
                }

                // Shortcuts
                ColumnLayout {
                    id: shortcutPane
                    visible: root.pane === 11
                    Layout.fillWidth: true

                    property string filter: ""
                    // Which command is being recorded, where the recording is
                    // held, and any complaint or unresolved conflict by id.
                    property string recordingId: ""
                    property var messages: ({})
                    property var conflicts: ({})
                    property string pendingId: ""
                    property string pendingSeq: ""
                    property string pendingOther: ""

                    readonly property var allCommands: { backend.shortcutMap; return backend.shortcutCommands() }
                    function differs(cmd) {
                        const a = cmd.current, b = cmd.defaults
                        return a.length !== b.length || a.some((v, i) => v !== b[i])
                    }
                    function matches(cmd) {
                        const q = filter.trim().toLowerCase()
                        return q === "" || cmd.title.toLowerCase().includes(q) || backend.shortcutText(cmd.id).toLowerCase().includes(q)
                    }
                    function beginRecording(id) {
                        clearTransient()
                        recordingId = id
                        backend.recordingShortcut = true
                        catcher.forceActiveFocus()
                    }
                    function stopRecording() {
                        recordingId = ""
                        backend.recordingShortcut = false
                    }
                    function clearTransient() {
                        recordingId = ""; pendingId = ""; pendingSeq = ""; pendingOther = ""
                        backend.recordingShortcut = false
                    }
                    function setMessage(id, text) {
                        const m = Object.assign({}, messages); m[id] = text; messages = m
                    }
                    function capture(event) {
                        if (!recordingId) return
                        const k = event.key
                        if (k === Qt.Key_Shift || k === Qt.Key_Control || k === Qt.Key_Alt || k === Qt.Key_Meta)
                            return  // A bare modifier is not a shortcut.
                        event.accepted = true
                        if (k === Qt.Key_Escape) { stopRecording(); return }
                        const id = recordingId
                        const seq = backend.shortcutFor(k, event.modifiers)
                        if (!seq) { stopRecording(); return }
                        const problem = backend.shortcutProblem(id, seq)
                        if (problem !== "") {
                            setMessage(id, problem)
                            stopRecording()
                            return
                        }
                        const other = backend.shortcutConflict(id, seq)
                        if (other !== "") {
                            pendingId = id; pendingSeq = seq; pendingOther = other
                            stopRecording()
                            return
                        }
                        backend.setShortcut(id, seq)
                        setMessage(id, "")
                        stopRecording()
                    }
                    function resolveConflict(swap) {
                        const id = pendingId, other = pendingOther, seq = pendingSeq
                        const old = (backend.shortcutMap[id] || [])[0] || ""
                        if (swap) backend.setShortcut(other, old)
                        else backend.setShortcut(other, "")
                        backend.setShortcut(id, seq)
                        setMessage(id, "")
                        clearTransient()
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.bottomMargin: 4
                        TextField {
                            objectName: "shortcutFilter"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            placeholderText: "Filter shortcuts"
                            Accessible.name: "Filter shortcuts"
                            selectByMouse: true
                            text: shortcutPane.filter
                            onTextEdited: shortcutPane.filter = text
                        }
                        Button { objectName: "shortcutResetAll"; text: "Reset All…"; flat: true; onClicked: confirmShortcuts.open() }
                    }
                    Hint {
                        Layout.topMargin: 0
                        Layout.minimumWidth: 0
                        text: "Shortcuts in the task list work while the list has focus. The system-wide Quick Entry key is a Hyprland binding that runs fairfax --quick; fairfax setup omarchy adds it."
                    }

                    // Takes the keyboard while a shortcut is recorded, so the
                    // next key is captured instead of acted on.
                    Item {
                        id: catcher
                        objectName: "shortcutCatcher"
                        width: 0
                        height: 0
                        Keys.onPressed: (event) => shortcutPane.capture(event)
                        // Losing focus (a click elsewhere) ends the recording.
                        onActiveFocusChanged: if (!activeFocus && shortcutPane.recordingId !== "") shortcutPane.stopRecording()
                    }

                    // Leaving the pane or closing the window ends it too.
                    onVisibleChanged: if (!visible) clearTransient()
                    Component.onDestruction: backend.recordingShortcut = false

                    Repeater {
                        model: shortcutPane.allCommands.map(c => c.group).filter((g, i, a) => a.indexOf(g) === i)
                        ColumnLayout {
                            id: groupBlock
                            required property string modelData
                            Layout.fillWidth: true
                            Layout.topMargin: 10
                            spacing: 4
                            Text {
                                text: groupBlock.modelData
                                color: theme.accent
                                font.pixelSize: backend.fontSize - 2
                                font.weight: Font.Bold
                                Layout.bottomMargin: 2
                            }
                            Repeater {
                                model: shortcutPane.allCommands.filter(c => c.group === groupBlock.modelData && shortcutPane.matches(c))
                                ShortcutRow { required property var modelData; cmd: modelData }
                            }
                        }
                    }

                    ThemedDialog {
                        id: confirmShortcuts
                        objectName: "shortcutResetAllConfirm"
                        width: Math.min(440, (parent ? parent.width : 480) - 40)
                        anchors.centerIn: parent
                        modal: true
                        title: "Reset all shortcuts?"
                        Label { text: "Every shortcut goes back to its default key." }
                        standardButtons: Dialog.Cancel | Dialog.Ok
                        onAccepted: { backend.resetAllShortcuts(); shortcutPane.clearTransient() }
                    }
                }

                // Diagnostics
                ColumnLayout {
                    id: diag
                    visible: root.pane === 12
                    Layout.fillWidth: true
                    // Rebuilt each time the pane opens, so the log is current.
                    property string report: ""
                    readonly property string syncStatus: [
                        "Git sync: " + (gitSync ? gitSync.status : "off"),
                        "Todoist: " + (todoist ? todoist.status : "off"),
                        "Folder sync: " + (folderSync ? folderSync.status : "off"),
                        "WebDAV sync: " + (webDavSync ? webDavSync.status : "off")
                    ].join("\n")
                    onVisibleChanged: if (visible) report = backend.diagnosticsReport(syncStatus)
                    Hint { Layout.topMargin: 0; text: "What to include in a bug report. Nothing here leaves your machine unless you copy it." }
                    Frame {
                        Layout.fillWidth: true
                        Text {
                            objectName: "diagReport"
                            width: parent.width
                            text: diag.report
                            color: theme.foreground
                            font.family: theme.monoFont
                            font.pixelSize: backend.fontSize - 2
                            wrapMode: Text.WrapAnywhere
                            textFormat: Text.PlainText
                        }
                    }
                    RowLayout {
                        Button { objectName: "diagCopy"; text: "Copy Diagnostics"; onClicked: backend.copyDiagnostics(diag.syncStatus) }
                        Button { objectName: "diagRefresh"; text: "Refresh"; flat: true; onClicked: diag.report = backend.diagnosticsReport(diag.syncStatus) }
                    }
                }

                Text { id: status; color: theme.overdue; font.pixelSize: backend.fontSize - 1; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
        }
    }

    ThemedDialog {

        width: Math.min(440, (parent ? parent.width : 480) - 40)
        id: confirmInbox
        objectName: "confirmInbox"
        anchors.centerIn: parent
        modal: true
        title: "Turn off the Inbox?"
        Label { text: "The Inbox list and its tasks are deleted. Ctrl+Z brings them back." }
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: backend.setInboxEnabled(false)
    }

    // The licence text behind the About block's "MIT License" link.
    ThemedDialog {
        id: licenseDialog
        objectName: "licenseDialog"
        property string text: ""
        width: Math.min(520, (parent ? parent.width : 560) - 40)
        anchors.centerIn: parent
        modal: true
        title: "MIT License"
        standardButtons: Dialog.Close
        onAboutToShow: if (!text) text = backend.licenseText()
        Label {
            objectName: "licenseText"
            width: parent ? parent.width : 400
            text: licenseDialog.text
            wrapMode: Text.Wrap
            color: theme.foreground
            font.pixelSize: backend.fontSize - 1
        }
    }

    ThemedDialog {

        width: Math.min(440, (parent ? parent.width : 480) - 40)
        id: confirmRestore
        objectName: "confirmRestore"
        property string path
        anchors.centerIn: parent
        modal: true
        title: "Restore this backup?"
        Label { text: "Everything is replaced by the backup. Your current tasks are backed up first." }
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: backend.restoreBackup(path)
    }

    ThemedDialog {
        width: Math.min(440, (parent ? parent.width : 480) - 40)
        id: confirmDelete
        objectName: "confirmDelete"
        property string path
        anchors.centerIn: parent
        modal: true
        title: "Delete this backup?"
        Label { text: "The backup file is removed from disk." }
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: { backend.deleteBackup(path); backupList.model = backend.backups() }
    }

    // Restore Selected Lists: pick lists out of a backup and add them, with
    // their tasks, as new records in a "Restored" group. Nothing is replaced.
    ThemedDialog {
        id: restoreLists
        property string path
        property string date
        property var lists: []
        property var chosen: ({})
        function show(p, d) {
            path = p; date = d
            lists = backend.backupLists(p)
            chosen = ({})
            open()
        }
        width: Math.min(460, (parent ? parent.width : 480) - 40)
        height: Math.min(460, (parent ? parent.height : 480) - 40)
        anchors.centerIn: parent
        modal: true
        title: "Restore lists from " + date
        standardButtons: Dialog.Cancel
        ColumnLayout {
            anchors.fill: parent
            Label { text: "Pick the lists to add. They arrive as new lists in a “Restored” group; nothing you have now is changed, and Ctrl+Z undoes it."; wrapMode: Text.Wrap; Layout.fillWidth: true; color: theme.muted }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: restoreLists.lists
                delegate: CheckBox {
                    required property var modelData
                    objectName: "restoreList:" + modelData.name
                    width: ListView.view.width
                    text: modelData.name + "  (" + modelData.tasks + (modelData.tasks === 1 ? " task)" : " tasks)")
                    checked: !!restoreLists.chosen[modelData.uid]
                    onToggled: { const c = Object.assign({}, restoreLists.chosen); c[modelData.uid] = checked; restoreLists.chosen = c }
                }
            }
            Button {
                objectName: "restoreSelected"
                text: "Restore Selected Lists"
                enabled: Object.keys(restoreLists.chosen).some(k => restoreLists.chosen[k])
                onClicked: {
                    backend.restoreBackupLists(restoreLists.path, Object.keys(restoreLists.chosen).filter(k => restoreLists.chosen[k]))
                    restoreLists.close()
                }
            }
        }
    }

    ExportDialog { id: exportDialog; owner: "settings" }

    // The tray badge's colour picker: the app's usual HSV picker, storing the
    // choice as #RRGGBB in tray.badgeColor.
    ColorPickerDialog {
        id: trayColourPicker
        onPicked: (value) => backend.setSetting("tray.badgeColor", String(value).slice(0, 7).toUpperCase())
    }

    component Toggle: ColumnLayout {
        property string key
        property bool fallback: false
        property string label
        property string note
        Layout.fillWidth: true
        spacing: 0
        CheckBox {
            objectName: "setting:" + parent.key
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

    // The question a folder or WebDAV sync asks when both sides already hold tasks.
    component FirstSyncChoice: ColumnLayout {
        property var provider
        objectName: "firstSyncChoice"
        visible: !!provider && provider.needsChoice
        Layout.fillWidth: true
        Text {
            text: "This machine and the remote both have tasks. How should the first sync go?"
            color: theme.foreground
            font.pixelSize: backend.fontSize - 1
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        RowLayout {
            Button { objectName: "firstSyncMerge"; text: "Merge"; onClicked: provider.chooseFirstSync("merge") }
            Button { objectName: "firstSyncReplaceLocal"; text: "Replace Local"; onClicked: provider.chooseFirstSync("local") }
            Button { objectName: "firstSyncReplaceServer"; text: "Replace Server"; onClicked: provider.chooseFirstSync("server") }
        }
        Text {
            text: "Merge keeps the newest version of every task. Replace Local discards this machine's tasks for the remote's (a backup is made first). Replace Server overwrites the remote with this machine's tasks."
            color: theme.muted
            font.pixelSize: backend.fontSize - 2
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }

    // A text field bound to a Mail option: it is written when the field is left or Return is pressed.
    component MailField: RowLayout {
        property string key
        property string label
        property string placeholder
        Layout.fillWidth: true
        Label { text: parent.label; Layout.preferredWidth: 160 }
        TextField {
            objectName: "setting:" + parent.key
            Layout.fillWidth: true
            placeholderText: parent.placeholder
            selectByMouse: true
            text: { mailFetch.revision; return mailFetch.option(parent.key) }
            onEditingFinished: if (text.trim() !== mailFetch.option(parent.key)) mailFetch.setOption(parent.key, text.trim())
        }
    }

    // The password goes to the keyring as soon as it is entered; the field never shows it again.
    component MailPassword: RowLayout {
        property string label
        Layout.fillWidth: true
        Label { text: parent.label; Layout.preferredWidth: 160 }
        TextField {
            id: mailPasswordField
            objectName: "mailPassword"
            Layout.fillWidth: true
            echoMode: TextInput.Password
            selectByMouse: true
            placeholderText: { mailFetch.revision; return mailFetch.hasPassword ? "Saved in your keyring" : "Not set" }
            onEditingFinished: if (text !== "") { mailFetch.setPassword(text); text = "" }
        }
        Button {
            objectName: "mailPasswordClear"
            text: "Forget"
            flat: true
            visible: { mailFetch.revision; return mailFetch.hasPassword }
            onClicked: mailFetch.setPassword("")
        }
    }

    // Sign in with a client the user brought: waiting, signed in and failed states.
    component MailSignIn: ColumnLayout {
        property string provider
        Layout.fillWidth: true
        RowLayout {
            Button {
                objectName: "mailSignIn"
                visible: mailFetch.oauthState === "none"
                text: "Sign in with " + parent.parent.provider
                onClicked: mailFetch.signIn()
            }
            Text {
                objectName: "mailOAuthStatus"
                visible: mailFetch.oauthState !== "none"
                color: theme.foreground
                font.pixelSize: backend.fontSize - 1
                text: mailFetch.oauthState === "waiting" ? "Waiting for the browser…" : "Signed in as " + mailFetch.signedInAs
            }
            Button { objectName: "mailCancelSignIn"; visible: mailFetch.oauthState === "waiting"; text: "Cancel"; flat: true; onClicked: mailFetch.cancelSignIn() }
            Button { objectName: "mailSignOut"; visible: mailFetch.oauthState === "signedIn"; text: "Sign Out"; flat: true; onClicked: mailFetch.signOut() }
        }
        Text {
            objectName: "mailOAuthError"
            visible: text !== ""
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme.overdue
            font.pixelSize: backend.fontSize - 1
            text: mailFetch.oauthError
        }
    }

    component ShortcutRow: ColumnLayout {
        property var cmd
        readonly property string id: cmd.id
        objectName: "shortcutRow:" + id
        Layout.fillWidth: true
        spacing: 2
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: cmd.title
                color: theme.foreground
                font.pixelSize: backend.fontSize
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 1
            }
            // Fixed commands are shown but not editable.
            Text {
                visible: cmd.fixed
                text: backend.shortcutText(id)
                color: theme.muted
                font.family: theme.monoFont
                font.pixelSize: backend.fontSize - 2
                horizontalAlignment: Text.AlignHCenter
                Layout.preferredWidth: 170
                Layout.rightMargin: 148   // Lines the key up with the buttons of the editable rows.
            }
            Button {
                objectName: "shortcut:" + id
                visible: !cmd.fixed
                Layout.preferredWidth: 170
                Layout.minimumWidth: 0
                topPadding: 2
                bottomPadding: 2
                font.family: theme.monoFont
                font.pixelSize: backend.fontSize - 2
                background: Rectangle {
                    implicitHeight: 26
                    radius: 6
                    color: "transparent"
                    border.color: shortcutPane.recordingId === id ? theme.accent : theme.divider
                }
                text: shortcutPane.recordingId === id ? "Press a shortcut…" : backend.shortcutText(id)
                onClicked: shortcutPane.beginRecording(id)
            }
            // The slots keep the key buttons in line when a row has no Clear or Reset.
            Item {
                visible: !cmd.fixed
                Layout.preferredWidth: 68
                Layout.minimumWidth: 68
                implicitHeight: 28
                Button {
                    id: clearButton
                    objectName: "shortcutClear:" + id
                    anchors.fill: parent
                    visible: cmd.current.length > 0
                    text: "Clear"
                    topPadding: 2
                    bottomPadding: 2
                    flat: true
                    onClicked: { backend.setShortcut(id, ""); shortcutPane.setMessage(id, "") }
                }
            }
            Item {
                visible: !cmd.fixed
                Layout.preferredWidth: 68
                Layout.minimumWidth: 68
                implicitHeight: 28
                Button {
                    id: resetButton
                    objectName: "shortcutReset:" + id
                    anchors.fill: parent
                    visible: shortcutPane.differs(cmd)
                    text: "Reset"
                    topPadding: 2
                    bottomPadding: 2
                    flat: true
                    onClicked: { backend.resetShortcut(id); shortcutPane.setMessage(id, "") }
                }
            }
        }
        Text {
            objectName: "shortcutMessage:" + id
            visible: text !== ""
            text: shortcutPane.messages[id] || ""
            color: theme.overdue
            font.pixelSize: backend.fontSize - 2
            leftPadding: 8
        }
        RowLayout {
            objectName: "shortcutConflict:" + id
            visible: shortcutPane.pendingId === id
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Text {
                color: theme.overdue
                font.pixelSize: backend.fontSize - 2
                text: {
                    const other = backend.shortcutCommands().find(c => c.id === shortcutPane.pendingOther)
                    return shortcutPane.pendingSeq + " is used by " + (other ? other.title : shortcutPane.pendingOther) + "."
                }
            }
            Button { objectName: "shortcutSwap"; text: "Swap"; flat: true; onClicked: shortcutPane.resolveConflict(true) }
            Button { objectName: "shortcutReplace"; text: "Replace"; flat: true; onClicked: shortcutPane.resolveConflict(false) }
            Button { objectName: "shortcutCancel"; text: "Cancel"; flat: true; onClicked: shortcutPane.clearTransient() }
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
