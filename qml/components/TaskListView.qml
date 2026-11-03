import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI
import QtCore

// TaskListView wraps a ListView of tasks with a header row that exposes
// bulk "expand all" / "collapse all" actions for subtask sections.
//
// Usage:
//   TaskListView {
//       anchors.fill: parent
//       model: taskViewModel
//   }
//
// Bulk actions go through ONE shared bus object (bulkBus below) handed to
// every TaskItem delegate as `expansionHost`; bumping its expandAllToken /
// collapseAllToken fans out through the Connections block in TaskItem.qml.
// (A Settings-backed token per instance never worked: separate Settings
// objects on the same category don't notify each other.)
Item {
    id: root

    // ── Public API ───────────────────────────────────────────────────────
    // The ListView model. Accepts a QAbstractItemModel (typically
    // taskViewModel) or a JS array of task objects.
    property alias model: taskList.model

    // Emitted when a delegate task row is clicked. Parent typically opens
    // the editor by setting taskViewModel.selectedTask.
    signal taskClicked(string taskId)

    // ── Shared bulk-trigger bus ───────────────────────────────────────────
    // Passed to every delegate as TaskItem.expansionHost.
    QtObject {
        id: bulkBus
        property int expandAllToken: 0
        property int collapseAllToken: 0
    }

    function _expandAll() {
        bulkBus.expandAllToken = bulkBus.expandAllToken + 1
        if (typeof telemetry !== "undefined" && telemetry)
            telemetry.trackEvent("subtasks_bulk_expand", {})
    }

    function _collapseAll() {
        bulkBus.collapseAllToken = bulkBus.collapseAllToken + 1
        if (typeof telemetry !== "undefined" && telemetry)
            telemetry.trackEvent("subtasks_bulk_collapse", {})
    }

    // ── Layout ───────────────────────────────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header row — bulk subtask controls.
        RowLayout {
            id: headerRow
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            Layout.leftMargin: Spacing.md
            Layout.rightMargin: Spacing.sm
            spacing: Spacing.xs

            Item { Layout.fillWidth: true }

            // Q4.01 (redesign): "📂" emoji prefix dropped — companion
            // collapseAllButton was already stripped under the same iter, this
            // brings the pair back to visual parity (both labels, no glyphs).
            ToolButton {
                id: expandAllButton
                flat: true
                text: qsTr("Развернуть всё")
                font.pixelSize: FontSize.caption
                Material.foreground: ThemeManager.secondaryTextColor
                ToolTip.text: qsTr("Развернуть подзадачи у всех задач")
                ToolTip.visible: hovered
                ToolTip.delay: 500
                onClicked: root._expandAll()
            }

            // Q4.01 (redesign): "📁" заменён на чистый текст; иконку папки
            // нужно класть в contentItem с IconSvg, но соседний "expand all"
            // тоже без иконки — оставляем парность.
            ToolButton {
                id: collapseAllButton
                flat: true
                text: qsTr("Свернуть всё")
                font.pixelSize: FontSize.caption
                Material.foreground: ThemeManager.secondaryTextColor
                ToolTip.text: qsTr("Свернуть подзадачи у всех задач")
                ToolTip.visible: hovered
                ToolTip.delay: 500
                onClicked: root._collapseAll()
            }
        }

        // Thin divider under the header so it visually detaches from the list.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: ThemeManager.borderColor
        }

        // Task list. The delegate mirrors the role wiring used by
        // ProjectView / DesktopLayout so behaviour stays identical.
        ListView {
            id: taskList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 0

            delegate: Item {
                id: delegateRoot
                width: taskList.width
                height: rowItem.implicitHeight

                required property int    index
                required property string id
                required property string content
                required property string description
                required property string projectId
                required property string sectionId
                required property string parentId
                required property int    order
                required property int    priority
                required property var    labels
                required property bool   isCompleted
                required property string completedAt
                required property string dueDate
                required property string dueString
                required property bool   isRecurring
                required property string deadline
                required property int    durationAmount
                required property string durationUnit
                required property int    dayOrder
                required property string color

                TaskItem {
                    id: rowItem
                    width: parent.width

                    index:          delegateRoot.index
                    content:        delegateRoot.content
                    description:    delegateRoot.description
                    projectId:      delegateRoot.projectId
                    sectionId:      delegateRoot.sectionId
                    parentId:       delegateRoot.parentId
                    order:          delegateRoot.order
                    priority:       delegateRoot.priority
                    labels:         delegateRoot.labels
                    isCompleted:    delegateRoot.isCompleted
                    completedAt:    delegateRoot.completedAt
                    dueDate:        delegateRoot.dueDate
                    dueString:      delegateRoot.dueString
                    isRecurring:    delegateRoot.isRecurring
                    deadline:       delegateRoot.deadline
                    durationAmount: delegateRoot.durationAmount
                    durationUnit:   delegateRoot.durationUnit
                    dayOrder:       delegateRoot.dayOrder
                    color:          delegateRoot.color
                    expansionHost:  bulkBus

                    onTaskClicked: function(taskId) {
                        root.taskClicked(taskId)
                    }
                }

                // `id` is a reserved attribute name in QML object initialisers,
                // so it must be wired through a sibling Binding (matches the
                // pattern used by DesktopLayout / ProjectView delegates).
                Binding {
                    target: rowItem
                    property: "id"
                    value: delegateRoot.id
                }
            }

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        }
    }
}
