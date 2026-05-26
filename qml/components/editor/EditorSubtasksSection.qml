import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

// Subtasks section: list + inline add field + AI generator button.
ColumnLayout {
    id: root

    property string taskId:      ""
    property string taskContent: ""   // passed in from TaskEditor for the dialog header

    Layout.fillWidth:   true
    Layout.leftMargin:  Spacing.md
    Layout.rightMargin: Spacing.md
    spacing: Spacing.xs

    // B-7.8: correct Russian plural, handles the 11-14 edge case
    function _pluralize(n) {
        var mod10  = n % 10
        var mod100 = n % 100
        if (mod10 === 1 && mod100 !== 11)                              return qsTr("подзадача")
        if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14)) return qsTr("подзадачи")
        return qsTr("подзадач")
    }

    // ── Section header row ─────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Spacing.sm

        Text {
            text:           qsTr("Подзадачи")
            font.pixelSize: FontSize.bodySmall
            font.weight:    Font.Medium
            color:          ThemeManager.textColor
        }

        Item { Layout.fillWidth: true }

        // AI generate button — hidden when AI not configured or task has no content
        Rectangle {
            id: aiButton
            visible:  typeof aiAssistant !== "undefined"
                      && aiAssistant.isConfigured
                      && root.taskContent.trim().length > 0
            height:   24
            width:    aiButtonRow.implicitWidth + Spacing.md
            radius:   Radius.sm
            color:    aiButtonMouseArea.containsMouse
                      ? Qt.rgba(ThemeManager.accentColor.r,
                                ThemeManager.accentColor.g,
                                ThemeManager.accentColor.b, 0.12)
                      : Qt.rgba(ThemeManager.accentColor.r,
                                ThemeManager.accentColor.g,
                                ThemeManager.accentColor.b, 0.06)
            border.color: ThemeManager.accentColor
            border.width: 1

            Row {
                id:           aiButtonRow
                anchors.centerIn: parent
                spacing:      4

                // Q4.01 (redesign): "🤖" → "sparkles" stroke-иконка для
                // консистентности AI-точек входа.
                IconSvg {
                    name:  "sparkles"
                    size:  11
                    color: ThemeManager.accentColor
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text:           qsTr("Разбить на подзадачи")
                    font.pixelSize: FontSize.caption
                    color:          ThemeManager.accentColor
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            MouseArea {
                id:          aiButtonMouseArea
                anchors.fill: parent
                cursorShape:  Qt.PointingHandCursor
                hoverEnabled: true
                onClicked: {
                    if (root.taskContent.trim().length === 0) {
                        // Should not happen (button is hidden) but defensive check
                        return
                    }
                    subtaskDialog.taskId      = root.taskId
                    subtaskDialog.taskContent = root.taskContent
                    subtaskDialog.open()
                }
            }
        }
    }

    // Refresh-token bumped from a Connections{taskViewModel} below.
    property int _editorSubtaskRefreshToken: 0
    Connections {
        target: taskViewModel
        function onDataChanged() {
            root._editorSubtaskRefreshToken = root._editorSubtaskRefreshToken + 1
        }
    }

    // ── Existing subtask list ──────────────────────────────────────────────────
    Repeater {
        id: subtaskRepeater
        // taskViewModel.subtasksOf() returns a proper JS array of QVariantMap
        // entries (length === 2 when there are 2 subtasks; isArray === false
        // but JS dot-notation iteration works).
        //
        // We earlier tried dataStore.subtasks() which returns QList<Task*>,
        // expecting it to convert through Qt's QObject* QML bridge. But
        // Qt 6.11 does NOT auto-convert QList<Task*> from a Q_INVOKABLE to
        // a usable JS array — `.length` is undefined and Repeater iterates 0
        // delegates. The original subtasksOf path IS the right one; the
        // delegate must read modelData via QVariantMap-key access pattern.
        model: (typeof taskViewModel !== "undefined"
                && taskViewModel
                && root.taskId !== ""
                && root._editorSubtaskRefreshToken >= 0)
               ? taskViewModel.subtasksOf(root.taskId)
               : []

        delegate: Item {
            // ── Delegate root — drag source ────────────────────────────────────
            required property var modelData

            // subtaskId is resolved once at instantiation from modelData.id
            // (QVariantMap key) so it remains stable even while dragging.
            property string subtaskId: modelData.id || ""

            // Previously: `width: subtaskRow.width` while subtaskRow had
            // `width: parent.width` — a circular binding that pinned both to
            // 0, so the Text inside (Layout.fillWidth + ElideRight) collapsed
            // to an empty render. The fixed-width children (drag handle,
            // completion circle) still showed because they don't depend on
            // the row width, which is why the bug looked like "subtasks
            // have circles but no text." Take the width from the ColumnLayout
            // parent instead.
            Layout.fillWidth: true
            height: 32

            // Drag configuration — we use Drag.Automatic so Qt handles the
            // grab and cursor; the actual drag starts when dragHandleArea's
            // drag.active fires via preventStealing.
            Drag.active:           dragHandleArea.drag.active
            Drag.dragType:         Drag.Automatic
            Drag.supportedActions: Qt.MoveAction
            Drag.mimeData:         ({ "application/x-opentodo-subtask": subtaskId })
            Drag.keys:             ["application/x-opentodo-subtask"]

            // Hover detection for the whole row (shows drag handle on hover).
            HoverHandler { id: rowHover }

            // ── Row content ────────────────────────────────────────────────────
            RowLayout {
                id:     subtaskRow
                anchors.fill: parent
                spacing: 8

                // Drag handle — shown on hover, sits to the left of the
                // checkbox.  width=12 keeps it compact; cursor changes to
                // SizeVerCursor to signal vertical reorder.
                Item {
                    id:      dragHandle
                    width:   12
                    height:  20
                    visible: rowHover.hovered || dragHandleArea.drag.active

                    Text {
                        anchors.centerIn: parent
                        text:            "⋮⋮"
                        color:           ThemeManager.secondaryTextColor
                        font.pixelSize:  FontSize.small
                    }

                    MouseArea {
                        id:              dragHandleArea
                        anchors.fill:    parent
                        cursorShape:     Qt.SizeVerCursor
                        // drag.target=null lets Qt use Drag.Automatic on the
                        // parent Item instead of physically moving it.
                        drag.target:     null
                        drag.axis:       Drag.YAxis
                        // preventStealing so the ColumnLayout's scroll handler
                        // doesn't consume the press before we can start a drag.
                        preventStealing: true
                    }
                }

                // Completion circle
                Rectangle {
                    width:  14
                    height: 14
                    radius: 7
                    color:  modelData.isCompleted ? ThemeManager.secondaryTextColor : "transparent"
                    border.color: ThemeManager.secondaryTextColor
                    border.width: 1.5

                    MouseArea {
                        anchors.fill: parent
                        cursorShape:  Qt.PointingHandCursor
                        onClicked: {
                            if (modelData.isCompleted)
                                taskViewModel.uncompleteTask(modelData.id)
                            else
                                taskViewModel.completeTask(modelData.id)
                            // Refresh list after completion toggle
                            subtaskRepeater.model = taskViewModel.subtasksOf(root.taskId)
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text:            modelData ? modelData.content : ""
                    font.pixelSize:  FontSize.bodySmall
                    color:           modelData && modelData.isCompleted
                                     ? ThemeManager.secondaryTextColor
                                     : ThemeManager.textColor
                    font.strikeout:  modelData && modelData.isCompleted
                    elide:           Text.ElideRight
                }
            }

            // ── Drop zone — split top/bottom halves ────────────────────────────
            // Covers the full item area so a dragged row can be dropped anywhere
            // on another row.  The insertion line sits at the top or bottom edge
            // depending on which half the cursor is in.
            //
            // Order update strategy: moveTaskOrder() operates on m_tasks, which
            // is the main filtered task list.  Subtasks are explicitly excluded
            // from that list (fetchTasks() filters out tasks with parentId set),
            // so moveTaskOrder() would return false for any subtask id.
            // Instead we use updateTask("order", N) which writes Task::order
            // directly — the same field that subtasksOf() sorts by — and emits
            // taskChanged so autosave fires.  After the drop we re-assign
            // order values in multiples of 10 to the full sibling list so that
            // future inserts don't collide (same spacing strategy used by
            // moveTaskOrder for the main list).
            DropArea {
                id:           dropArea
                anchors.fill: parent
                keys:         ["application/x-opentodo-subtask"]

                // true  → insert ABOVE this row
                // false → insert BELOW this row
                property bool _isAbove: false

                onPositionChanged: function(drag) {
                    _isAbove = drag.y < height / 2
                }

                onDropped: function(drop) {
                    const draggedId = drop.getDataAsString("application/x-opentodo-subtask")
                    if (!draggedId || draggedId === subtaskId) {
                        drop.accept(Qt.MoveAction)
                        return
                    }

                    // Fetch current sorted sibling list so we can compute the
                    // target position and re-assign order values in one pass.
                    const subs = taskViewModel.subtasksOf(root.taskId)

                    // Find dragged item's current position and target row's position.
                    let draggedPos = -1
                    let targetPos  = -1
                    for (let i = 0; i < subs.length; i++) {
                        if (subs[i].id === draggedId) draggedPos = i
                        if (subs[i].id === subtaskId)  targetPos  = i
                    }

                    if (draggedPos < 0 || targetPos < 0) {
                        drop.accept(Qt.MoveAction)
                        return
                    }

                    // Build the reordered id array by removing the dragged item
                    // and inserting it before or after the target row.
                    let ids = []
                    for (let i = 0; i < subs.length; i++)
                        ids.push(subs[i].id)

                    ids.splice(draggedPos, 1)

                    // After removal the targetPos index may have shifted if
                    // draggedPos was before it.
                    let insertIdx = targetPos
                    if (draggedPos < targetPos) insertIdx--
                    if (!_isAbove) insertIdx++

                    // Clamp to valid range
                    insertIdx = Math.max(0, Math.min(insertIdx, ids.length))
                    ids.splice(insertIdx, 0, draggedId)

                    // Assign spaced order values (10, 20, 30…) to all siblings
                    // so future inserts between any two slots don't collide.
                    for (let i = 0; i < ids.length; i++) {
                        taskViewModel.updateTask(ids[i], "order", (i + 1) * 10)
                    }

                    drop.accept(Qt.MoveAction)

                    // Refresh the Repeater to reflect the new order.
                    subtaskRepeater.model = taskViewModel.subtasksOf(root.taskId)
                }

                // Insertion-line indicator: a 2-px accent-coloured bar shown at
                // the top or bottom edge of the row while a compatible item
                // hovers over this drop zone.
                Rectangle {
                    visible:      dropArea.containsDrag
                    height:       2
                    color:        ThemeManager.accentColor
                    anchors.left:  parent.left
                    anchors.right: parent.right
                    // Sits at y=0 (above) or y=height-2 (below) depending on
                    // which half of the row the cursor is currently in.
                    y: dropArea._isAbove ? 0 : parent.height - 2
                }
            }
        }
    }

    // ── Inline add subtask row ─────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        height: 32
        spacing: 8

        Rectangle {
            width:  14
            height: 14
            radius: 7
            color:  "transparent"
            border.color: ThemeManager.borderColor
            border.width: 1.5
        }

        TextField {
            id: subtaskInput
            Layout.fillWidth: true
            placeholderText:  qsTr("Добавить подзадачу…")
            font.pixelSize:   FontSize.bodySmall
            color:            ThemeManager.textColor
            background: null

            onAccepted: {
                const trimmed = text.trim()
                if (trimmed !== "" && root.taskId !== "") {
                    taskViewModel.addSubtask(root.taskId, trimmed)
                    text = ""
                    // Refresh the repeater model
                    subtaskRepeater.model = taskViewModel.subtasksOf(root.taskId)
                }
            }
        }
    }

    // ── SubtaskGeneratorDialog (modal, parent = overlay) ───────────────────────
    SubtaskGeneratorDialog {
        id: subtaskDialog
        // Anchor to the nearest Overlay (full application window)
        parent: Overlay.overlay

        onSubtasksAdded: function(count) {
            // Refresh the inline subtask list
            subtaskRepeater.model = taskViewModel.subtasksOf(root.taskId)
            if (count > 0) {
                // B-7.8: proper Russian plural for numbers like 11-14
                const noun = _pluralize(count)
                const msg  = qsTr("Добавлено %1 %2").arg(count).arg(noun)
                let w = root.Window.window
                if (w && typeof w.showToast === "function")
                    w.showToast(msg, "success")
            }
        }
    }
}
