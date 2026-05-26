import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

// Title / content TextArea — fires updateTask on editing finished.
// Q4.01 (redesign): wrapped in a Card-like Rectangle (surface bg + 1px
// border + radius 10) matching the Card pattern from view-others.jsx.
// fontSize 16 / DemiBold per design spec for the task title; counter sits
// inline at top-right so it doesn't push the field down.
ColumnLayout {
    id: root

    property string taskId: ""
    property var    task:   null

    Layout.fillWidth:    true
    Layout.leftMargin:   12
    Layout.rightMargin:  12
    Layout.bottomMargin: 0

    spacing: 0

    // Forward forceActiveFocus() calls (TaskEditor.qml uses this on open)
    function forceActiveFocus() { contentField.forceActiveFocus() }

    // ── Card wrapper ──────────────────────────────────────────────────────
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: cardContent.implicitHeight + 28
        color:        ThemeManager.surfaceColor
        border.color: ThemeManager.borderColor
        border.width: 1
        radius:       10

        ColumnLayout {
            id: cardContent
            anchors {
                left:    parent.left
                right:   parent.right
                top:     parent.top
                margins: 14
            }
            spacing: 4

            TextArea {
                id: contentField

                Layout.fillWidth: true

                text:            root.task ? (root.task.content || "") : ""
                placeholderText: qsTr("Название задачи")
                // Q4.01: design uses 16px / DemiBold for the title input.
                font.pixelSize:  16
                font.weight:     Font.DemiBold
                color:           ThemeManager.textColor
                wrapMode:        TextEdit.Wrap
                background:      null
                topPadding:    0
                bottomPadding: 0
                leftPadding:   0
                rightPadding:  0

                onEditingFinished: {
                    if (root.taskId !== "" && root.task && text !== root.task.content)
                        taskViewModel.updateTask(root.taskId, "content", text)
                }
            }

            // Character counter — hidden for short content, warning colours near/over limit
            Label {
                // Show only when content is long enough to warrant attention (>= 50 chars)
                visible: contentField.length >= 50

                Layout.alignment: Qt.AlignRight

                text: contentField.length + " / 500"
                font.pixelSize: FontSize.caption

                color: contentField.length > 500
                    ? ThemeManager.errorColor      // red: over hard limit
                    : contentField.length >= 400
                        ? ThemeManager.warningColor    // amber: approaching limit
                        : ThemeManager.secondaryTextColor
            }
        }
    }

    // Sync text back when the task object is replaced (e.g. navigating between tasks)
    Connections {
        target: root
        function onTaskChanged() {
            if (root.task) contentField.text = root.task.content || ""
        }
    }
}
