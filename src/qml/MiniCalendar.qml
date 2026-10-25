import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The month calendar under the Lists panel. Busy days are tinted as a heat
// map or dotted; clicking a day filters the task list to it (again clears),
// dragging across days filters to the range, Alt-click adds a task due that
// day, and the wheel pages through months.
ColumnLayout {
    id: root
    spacing: 2

    property date shown: new Date()
    readonly property int year: shown.getFullYear()
    readonly property int month: shown.getMonth()
    readonly property int firstDay: backend.firstDayOfWeek  // 0 = Sunday; the "Week starts" setting.
    // The Busy Days styles: a heat-map tint, dots under the date, or none,
    // which skips the counting entirely.
    readonly property string style: (backend.settingsRevision, String(backend.setting("appearance.calendarStyle").value || "heat"))
    readonly property bool shadeWeekends: (backend.settingsRevision, backend.settingFlag("appearance.calendarWeekends", false))
    property var counts: style === "none" ? [] : backend.monthDays(year, month + 1)
    readonly property bool showingToday: year === new Date().getFullYear() && month === new Date().getMonth()

    // While tasks are dragged over the top or bottom week, the month pages
    // back or forward after a pause and keeps going while the pointer stays.
    property int pageDir: 0
    Timer {
        interval: 900
        repeat: true
        running: root.pageDir !== 0
        onTriggered: root.shown = new Date(root.year, root.month + root.pageDir, 1)
    }

    // A drag across days, as indexes into the 42-cell grid.
    property int dragFrom: -1
    property int dragTo: -1

    Connections {
        target: backend
        function onDataChanged() { if (root.style !== "none") root.counts = backend.monthDays(root.year, root.month + 1) }
    }
    WheelHandler {
        property real pending: 0
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: (e) => {
            pending += e.angleDelta.y
            if (Math.abs(pending) >= 120) {
                root.shown = new Date(root.year, root.month + (pending > 0 ? -1 : 1), 1)
                pending = 0
            }
        }
    }

    // The date in grid cell `index`, which starts on the week containing the 1st.
    function isoAt(index) {
        const lead = (new Date(year, month, 1).getDay() - firstDay + 7) % 7
        return iso(new Date(year, month, 1 + index - lead))
    }
    function iso(d) {
        return d.getFullYear() + "-" + String(d.getMonth() + 1).padStart(2, "0") + "-" + String(d.getDate()).padStart(2, "0")
    }

    RowLayout {
        Layout.fillWidth: true
        IconButton { objectName: "calPrevious"; icon: "chevronLeft"; size: 12; accessibleName: "Previous month"; onClicked: root.shown = new Date(root.year, root.month - 1, 1) }
        Text {
            objectName: "calTitle"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: Qt.locale("en_GB").monthName(root.month, Locale.LongFormat).toUpperCase() + ", " + root.year
            color: theme.muted
            font.pixelSize: 11
            font.weight: Font.DemiBold
            Accessible.role: Accessible.Button
            Accessible.name: text + ", go to the current month"
            Accessible.onPressAction: root.shown = new Date()
            MouseArea { anchors.fill: parent; onClicked: root.shown = new Date() }
        }
        // Back to today, only once today's month is out of view.
        Text {
            objectName: "calToday"
            visible: !root.showingToday
            text: "Today"
            color: theme.today
            font.pixelSize: 10
            font.weight: Font.DemiBold
            MouseArea { anchors.fill: parent; anchors.margins: -4; cursorShape: Qt.PointingHandCursor; onClicked: root.shown = new Date() }
        }
        IconButton { objectName: "calNext"; icon: "chevronRight"; size: 12; accessibleName: "Next month"; onClicked: root.shown = new Date(root.year, root.month + 1, 1) }
    }

    Item {
    Layout.fillWidth: true
    implicitHeight: grid.implicitHeight
    Grid {
        id: grid
        width: parent.width
        columns: 7
        readonly property real cell: width / 7

        Repeater {
            model: 7
            Text {
                required property int index
                readonly property int weekday: (index + root.firstDay) % 7
                width: parent.cell
                horizontalAlignment: Text.AlignHCenter
                Rectangle { anchors.fill: parent; z: -1; visible: root.shadeWeekends && (parent.weekday === 0 || parent.weekday === 6); color: Qt.alpha(theme.foreground, theme.dark ? 0.05 : 0.045) }
                text: Qt.locale("en_GB").dayName((index + root.firstDay) % 7, Locale.ShortFormat).toUpperCase()
                color: theme.faint
                font.pixelSize: 9
                font.weight: Font.DemiBold
            }
        }

        Repeater {
            model: 42
            Item {
                id: cell
                objectName: "calDay:" + dayIso
                required property int index
                width: parent.cell
                height: 21
                readonly property int lead: (new Date(root.year, root.month, 1).getDay() - root.firstDay + 7) % 7
                readonly property date day: new Date(root.year, root.month, 1 + index - lead)
                readonly property bool inMonth: day.getMonth() === root.month
                readonly property bool isToday: root.iso(day) === backend.today
                readonly property int busy: inMonth ? (root.counts[day.getDate() - 1] || 0) : 0
                readonly property string dayIso: root.iso(day)
                readonly property bool dragged: root.dragFrom >= 0 && index >= Math.min(root.dragFrom, root.dragTo) && index <= Math.max(root.dragFrom, root.dragTo)
                readonly property bool filtered: dragged || (root.dragFrom < 0 && backend.dayFilter !== ""
                                                             && dayIso >= backend.dayFilter && dayIso <= (backend.dayFilterTo || backend.dayFilter))

                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: Qt.formatDate(day, "dddd d MMMM yyyy")
                                 + (busy > 0 ? ", " + busy + (busy === 1 ? " task due" : " tasks due") : "")
                Accessible.selected: filtered
                Accessible.description: isToday ? "Today" : ""
                Accessible.onPressAction: backend.setDayFilter(cell.dayIso)

                Keys.onSpacePressed: backend.setDayFilter(cell.dayIso)
                Keys.onReturnPressed: backend.setDayFilter(cell.dayIso)
                Keys.onEnterPressed: backend.setDayFilter(cell.dayIso)

                Rectangle {
                    objectName: "calWeekendShade"
                    anchors.fill: parent
                    visible: root.shadeWeekends && (cell.day.getDay() === 0 || cell.day.getDay() === 6)
                    color: Qt.alpha(theme.foreground, theme.dark ? 0.05 : 0.045)
                }
                // "Show month dividers": a line wherever the next cell to the
                // right or below belongs to another month.
                Rectangle {
                    objectName: "calDividerRight"
                    visible: backend.monthDividers && index % 7 < 6 && cell.day.getMonth() !== new Date(root.year, root.month, 2 + cell.index - cell.lead).getMonth()
                    anchors.right: parent.right
                    width: 1
                    height: parent.height
                    color: Qt.alpha(theme.foreground, 0.28)
                }
                Rectangle {
                    objectName: "calDividerBottom"
                    visible: backend.monthDividers && index < 35 && cell.day.getMonth() !== new Date(root.year, root.month, 8 + cell.index - cell.lead).getMonth()
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Qt.alpha(theme.foreground, 0.28)
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 20
                    height: 20
                    radius: 10
                    // Ten steps of tint: one task is the lightest, ten or more the deepest.
                    color: cell.busy && root.style === "heat" ? Qt.alpha(theme.accent, 0.04 + 0.026 * Math.min(10, cell.busy)) : "transparent"
                    // The filtered day keeps its 1.5 px ring; a focused one grows
                    // to the 2 px keyboard ring, so the mouse look is unchanged.
                    border.width: cell.filtered ? 1.5 : cell.activeFocus ? 2 : 0
                    border.color: theme.accent
                }
                // Dots: one per task, up to three; red on past days, whose open
                // tasks are overdue.
                Row {
                    objectName: "calDots"
                    visible: root.style === "dots" && cell.busy > 0
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: -1
                    spacing: 2
                    Repeater {
                        model: Math.min(3, cell.busy)
                        Rectangle {
                            width: 3
                            height: 3
                            radius: 1.5
                            color: root.iso(cell.day) < backend.today ? theme.overdue : theme.muted
                        }
                    }
                }
                Text {
                    anchors.centerIn: parent
                    text: cell.day.getDate()
                    font.pixelSize: 11
                    font.bold: cell.isToday
                    color: cell.isToday ? theme.today : cell.inMonth ? theme.foreground : theme.faint
                }
                // Dropping tasks on a day defers them to it.
                DropArea {
                    id: dayDrop
                    anchors.fill: parent
                    keys: ["fairfax-tasks"]
                    readonly property var proxy: ApplicationWindow.window ? ApplicationWindow.window.dragProxy : null
                    onEntered: {
                        if (proxy) proxy.hint = "Defer to " + Qt.formatDate(cell.day, "ddd d MMM")
                        root.pageDir = cell.index < 7 ? -1 : cell.index >= 35 ? 1 : 0
                    }
                    onExited: { if (proxy) proxy.hint = ""; root.pageDir = 0 }
                    onDropped: { root.pageDir = 0; backend.dropOnDate(root.iso(cell.day)) }
                }
                Rectangle { anchors.centerIn: parent; width: 22; height: 22; radius: 11; color: "transparent"; border.width: 2; border.color: theme.accent; visible: dayDrop.containsDrag }
            }
        }
    }
    // One mouse area over the whole grid so a press can drag across days. It
    // is a gestural layer, not a control: the day cells it covers are the
    // accessible targets, so the guard passes over the wrapper that owns it.
    MouseArea {
        anchors.fill: grid
        function cellAt(x, y) {
            const c = grid.childAt(x, y)
            return c && c.dayIso !== undefined ? c : null
        }
        onPressed: (mouse) => {
            const c = cellAt(mouse.x, mouse.y)
            if (!c || (mouse.modifiers & Qt.AltModifier)) return
            root.dragFrom = root.dragTo = c.index
        }
        onPositionChanged: (mouse) => {
            const c = cellAt(mouse.x, mouse.y)
            if (c && root.dragFrom >= 0) root.dragTo = c.index
        }
        onReleased: (mouse) => {
            const c = cellAt(mouse.x, mouse.y)
            if (c && (mouse.modifiers & Qt.AltModifier)) {
                backend.newTask()
                backend.setDue(backend.editingTaskId, c.dayIso)
            } else if (root.dragFrom >= 0 && root.dragFrom !== root.dragTo) {
                backend.setDayRange(root.isoAt(root.dragFrom), root.isoAt(root.dragTo))
            } else if (c) {
                backend.setDayFilter(c.dayIso)
            }
            root.dragFrom = root.dragTo = -1
        }
        onCanceled: root.dragFrom = root.dragTo = -1
    }
    }
}
