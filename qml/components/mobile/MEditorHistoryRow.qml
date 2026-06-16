import QtQuick
import OpenTodoUI

// MEditorHistoryRow — one timeline entry in the editor's change history.
Item {
    id: root
    property string label: ""
    property string when: ""
    height: 40

    Rectangle {
        id: dot
        anchors.left: parent.left; anchors.leftMargin: 6
        anchors.top: parent.top; anchors.topMargin: 9
        width: 7; height: 7; radius: 4
        color: ThemeManager.faintTextColor
    }
    Column {
        anchors.left: dot.right; anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Text { text: root.label; font.pixelSize: 13; color: ThemeManager.textColor }
        Text { text: root.when; font.pixelSize: 11; color: ThemeManager.faintTextColor }
    }
}
