import QtQuick
import QtQuick.Layouts
import OpenTodoUI
import "editor"

Rectangle {
    id: root

    // ── Public API ────────────────────────────────────────────────────────
    // taskId is derived from `task` so callers only need to set the task
    // object. Inner editor components (subtasks, comments, save callbacks)
    // all key off taskId — passing the task without the id would silently
    // disable every "real" editing action.
    property string taskId: task ? (task.id || "") : ""
    property var    task:   null
    signal closeRequested()

    // ── Geometry ──────────────────────────────────────────────────────────
    width:  400
    height: parent ? parent.height : 600

    color:  ThemeManager.surfaceColor
    radius: 0

    // ── Left border separator ─────────────────────────────────────────────
    Rectangle {
        anchors.left:   parent.left
        anchors.top:    parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: ThemeManager.borderColor
    }

    // ── Slide-in animation from right ─────────────────────────────────────
    property real _targetX: 0

    x: _targetX + width

    Component.onCompleted: {
        slideIn.start()
        // ITER1 #15: focus the title field when the editor opens for a new task
        // (taskId === "" means no existing task is loaded yet).
        if (root.taskId === "")
            Qt.callLater(function() { editorContentField.forceActiveFocus() })
    }

    NumberAnimation {
        id: slideIn
        target:   root
        property: "x"
        to:       root._targetX
        duration: Duration.slow
        easing.type: Easing.OutCubic
    }

    function slideOut(callback) {
        // B-7.79: use a one-shot wrapper so repeated calls to slideOut() don't
        // accumulate listeners on the stopped signal.
        var _handler = function() {
            slideOutAnim.stopped.disconnect(_handler)
            callback()
        }
        slideOutAnim.stopped.connect(_handler)
        slideOutAnim.start()
    }

    NumberAnimation {
        id: slideOutAnim
        target:   root
        property: "x"
        to:       root._targetX + root.width
        duration: Duration.slow
        easing.type: Easing.InCubic
    }

    // ── Load task data whenever taskId changes ────────────────────────────
    onTaskIdChanged: {
        _loadTask()
        // ITER1 #15: also focus the content field when switching to a new-task
        // slot (taskId becomes ""). For existing tasks the field is pre-filled
        // so we don't steal focus unexpectedly.
        if (root.taskId === "")
            Qt.callLater(function() { editorContentField.forceActiveFocus() })
    }

    function _loadTask() {
        if (!taskId || taskId === "") return
        // ITER1 #5: taskViewModel.getTask() doesn't exist; use DataStore directly.
        if (typeof dataStore !== "undefined" && dataStore) {
            const t = dataStore.taskById(taskId)
            if (t) root.task = t
        }
    }

    // ── Scroll area ───────────────────────────────────────────────────────
    Flickable {
        anchors {
            top:    parent.top
            left:   parent.left
            right:  parent.right
            bottom: actionBar.top
        }
        contentWidth:  width
        contentHeight: editorColumn.implicitHeight + 24
        clip:          true

        ColumnLayout {
            id: editorColumn
            width: parent.width
            spacing: 0

            EditorHeader {
                id: editorHeader
                Layout.fillWidth: true
                onCloseClicked:   root.closeRequested()
                onReadingModeClicked: readingModeOverlay.open(
                    root.task ? (root.task.content     || "") : "",
                    root.task ? (root.task.description || "") : "")
            }

            EditorContentField {
                id: editorContentField
                Layout.fillWidth: true
                taskId: root.taskId
                task:   root.task
            }

            EditorDescriptionField {
                Layout.fillWidth: true
                taskId: root.taskId
                task:   root.task
            }

            // Divider
            Rectangle {
                Layout.fillWidth:    true
                Layout.leftMargin:   Spacing.md
                Layout.rightMargin:  Spacing.md
                Layout.bottomMargin: Spacing.sm
                height: 1
                color:  ThemeManager.borderColor
            }

            EditorMetaRow {
                Layout.fillWidth: true
                taskId: root.taskId
                task:   root.task
            }

            // Divider
            Rectangle {
                Layout.fillWidth:    true
                Layout.leftMargin:   Spacing.md
                Layout.rightMargin:  Spacing.md
                Layout.topMargin:    12
                Layout.bottomMargin: Spacing.xs
                height: 1
                color:  ThemeManager.borderColor
            }

            EditorSubtasksSection {
                Layout.fillWidth: true
                taskId:      root.taskId
                taskContent: root.task ? (root.task.content || "") : ""
            }

            // Divider
            Rectangle {
                Layout.fillWidth:    true
                Layout.leftMargin:   Spacing.md
                Layout.rightMargin:  Spacing.md
                Layout.topMargin:    12
                Layout.bottomMargin: Spacing.xs
                height: 1
                color:  ThemeManager.borderColor
            }

            EditorRemindersSection {
                Layout.fillWidth: true
                taskId: root.taskId
            }

            // Divider
            Rectangle {
                Layout.fillWidth:    true
                Layout.leftMargin:   Spacing.md
                Layout.rightMargin:  Spacing.md
                Layout.topMargin:    12
                Layout.bottomMargin: Spacing.xs
                height: 1
                color:  ThemeManager.borderColor
            }

            EditorCommentsSection {
                Layout.fillWidth: true
                taskId: root.taskId
            }
        }
    }

    // ── Bottom action bar ─────────────────────────────────────────────────
    EditorActionBar {
        id: actionBar
        anchors.bottom: parent.bottom
        anchors.left:   parent.left
        anchors.right:  parent.right
        taskId: root.taskId
        task:   root.task
        onDeleteRequested: root.closeRequested()
    }

    // ── Reading mode overlay ──────────────────────────────────────────────
    // Read-only full-screen view of the task description rendered as Markdown.
    // Triggered from the header button or Ctrl+Shift+M while the editor lives.
    ReadingModeOverlay {
        id: readingModeOverlay
    }

    // Ctrl+Shift+M opens the reading-mode overlay. The Shortcut only exists
    // while the editor is instantiated (parent is destroyed when the panel
    // closes), and we additionally guard on visibility so it can't fire when
    // the editor is rendered but its window isn't focused.
    Shortcut {
        sequence: "Ctrl+Shift+M"
        context:  Qt.WindowShortcut
        enabled:  root.visible
        onActivated: readingModeOverlay.open(
            root.task ? (root.task.content     || "") : "",
            root.task ? (root.task.description || "") : "")
    }
}
