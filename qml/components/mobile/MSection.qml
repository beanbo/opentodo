import QtQuick
import OpenTodoUI

// MSection — collapsible list section header. Ported from m-components.jsx
// MSection: rotating chevron + bold title + muted count, hairline underline.
Rectangle {
    id: root

    property string title: ""
    property int    count: -1
    property bool   collapsed: false
    property var    dens
    signal toggled()

    width: parent ? parent.width : 0
    height: dens ? dens.sectionH : 46
    color: ThemeManager.backgroundColor

    Rectangle {  // hairline underline
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: ThemeManager.hairlineColor
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: dens ? dens.rowPadH : 18
        anchors.verticalCenter: parent.verticalCenter
        spacing: 9

        IconSvg {
            anchors.verticalCenter: parent.verticalCenter
            name: "chevron-down"
            size: 15
            color: ThemeManager.secondaryTextColor
            rotation: root.collapsed ? -90 : 0
            Behavior on rotation { NumberAnimation { duration: Duration.normal } }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.title
            font.pixelSize: dens ? dens.sectionFs : 13
            font.weight: Font.Bold
            font.letterSpacing: 0.2
            color: ThemeManager.textColor
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.count >= 0
            text: root.count
            font.pixelSize: (dens ? dens.sectionFs : 13) - 1
            font.weight: Font.DemiBold
            color: ThemeManager.secondaryTextColor
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggled()
    }
}
