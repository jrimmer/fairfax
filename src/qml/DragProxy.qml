import QtQuick

// What follows the pointer while dragging tasks: a pill with the count, and
// the drop target's hint ("Drop to Star", "Drop to Move") as you hover.
Rectangle {
    id: root

    property string label
    property string hint

    // Placed under the pointer on press, before the drag threshold is
    // crossed, because MouseArea drags its target from where it was then.
    function place(scenePoint) {
        if (Drag.active) return
        x = scenePoint.x - 10
        y = scenePoint.y - 13
    }
    function start(text) {
        label = text
        hint = ""
        Drag.active = true
    }
    function finish() {
        if (!Drag.active) return
        Drag.drop()
        Drag.active = false
        hint = ""
    }

    visible: Drag.active
    z: 1000
    width: content.implicitWidth + 22
    height: 26
    radius: 13
    color: theme.accent
    opacity: 0.94
    Drag.keys: ["precrime-tasks"]
    Drag.hotSpot.x: 10
    Drag.hotSpot.y: 13

    Text {
        id: content
        anchors.centerIn: parent
        text: root.hint ? root.label + " · " + root.hint : root.label
        color: theme.accentForeground
        font.pixelSize: backend.fontSize - 2
        font.weight: Font.DemiBold
    }
}
