import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// the reference app's date picker: preset chips (Today, Tomorrow, the day after tomorrow by
// weekday name, Week, None) above a month calendar. Everything is reachable
// from the keyboard: arrows move the cursor, PageUp/PageDown change month,
// Return picks and Alt+0…4 trigger the chips.
Popup {
    id: root

    property string selectedIso
    property bool showNoTime: false
    signal picked(string iso)
    signal noTime()

    readonly property int firstDay: Qt.locale().firstDayOfWeek  // 0 = Sunday, as in Date.getDay().
    property date cursor: new Date()                            // The day the keyboard is on.
    property int shownYear: cursor.getFullYear()
    property int shownMonth: cursor.getMonth()
    property var counts: []
    readonly property int peak: Math.max(1, ...counts)
    readonly property date todayDate: parseIso(backend.today) || new Date()

    readonly property real cellSize: 34

    parent: Overlay.overlay
    padding: 10
    focus: true
    modal: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: 7 * cellSize + 2 * padding
    background: Rectangle { color: theme.raised; radius: 12; border.color: theme.divider }

    function iso(d) {
        return d.getFullYear() + "-" + String(d.getMonth() + 1).padStart(2, "0") + "-" + String(d.getDate()).padStart(2, "0")
    }

    // Returns null for anything that is not a plain YYYY-MM-DD string, so callers can fall back to today.
    function parseIso(s) {
        const m = /^(\d{4})-(\d{2})-(\d{2})/.exec(s || "")
        return m ? new Date(+m[1], +m[2] - 1, +m[3]) : null
    }

    // Offsets are built from year/month/day parts rather than milliseconds so daylight-saving changes cannot skip a day.
    function addDays(d, n) { return new Date(d.getFullYear(), d.getMonth(), d.getDate() + n) }

    function refreshCounts() { counts = backend.monthDays(shownYear, shownMonth + 1) }
    onShownYearChanged: refreshCounts()
    onShownMonthChanged: refreshCounts()

    Connections {
        target: backend
        ignoreUnknownSignals: true
        function onDataChanged() { root.refreshCounts() }
    }

    // Moves the cursor and drags the displayed month along with it.
    function moveCursor(d) {
        cursor = d
        shownYear = d.getFullYear()
        shownMonth = d.getMonth()
    }

    // Month steps clamp the day so that Jan 31 + 1 month lands on Feb 28 rather than spilling into March.
    function stepMonth(n) {
        const first = new Date(shownYear, shownMonth + n, 1)
        const last = new Date(first.getFullYear(), first.getMonth() + 1, 0).getDate()
        moveCursor(new Date(first.getFullYear(), first.getMonth(), Math.min(cursor.getDate(), last)))
    }

    readonly property var chips: [
        { label: "Today", days: 0 },
        { label: "Tomorrow", days: 1 },
        { label: Qt.locale().dayName(addDays(todayDate, 2).getDay(), Locale.ShortFormat), days: 2 },
        { label: "Week", days: 7 },
        { label: "None", days: -1 }
    ].concat(showNoTime ? [{ label: "No Time", days: -2 }] : [])

    function choose(d) { picked(iso(d)); close() }

    function acceptChip(i) {
        const c = chips[i]
        if (!c) return
        if (c.days === -2) noTime()
        else if (c.days < 0) picked("")
        else picked(iso(addDays(todayDate, c.days)))
        close()
    }

    function openAt(item, isoDate) {
        selectedIso = isoDate || ""
        moveCursor(parseIso(selectedIso) || todayDate)
        refreshCounts()
        const pt = item.mapToItem(Overlay.overlay, 0, item.height + 4)
        // Right-align under the item when there is room, as the field it serves sits at the editor's right edge.
        const ow = Overlay.overlay.width, oh = Overlay.overlay.height
        const h = implicitHeight
        x = Math.max(8, Math.min(pt.x + item.width - width, ow - width - 8))
        y = pt.y + h > oh - 8 ? Math.max(8, item.mapToItem(Overlay.overlay, 0, 0).y - h - 4) : pt.y
        open()
        grid.forceActiveFocus()
    }

    contentItem: ColumnLayout {
        spacing: 8

        Flow {
            Layout.fillWidth: true
            spacing: 5
            Repeater {
                model: root.chips
                Rectangle {
                    required property var modelData
                    required property int index
                    width: chipText.implicitWidth + 16
                    height: 24
                    radius: 12
                    color: chipMouse.containsMouse ? theme.selection : theme.mix(theme.raised, theme.foreground, 0.07)
                    Text {
                        id: chipText
                        anchors.centerIn: parent
                        // The Alt hint for None is 0 in the reference app rather than its position in the row; No Time has no shortcut.
                        text: modelData.label + (modelData.days === -2 ? "" : "  ⌥" + (modelData.days === -1 ? 0 : index + 1))
                        color: theme.foreground
                        font.pixelSize: backend.fontSize - 2
                    }
                    MouseArea { id: chipMouse; anchors.fill: parent; hoverEnabled: true; onClicked: root.acceptChip(index) }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            IconButton { icon: "chevronLeft"; size: 12; onClicked: root.stepMonth(-1) }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: Qt.locale().monthName(root.shownMonth, Locale.LongFormat) + " " + root.shownYear
                color: theme.foreground
                font.pixelSize: backend.fontSize
                font.weight: Font.DemiBold
                // Clicking the title jumps back to the current month, as in the sidebar calendar.
                MouseArea { anchors.fill: parent; onClicked: root.moveCursor(root.todayDate) }
            }
            IconButton { icon: "chevronRight"; size: 12; onClicked: root.stepMonth(1) }
        }

        Grid {
            id: grid
            columns: 7
            focus: true
            activeFocusOnTab: true

            Keys.onPressed: (e) => {
                if (e.modifiers & Qt.AltModifier) {
                    if (e.key >= Qt.Key_0 && e.key <= Qt.Key_4) {
                        root.acceptChip(e.key === Qt.Key_0 ? root.chips.findIndex(c => c.days === -1) : e.key - Qt.Key_1)
                        e.accepted = true
                    }
                    return
                }
                switch (e.key) {
                case Qt.Key_Left: root.moveCursor(root.addDays(root.cursor, -1)); break
                case Qt.Key_Right: root.moveCursor(root.addDays(root.cursor, 1)); break
                case Qt.Key_Up: root.moveCursor(root.addDays(root.cursor, -7)); break
                case Qt.Key_Down: root.moveCursor(root.addDays(root.cursor, 7)); break
                case Qt.Key_PageUp: root.stepMonth(-1); break
                case Qt.Key_PageDown: root.stepMonth(1); break
                case Qt.Key_Return: case Qt.Key_Enter: root.choose(root.cursor); break
                case Qt.Key_Escape: root.close(); break
                default: return
                }
                e.accepted = true
            }

            Repeater {
                model: 7
                Text {
                    required property int index
                    width: root.cellSize
                    horizontalAlignment: Text.AlignHCenter
                    text: Qt.locale().dayName((index + root.firstDay) % 7, Locale.ShortFormat).toUpperCase().slice(0, 3)
                    color: theme.faint
                    font.pixelSize: backend.fontSize - 3
                    font.weight: Font.DemiBold
                    bottomPadding: 2
                }
            }

            Repeater {
                model: 42
                Item {
                    id: cell
                    required property int index
                    width: root.cellSize
                    height: root.cellSize - 4
                    readonly property int lead: (new Date(root.shownYear, root.shownMonth, 1).getDay() - root.firstDay + 7) % 7
                    readonly property date day: new Date(root.shownYear, root.shownMonth, 1 + index - lead)
                    readonly property string dayIso: root.iso(day)
                    readonly property bool inMonth: day.getMonth() === root.shownMonth
                    readonly property bool isToday: dayIso === backend.today
                    readonly property bool isSelected: dayIso === root.selectedIso
                    readonly property bool isCursor: dayIso === root.iso(root.cursor)
                    readonly property bool weekend: day.getDay() === 0 || day.getDay() === 6
                    readonly property int busy: inMonth ? (root.counts[day.getDate() - 1] || 0) : 0

                    // Weekend cells get a faint wash that fills the whole cell, so adjacent rows join into one column band.
                    Rectangle {
                        anchors.fill: parent
                        color: cell.weekend ? Qt.alpha(theme.foreground, 0.04) : "transparent"
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: 28; height: 28; radius: 14
                        // Same ten-step heat ramp as the sidebar calendar so busy days look alike in both.
                        color: cell.busy ? Qt.alpha(theme.accent, 0.05 + 0.04 * Math.ceil(10 * cell.busy / root.peak)) : "transparent"
                        border.width: cell.isSelected ? 2 : cell.isCursor && grid.activeFocus ? 1 : 0
                        border.color: cell.isSelected ? theme.accent : theme.muted
                    }
                    Text {
                        anchors.centerIn: parent
                        text: cell.day.getDate()
                        font.pixelSize: backend.fontSize
                        font.bold: cell.isToday
                        color: cell.isToday ? theme.today : cell.inMonth ? theme.foreground : theme.faint
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.choose(cell.day)
                    }
                }
            }
        }
    }
}
