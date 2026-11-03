import QtQuick
import OpenTodoUI

// MSnackbar — undo snackbar. Ported from m-pickers.jsx MSnackbar.
// Inverted pill (light on dark theme / dark on light), 4.2s auto-hide,
// slide-up. Call show(message, actionLabel); actionTriggered() fires when the
// user taps the action (e.g. «Отменить»). bottomMargin lifts it above the nav.
Item {
    id: root

    property int    bottomMargin: 78
    property string message: ""
    property string actionLabel: ""
    property bool   showing: false
    signal actionTriggered()

    function show(msg, action) {
        message = msg
        actionLabel = (action === undefined || action === null) ? "" : action
        showing = true
        hideTimer.restart()
    }
    function hide() { showing = false; hideTimer.stop() }

    anchors.fill: parent
    visible: showing || bar.y < (root.height + 20)
    z: 90

    Timer { id: hideTimer; interval: 4200; onTriggered: root.showing = false }

    Rectangle {
        id: bar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        height: 50
        radius: 13
        // Inverse surface from theme tokens (text colour as the bar, surface
        // colour for its text) instead of hardcoded greys.
        color: ThemeManager.textColor
        y: root.showing ? (root.height - root.bottomMargin - height) : (root.height + 20)
        Behavior on y { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
        opacity: root.showing ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.right: actionBtn.visible ? actionBtn.left : parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: root.message
            color: ThemeManager.surfaceColor
            font.pixelSize: 14
            font.weight: Font.Medium
            elide: Text.ElideRight
        }

        Rectangle {
            id: actionBtn
            visible: root.actionLabel !== ""
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            width: visible ? actionTxt.implicitWidth + 28 : 0
            height: 34
            radius: 8
            color: actionMa.pressed
                   ? Qt.rgba(ThemeManager.accentColor.r, ThemeManager.accentColor.g,
                             ThemeManager.accentColor.b, 0.15)
                   : "transparent"
            Text {
                id: actionTxt
                anchors.centerIn: parent
                text: root.actionLabel
                color: ThemeManager.accentColor
                font.pixelSize: 14
                font.weight: Font.Bold
            }
            MouseArea {
                id: actionMa
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: { root.actionTriggered(); root.hide() }
            }
        }
    }
}
