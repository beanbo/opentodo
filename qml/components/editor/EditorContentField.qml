import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

// Title / content TextArea — fires updateTask on editing finished.
// Wrapped in a ColumnLayout so the character counter Label can sit
// directly below the field without disturbing the parent ColumnLayout.
ColumnLayout {
    id: root

    property string taskId: ""
    property var    task:   null

    Layout.fillWidth:    true
    Layout.leftMargin:   Spacing.md
    Layout.rightMargin:  Spacing.md
    Layout.bottomMargin: Spacing.xs

    spacing: 0

    // Forward forceActiveFocus() calls (TaskEditor.qml uses this on open)
    function forceActiveFocus() { contentField.forceActiveFocus() }

    TextArea {
        id: contentField

        Layout.fillWidth: true

        text:            root.task ? (root.task.content || "") : ""
        placeholderText: "Название задачи"
        font.pixelSize:  FontSize.title
        font.weight:     Font.Medium
        color:           ThemeManager.textColor
        wrapMode:        TextEdit.Wrap
        background:      null

        onEditingFinished: {
            if (root.taskId !== "" && root.task && text !== root.task.content)
                taskViewModel.updateTask(root.taskId, "content", text)
        }
    }

    // Character counter — hidden for short content, warning colours near/over limit
    Label {
        // Show only when content is long enough to warrant attention (>= 50 chars)
        visible: contentField.length >= 50

        Layout.alignment:   Qt.AlignRight
        Layout.topMargin:   -4    // tuck close to the field
        Layout.rightMargin: Spacing.sm

        text: contentField.length + " / 500"
        font.pixelSize: FontSize.caption

        color: contentField.length > 500
            ? ThemeManager.errorColor      // red: over hard limit
            : contentField.length >= 400
                ? ThemeManager.warningColor    // amber: approaching limit
                : ThemeManager.secondaryTextColor
    }

    // Sync text back when the task object is replaced (e.g. navigating between tasks)
    Connections {
        target: root
        function onTaskChanged() {
            if (root.task) contentField.text = root.task.content || ""
        }
    }
}
