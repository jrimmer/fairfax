import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Plain
import QtQuick.Layouts

// the reference app's Repeat popup. Every control writes the rule straight back through
// backend.setRecurrence, so there is no OK button; the summary line shows
// what the backend will make of the rule. While the rule is "Never" the six
// preset chips (Alt+1…6) offer a one-click start.
Popup {
    id: root

    property var taskId
    // A working copy of backend.editor.recurrence; the editor map is re-read each time the popup opens.
    property var rule: blankRule()
    property string summary: ""

    readonly property var unitNames: ["", "day", "week", "month", "year"]
    readonly property var frequencyNames: ["Never", "Daily", "Weekly", "Monthly", "Yearly", "Weekdays"]
    readonly property var weekdayNames: ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"]
    readonly property var ordinals: [{ t: "1st", v: 1 }, { t: "2nd", v: 2 }, { t: "3rd", v: 3 }, { t: "4th", v: 4 }, { t: "5th", v: 5 }, { t: "Last", v: -1 }]
    readonly property var presets: [
        { label: "Daily", rule: { frequency: 1, interval: 1 } },
        { label: "Weekly", rule: { frequency: 2, interval: 1 } },
        { label: "Every 2 weeks", rule: { frequency: 2, interval: 2 } },
        { label: "Monthly", rule: { frequency: 3, interval: 1 } },
        { label: "Quarterly", rule: { frequency: 3, interval: 3 } },
        { label: "Yearly", rule: { frequency: 4, interval: 1 } }
    ]
    readonly property int frequency: rule.frequency || 0

    parent: Overlay.overlay
    padding: 14
    width: 380
    focus: true
    modal: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: theme.raised; radius: 12; border.color: theme.divider }

    function blankRule() {
        return { frequency: 0, interval: 1, from: 0, weekdays: 0, monthOrdinal: 0, monthWeekday: 1, endType: 0, endCount: 1, endDate: "" }
    }

    // The task's own date anchors defaults (which weekday, which day of month); with no date, today is used.
    function anchorDate() {
        const m = /^(\d{4})-(\d{2})-(\d{2})/.exec(backend.editor.dueIso || backend.editor.startIso || backend.today || "")
        return m ? new Date(+m[1], +m[2] - 1, +m[3]) : new Date()
    }
    function anchorWeekday() { return (anchorDate().getDay() + 6) % 7 }  // 0 = Monday, as in the bitmask.

    // Merges a change into the rule, fills in what the new shape needs, and applies it at once.
    function update(patch) {
        const r = Object.assign({}, rule, patch)
        if (r.frequency === 2 && !r.weekdays) r.weekdays = 1 << anchorWeekday()
        r.interval = Math.max(1, Math.min(999, r.interval || 1))
        r.endCount = Math.max(1, Math.min(99, r.endCount || 1))
        rule = r
        summary = r.frequency ? backend.describeRecurrence(r) : "Never"
        backend.setRecurrence(taskId, r)
    }

    // Switching to "the Nth weekday" starts from the task's own weekday, so the rule still matches its current date.
    function selectNth(patch) {
        if (rule.monthOrdinal) { update(patch); return }
        update(Object.assign({ monthOrdinal: 1, monthWeekday: anchorWeekday() + 1 }, patch))
    }

    function setFrequency(f) {
        // Weekdays has no interval, and a stale weekday mask would only confuse the other shapes, so reset both.
        update({ frequency: f, interval: f === 5 ? 1 : rule.interval, weekdays: f === 2 ? rule.weekdays : 0, monthOrdinal: f === 3 ? rule.monthOrdinal : 0 })
    }

    function applyPreset(i) {
        update(Object.assign({}, blankRule(), presets[i].rule, { weekdays: 0 }))
    }

    function openAt(item) {
        rule = Object.assign(blankRule(), backend.editor.recurrence || {})
        summary = rule.frequency ? backend.describeRecurrence(rule) : "Never"
        const pt = item.mapToItem(Overlay.overlay, 0, item.height + 4)
        const ow = Overlay.overlay.width, oh = Overlay.overlay.height
        x = Math.max(8, Math.min(pt.x + item.width - width, ow - width - 8))
        y = pt.y + implicitHeight > oh - 8 ? Math.max(8, item.mapToItem(Overlay.overlay, 0, 0).y - implicitHeight - 4) : pt.y
        open()
        content.forceActiveFocus()
    }

    // A small flat toggle used for chips, segments and weekday buttons.
    component Pill: Rectangle {
        id: pill
        property string label
        property bool on: false
        property real minWidth: 0
        property real hPad: 18
        signal clicked()
        implicitWidth: Math.max(minWidth, pillText.implicitWidth + hPad)
        implicitHeight: 26
        radius: height / 2
        color: on ? theme.accent : pillMouse.containsMouse ? theme.selection : theme.mix(theme.raised, theme.foreground, 0.07)
        Text {
            id: pillText
            anchors.centerIn: parent
            text: pill.label
            color: pill.on ? theme.accentForeground : theme.foreground
            font.pixelSize: backend.fontSize - 1
            font.weight: pill.on ? Font.DemiBold : Font.Normal
        }
        MouseArea { id: pillMouse; anchors.fill: parent; hoverEnabled: true; onClicked: pill.clicked() }
    }

    component Radio: Rectangle {
        property bool on: false
        implicitWidth: 16; implicitHeight: 16; radius: 8
        color: "transparent"
        border.color: on ? theme.accent : theme.faint
        border.width: 1.5
        Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 4; color: theme.accent; visible: parent.on }
    }

    // A themed spin box: Basic's default would draw with the system palette and clash with the popup.
    component NumberBox: Plain.SpinBox {
        id: nb
        implicitWidth: 78
        implicitHeight: 28
        editable: true
        font.pixelSize: backend.fontSize
        leftPadding: 22
        rightPadding: 22
        background: Rectangle { radius: 6; color: theme.background; border.color: nb.activeFocus ? theme.accent : theme.divider }
        contentItem: TextInput {
            text: nb.textFromValue(nb.value, nb.locale)
            font: nb.font
            color: theme.foreground
            selectionColor: theme.selection
            selectedTextColor: theme.foreground
            horizontalAlignment: Qt.AlignHCenter
            verticalAlignment: Qt.AlignVCenter
            readOnly: !nb.editable
            validator: nb.validator
            inputMethodHints: Qt.ImhFormattedNumbersOnly
        }
        down.indicator: Item {
            x: 0; width: 22; height: parent.height
            Icon { anchors.centerIn: parent; name: "chevronLeft"; size: 10; color: nb.down.pressed ? theme.accent : theme.muted }
        }
        up.indicator: Item {
            x: parent.width - 22; width: 22; height: parent.height
            Icon { anchors.centerIn: parent; name: "chevronRight"; size: 10; color: nb.up.pressed ? theme.accent : theme.muted }
        }
    }

    // A themed drop-down for the ordinal and weekday choices.
    component Choice: Plain.ComboBox {
        id: cb
        implicitHeight: 28
        font.pixelSize: backend.fontSize
        background: Rectangle { radius: 6; color: theme.background; border.color: cb.activeFocus ? theme.accent : theme.divider }
        contentItem: Text {
            leftPadding: 8
            text: cb.displayText
            color: theme.foreground
            font: cb.font
            verticalAlignment: Text.AlignVCenter
        }
        indicator: Icon { x: cb.width - width - 6; y: (cb.height - height) / 2; name: "chevronDown"; size: 10; color: theme.muted }
        delegate: ItemDelegate {
            required property int index
            required property var modelData
            width: cb.width
            height: 26
            highlighted: cb.highlightedIndex === index
            contentItem: Text { text: parent.modelData; color: theme.foreground; font: cb.font; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { radius: 4; color: parent.highlighted ? theme.selection : "transparent" }
        }
        popup: Popup {
            y: cb.height + 2
            width: cb.width
            padding: 3
            implicitHeight: contentItem.implicitHeight + 6
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: cb.popup.visible ? cb.delegateModel : null
                currentIndex: cb.highlightedIndex
            }
            background: Rectangle { color: theme.raised; radius: 8; border.color: theme.divider }
        }
    }

    DatePicker {
        id: endPicker
        objectName: "endPicker"
        onPicked: (iso) => root.update(iso ? { endType: 2, endDate: iso } : { endType: 0 })
    }

    contentItem: ColumnLayout {
        id: content
        spacing: 10
        focus: true

        Keys.onPressed: (e) => {
            if ((e.modifiers & Qt.AltModifier) && root.frequency === 0 && e.key >= Qt.Key_1 && e.key <= Qt.Key_6) {
                root.applyPreset(e.key - Qt.Key_1)
                e.accepted = true
            }
        }

        Text {
            text: "REPEAT"
            color: theme.muted
            font.pixelSize: backend.fontSize - 2
            font.weight: Font.Bold
            font.letterSpacing: 0.5
        }

        // Presets only make sense as a starting point, so they step aside once a rule exists.
        Flow {
            Layout.fillWidth: true
            visible: root.frequency === 0
            spacing: 6
            Repeater {
                model: root.presets
                Pill {
                    required property var modelData
                    required property int index
                    label: modelData.label + "  ⌥" + (index + 1)
                    onClicked: root.applyPreset(index)
                }
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: root.frequencyNames
                Pill {
                    required property string modelData
                    required property int index
                    hPad: 14
                    label: modelData
                    on: root.frequency === index
                    onClicked: root.setFrequency(index)
                }
            }
        }

        // Every [N] units, for the shapes that take an interval.
        RowLayout {
            visible: root.frequency >= 1 && root.frequency <= 4
            spacing: 8
            Text { text: "Every"; color: theme.foreground; font.pixelSize: backend.fontSize }
            NumberBox {
                from: 1; to: 999
                value: root.rule.interval || 1
                onValueModified: root.update({ interval: value })
            }
            Text {
                text: root.unitNames[root.frequency] + ((root.rule.interval || 1) === 1 ? "" : "s")
                color: theme.foreground
                font.pixelSize: backend.fontSize
            }
        }

        // Weekly: seven toggles, starting on the locale's first day of the week.
        RowLayout {
            visible: root.frequency === 2
            spacing: 4
            Repeater {
                model: 7
                Pill {
                    required property int index
                    readonly property int day: (index + backend.firstDayOfWeek + 6) % 7  // Sunday-based locale index to Monday-based bit.
                    minWidth: 34
                    label: root.weekdayNames[day][0]
                    on: ((root.rule.weekdays || 0) >> day) & 1
                    onClicked: {
                        const bits = (root.rule.weekdays || 0) ^ (1 << day)
                        // An empty mask would mean no day at all, so the last toggle cannot be switched off.
                        if (bits) root.update({ weekdays: bits })
                    }
                }
            }
        }

        // Monthly: a fixed date or "the Nth weekday".
        ColumnLayout {
            visible: root.frequency === 3
            spacing: 6
            RowLayout {
                spacing: 8
                Radio { on: !root.rule.monthOrdinal }
                Text {
                    text: "On day " + root.anchorDate().getDate() + " of the month"
                    color: theme.foreground
                    font.pixelSize: backend.fontSize
                }
                TapHandler { onTapped: root.update({ monthOrdinal: 0 }) }
            }
            RowLayout {
                spacing: 8
                Radio { on: !!root.rule.monthOrdinal }
                Text { text: "On the"; color: theme.foreground; font.pixelSize: backend.fontSize }
                Choice {
                    implicitWidth: 66
                    model: root.ordinals.map(o => o.t)
                    currentIndex: Math.max(0, root.ordinals.findIndex(o => o.v === root.rule.monthOrdinal))
                    onActivated: (i) => root.update({ monthOrdinal: root.ordinals[i].v })
                }
                Choice {
                    implicitWidth: 112
                    model: root.weekdayNames
                    currentIndex: Math.max(0, (root.rule.monthWeekday || 1) - 1)
                    onActivated: (i) => root.selectNth({ monthWeekday: i + 1 })
                }
                // A TapHandler rather than a MouseArea: anchors inside a layout are not allowed.
                TapHandler { onTapped: root.selectNth({}) }
            }
        }

        RowLayout {
            visible: root.frequency !== 0
            spacing: 8
            Rectangle {
                implicitWidth: 16; implicitHeight: 16; radius: 4
                color: root.rule.from === 1 ? theme.accent : "transparent"
                border.color: root.rule.from === 1 ? theme.accent : theme.faint
                border.width: 1.5
                Icon { anchors.centerIn: parent; name: "check"; size: 11; stroke: 2; color: theme.accentForeground; visible: root.rule.from === 1 }
            }
            Text { text: "after completion"; color: theme.foreground; font.pixelSize: backend.fontSize }
            TapHandler { onTapped: root.update({ from: root.rule.from === 1 ? 0 : 1 }) }
        }

        // End condition.
        ColumnLayout {
            visible: root.frequency !== 0
            spacing: 6
            Text { text: "END"; color: theme.muted; font.pixelSize: backend.fontSize - 2; font.weight: Font.Bold; font.letterSpacing: 0.5 }
            RowLayout {
                spacing: 8
                Radio { on: !root.rule.endType }
                Text { text: "Never"; color: theme.foreground; font.pixelSize: backend.fontSize }
                TapHandler { onTapped: root.update({ endType: 0 }) }
            }
            RowLayout {
                spacing: 8
                Radio { on: root.rule.endType === 1 }
                Text { text: "After"; color: theme.foreground; font.pixelSize: backend.fontSize }
                NumberBox {
                    from: 1; to: 99
                    value: root.rule.endCount || 1
                    onValueModified: root.update({ endType: 1, endCount: value })
                }
                Text { text: (root.rule.endCount || 1) === 1 ? "time" : "times"; color: theme.foreground; font.pixelSize: backend.fontSize }
                TapHandler { onTapped: root.update({ endType: 1 }) }
            }
            RowLayout {
                spacing: 8
                Radio { on: root.rule.endType === 2 }
                Text { text: "On"; color: theme.foreground; font.pixelSize: backend.fontSize }
                Rectangle {
                    id: dateButton
                    implicitWidth: 130
                    implicitHeight: 28
                    radius: 6
                    color: theme.background
                    border.color: endPicker.opened ? theme.accent : theme.divider
                    Text {
                        anchors.centerIn: parent
                        text: root.rule.endDate ? Qt.formatDate(endPicker.parseIso(root.rule.endDate), "ddd, d MMM yyyy") : "Choose date"
                        color: root.rule.endDate ? theme.foreground : theme.faint
                        font.pixelSize: backend.fontSize
                    }
                    MouseArea { anchors.fill: parent; onClicked: endPicker.openAt(dateButton, root.rule.endDate) }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.divider }

        RowLayout {
            Layout.fillWidth: true
            Icon { name: "repeat"; size: 14; color: theme.muted }
            Text {
                Layout.fillWidth: true
                text: root.summary
                wrapMode: Text.WordWrap
                color: theme.foreground
                font.pixelSize: backend.fontSize - 1
            }
            Pill {
                visible: root.frequency !== 0
                label: "Remove Repeat"
                onClicked: root.update(root.blankRule())
            }
        }
    }
}
