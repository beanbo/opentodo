import QtQuick
import OpenTodoUI

// MNavItem — one bottom-navigation slot. Ported from m-app.jsx NavItem.
// 56×30 tonal pill behind the icon when active, short label below; empty
// (dim) slots get a dashed pill. Pop-animates on activation, supports
// long-press (→ manage pins).
Item {
    id: root

    property string iconName: ""
    property string label: ""
    property bool   active: false
    property bool   dim: false
    signal clicked()
    signal longPressed()

    // Accessibility: a navigation button that reports its selected state.
    Accessible.role: Accessible.Button
    Accessible.name: label
    Accessible.checkable: true
    Accessible.checked: root.active
    Accessible.onPressAction: root.clicked()

    property bool _fired: false

    Column {
        id: col
        anchors.centerIn: parent
        spacing: 3
        width: parent.width

        Rectangle {
            id: pill
            anchors.horizontalCenter: parent.horizontalCenter
            width: 56; height: 30; radius: 16
            color: root.active ? ThemeManager.accentContainer : "transparent"
            border.width: root.dim ? 1.5 : 0
            border.color: root.dim ? ThemeManager.borderColor : "transparent"
            Behavior on color { ColorAnimation { duration: Duration.normal } }

            IconSvg {
                anchors.centerIn: parent
                name:  root.iconName
                size:  21
                color: root.active ? ThemeManager.accentColor
                       : (root.dim ? ThemeManager.faintTextColor : ThemeManager.secondaryTextColor)
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(implicitWidth, 88)
            text: root.label
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            font.pixelSize: 11
            font.weight: root.active ? Font.Bold : Font.Medium
            color: root.active ? ThemeManager.accentColor
                   : (root.dim ? ThemeManager.faintTextColor : ThemeManager.secondaryTextColor)
        }
    }

    // Pop animation on activation: 0.62 → 1.06 → 1.0 (matches the design).
    SequentialAnimation {
        id: popAnim
        NumberAnimation { target: col; property: "scale"; from: 0.62; to: 1.06; duration: 180; easing.type: Easing.OutCubic }
        NumberAnimation { target: col; property: "scale"; to: 1.0; duration: 160; easing.type: Easing.OutCubic }
    }
    onActiveChanged: if (active) popAnim.restart()

    Timer {
        id: lpTimer
        interval: 460
        onTriggered: { root._fired = true; root.longPressed() }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onPressed: { root._fired = false; lpTimer.restart() }
        onReleased: lpTimer.stop()
        onCanceled: lpTimer.stop()
        onClicked: {
            if (root._fired) { root._fired = false; return }
            root.clicked()
        }
    }
}
