import QtQuick
import OpenTodoUI

// MCheck — circular completion checkbox tinted by task priority.
// Ported from m-components.jsx MCheck: priority-coloured ring, a faint inner
// wash for unfinished P1–P3 so the circle reads on busy backgrounds, and an
// accent-foreground check glyph when done.
Item {
    id: root

    property bool done: false
    property int  priority: 4
    property var  dens
    signal toggled()

    readonly property color checkColor: {
        switch (priority) {
        case 1:  return ThemeManager.priorityP1Color
        case 2:  return ThemeManager.priorityP2Color
        case 3:  return ThemeManager.priorityP3Color
        default: return ThemeManager.priorityP4Color
        }
    }

    implicitWidth:  dens ? dens.check : 24
    implicitHeight: implicitWidth

    Rectangle {
        id: circ
        anchors.fill: parent
        radius: width / 2
        color: root.done ? root.checkColor : "transparent"
        border.color: root.checkColor
        border.width: 2

        Behavior on color { ColorAnimation { duration: Duration.fast } }

        // Faint priority wash for unfinished P1–P3.
        Rectangle {
            visible: !root.done && root.priority <= 3
            anchors.fill: parent
            anchors.margins: 3
            radius: width / 2
            color: root.checkColor
            opacity: 0.12
        }

        IconSvg {
            visible: root.done
            anchors.centerIn: parent
            name: "check"
            size: root.dens ? root.dens.checkInner : 13
            color: ThemeManager.accentForeground
        }
    }

    MouseArea {
        anchors.fill: parent
        anchors.margins: -8   // expand the hit-zone past the visual circle
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggled()
    }
}
