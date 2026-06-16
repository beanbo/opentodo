import QtQuick
import OpenTodoUI

// MPinRow — one row in the pin-management sheet (smart list or project).
// Tonal background + №N badge when pinned; a pin toggle on the right.
Rectangle {
    id: root

    property string rowId: ""
    property string rowName: ""
    property string rowIconName: "hash"
    property color  rowColor: ThemeManager.secondaryTextColor
    property bool   pinned: false
    property int    pinIndex: -1     // 0-based; renders №(pinIndex+1)
    signal toggled()

    width: parent ? parent.width : 0
    height: 54
    radius: 14
    color: pinned ? ThemeManager.accentContainer
           : (ma.pressed ? ThemeManager.hoverColor : "transparent")
    Behavior on color { ColorAnimation { duration: Duration.fast } }

    IconSvg {
        id: ic
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        name:  root.rowIconName
        size:  22
        color: root.rowColor
    }

    Text {
        anchors.left: ic.right
        anchors.leftMargin: 14
        anchors.right: numBadge.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: root.rowName
        elide: Text.ElideRight
        font.pixelSize: 15
        font.weight: root.pinned ? Font.Bold : Font.Medium
        color: ThemeManager.textColor
    }

    Text {
        id: numBadge
        visible: root.pinned
        anchors.right: pinCircle.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: "№" + (root.pinIndex + 1)
        font.pixelSize: 12
        font.weight: Font.Bold
        color: ThemeManager.accentColor
    }

    Rectangle {
        id: pinCircle
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        width: 30; height: 30; radius: 15
        color: root.pinned ? ThemeManager.accentColor : "transparent"
        border.width: root.pinned ? 0 : 1.5
        border.color: ThemeManager.borderColor

        IconSvg {
            anchors.centerIn: parent
            name:  "pin"
            size:  16
            color: root.pinned ? ThemeManager.accentForeground : ThemeManager.faintTextColor
        }
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggled()
    }
}
