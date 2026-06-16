import QtQuick
import OpenTodoUI

// MSheet — bottom-sheet container with scrim + slide-up. Ported from
// m-components.jsx MSheet (radius 22 top corners, 36×4 drag handle,
// .3s cubic-bezier slide, max 88% height). The parent owns the `open` flag;
// tapping the scrim or handle emits closeRequested() so the parent can clear
// its state (mirrors the design's onClose callback).
Item {
    id: root

    property bool open: false
    property int  panelMaxHeight: Math.round(height * 0.88)
    default property alias content: contentHost.data
    signal closeRequested()

    anchors.fill: parent
    visible: open || panel.y < height
    z: 100

    Rectangle {
        id: scrim
        anchors.fill: parent
        color: ThemeManager.scrimColor
        opacity: root.open ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 250 } }
        MouseArea {
            anchors.fill: parent
            enabled: root.open
            onClicked: root.closeRequested()
        }
    }

    Rectangle {
        id: panel
        width: parent.width
        x: 0
        readonly property int _contentH: handle.height + 14 + contentHost.implicitHeight + 12
        height: Math.min(root.panelMaxHeight, _contentH)
        y: root.open ? (root.height - height) : root.height
        Behavior on y { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
        color: ThemeManager.surfaceColor
        radius: 22

        // Square off the bottom corners (radius rounds all four).
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: parent.radius
            color: parent.color
        }

        // Swallow taps on empty panel area so they don't reach the scrim.
        MouseArea { anchors.fill: parent }

        Rectangle {
            id: handle
            anchors.horizontalCenter: parent.horizontalCenter
            y: 8
            width: 36; height: 4; radius: 2
            color: ThemeManager.borderColor
        }

        Column {
            id: contentHost
            anchors.top: handle.bottom
            anchors.topMargin: 6
            anchors.left: parent.left
            anchors.right: parent.right
        }
    }
}
