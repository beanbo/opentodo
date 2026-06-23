import QtQuick
import QtQuick.Effects
import OpenTodoUI

// MFab — floating action button. Ported from m-components.jsx MFab.
// 60×60 squircle (radius 19), accent fill, drop shadow, press-scale.
Item {
    id: root

    property bool small: false
    property string iconName: "plus"
    signal clicked()

    // Accessibility: announce the FAB as a button to TalkBack.
    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Добавить задачу")
    Accessible.onPressAction: root.clicked()

    readonly property int s: small ? 48 : 60
    implicitWidth:  s
    implicitHeight: s

    Rectangle {
        id: fab
        anchors.fill: parent
        radius: root.small ? 16 : 19
        color: ThemeManager.accentColor
        scale: ma.pressed ? 0.92 : 1.0
        Behavior on scale { NumberAnimation { duration: Duration.fast; easing.type: Easing.InOutQuad } }

        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: ThemeManager.isDark ? Qt.rgba(0, 0, 0, 0.5)
                                             : Qt.rgba(0.86, 0.30, 0.25, 0.34)
            shadowVerticalOffset: 8
            shadowBlur: 0.7
        }

        IconSvg {
            anchors.centerIn: parent
            name:  root.iconName
            size:  root.small ? 22 : 27
            color: ThemeManager.accentForeground
        }

        MouseArea {
            id: ma
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.clicked()
        }
    }
}
