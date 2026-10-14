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

    // Once the pointer leaves the window, the same drag becomes a system
    // drag carrying the tasks as a file (an .ics for one dated task, else a
    // .txt), so they can be dropped on a file manager, a mail composer or a
    // calendar. The internal drag is cancelled first so no row sees a drop.
    function startExternal() {
        if (!Drag.active) return false
        const url = backend.dragOutFile()
        if (!url) return false
        Drag.cancel()
        hint = ""
        Drag.dragType = Drag.None
        Drag.mimeData = { "text/uri-list": url + "\r\n", "text/plain": url }
        Drag.startDrag(Qt.CopyAction)
        Drag.dragType = Drag.Internal
        return true
    }

    visible: Drag.active
    z: 1000
    width: content.implicitWidth + 22
    height: 26
    radius: 13
    color: theme.accent
    opacity: 0.94
    Drag.keys: ["fairfax-tasks"]
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
