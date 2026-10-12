import QtQuick

// An integer that reads like a Text but rolls like a number wheel: each digit
// that changes slides out and the new one slides in (up when the value grows,
// down when it shrinks). Anything above 999 reads "999+". All motion is
// declarative property animation, so nothing runs per frame in JS.
Item {
    id: root

    property int value: 0
    property color color: theme.foreground
    property int pixelSize: 12
    property alias font: metrics.font
    property int duration: 220

    // Set before `shown` so digits know which way to roll.
    property int direction: 1
    property string shown: format(value)
    property int lastValue: value

    function format(v) { return v > 999 ? "999+" : String(Math.max(0, v)) }

    onValueChanged: {
        direction = value >= lastValue ? 1 : -1
        lastValue = value
        shown = format(value)
    }

    Text {
        id: metrics
        visible: false
        font.pixelSize: root.pixelSize
        text: "0"
    }

    implicitWidth: row.implicitWidth
    implicitHeight: Math.ceil(metrics.implicitHeight)
    clip: true

    Row {
        id: row
        Repeater {
            model: root.shown.length
            delegate: Item {
                id: slot
                required property int index
                readonly property string ch: root.shown.charAt(index)
                property bool ready: false
                implicitWidth: cur.implicitWidth
                width: implicitWidth
                height: root.height

                Component.onCompleted: ready = true

                onChChanged: {
                    if (!ready) return
                    old.text = cur.text
                    cur.text = ch
                    const h = root.height * root.direction
                    roll.stop()
                    curIn.from = h; oldOut.to = -h
                    roll.start()
                }

                Text {
                    id: old
                    font: metrics.font
                    color: root.color
                    opacity: 0
                }
                Text {
                    id: cur
                    font: metrics.font
                    color: root.color
                    text: slot.ch
                }
                ParallelAnimation {
                    id: roll
                    NumberAnimation { id: curIn; target: cur; property: "y"; to: 0; duration: root.duration; easing.type: Easing.OutCubic }
                    NumberAnimation { id: oldOut; target: old; property: "y"; from: 0; duration: root.duration; easing.type: Easing.OutCubic }
                    NumberAnimation { target: old; property: "opacity"; from: 1; to: 0; duration: root.duration }
                    NumberAnimation { target: cur; property: "opacity"; from: 0; to: 1; duration: root.duration }
                }
            }
        }
    }
}
