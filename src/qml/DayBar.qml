import QtQuick

// The Calendar Day Bar: today's calendar events as a slim timeline above the
// task list, expandable into a scrollable hour grid with the free gaps marked.
// Tasks dragged onto either view are time-blocked into the slot under the
// pointer.
//
// Context properties used: theme, backend (fontSize, timeBlock) and calendar
// (a CalendarFeed). Place it at the top of the task list; it collapses to zero
// height when no calendar sources are configured.
Item {
    id: root

    property bool expanded: false
    // The day shown; always today for now, but kept as a property so a date
    // picker can drive it later.
    property string isoDate: Qt.formatDate(new Date(), "yyyy-MM-dd")
    readonly property var events: calendar ? calendar.today : []
    readonly property int nowMinute: calendar ? calendar.nowMinute : 0
    readonly property bool isToday: isoDate === Qt.formatDate(new Date(), "yyyy-MM-dd")

    // Show 7:00-22:00 unless an event falls outside it, so a late meeting is
    // never clipped off the edge.
    readonly property int firstMinute: {
        let m = 7 * 60
        for (const e of events) if (!e.allDay) m = Math.min(m, Math.floor(e.start / 60) * 60)
        return m
    }
    readonly property int lastMinute: {
        let m = 22 * 60
        for (const e of events) if (!e.allDay) m = Math.max(m, Math.ceil(e.end / 60) * 60)
        return Math.max(m, firstMinute + 60)
    }
    readonly property int span: lastMinute - firstMinute
    readonly property int hourCount: Math.ceil(span / 60)
    readonly property real rowHeight: 48
    readonly property int pointSize: (backend ? backend.fontSize : 13) - 2

    property int hoverMinute: -1
    property int hoverDuration: 30

    visible: !!calendar && calendar.sources.length > 0
    implicitWidth: 400
    implicitHeight: visible ? column.height : 0
    height: implicitHeight

    Timer {
        interval: 60 * 1000
        running: root.visible
        repeat: true
        onTriggered: root.isoDate = Qt.formatDate(new Date(), "yyyy-MM-dd")
    }

    function snap(minute) {
        return Math.max(0, Math.min(24 * 60 - 15, Math.round(minute / 15) * 15))
    }

    // A dropped task fills 30 minutes, or what is left of the free gap under
    // the pointer when that is shorter, so it never overlaps the next event.
    function durationAt(minute) {
        for (const slot of calendar.freeSlots(isoDate, firstMinute, lastMinute))
            if (minute >= slot.start && minute < slot.end)
                return Math.max(15, Math.min(30, slot.end - minute))
        return 30
    }

    function commitDrop(rawMinute) {
        const minute = snap(rawMinute)
        if (backend && backend.timeBlock) backend.timeBlock(isoDate, minute, durationAt(minute))
        hoverMinute = -1
    }

    function hover(rawMinute) {
        hoverMinute = snap(rawMinute)
        hoverDuration = durationAt(hoverMinute)
    }

    // Overlapping events share the width: each cluster of overlaps is split
    // into as many columns as its busiest moment needs.
    function layout(list) {
        const items = list.filter(e => !e.allDay).map(e => ({ e: e, col: 0, cols: 1 })).sort((a, b) => a.e.start - b.e.start)
        let cluster = [], colEnds = [], clusterEnd = -1
        const flush = () => {
            for (const it of cluster) it.cols = colEnds.length
            cluster = []; colEnds = []; clusterEnd = -1
        }
        for (const it of items) {
            if (cluster.length && it.e.start >= clusterEnd) flush()
            let c = colEnds.findIndex(end => end <= it.e.start)
            if (c < 0) { c = colEnds.length; colEnds.push(0) }
            colEnds[c] = it.e.end
            it.col = c
            cluster.push(it)
            clusterEnd = Math.max(clusterEnd, it.e.end)
        }
        flush()
        return items
    }

    function hourLabel(h) {
        const hh = h % 24
        return (hh % 12 === 0 ? 12 : hh % 12) + (hh < 12 ? " AM" : " PM")
    }

    function scrollToNow() {
        const y = (nowMinute - firstMinute) / 60 * rowHeight - rowHeight
        grid.contentY = Math.max(0, Math.min(y, grid.contentHeight - grid.height))
    }
    onExpandedChanged: if (expanded) Qt.callLater(scrollToNow)

    Column {
        id: column
        width: parent.width
        spacing: 4

        // Caption: what is next (or on now), with the expand chevron.
        Item {
            width: parent.width
            height: 20

            readonly property int sep: calendar ? calendar.caption.indexOf(" · ") : -1

            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                Rectangle {
                    visible: parent.parent.sep >= 0
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8; height: 8; radius: 4
                    color: calendar && calendar.captionColor.valid ? calendar.captionColor : theme.accent
                }
                Text {
                    visible: parent.parent.sep >= 0
                    text: calendar ? calendar.caption.substring(0, parent.parent.sep) : ""
                    color: theme.foreground
                    font.pixelSize: root.pointSize
                    font.weight: Font.DemiBold
                }
                Text {
                    text: !calendar ? "" : parent.parent.sep >= 0 ? calendar.caption.substring(parent.parent.sep + 3) : calendar.caption
                    color: theme.muted
                    font.pixelSize: root.pointSize
                }
                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "chevronDown"
                    size: 12
                    color: theme.muted
                    rotation: root.expanded ? 180 : 0
                }
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.expanded = !root.expanded
            }
        }

        // The collapsed strip: all-day pills, then the timeline.
        Item {
            width: parent.width
            height: 22

            Row {
                id: pills
                spacing: 4
                height: parent.height
                Repeater {
                    model: root.events.filter(e => e.allDay)
                    delegate: Rectangle {
                        required property var modelData
                        anchors.verticalCenter: parent.verticalCenter
                        height: 16
                        radius: 8
                        width: Math.min(110, pillText.implicitWidth + 12)
                        color: Qt.alpha(modelData.color, 0.7)
                        Text {
                            id: pillText
                            anchors.centerIn: parent
                            width: Math.min(implicitWidth, 98)
                            elide: Text.ElideRight
                            text: modelData.title
                            color: theme.foreground
                            font.pixelSize: root.pointSize - 1
                        }
                    }
                }
            }

            Rectangle {
                id: timeline
                x: pills.width > 0 ? pills.width + 6 : 0
                width: parent.width - x
                height: parent.height
                radius: 4
                color: theme.raised
                clip: true

                Repeater {
                    model: root.hourCount
                    delegate: Rectangle {
                        required property int index
                        x: index * 60 / root.span * timeline.width
                        anchors.bottom: parent.bottom
                        width: 1
                        height: (root.firstMinute / 60 + index) % 3 === 0 ? 8 : 4
                        color: theme.divider
                    }
                }
                Repeater {
                    model: root.events.filter(e => !e.allDay)
                    delegate: Rectangle {
                        required property var modelData
                        x: (modelData.start - root.firstMinute) / root.span * timeline.width
                        width: Math.max(3, (modelData.end - modelData.start) / root.span * timeline.width)
                        y: 3
                        height: parent.height - 6
                        radius: 2
                        color: Qt.alpha(modelData.color, 0.7)
                    }
                }
                Rectangle {
                    visible: root.hoverMinute >= 0 && stripDrop.containsDrag
                    x: (root.hoverMinute - root.firstMinute) / root.span * timeline.width
                    width: Math.max(3, root.hoverDuration / root.span * timeline.width)
                    height: parent.height
                    color: Qt.alpha(theme.accent, 0.45)
                    border.color: theme.accent
                }
                // The "now" marker: a red hairline with a dot on top.
                Rectangle {
                    visible: root.isToday && root.nowMinute >= root.firstMinute && root.nowMinute <= root.lastMinute
                    x: (root.nowMinute - root.firstMinute) / root.span * timeline.width - 0.5
                    width: 1
                    height: parent.height
                    color: theme.overdue
                    Rectangle {
                        x: -2.5; y: 0
                        width: 6; height: 6; radius: 3
                        color: theme.overdue
                    }
                }
                DropArea {
                    id: stripDrop
                    anchors.fill: parent
                    keys: ["precrime-tasks"]
                    onPositionChanged: drag => root.hover(root.firstMinute + drag.x / width * root.span)
                    onExited: root.hoverMinute = -1
                    onDropped: drop => {
                        root.commitDrop(root.firstMinute + drop.x / width * root.span)
                        drop.accept()
                    }
                }
            }
        }

        // The expanded hour grid.
        Flickable {
            id: grid
            visible: root.expanded
            width: parent.width
            height: root.expanded ? 360 : 0
            contentWidth: width
            contentHeight: root.hourCount * root.rowHeight + 8
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            readonly property real labelWidth: 48
            readonly property var slots: root.expanded
                ? calendar.freeSlots(root.isoDate, root.isToday ? Math.max(root.firstMinute, root.nowMinute) : root.firstMinute, root.lastMinute) : []

            function yFor(minute) { return (minute - root.firstMinute) / 60 * root.rowHeight + 4 }

            Repeater {
                model: root.hourCount + 1
                delegate: Item {
                    required property int index
                    y: grid.yFor(root.firstMinute + index * 60)
                    width: grid.width
                    Text {
                        y: -height / 2
                        text: root.hourLabel(root.firstMinute / 60 + index)
                        color: theme.faint
                        font.pixelSize: root.pointSize - 1
                    }
                    Rectangle {
                        x: grid.labelWidth
                        width: parent.width - x
                        height: 1
                        color: theme.divider
                    }
                }
            }

            Repeater {
                model: grid.slots
                delegate: Rectangle {
                    required property var modelData
                    x: grid.labelWidth + (parent.width - grid.labelWidth - width) / 2
                    y: grid.yFor(modelData.start) + (grid.yFor(modelData.end) - grid.yFor(modelData.start) - height) / 2
                    width: freeText.implicitWidth + 18
                    height: 20
                    radius: 10
                    color: "transparent"
                    border.color: theme.divider
                    Text {
                        id: freeText
                        anchors.centerIn: parent
                        text: modelData.label
                        color: theme.muted
                        font.pixelSize: root.pointSize - 1
                    }
                }
            }

            Repeater {
                model: root.layout(root.events)
                delegate: Rectangle {
                    required property var modelData
                    readonly property real colWidth: (grid.width - grid.labelWidth - 6) / modelData.cols
                    x: grid.labelWidth + 2 + modelData.col * colWidth
                    y: grid.yFor(modelData.e.start)
                    width: colWidth - 2
                    height: Math.max(18, (modelData.e.end - modelData.e.start) / 60 * root.rowHeight - 1)
                    radius: 5
                    color: Qt.alpha(modelData.e.color, 0.7)
                    clip: true
                    Column {
                        x: 6; y: 2
                        width: parent.width - 12
                        Text {
                            width: parent.width
                            elide: Text.ElideRight
                            text: modelData.e.title
                            color: theme.foreground
                            font.pixelSize: root.pointSize
                            font.weight: Font.DemiBold
                        }
                        Text {
                            visible: modelData.e.location !== "" && parent.parent.height > 38
                            width: parent.width
                            elide: Text.ElideRight
                            text: modelData.e.location
                            color: theme.muted
                            font.pixelSize: root.pointSize - 1
                        }
                    }
                }
            }

            Rectangle {
                visible: root.hoverMinute >= 0 && gridDrop.containsDrag
                x: grid.labelWidth
                y: grid.yFor(root.hoverMinute)
                width: grid.width - grid.labelWidth
                height: root.hoverDuration / 60 * root.rowHeight
                color: Qt.alpha(theme.accent, 0.35)
                border.color: theme.accent
                radius: 4
            }

            Rectangle {
                visible: root.isToday && root.nowMinute >= root.firstMinute && root.nowMinute <= root.lastMinute
                x: grid.labelWidth - 3
                y: grid.yFor(root.nowMinute) - 0.5
                width: grid.width - x
                height: 1
                color: theme.overdue
                Rectangle {
                    x: 0; y: -2.5
                    width: 6; height: 6; radius: 3
                    color: theme.overdue
                }
            }

            DropArea {
                id: gridDrop
                x: grid.labelWidth
                y: 0
                width: grid.width - grid.labelWidth
                height: grid.contentHeight
                keys: ["precrime-tasks"]
                onPositionChanged: drag => root.hover(root.firstMinute + (drag.y + y - 4) / root.rowHeight * 60)
                onExited: root.hoverMinute = -1
                onDropped: drop => {
                    root.commitDrop(root.firstMinute + (drop.y + y - 4) / root.rowHeight * 60)
                    drop.accept()
                }
            }
        }
    }
}
