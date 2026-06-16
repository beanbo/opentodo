import QtQuick
import OpenTodoUI

// MDrawerItem — one row in the mobile navigation drawer. Ported from
// m-screens.jsx MDrawerItem: icon + label + optional count/badge + optional
// pin toggle (for projects).
Rectangle {
    id: root
    property string itemIcon: ""
    property color  itemIconColor: ThemeManager.secondaryTextColor
    property string itemLabel: ""
    property int    count: -1
    property int    badge: -1
    property bool   active: false
    property bool   showPin: false
    property bool   pinned: false
    signal clicked()
    signal pinClicked()

    height: 50; radius: 13
    color: active ? ThemeManager.accentContainer : (ma.pressed ? ThemeManager.hoverColor : "transparent")

    MouseArea { id: ma; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.clicked() }

    IconSvg {
        id: ic
        anchors.left: parent.left; anchors.leftMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        name: root.itemIcon; size: 21
        color: root.active ? ThemeManager.accentColor : root.itemIconColor
    }
    Text {
        anchors.left: ic.right; anchors.leftMargin: 15
        anchors.right: rightStuff.left; anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: root.itemLabel
        font.pixelSize: 15; font.weight: root.active ? Font.Bold : Font.Medium
        color: root.active ? ThemeManager.accentColor : ThemeManager.textColor
        elide: Text.ElideRight
    }
    Row {
        id: rightStuff
        anchors.right: parent.right; anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6
        Rectangle {
            visible: root.badge >= 0
            anchors.verticalCenter: parent.verticalCenter
            height: 18; width: Math.max(18, bt.implicitWidth + 10); radius: 9
            color: ThemeManager.errorColor
            Text { id: bt; anchors.centerIn: parent; text: root.badge; color: "#ffffff"; font.pixelSize: 11; font.weight: Font.Bold }
        }
        Text {
            visible: root.count >= 0
            anchors.verticalCenter: parent.verticalCenter
            text: root.count; font.pixelSize: 13; font.weight: Font.DemiBold
            color: ThemeManager.secondaryTextColor
        }
        Rectangle {
            visible: root.showPin
            anchors.verticalCenter: parent.verticalCenter
            width: 36; height: 36; radius: 18
            color: root.pinned ? ThemeManager.accentContainer : "transparent"
            IconSvg { anchors.centerIn: parent; name: "pin"; size: 17; color: root.pinned ? ThemeManager.accentColor : ThemeManager.faintTextColor }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.pinClicked() }
        }
    }
}
