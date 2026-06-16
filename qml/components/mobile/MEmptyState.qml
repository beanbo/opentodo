import QtQuick
import OpenTodoUI

// MEmptyState — per-view empty state (icon badge + title + multi-line text).
// Ported from m-screens.jsx emptyByView.
Column {
    id: root
    property string iconName: "inbox"
    property color  iconColor: ThemeManager.accentColor
    property string title: ""
    property string text: ""

    spacing: 0
    width: Math.min(parent ? parent.width - 64 : 300, 320)

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        width: 80; height: 80; radius: 40
        color: ThemeManager.surfaceColor
        border.width: 1; border.color: ThemeManager.borderColor
        IconSvg { anchors.centerIn: parent; name: root.iconName; size: 38; color: root.iconColor }
    }
    Item { width: 1; height: 18 }
    Text {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        text: root.title
        font.pixelSize: 17; font.weight: Font.Bold
        color: ThemeManager.textColor
    }
    Item { width: 1; height: 7 }
    Text {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        text: root.text
        font.pixelSize: 14
        color: ThemeManager.secondaryTextColor
        wrapMode: Text.WordWrap
        lineHeight: 1.5
        lineHeightMode: Text.ProportionalHeight
    }
}
