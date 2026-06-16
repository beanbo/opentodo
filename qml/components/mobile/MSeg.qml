import QtQuick
import OpenTodoUI

// MSeg — M3 segmented control. Ported from m-components.jsx MSeg.
// `options` is an array of { value, label, icon } objects; the selected
// segment gets a raised surface pill, the rest are flat.
Rectangle {
    id: root

    property var options: []          // [{ value, label, icon }]
    property string value: ""
    signal changed(string value)

    implicitHeight: 40
    radius: 11
    color: ThemeManager.isDark ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(0, 0, 0, 0.05)

    Row {
        anchors.fill: parent
        anchors.margins: 3
        spacing: 2

        Repeater {
            model: root.options
            delegate: Rectangle {
                required property var modelData
                readonly property bool on: modelData.value === root.value
                width: (root.width - 6 - (root.options.length - 1) * 2) / root.options.length
                height: parent.height
                radius: 9
                color: on ? ThemeManager.surfaceColor : "transparent"
                Behavior on color { ColorAnimation { duration: Duration.fast } }

                Row {
                    anchors.centerIn: parent
                    spacing: 6
                    IconSvg {
                        visible: !!modelData.icon
                        anchors.verticalCenter: parent.verticalCenter
                        name:  modelData.icon || ""
                        size:  15
                        color: on ? ThemeManager.textColor : ThemeManager.secondaryTextColor
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label || ""
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        color: on ? ThemeManager.textColor : ThemeManager.secondaryTextColor
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.changed(modelData.value)
                }
            }
        }
    }
}
