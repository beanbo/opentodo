import QtQuick
import OpenTodoUI

// MSwitch — 50×30 toggle. Ported from m-screens2.jsx MToggle.
Rectangle {
    id: root
    property bool checked: false
    signal toggled()

    width: 50; height: 30; radius: 15
    color: checked ? ThemeManager.accentColor : (ThemeManager.isDark ? "#4a4a4a" : "#d4d4d0")
    Behavior on color { ColorAnimation { duration: Duration.fast } }

    Rectangle {
        x: root.checked ? 23 : 3
        anchors.verticalCenter: parent.verticalCenter
        width: 24; height: 24; radius: 12
        color: "#ffffff"
        Behavior on x { NumberAnimation { duration: Duration.fast; easing.type: Easing.OutCubic } }
    }
    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.toggled() }
}
