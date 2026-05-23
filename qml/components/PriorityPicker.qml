import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

Popup {
    id: root

    // ─── Public API ────────────────────────────────────────────────────────────
    property int selectedPriority: 4
    signal prioritySelected(int priority)

    // ─── Internals ─────────────────────────────────────────────────────────────
    readonly property bool isDark: ThemeManager.isDark

    readonly property color bgColor:   ThemeManager.backgroundColor
    readonly property color borderCol: ThemeManager.borderColor
    readonly property color textCol:   ThemeManager.textColor
    readonly property color hoverCol:  ThemeManager.hoverColor

    // Priority definitions
    readonly property var priorities: [
        { value: 1, label: qsTr("Приоритет 1"), flagColor: "#db4c3f" },
        { value: 2, label: qsTr("Приоритет 2"), flagColor: "#f49c18" },
        { value: 3, label: qsTr("Приоритет 3"), flagColor: "#4073ff" },
        { value: 4, label: qsTr("Приоритет 4"), flagColor: "#808080" }
    ]

    // ─── Popup geometry ────────────────────────────────────────────────────────
    width:  200
    height: 4 * 36 + 16   // 4 rows × 36px + top+bottom padding
    padding: 0
    modal:  false
    focus:  true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // ─── Background ────────────────────────────────────────────────────────────
    background: Rectangle {
        color:        root.bgColor
        radius:       8
        border.color: root.borderCol
        border.width: 1
    }

    // ─── Content ───────────────────────────────────────────────────────────────
    ColumnLayout {
        anchors {
            left:    parent.left
            right:   parent.right
            top:     parent.top
            topMargin:    8
            bottomMargin: 8
        }
        spacing: 0

        Repeater {
            model: root.priorities

            delegate: Rectangle {
                required property var  modelData
                required property int  index

                Layout.fillWidth: true
                height: 36

                // B-7.25: state-driven hover color binding — avoids breaking
                // QML binding by assigning color imperatively in onEntered/onExited.
                color: prioRowMA.containsMouse ? root.hoverCol : "transparent"
                radius: 4

                RowLayout {
                    anchors {
                        fill:            parent
                        leftMargin:      12
                        rightMargin:     12
                    }
                    spacing: 10

                    // Flag icon
                    Text {
                        text:            "\u2691"   // ⚑
                        font.pixelSize:  16
                        color:           modelData.flagColor
                        verticalAlignment: Text.AlignVCenter
                        Layout.alignment: Qt.AlignVCenter
                    }

                    // Label
                    Text {
                        Layout.fillWidth: true
                        text:  modelData.label
                        color: root.textCol
                        font.pixelSize: 14
                        verticalAlignment: Text.AlignVCenter
                    }

                    // Checkmark — task 17cea65b iter-3: было unicode "✓".
                    IconSvg {
                        name: "check"
                        size: 14
                        color: modelData.flagColor
                        visible: root.selectedPriority === modelData.value
                        Layout.alignment: Qt.AlignVCenter
                    }
                }

                // Hover + click
                MouseArea {
                    id: prioRowMA
                    anchors.fill:  parent
                    cursorShape:   Qt.PointingHandCursor
                    hoverEnabled:  true

                    onClicked: {
                        root.selectedPriority = modelData.value
                        root.prioritySelected(modelData.value)
                        root.close()
                    }
                }
            }
        }
    }
}
