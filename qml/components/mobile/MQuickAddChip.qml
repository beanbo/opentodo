import QtQuick
import OpenTodoUI

// MQuickAddChip — togglable pill chip for the quick-add chip row.
// Active → tinted background in chipColor; inactive → bordered + muted.
Rectangle {
    id: root

    property bool   active: false
    property string iconName: ""
    property string label: ""
    property color  chipColor: ThemeManager.accentColor
    signal clicked()

    height: 36
    radius: 20
    implicitWidth: row.implicitWidth + 26
    width: implicitWidth
    color: active ? Qt.rgba(chipColor.r, chipColor.g, chipColor.b, 0.12) : "transparent"
    border.width: active ? 0 : 1
    border.color: ThemeManager.borderColor

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6
        IconSvg {
            anchors.verticalCenter: parent.verticalCenter
            name: root.iconName; size: 15
            color: root.active ? root.chipColor : ThemeManager.secondaryTextColor
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.label
            font.pixelSize: 13; font.weight: Font.DemiBold
            color: root.active ? root.chipColor : ThemeManager.secondaryTextColor
        }
    }
    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.clicked() }
}
