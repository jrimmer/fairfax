import QtQuick
import QtQuick.Shapes
import "Icons.js" as Icons

// A themed outline icon. `name` picks a path from Icons.js; the stroke stays
// visually constant whatever the size.
Item {
    id: root

    property string name
    property color color: theme.foreground
    property real size: 16
    property real stroke: 1.6
    property bool filled: false

    implicitWidth: size
    implicitHeight: size

    Shape {
        width: 24
        height: 24
        preferredRendererType: Shape.CurveRenderer
        transform: Scale { xScale: root.width / 24; yScale: root.height / 24 }

        ShapePath {
            strokeColor: root.color
            strokeWidth: root.stroke * 24 / Math.max(1, root.width)
            fillColor: root.filled ? root.color : "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: Icons.paths[root.name] || "" }
        }
    }
}
