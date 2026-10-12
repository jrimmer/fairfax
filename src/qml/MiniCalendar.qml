import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The month calendar under the Lists panel. Busy days are tinted as a heat
// map; clicking a day filters the task list to it, and clicking again clears.
ColumnLayout {
    id: root
    spacing: 2

    property date shown: new Date()
    readonly property int year: shown.getFullYear()
    readonly property int month: shown.getMonth()
    readonly property int firstDay: Qt.locale().firstDayOfWeek  // 0 = Sunday.
    property var counts: backend.monthDays(year, month + 1)
    readonly property int peak: Math.max(1, ...counts)

    Connections {
        target: backend
        function onDataChanged() { root.counts = backend.monthDays(root.year, root.month + 1) }
    }

    function iso(d) {
        return d.getFullYear() + "-" + String(d.getMonth() + 1).padStart(2, "0") + "-" + String(d.getDate()).padStart(2, "0")
    }

    RowLayout {
        Layout.fillWidth: true
        IconButton { icon: "chevronLeft"; size: 12; onClicked: root.shown = new Date(root.year, root.month - 1, 1) }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: Qt.locale("en_GB").monthName(root.month, Locale.LongFormat).toUpperCase() + ", " + root.year
            color: theme.muted
            font.pixelSize: 11
            font.weight: Font.DemiBold
            MouseArea { anchors.fill: parent; onClicked: root.shown = new Date() }
        }
        IconButton { icon: "chevronRight"; size: 12; onClicked: root.shown = new Date(root.year, root.month + 1, 1) }
    }

    Grid {
        Layout.fillWidth: true
        columns: 7
        readonly property real cell: width / 7

        Repeater {
            model: 7
            Text {
                required property int index
                width: parent.cell
                horizontalAlignment: Text.AlignHCenter
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
                required property int index
                width: parent.cell
                height: 21
                readonly property int lead: (new Date(root.year, root.month, 1).getDay() - root.firstDay + 7) % 7
                readonly property date day: new Date(root.year, root.month, 1 + index - lead)
                readonly property bool inMonth: day.getMonth() === root.month
                readonly property bool isToday: root.iso(day) === backend.today
                readonly property int busy: inMonth ? (root.counts[day.getDate() - 1] || 0) : 0
                readonly property bool filtered: backend.dayFilter === root.iso(day)

                Rectangle {
                    anchors.centerIn: parent
                    width: 20
                    height: 20
                    radius: 10
                    // Ten steps of tint, deepening with the number of tasks that day.
                    color: cell.busy ? Qt.alpha(theme.accent, 0.03 + 0.024 * Math.ceil(10 * cell.busy / root.peak)) : "transparent"
                    border.width: cell.filtered ? 1.5 : 0
                    border.color: theme.accent
                }
                Text {
                    anchors.centerIn: parent
                    text: cell.day.getDate()
                    font.pixelSize: 11
                    font.bold: cell.isToday
                    color: cell.isToday ? theme.today : cell.inMonth ? theme.foreground : theme.faint
                }
                MouseArea { anchors.fill: parent; onClicked: backend.setDayFilter(root.iso(cell.day)) }
                // Dropping tasks on a day defers them to it.
                DropArea {
                    id: dayDrop
                    anchors.fill: parent
                    keys: ["precrime-tasks"]
                    readonly property var proxy: ApplicationWindow.window ? ApplicationWindow.window.dragProxy : null
                    onEntered: if (proxy) proxy.hint = "Defer to " + Qt.formatDate(cell.day, "ddd d MMM")
                    onExited: if (proxy) proxy.hint = ""
                    onDropped: backend.dropOnDate(root.iso(cell.day))
                }
                Rectangle { anchors.centerIn: parent; width: 22; height: 22; radius: 11; color: "transparent"; border.width: 2; border.color: theme.accent; visible: dayDrop.containsDrag }
            }
        }
    }
}
