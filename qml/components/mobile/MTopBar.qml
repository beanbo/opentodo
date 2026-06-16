import QtQuick
import OpenTodoUI

// MTopBar — screen top bar. Ported from m-screens.jsx MTopBar.
// Compact mode: 56px row with hamburger + inline title + right actions.
// Big mode: 56px icon row, then a large 27px title block with optional
// subtitle below. Right-side action buttons are injected as default children.
Item {
    id: root

    property bool   big: false
    property string title: ""
    property string titleIconName: ""
    property color  titleColor: ThemeManager.accentColor
    property string subtitle: ""
    default property alias rightActions: rightRow.data
    signal menuClicked()

    width: parent ? parent.width : 0
    implicitHeight: big ? (58 + bigBlock.implicitHeight) : 58

    Rectangle { anchors.fill: parent; color: ThemeManager.backgroundColor }

    Item {
        id: iconRow
        width: parent.width
        height: 56
        y: 2

        MIconButton {
            id: menuBtn
            anchors.left: parent.left
            anchors.leftMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            iconName: "menu"
            iconColor: ThemeManager.textColor
            iconSize: 23
            onClicked: root.menuClicked()
        }

        // Compact inline title (hidden in big mode).
        Item {
            visible: !root.big
            anchors.left: menuBtn.right
            anchors.leftMargin: 6
            anchors.right: rightRow.left
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            height: parent.height

            IconSvg {
                id: ctIcon
                visible: root.titleIconName !== ""
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                name: root.titleIconName
                size: 21
                color: root.titleColor
            }
            Text {
                anchors.left: ctIcon.visible ? ctIcon.right : parent.left
                anchors.leftMargin: ctIcon.visible ? 9 : 0
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: root.title
                font.pixelSize: 20
                font.weight: Font.DemiBold
                color: ThemeManager.textColor
                elide: Text.ElideRight
            }
        }

        Row {
            id: rightRow
            anchors.right: parent.right
            anchors.rightMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0
        }
    }

    // Big title block.
    Column {
        id: bigBlock
        visible: root.big
        anchors.top: iconRow.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        topPadding: 2
        bottomPadding: 14
        spacing: 5

        Row {
            spacing: 10
            IconSvg {
                id: bigIcon
                visible: root.titleIconName !== ""
                anchors.verticalCenter: parent.verticalCenter
                name: root.titleIconName
                size: 26
                color: root.titleColor
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.title
                font.pixelSize: 27
                font.weight: Font.Bold
                font.letterSpacing: -0.3
                color: ThemeManager.textColor
            }
        }
        Text {
            visible: root.subtitle !== ""
            leftPadding: root.titleIconName !== "" ? 36 : 0
            text: root.subtitle
            font.pixelSize: 13
            font.weight: Font.Medium
            color: ThemeManager.secondaryTextColor
        }
    }
}
