import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

Rectangle {
    id: root

    required property string labelName
    property string labelColor: "#808080"
    property bool removable: false

    signal removed()
    signal clicked()

    function hexToRgba(hex, alpha) {
        var r = parseInt(hex.slice(1, 3), 16) / 255
        var g = parseInt(hex.slice(3, 5), 16) / 255
        var b = parseInt(hex.slice(5, 7), 16) / 255
        return Qt.rgba(r, g, b, alpha)
    }

    implicitHeight: 22
    implicitWidth: chipRow.implicitWidth + 16
    radius: 10

    color: hoverHandler.hovered ? hexToRgba(root.labelColor, 0.25) : hexToRgba(root.labelColor, 0.15)

    Behavior on color {
        ColorAnimation { duration: 120 }
    }

    RowLayout {
        id: chipRow
        anchors.centerIn: parent
        spacing: 4

        Text {
            id: labelText
            text: root.labelName
            font.pixelSize: FontSize.small
            color: root.labelColor
            verticalAlignment: Text.AlignVCenter
        }

        Text {
            id: removeButton
            visible: root.removable
            text: "\u00D7"
            font.pixelSize: FontSize.bodySmall
            color: root.labelColor
            verticalAlignment: Text.AlignVCenter

            MouseArea {
                anchors.fill: parent
                anchors.margins: -2
                cursorShape: Qt.PointingHandCursor
                onClicked: (mouse) => {
                    mouse.accepted = true
                    root.removed()
                }
            }
        }
    }

    HoverHandler {
        id: hoverHandler
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: (mouse) => {
            if (!root.removable) {
                root.clicked()
            }
        }
    }
}
