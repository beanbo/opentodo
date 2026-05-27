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
    // Q4.01 (night iter-14): theme-aware. Раньше hardcoded hex для P1-P4
    // не подстраивались под темы kale/blueberry/etc. TaskItem priority
    // circle уже использует ThemeManager.priorityP*Color — синхронизируем.
    readonly property var priorities: [
        { value: 1, label: qsTr("Приоритет 1"), flagColor: ThemeManager.priorityP1Color },
        { value: 2, label: qsTr("Приоритет 2"), flagColor: ThemeManager.priorityP2Color },
        { value: 3, label: qsTr("Приоритет 3"), flagColor: ThemeManager.priorityP3Color },
        { value: 4, label: qsTr("Приоритет 4"), flagColor: ThemeManager.priorityP4Color }
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
                    // Q4.01 (redesign): unicode flag (U+2691) → stroke IconSvg "flag"
                    IconSvg {
                        name: "flag"
                        size: 14
                        color: modelData.flagColor
                        Layout.alignment: Qt.AlignVCenter
                    }

                    // Label
                    Text {
                        Layout.fillWidth: true
                        text:  modelData.label
                        color: root.textCol
                        font.pixelSize: FontSize.bodySmall
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
