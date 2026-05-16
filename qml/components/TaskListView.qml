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
// The bulk-action mechanism uses a Settings-backed token shared with every
// TaskItem instance via the "TaskItemExpansion" category. Bumping
// expandedSettings.expandAllToken (or collapseAllToken) fans out to every
// alive TaskItem delegate through a Connections block defined in TaskItem.qml.
// This keeps cross-delegate signalling decoupled from any specific host —
// DesktopLayout, ProjectView, BoardView, etc. all share the same wiring.
Item {
    id: root

    // ── Public API ───────────────────────────────────────────────────────
    // The ListView model. Accepts a QAbstractItemModel (typically
    // taskViewModel) or a JS array of task objects.
    property alias model: taskList.model

    // Emitted when a delegate task row is clicked. Parent typically opens
    // the editor by setting taskViewModel.selectedTask.
    signal taskClicked(string taskId)

    // ── Shared bulk-trigger Settings ──────────────────────────────────────
    // Same category as TaskItem.qml's expandedSettings — when we increment
    // the token here, every TaskItem delegate gets the change-notification
    // through its own Settings binding.
    Settings {
        id: bulkSettings
        category: "TaskItemExpansion"
        property int expandAllToken: 0
        property int collapseAllToken: 0
    }

    function _expandAll() {
        bulkSettings.expandAllToken = bulkSettings.expandAllToken + 1
        if (typeof telemetry !== "undefined" && telemetry)
            telemetry.trackEvent("subtasks_bulk_expand", {})
    }

    function _collapseAll() {
        bulkSettings.collapseAllToken = bulkSettings.collapseAllToken + 1
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
            spacing: 4

            Item { Layout.fillWidth: true }

            ToolButton {
                id: expandAllButton
                flat: true
                text: qsTr("📂 Развернуть всё")
                font.pixelSize: FontSize.caption
                Material.foreground: ThemeManager.secondaryTextColor
                ToolTip.text: qsTr("Развернуть подзадачи у всех задач")
                ToolTip.visible: hovered
                ToolTip.delay: 500
                onClicked: root._expandAll()
            }

            ToolButton {
                id: collapseAllButton
                flat: true
                text: qsTr("📁 Свернуть всё")
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
            color: ThemeManager.isDark ? "#363636" : "#f0f0f0"
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
