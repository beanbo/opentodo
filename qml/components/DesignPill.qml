import QtQuick
import QtQuick.Controls.Material
import OpenTodoUI

// Дизайнерский Pill (.design-temp/opentodo/project/view-others.jsx:253-260)
// Используется как сегмент в группе выбора (например 12/24-hour).
AbstractButton {
    id: control
    implicitHeight: 24
    leftPadding: 10
    rightPadding: 10
    topPadding: 4
    bottomPadding: 4

    property bool active: control.checked || control.highlighted

    background: Rectangle {
        radius: Radius.md
        color: control.active ? ThemeManager.accentColor : "transparent"
        border.color: control.active ? "transparent" : ThemeManager.borderColor
        border.width: 1

        Behavior on color { ColorAnimation { duration: 150 } }
        Behavior on border.color { ColorAnimation { duration: 150 } }
    }

    contentItem: Text {
        text: control.text
        font.pixelSize: FontSize.small
        color: control.active ? "#ffffff" : ThemeManager.secondaryTextColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
