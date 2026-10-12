import QtQuick

// A celebratory overlay. Set anchors.fill, then call burst(x, y) to throw
// small rotating pieces in the list palette up and let them fall. Pieces
// animate on their own and free themselves; the overlay never takes input.
Item {
    id: root

    property int count: 120
    property int duration: 1600

    function burst(x, y) {
        if (!enabled || !visible) return
        const palette = (theme.listPalette && theme.listPalette.length) ? theme.listPalette : [theme.accent]
        for (let i = 0; i < count; ++i) {
            const colors = i % 5 === 0 ? theme.accent : palette[i % palette.length]
            const size = 5 + Math.random() * 6
            const angle = -Math.PI / 2 + (Math.random() - 0.5) * Math.PI * 0.9
            const speed = 0.45 + Math.random() * 0.55
            piece.createObject(root, {
                x0: x, y0: y,
                size: size,
                round: Math.random() < 0.3,
                tint: colors,
                dx: Math.cos(angle) * speed * 380,
                peak: -Math.sin(angle) * speed * 330,
                fall: root.height * 0.5 + Math.random() * 120,
                spin: (Math.random() - 0.5) * 1440,
                life: root.duration * (0.8 + Math.random() * 0.2)
            })
        }
    }

    Component {
        id: piece
        Rectangle {
            id: p
            property real x0
            property real y0
            property real size: 6
            property bool round: false
            property color tint
            property real dx
            property real peak
            property real fall
            property real spin
            property int life: 1600

            x: x0; y: y0
            width: size
            height: round ? size : size * 0.5
            radius: round ? size / 2 : 0
            color: tint
            antialiasing: true
            transformOrigin: Item.Center

            ParallelAnimation {
                running: true
                onFinished: p.destroy()
                NumberAnimation { target: p; property: "x"; to: p.x0 + p.dx; duration: p.life; easing.type: Easing.OutQuad }
                SequentialAnimation {
                    NumberAnimation { target: p; property: "y"; to: p.y0 - p.peak; duration: p.life * 0.38; easing.type: Easing.OutQuad }
                    NumberAnimation { target: p; property: "y"; to: p.y0 - p.peak + p.fall; duration: p.life * 0.62; easing.type: Easing.InQuad }
                }
                NumberAnimation { target: p; property: "rotation"; to: p.spin; duration: p.life }
                SequentialAnimation {
                    PauseAnimation { duration: p.life * 0.65 }
                    NumberAnimation { target: p; property: "opacity"; to: 0; duration: p.life * 0.35 }
                }
            }
        }
    }
}
