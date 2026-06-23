import QtQuick
import QtQuick.Controls
import OpenTodoUI

// MIconButton — a flat, circular icon tap-target. Default 46×46 (≥48dp-class
// touch zone per the design's iconBtnM). Subtle pressed wash; no border.
Item {
    id: root

    property string iconName: ""
    property color  iconColor: ThemeManager.textColor
    property int    iconSize: 21
    property int    diameter: 46
    property alias  pressed: ma.pressed
    property string tooltip: ""
    signal clicked()

    // Accessibility: announce as a button to TalkBack/screen readers, named by
    // its tooltip (or icon) and invokable via the accessibility press action.
    Accessible.role: Accessible.Button
    Accessible.name: tooltip !== "" ? tooltip : iconName
    Accessible.onPressAction: root.clicked()

    implicitWidth:  diameter
    implicitHeight: diameter

    Rectangle {
        anchors.centerIn: parent
        width:  root.diameter
        height: root.diameter
        radius: root.diameter / 2
        color:  ma.pressed ? ThemeManager.hoverColor : "transparent"
        Behavior on color { ColorAnimation { duration: Duration.fast } }
    }

    IconSvg {
        anchors.centerIn: parent
        name:  root.iconName
        size:  root.iconSize
        color: root.iconColor
        visible: root.iconName !== ""
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    ToolTip.visible: tooltip !== "" && ma.pressed
    ToolTip.text: tooltip
}
