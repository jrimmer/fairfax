import QtQuick
import QtQuick.Controls

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
    // The My Day utility panel wants the timeline always open without
    // touching the remembered expanded state of the list's own bar.
    property bool forceExpanded: false
    readonly property bool isExpanded: expanded || forceExpanded
    // Days away from today that the bar shows; 0 is today.
    property int dayOffset: 0
    // Today's date, kept current by the minute timer independently of dayOffset
    // so browsing survives midnight without jumping back.
    property string baseIso: Qt.formatDate(new Date(), "yyyy-MM-dd")
    readonly property string isoDate: addDays(baseIso, dayOffset)
    readonly property date shownDate: parseIso(isoDate)
    // Reading calendar.today makes the binding re-run whenever the feed changes.
    readonly property var events: {
        if (!calendar) return []
        const today = calendar.today
        return dayOffset === 0 ? today : calendar.eventsFor(isoDate)
    }
    readonly property int nowMinute: calendar ? calendar.nowMinute : 0
    readonly property bool isToday: dayOffset === 0
    // The caption for another day describes that day; today's comes from the feed.
    readonly property string captionText: {
        if (!calendar) return ""
        if (dayOffset === 0) return calendar.caption
        const n = events.length
        const name = dayOffset === 1 ? "Tomorrow" : dayOffset === -1 ? "Yesterday" : Qt.formatDate(shownDate, "ddd, MMM d")
        return name + " · " + (n === 0 ? "no events" : n === 1 ? "1 event" : n + " events")
    }
    property var detailEvent: null

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
        onTriggered: root.baseIso = Qt.formatDate(new Date(), "yyyy-MM-dd")
    }

    function parseIso(iso) {
        const p = iso.split("-")
        return new Date(Number(p[0]), Number(p[1]) - 1, Number(p[2]))
    }

    function addDays(iso, n) {
        const d = parseIso(iso)
        d.setDate(d.getDate() + n)
        return Qt.formatDate(d, "yyyy-MM-dd")
    }

    // Expanded or collapsed is remembered between sessions. The backend is
    // absent or minimal in tests, so every call is guarded.
    readonly property bool canPersist: !!backend && typeof backend.settingFlag === "function" && typeof backend.setSetting === "function"
    property bool restored: false
    Component.onCompleted: {
        if (canPersist) expanded = backend.settingFlag("calendar.expanded", false)
        restored = true
    }
    onExpandedChanged: {
        // Collapsing returns to today, as the collapsed strip is always about today.
        if (!expanded) dayOffset = 0
        else Qt.callLater(scrollToNow)
        if (restored && canPersist) backend.setSetting("calendar.expanded", expanded ? "true" : "false")
    }

    function fmt(minute) {
        const h = Math.floor(minute / 60) % 24, m = minute % 60
        return (h % 12 === 0 ? 12 : h % 12) + ":" + (m < 10 ? "0" : "") + m + (h < 12 ? " AM" : " PM")
    }

    function timeRange(e) {
        return e.allDay ? "All day" : fmt(e.start) + " – " + fmt(e.end === 1440 ? 0 : e.end)
    }

    function showDetails(e, item) {
        detailEvent = e
        const p = item.mapToItem(root, 0, item.height + 4)
        details.x = Math.max(0, Math.min(p.x, root.width - details.width))
        details.y = p.y
        details.open()
    }

    // The link to offer: the event's URL, else the first address in its text.
    function linkFor(e) {
        if (e.url) return e.url
        const m = (e.location + " " + e.description).match(/https?:\/\/[^\s<>"]+/)
        return m ? m[0] : ""
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


    Column {
        id: column
        width: parent.width
        spacing: 4

        // Caption: what is next (or on now), with the expand chevron.
        Item {
            width: parent.width
            height: 20

            readonly property int sep: root.captionText.indexOf(" · ")

            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                Rectangle {
                    visible: parent.parent.sep >= 0
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8; height: 8; radius: 4
                    color: calendar && root.isToday && calendar.captionColor.valid ? calendar.captionColor : theme.accent
                }
                Text {
                    visible: parent.parent.sep >= 0
                    text: root.captionText.substring(0, parent.parent.sep)
                    color: theme.foreground
                    font.pixelSize: root.pointSize
                    font.weight: Font.DemiBold
                }
                Text {
                    text: parent.parent.sep >= 0 ? root.captionText.substring(parent.parent.sep + 3) : root.captionText
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
                objectName: "dayBarToggle"
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: if (!root.forceExpanded) root.expanded = !root.expanded
            }

            // Browse other days: a "Today" link appears once away from today.
            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                Text {
                    objectName: "dayBarToday"
                    visible: root.dayOffset !== 0
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Today"
                    color: theme.accent
                    font.pixelSize: root.pointSize
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.dayOffset = 0
                    }
                }
                Repeater {
                    model: [{ icon: "chevronLeft", step: -1 }, { icon: "chevronRight", step: 1 }]
                    delegate: Item {
                        required property var modelData
                        objectName: "dayBarStep:" + modelData.step
                        width: 20; height: 20
                        activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: modelData.step < 0 ? "Previous day" : "Next day"
                        Accessible.onPressAction: root.dayOffset += modelData.step
                        Keys.onSpacePressed: root.dayOffset += modelData.step
                        Keys.onReturnPressed: root.dayOffset += modelData.step
                        Keys.onEnterPressed: root.dayOffset += modelData.step
                        Icon { anchors.centerIn: parent; name: modelData.icon; size: 12; color: theme.muted }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.dayOffset += modelData.step
                        }
                    }
                }
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
                        objectName: "dayBarPill:" + modelData.title
                        anchors.verticalCenter: parent.verticalCenter
                        height: 16
                        radius: 8
                        width: Math.min(110, pillText.implicitWidth + 12)
                        color: Qt.alpha(modelData.color, 0.7)
                        opacity: modelData.declined ? 0.4 : 1
                        Accessible.role: Accessible.StaticText
                        Accessible.name: modelData.title + ", all day"
                        Accessible.onPressAction: root.showDetails(modelData, parent)
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.showDetails(modelData, parent)
                        }
                        Text {
                            id: pillText
                            anchors.centerIn: parent
                            width: Math.min(implicitWidth, 98)
                            elide: Text.ElideRight
                            text: modelData.title
                            color: theme.foreground
                            font.pixelSize: root.pointSize - 1
                            font.strikeout: modelData.declined
                        }
                    }
                }
            }

            Rectangle {
                id: timeline
                objectName: "dayBarStrip"
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
                        objectName: "dayBarEvent:" + modelData.title
                        x: (modelData.start - root.firstMinute) / root.span * timeline.width
                        width: Math.max(3, (modelData.end - modelData.start) / root.span * timeline.width)
                        y: 3
                        height: parent.height - 6
                        radius: 2
                        color: Qt.alpha(modelData.color, 0.7)
                        opacity: modelData.declined ? 0.35 : 1
                        Accessible.role: Accessible.StaticText
                        Accessible.name: modelData.title + ", " + root.timeRange(modelData)
                        Accessible.onPressAction: root.showDetails(modelData, parent)
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -2
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.showDetails(modelData, parent)
                        }
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
                    keys: ["fairfax-tasks"]
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
            visible: root.isExpanded
            width: parent.width
            height: root.isExpanded ? 360 : 0
            contentWidth: width
            contentHeight: root.hourCount * root.rowHeight + 8
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            readonly property real labelWidth: 48
            readonly property var slots: root.isExpanded && root.events
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
                    objectName: "dayBarGridEvent:" + modelData.e.title
                    readonly property real colWidth: (grid.width - grid.labelWidth - 6) / modelData.cols
                    x: grid.labelWidth + 2 + modelData.col * colWidth
                    y: grid.yFor(modelData.e.start)
                    width: colWidth - 2
                    height: Math.max(18, (modelData.e.end - modelData.e.start) / 60 * root.rowHeight - 1)
                    radius: 5
                    color: Qt.alpha(modelData.e.color, 0.7)
                    opacity: modelData.e.declined ? 0.4 : 1
                    clip: true
                    Accessible.role: Accessible.StaticText
                    Accessible.name: modelData.e.title + ", " + root.timeRange(modelData.e)
                    Accessible.onPressAction: root.showDetails(modelData.e, parent)
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.showDetails(modelData.e, parent)
                    }
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
                            font.strikeout: modelData.e.declined
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
                keys: ["fairfax-tasks"]
                onPositionChanged: drag => root.hover(root.firstMinute + (drag.y + y - 4) / root.rowHeight * 60)
                onExited: root.hoverMinute = -1
                onDropped: drop => {
                    root.commitDrop(root.firstMinute + (drop.y + y - 4) / root.rowHeight * 60)
                    drop.accept()
                }
            }
        }
    }

    // Details of the clicked event. Events are read-only here, as in the reference app.
    Popup {
        id: details
        objectName: "dayBarDetails"
        parent: root
        width: Math.min(320, root.width)
        padding: 12
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: theme.raised
            border.color: theme.divider
            radius: 8
        }
        contentItem: Column {
            spacing: 4
            readonly property var e: root.detailEvent || ({ title: "", start: 0, end: 0, allDay: false, color: "transparent", location: "", calendar: "", description: "", url: "", declined: false })
            Text {
                objectName: "dayBarDetailTitle"
                width: parent.width
                text: parent.e.title
                wrapMode: Text.Wrap
                color: theme.foreground
                font.pixelSize: root.pointSize + 1
                font.weight: Font.DemiBold
                font.strikeout: parent.e.declined
            }
            Text {
                width: parent.width
                text: Qt.formatDate(root.shownDate, "dddd, MMMM d") + " · " + root.timeRange(parent.e)
                color: theme.muted
                font.pixelSize: root.pointSize
                wrapMode: Text.Wrap
            }
            Text {
                visible: parent.e.declined
                text: "You declined this invitation"
                color: theme.overdue
                font.pixelSize: root.pointSize
            }
            Text {
                visible: parent.e.location !== ""
                width: parent.width
                text: parent.e.location
                color: theme.foreground
                font.pixelSize: root.pointSize
                wrapMode: Text.Wrap
            }
            Row {
                visible: parent.e.calendar !== ""
                spacing: 6
                Rectangle { y: 4; width: 8; height: 8; radius: 4; color: parent.parent.e.color }
                Text { text: parent.parent.e.calendar; color: theme.muted; font.pixelSize: root.pointSize }
            }
            Text {
                visible: parent.e.description !== ""
                width: parent.width
                text: parent.e.description
                maximumLineCount: 6
                elide: Text.ElideRight
                wrapMode: Text.Wrap
                color: theme.foreground
                font.pixelSize: root.pointSize
            }
            Text {
                readonly property string link: root.linkFor(parent.e)
                visible: link !== ""
                width: parent.width
                text: "<a href=\"" + link + "\">" + link + "</a>"
                textFormat: Text.StyledText
                elide: Text.ElideRight
                linkColor: theme.accent
                font.pixelSize: root.pointSize
                onLinkActivated: url => Qt.openUrlExternally(url)
            }
        }
    }
}
