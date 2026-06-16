import QtQuick
import OpenTodoUI

// MChip — meta chip for a task row (icon + text). Ported from m-components.jsx
// MChip: optional tinted pill background (chipColor @ ~12% alpha) or a bare
// transparent chip when `tinted` is false.
Rectangle {
    id: root

    property string text: ""
    property string iconName: ""
    property color  chipColor: ThemeManager.secondaryTextColor
    property bool   tinted: false
    property var    dens

    readonly property int _meta: dens ? dens.meta : 12

    height: dens ? dens.chipH : 23
    implicitWidth: row.implicitWidth + (tinted ? 18 : 4)
    width: implicitWidth
    radius: 7
    color: tinted
           ? Qt.rgba(chipColor.r, chipColor.g, chipColor.b, 0.12)
           : "transparent"

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 5

        IconSvg {
            visible: root.iconName !== ""
            anchors.verticalCenter: parent.verticalCenter
            name:  root.iconName
            size:  root._meta + 2
            color: root.chipColor
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.chipColor
            font.pixelSize: root._meta
            font.weight: Font.Medium
        }
    }
}
