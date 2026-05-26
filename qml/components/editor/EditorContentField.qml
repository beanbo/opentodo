import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

// Title / content TextArea — fires updateTask on editing finished.
// Q4.01 (iter-10): title теперь БЕЗ Card-обёртки. Дизайн-канвас показывает
// title как plain bold-text на bg родительского pane, без surface/border.
// Раньше title сидел в Card 14px-padded + 1px-border + radius 10, что
// делало его визуально heavy и непохоже на макет. Теперь просто TextArea
// с 18px font + DemiBold weight, выровненный к 16px горизонтальному
// padding'у editor pane.
//
// fontSize: design canvas использует ~18-22 для title в editor pane
// (close to `view-others.jsx` "Входящие" 20/600 заголовка); 18/DemiBold
// читается как primary, но не перекрывает meta-карточку ниже.
ColumnLayout {
    id: root

    property string taskId: ""
    property var    task:   null

    Layout.fillWidth:    true
    Layout.leftMargin:   16
    Layout.rightMargin:  16
    Layout.topMargin:    4
    Layout.bottomMargin: 0

    spacing: 2

    // Forward forceActiveFocus() calls (TaskEditor.qml uses this on open)
    function forceActiveFocus() { contentField.forceActiveFocus() }

    TextArea {
        id: contentField

        Layout.fillWidth: true

        text:            root.task ? (root.task.content || "") : ""
        placeholderText: qsTr("Название задачи")
        font.pixelSize:  18
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

    // Sync text back when the task object is replaced (e.g. navigating between tasks)
    Connections {
        target: root
        function onTaskChanged() {
            if (root.task) contentField.text = root.task.content || ""
        }
    }
}
