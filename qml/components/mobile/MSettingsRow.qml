import QtQuick
import OpenTodoUI

// MSettingsRow — one settings entry: icon tile + label + optional sub-value +
// (toggle switch OR chevron). Ported from m-screens2.jsx MSettingRow.
Rectangle {
    id: root
    property string rowIcon: ""
    property color  rowIconColor: ThemeManager.secondaryTextColor
    property string rowLabel: ""
    property string rowValue: ""
    property bool   controlChecked: false
    property bool   showChevron: false
    property bool   last: false
    signal toggled()
    signal rowClicked()

    height: Math.max(56, txtCol.implicitHeight + 16)
    color: (showChevron && ma.pressed) ? ThemeManager.hoverColor : "transparent"

    Rectangle {
        visible: !root.last
        anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.leftMargin: 18
        width: parent.width - 18; height: 1
        color: ThemeManager.hairlineColor
    }

    Rectangle {
        id: tile
        anchors.left: parent.left; anchors.leftMargin: 18
        anchors.verticalCenter: parent.verticalCenter
        width: 34; height: 34; radius: 10
        color: Qt.rgba(Qt.color(root.rowIconColor).r, Qt.color(root.rowIconColor).g, Qt.color(root.rowIconColor).b, 0.12)
        IconSvg { anchors.centerIn: parent; name: root.rowIcon; size: 18; color: root.rowIconColor }
    }
    Column {
        id: txtCol
        anchors.left: tile.right; anchors.leftMargin: 14
        anchors.right: parent.right; anchors.rightMargin: root.showChevron ? 44 : 62
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Text { width: parent.width; text: root.rowLabel; font.pixelSize: 15; font.weight: Font.DemiBold; color: ThemeManager.textColor; elide: Text.ElideRight }
        Text { visible: root.rowValue !== ""; width: parent.width; text: root.rowValue; font.pixelSize: 12; color: ThemeManager.secondaryTextColor; elide: Text.ElideRight }
    }

    MSwitch {
        id: ctl
        visible: !root.showChevron
        anchors.right: parent.right; anchors.rightMargin: 18
        anchors.verticalCenter: parent.verticalCenter
        checked: root.controlChecked
        onToggled: root.toggled()
    }
    IconSvg {
        visible: root.showChevron
        anchors.right: parent.right; anchors.rightMargin: 18
        anchors.verticalCenter: parent.verticalCenter
        name: "chevron-right"; size: 18; color: ThemeManager.faintTextColor
    }

    MouseArea { id: ma; anchors.fill: parent; enabled: root.showChevron; cursorShape: Qt.PointingHandCursor; onClicked: root.rowClicked() }
}
