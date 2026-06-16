import QtQuick
import OpenTodoUI

// MEditorMetaRow — a tappable property row in the editor (icon · label · value ·
// chevron). Ported from m-screens2.jsx MMetaRow.
Rectangle {
    id: root

    property string iconName: ""
    property color  iconColor: ThemeManager.secondaryTextColor
    property string label: ""
    property string value: ""
    property color  valueColor: ThemeManager.textColor
    property bool   last: false
    signal clicked()

    height: 54
    color: ma.pressed ? ThemeManager.hoverColor : "transparent"

    Rectangle {
        visible: !root.last
        anchors.bottom: parent.bottom
        anchors.left: parent.left; anchors.leftMargin: 18
        width: parent.width - 18; height: 1
        color: ThemeManager.hairlineColor
    }

    IconSvg {
        id: ic
        anchors.left: parent.left; anchors.leftMargin: 18
        anchors.verticalCenter: parent.verticalCenter
        name: root.iconName; size: 20; color: root.iconColor
    }
    Text {
        anchors.left: ic.right; anchors.leftMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        text: root.label
        font.pixelSize: 15; font.weight: Font.Medium
        color: ThemeManager.secondaryTextColor
    }
    IconSvg {
        id: chev
        anchors.right: parent.right; anchors.rightMargin: 18
        anchors.verticalCenter: parent.verticalCenter
        name: "chevron-right"; size: 16; color: ThemeManager.faintTextColor
    }
    Text {
        anchors.right: chev.left; anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(implicitWidth, root.width * 0.45)
        text: root.value
        font.pixelSize: 14; font.weight: Font.DemiBold
        color: root.valueColor
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignRight
    }
    MouseArea { id: ma; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.clicked() }
}
