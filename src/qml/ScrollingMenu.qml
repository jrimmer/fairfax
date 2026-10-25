import QtQuick
import QtQuick.Controls

// A Menu that keeps its scroll position in a window shorter than itself. The
// style's own list takes its height from the list's contentHeight, which
// ListView re-estimates as the menu scrolls; the menu then resized (and sprang
// back to the top) on every step. This list holds its height at the window's
// instead, and leaves it alone once the menu is taller than the window.
Menu {
    id: root

    contentItem: ListView {
        implicitHeight: Math.min(contentHeight, ApplicationWindow.window ? ApplicationWindow.window.height - root.topPadding - root.bottomPadding : contentHeight)
        model: root.contentModel
        interactive: ApplicationWindow.window ? contentHeight + root.topPadding + root.bottomPadding > root.height : false
        clip: true
        currentIndex: root.currentIndex
        ScrollIndicator.vertical: ScrollIndicator {}
    }
}
