import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI
import QtCore

// TaskItem renders one task row.
// TODO DesktopLayout/ProjectView: add `required property bool isPinned`
//      and `isPinned: delegateRoot.isPinned` binding.
//      Add a right-click context-menu entry "Закрепить наверху"
//      → taskViewModel.updateTask(id, "isPinned", !isPinned).
// When isSubtask == false (default) it also shows an expand toggle and, when
// expanded, an inline subtask list + "+ Add subtask" quick-entry row.
//
// TODO C++: sort by dayOrder when in TodayView (TaskViewModel::sortRows or similar).
Item {
    id: root

    // ── Model role properties (set by parent delegate, NOT required) ──────
    // Originally `required` for safety, but TaskItem is loaded through a
    // Loader (so the parent ListView delegate can wrap it for layout). With
    // Loader the component is created BEFORE the wrapper has a chance to
    // assign properties — required-property checks then fail at construction
    // time and the entire delegate is dropped, leaving the list empty.
    // Defaults below are inert; the wrapping delegate's Binding{} elements
    // overwrite them as soon as the Loader signals onLoaded.
    property int    index:        -1
    property string id:           ""
    property string content:      ""
    property string description:  ""
    property string projectId:    ""
    property string sectionId:    ""
    property string parentId:     ""
    property int    order:        0
    property int    priority:     4
    property var    labels:       []
    property bool   isCompleted:  false
    property string completedAt:  ""
    property string dueDate:      ""
    property string dueString:    ""
    property bool   isRecurring:  false
    property string deadline:       ""
    property int    durationAmount: 0
    property string durationUnit:   ""
    property int    dayOrder:       0
    // TODO DesktopLayout.qml + ProjectView.qml: add `required property string color`
    //      and `color: delegateRoot.color` binding to TaskItem instances.
    property string color:          ""
    // TODO DesktopLayout.qml + ProjectView.qml: add `required property bool isPinned`
    //      and `isPinned: delegateRoot.isPinned` binding to TaskItem instances.
    property bool   isPinned:       false
    // TODO DesktopLayout/ProjectView: add `required property int estimateMinutes`
    //      and `estimateMinutes: delegateRoot.estimateMinutes` binding to TaskItem instances.
    property int    estimateMinutes: 0

    // ── Public API ────────────────────────────────────────────────────────
    signal taskClicked(string taskId)

    property bool isDarkTheme: ThemeManager.isDark

    // ── Inline rename state ───────────────────────────────────────────────
    // When true the content row swaps the read-only Label for an editable
    // TextField. Toggled by double-clicking the title (TapHandler below) or
    // by F2 while the row is selected. Reset on Enter / Esc / focus-loss.
    property bool _inlineEditing: false

    // Commits the inline-edited title via the standard updateTask(..."content")
    // path. Empty text is treated as cancel — we never let the user erase
    // a task title to "" via this path.
    function _commitInlineRename(newText) {
        if (!root._inlineEditing) return
        root._inlineEditing = false
        if (!root.id) return
        const trimmed = (newText || "").trim()
        if (trimmed === "" || trimmed === root.content) return
        taskViewModel.updateTask(root.id, "content", trimmed)
        if (typeof telemetry !== "undefined" && telemetry)
            telemetry.trackEvent("task_inline_renamed", {})
    }

    function _cancelInlineRename() {
        root._inlineEditing = false
    }

    function _beginInlineRename() {
        if (root.isCompleted) return            // completed tasks: read-only
        if (!root.id) return
        root._inlineEditing = true
        Qt.callLater(function() { inlineEditField.forceActiveFocus() })
        Qt.callLater(function() { inlineEditField.selectAll() })
    }

    // ── F2 shortcut: rename when this row is the (single) selected task ──
    Shortcut {
        sequence: "F2"
        enabled: root.id !== "" && taskViewModel.isSelected(root.id)
                 && !root._inlineEditing
        onActivated: root._beginInlineRename()
    }

    // ── Subtask expansion persistence ─────────────────────────────────────
    // A single Settings object is shared across all TaskItem instances
    // (same category, same backing QSettings group). expandedIds holds a
    // comma-separated list of task UUIDs whose subtask section is open.
    //
    // TODO: prune orphan IDs on app start — deleted tasks accumulate in
    //       expandedIds indefinitely. For now the QSettings string stays
    //       manageable (a few hundred UUIDs ≈ ~10 KB). When pruning is
    //       added, iterate dataStore.allTasks() IDs and remove entries
    //       not present in that set.
    Settings {
        id: expandedSettings
        category: "TaskItemExpansion"
        property string expandedIds: ""   // comma-separated task UUIDs
        // Bulk expand/collapse triggers — bumped by header buttons in
        // TaskListView.qml (or any other host) to fan out an expand-all /
        // collapse-all command to every TaskItem delegate currently alive.
        // Using a Settings-backed token avoids a context-property dependency
        // and keeps the mechanism shared across the whole UI tree.
        property int expandAllToken: 0
        property int collapseAllToken: 0
    }

    // ── User-configurable «Отложить» durations ───────────────────────────────
    // Five slots persisted via Qt.labs.settings (category "deferDurations") and
    // edited from SettingsSectionDefer. Defaults match the historical fixed
    // values used by the in-app banner snooze submenu.
    Settings {
        id: deferSettings
        category: "deferDurations"
        property int slot1: 5
        property int slot2: 15
        property int slot3: 30
        property int slot4: 60
        property int slot5: 240
    }

    // Reschedule a notification N minutes from now via NotificationService.
    // Mirrors the snoozeBanner() helper in Main.qml so the context-menu defer
    // and the banner Snooze share semantics: a one-shot reminder fires after
    // the chosen interval, identified by "snooze-<taskId>" so a second defer
    // replaces the previously pending schedule for the same task.
    function _deferTaskMinutes(minutes) {
        if (!root.id) return
        if (typeof notificationService === "undefined" || !notificationService) return
        const t = dataStore.taskById(root.id)
        const body = t ? t.content : root.content
        const due = new Date(Date.now() + minutes * 60 * 1000)
        notificationService.scheduleNotification(
            qsTr("Отложенное напоминание"), body, due, "snooze-" + root.id)
    }

    // Returns true when taskId is in the persisted expanded set.
    function _isExpandedPersisted(taskId) {
        if (!taskId || expandedSettings.expandedIds === "") return false
        return ("," + expandedSettings.expandedIds + ",").indexOf("," + taskId + ",") >= 0
    }

    // Adds or removes taskId from the persisted expanded set.
    function _setExpandedPersisted(taskId, exp) {
        if (!taskId) return
        const ids = expandedSettings.expandedIds
            ? expandedSettings.expandedIds.split(",").filter(function(s) { return s !== "" })
            : []
        const idx = ids.indexOf(taskId)
        if (exp && idx < 0)  ids.push(taskId)
        else if (!exp && idx >= 0) ids.splice(idx, 1)
        expandedSettings.expandedIds = ids.join(",")
    }

    // Subtask expansion state — initialised from QSettings so the value
    // survives delegate recycling, view navigation, and app restarts.
    property bool expanded: _isExpandedPersisted(root.id)

    // Persist whenever the expanded flag changes (and we have a real id).
    onExpandedChanged: {
        if (root.id !== "")
            _setExpandedPersisted(root.id, root.expanded)
    }

    // ITER3 #5 / persistence: when this delegate is recycled for a different
    // task by ListView, read the new task's persisted state instead of
    // unconditionally resetting to false.
    onIdChanged: { root.expanded = _isExpandedPersisted(root.id) }
    property bool isSubtask: false

    // ── Drag-and-drop support ─────────────────────────────────────────────
    // dragActive is true while the user drags via the ⋮⋮ handle, holds the
    // left mouse button on a bare edge of the row, OR presses-and-drags over
    // ANY inner element (priority circle, content area, chip row, …).
    //
    // Bug 10 fix: dragging was originally restricted to the right-edge handle.
    // Adding `drag.target` to the row-wide MouseArea made bare edges draggable
    // too — but the row MouseArea sits BELOW the RowLayout (line ~525) so
    // child items (content text, labels, chevron, …) eat events and the drag
    // never engaged from those points.
    //
    // Fix v2: a `DragHandler` at TaskItem-root level. Pointer handlers don't
    // compete with MouseArea for click handling (TapHandler-style semantics) —
    // they only engage AFTER the drag-threshold is exceeded, so clicks on
    // inner items keep working while a press-and-drag from anywhere on the
    // row activates the drag. `target: null` keeps the visible delegate in
    // place; the Drag.Automatic system supplies the ghost image.
    property bool dragActive:
          dragHandleArea.drag.active
       || mouseArea.drag.active
       || rowDragHandler.active

    // Bind Drag.active via the dragActive property rather than re-evaluating
    // `... || ...` here — Qt 6 was flagging the latter as a binding loop
    // because Drag.Automatic mutates MouseArea.drag.active internally during
    // start/end and triggered re-evaluation of this expression. Routing
    // through dragActive breaks the cycle.
    Drag.active: root.dragActive
    Drag.hotSpot.x: width / 2
    Drag.hotSpot.y: 22
    Drag.dragType: Drag.Automatic

    // Row-wide drag from any point. Engages after dragThreshold pixels of
    // movement — clicks (no movement) still fall through to the row
    // MouseArea, the priority circle MouseArea, etc. Right-button drag is
    // disabled so the context menu remains intact.
    DragHandler {
        id: rowDragHandler
        target: null
        dragThreshold: 6
        acceptedButtons: Qt.LeftButton
        cursorShape: active ? Qt.ClosedHandCursor : Qt.ArrowCursor
    }
    Drag.supportedActions: Qt.MoveAction
    Drag.mimeData: ({ "application/x-opentodo-task": root.id })
    Drag.keys: ["application/x-opentodo-task"]

    // ── Geometry ──────────────────────────────────────────────────────────
    width:  parent ? parent.width : 0
    // Height grows to fit the main row plus the (optional) subtask section
    implicitHeight: mainRow.height + subtaskSection.implicitHeight

    // ── Internal helpers ──────────────────────────────────────────────────
    readonly property color _priorityColor: {
        switch (priority) {
            case 1:  return "#db4c3f"
            case 2:  return "#f49c18"
            case 3:  return "#4073ff"
            default: return "#808080"
        }
    }

    // BUG-FIX (C-6): bind to ThemeManager.todayStr instead of computing the
    // current date here. ThemeManager runs a single shared 1-minute timer
    // that updates todayStr at local-midnight rollover, so this binding
    // re-evaluates automatically — the previous per-delegate snapshot stayed
    // stale after midnight and yesterday's tasks kept looking "due today"
    // until the user restarted the app or scrolled the list out and back in.
    readonly property bool _isOverdue:
        dueDate !== "" && dueDate < ThemeManager.todayStr

    // ── Deadline helpers ──────────────────────────────────────────────────
    readonly property string _today: ThemeManager.todayStr

    readonly property bool _hasDeadline: deadline !== "" && deadline !== undefined

    readonly property color _deadlineColor: {
        if (!_hasDeadline) return root._subColor
        if (deadline < _today)  return "#db4c3f"          // overdue — red
        if (deadline === _today) return ThemeManager.accentColor  // today — accent
        return root._subColor                              // future — muted
    }

    // ── Estimate label helper ─────────────────────────────────────────────
    // Returns a localised string like "30 мин", "2 ч" or "" when no estimate.
    // Q4.01 (redesign): icon prefix moved out of the string — the chip wraps
    // an IconSvg + Text so the glyph carries the stroke-pack visual weight.
    readonly property string _estimateLabel: {
        if (estimateMinutes <= 0) return ""
        if (estimateMinutes < 60)       return qsTr("%1 мин").arg(estimateMinutes)
        if (estimateMinutes % 60 === 0) return qsTr("%1 ч").arg(estimateMinutes / 60)
        return qsTr("%1 ч %2 мин").arg(Math.floor(estimateMinutes / 60)).arg(estimateMinutes % 60)
    }

    // ── Duration label helper ─────────────────────────────────────────────
    // Returns a localised string like "25 мин", "2 ч", "3 дн" or "".
    readonly property string _durationLabel: {
        if (durationAmount <= 0 || durationUnit === "") return ""
        switch (durationUnit) {
            case "minute": return qsTr("%1 мин").arg(durationAmount)
            case "hour":   return qsTr("%1 ч").arg(durationAmount)
            case "day":    return qsTr("%1 дн").arg(durationAmount)
            default:       return durationAmount + " " + durationUnit
        }
    }

    // Q4.01 pixel-perfect: дизайн (.design-temp/.../view-project.jsx TaskRow
    // стр. 55) рисует строку с `background: 'transparent'` — задача сидит
    // на bgColor родительского контейнера. Раньше у нас был surfaceColor
    // (#282828), что давало карточкам task'ов "светлее, чем фон" вид,
    // которого в макете нет.
    readonly property color _bgColor:     ThemeManager.backgroundColor
    readonly property color _hoverColor:  ThemeManager.hoverColor
    readonly property color _textColor:   ThemeManager.textColor
    readonly property color _subColor:    ThemeManager.secondaryTextColor

    // ── Density helpers ───────────────────────────────────────────────────
    readonly property bool _compact: typeof settingsViewModel !== "undefined"
                                     && settingsViewModel.compactDensity
    readonly property int _rowMinHeight: _compact ? 32 : 36
    readonly property int _rowVPadding:  _compact ? 2  : Spacing.sm

    // ── Hover-preview helpers ─────────────────────────────────────────────
    // Show the popup only when there is extra information beyond the task title.
    readonly property bool _hasPreviewContent: {
        return (root.description !== "" && root.description !== undefined)
            || (root.dueString   !== "" && root.dueString   !== undefined)
            || (root.dueDate     !== "" && root.dueDate     !== undefined)
            || (root.labels && root.labels.length > 0)
            || root._hasDeadline
            || root.priority < 4
    }

    // Assembles the multi-line preview text shown in the popup.
    readonly property string _previewContent: {
        let s = root.content || ""
        if (root.description) s += "\n\n" + root.description

        const meta = []
        if (root.dueString)      meta.push("📅 " + root.dueString)
        else if (root.dueDate)   meta.push("📅 " + root.dueDate)
        if (root.labels && root.labels.length > 0)
            meta.push("@" + root.labels.join("  @"))
        if (root._hasDeadline)   meta.push("⏰ " + root.deadline)
        if (root.priority < 4)   meta.push("⚑ P" + root.priority)

        if (meta.length > 0) s += "\n\n" + meta.join("   ")
        return s
    }

    // ── URL auto-detection helpers ────────────────────────────────────────
    // Returns true when `s` contains at least one http(s) URL.
    function _hasUrl(s) {
        return /https?:\/\/[^\s<>"]+/i.test(s)
    }

    // Returns an HTML string with every URL in `s` wrapped in an <a> tag.
    // HTML-special characters in the original text are escaped first so that
    // user-typed `<`, `>`, `&`, `"` do not break the resulting markup.
    function _autolinkContent(s) {
        if (!s) return ""
        const escaped = s.replace(/&/g, "&amp;")
                         .replace(/</g, "&lt;")
                         .replace(/>/g, "&gt;")
                         .replace(/"/g, "&quot;")
        return escaped.replace(/(https?:\/\/[^\s<]+)/gi,
            '<a href="$1" style="color:' + ThemeManager.accentColor + '">$1</a>')
    }

    // ── Wiki-style task cross-reference helpers ───────────────────────────
    // Resolves [[Task Title]] references in a description string.
    // For each match, looks up a task whose content matches (case-insensitive,
    // trimmed) — first non-deleted match wins. If found, the bracket syntax is
    // replaced with a markdown link [Title](opentodo://task/<id>).
    // If no match exists, [[Title]] is left as plain text so the user sees the
    // unresolved reference rather than a silent no-op.
    // Performance: the early-return on missing "[["  is O(1). The O(N) title
    // scan only runs when "[[" is present. For typical task counts (<1000) this
    // is negligible. TODO: cache title→id map if task counts grow large.
    function _resolveTaskRefs(text) {
        if (!text || text.indexOf("[[") < 0) return text
        return text.replace(/\[\[([^\]\n]+)\]\]/g, function(_, title) {
            const trimmed = title.trim()
            const id = _findTaskIdByTitle(trimmed)
            if (!id) return "[[" + title + "]]"  // unresolved — leave as is
            return "[" + trimmed + "](opentodo://task/" + id + ")"
        })
    }

    function _findTaskIdByTitle(title) {
        const all = dataStore.allTasks()
        const target = title.toLowerCase()
        for (let i = 0; i < all.length; i++) {
            const t = all[i]
            if (!t || t.isDeleted) continue
            if (t.content && t.content.toLowerCase().trim() === target)
                return t.id
        }
        return ""
    }

    // ── Subtask progress badge helpers ────────────────────────────────────
    // Cached counts: recomputed on expand/collapse and on relevant VM signals.
    // Only populated for top-level tasks (isSubtask === false) so subtask rows
    // never show a nested badge.
    property int _subtaskTotal: 0
    property int _subtaskDone:  0

    function _refreshSubtaskCounts() {
        // Guard against post-destruction race: Qt.callLater may fire AFTER the
        // delegate's QML context is torn down (project switch, sync reload),
        // at which point context properties like `taskViewModel` are gone.
        // Without this typeof check we got
        //   "TaskItem.qml:358: ReferenceError: taskViewModel is not defined
        //    (exception occurred during delayed function evaluation)"
        // every time the user navigated between projects, and the chevron
        // for tasks-with-subtasks would never appear.
        if (typeof taskViewModel === "undefined" || !taskViewModel) {
            _subtaskTotal = 0
            _subtaskDone  = 0
            return
        }
        if (!root.id || root.isSubtask) {
            _subtaskTotal = 0
            _subtaskDone  = 0
            return
        }
        const subs = taskViewModel.subtasksOf(root.id)
        if (!subs || subs.length === 0) {
            _subtaskTotal = 0
            _subtaskDone  = 0
            return
        }
        _subtaskTotal = subs.length
        let done = 0
        for (let i = 0; i < subs.length; i++) {
            const s = subs[i]
            if (s && s.isCompleted) done++
        }
        _subtaskDone = done
    }

    Component.onCompleted: _refreshSubtaskCounts()
    // onIdChanged already resets expanded above; refresh counts too
    // (the existing onIdChanged handler uses a block, so we use a separate
    //  Connections to avoid duplicating the expanded-reset logic)
    Connections {
        target: root
        function onIdChanged() { Qt.callLater(_refreshSubtaskCounts) }
    }

    Connections {
        target: taskViewModel
        // Q3.25: filter signals by task id so the badge only refreshes when the
        // changed task is THIS row, one of its current subtasks, or any task in
        // a row that already shows a subtask badge (handles re-parenting AWAY
        // from this row).  Previously every TaskItem delegate listened to every
        // signal and called taskViewModel.subtasksOf(root.id) — an O(N)
        // DataStore::subtasks scan.  With 100 visible delegates and 500 tasks
        // that was 50,000 ops per single edit, the bulk of the residual UI lag
        // after Q3.24.  The "_subtaskTotal > 0" guard is the cheap heuristic
        // that keeps the badge correct under re-parenting: rows with no subtask
        // badge can only be AFFECTED by a change if the changed task's new
        // parent is this row (the second clause handles that).
        function onTaskChanged(id) {
            if (root.isSubtask || !root.id) return
            if (id === root.id) { Qt.callLater(_refreshSubtaskCounts); return }
            if (root._subtaskTotal > 0) { Qt.callLater(_refreshSubtaskCounts); return }
            const t = dataStore.taskById(id)
            if (t && t.parentId === root.id) Qt.callLater(_refreshSubtaskCounts)
        }
        function onTaskAdded(id) {
            if (root.isSubtask || !root.id) return
            const t = dataStore.taskById(id)
            if (t && t.parentId === root.id) Qt.callLater(_refreshSubtaskCounts)
        }
        function onTaskRemoved(id) {
            // The removed task is gone from DataStore, so we can't look up
            // its parentId. Refresh only if this row currently shows a
            // subtask badge — otherwise the removed task can't have been one
            // of ours and the counts can't have changed.
            if (root.isSubtask || !root.id) return
            if (root._subtaskTotal > 0) Qt.callLater(_refreshSubtaskCounts)
        }
        // Q3.24: onDataChanged removed — it fired for ALL delegates on every
        // single task mutation (TaskViewModel::refresh emits dataChanged(0,N-1)).
        // onTaskChanged/Added/Removed already cover every relevant mutation;
        // bulk sync reloads emit onModelReset which destroys and recreates
        // delegates so Component.onCompleted → _refreshSubtaskCounts fires anyway.
    }

    // ── Bulk expand/collapse triggers ─────────────────────────────────────
    // Listen for header-driven "expand all" / "collapse all" commands. Only
    // top-level rows that actually have subtasks should react — subtask rows
    // and orphan rows have no expand affordance so toggling their state is a
    // no-op that would still pollute QSettings via _setExpandedPersisted().
    Connections {
        target: expandedSettings
        function onExpandAllTokenChanged() {
            if (!root.isSubtask && root.id !== ""
                    && taskViewModel.hasSubtasks(root.id))
                root.expanded = true
        }
        function onCollapseAllTokenChanged() {
            if (!root.isSubtask && root.id !== "")
                root.expanded = false
        }
    }

    // ── Background ────────────────────────────────────────────────────────
    Rectangle {
        id: background
        anchors.fill: parent
        color: {
            if (taskViewModel.isSelected(root.id))
                return Qt.rgba(ThemeManager.accentColor.r,
                               ThemeManager.accentColor.g,
                               ThemeManager.accentColor.b, 0.15)
            return mouseArea.containsMouse ? root._hoverColor : root._bgColor
        }

        Behavior on color {
            ColorAnimation { duration: Duration.fast }
        }
    }

    // ── Bottom divider — удалён в Q4.01 pixel-perfect. Замещён full-width
    // themed border в самом конце root Item (см. конец файла). Старый
    // divider использовал hardcoded `#363636/#f0f0f0` и leftMargin Spacing.md,
    // новый — ThemeManager.borderColor full-width (как в дизайне TaskRow).

    // ── Color stripe (left edge) — visible only when task has a color set ─
    // Width 3px, height matches the main row; rendered above background but
    // below the row content (z:1). Does not affect layout width — anchored
    // absolutely to the parent left edge.
    Rectangle {
        id: colorStripe
        visible: root.color !== "" && root.color !== undefined
        anchors.left:   parent.left
        anchors.top:    mainRow.top
        anchors.bottom: mainRow.bottom
        width: 3
        color: root.color
        z: 1   // above bg rectangle, below content
    }

    // ── Project color stripe (left edge) ──────────────────────────────────
    // Thin 3px vertical bar tinted by the task's project color, used as a
    // fast visual indicator of project membership in mixed-project views
    // (Today/Upcoming/Inbox/Filter/Label). Hidden inside ProjectView, where
    // every visible row already belongs to the same project — the stripe
    // would just add visual noise. Also hidden when the task has no project
    // (e.g. a freshly created task pinned to no list) and when the parent
    // task-level colorStripe above is showing, the project stripe shifts
    // 4 px right so both are visible side-by-side.
    //
    // Reactive bind: _projectColor recomputes whenever projectId changes or
    // any project mutation fires (project rename / colour change emits
    // dataStore.projectsChanged via dataStore.markProjectModified).
    property string _projectColor: {
        // Re-evaluate when projects change — touch the count so the binding
        // depends on dataStore.projectsChanged.
        const _touch = dataStore.projectCount
        if (!root.projectId || root.projectId === "") return ""
        const p = dataStore.projectById(root.projectId)
        if (!p) return ""
        const c = p.color
        if (!c || c === "" || c === "charcoal" || c === "grey") return ""
        return c
    }

    Rectangle {
        id: projectColorStripe
        visible: root._projectColor !== ""
                 && taskViewModel.filterMode !== TaskViewModel.Project
        anchors.left:   parent.left
        // Offset by 4px when the per-task colorStripe is also visible so both
        // bars sit side-by-side without overlap.
        anchors.leftMargin: colorStripe.visible ? 4 : 0
        anchors.top:    mainRow.top
        anchors.bottom: mainRow.bottom
        width: 3
        color: root._projectColor !== "" ? root._projectColor : "transparent"
        z: 1   // same plane as colorStripe — above bg, below row content
    }

    // ── Main task row ─────────────────────────────────────────────────────
    Item {
        id: mainRow
        anchors.top:   parent.top
        anchors.left:  parent.left
        anchors.right: parent.right
        // Minimum height follows density mode; grows to fit description when present
        height: Math.max(_rowMinHeight, mainRowLayout.implicitHeight + _rowVPadding * 2)

        // ── Whole-row mouse area + right-click context menu ───────────────
        // MUST be declared BEFORE the RowLayout. Otherwise it sits on top of
        // the inner MouseAreas (priority circle, expand toggle, drag handle)
        // and swallows their clicks — clicking the priority circle would open
        // the editor instead of toggling completion.
        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            // Bug 10: enable drag-from-anywhere-on-row. drag.target is bound to
            // the same invisible dragGhost helper used by the right-edge ⋮⋮
            // handle so the visible row never gets reparented or shifted out of
            // the ListView. drag.threshold matches the handle's threshold so the
            // disambiguation between click and drag is identical. drag.active is
            // false during a click — onClicked still fires for taps and right-
            // clicks. Right-click is excluded from drag because Qt only honours
            // drag for the configured drag.button (defaults to Qt.LeftButton),
            // so the context menu remains untouched.
            drag.target: dragGhost
            drag.threshold: 6
            drag.smoothed: false
            drag.axis: Drag.XAndYAxis
            cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.PointingHandCursor
            onContainsMouseChanged: {
                if (containsMouse)
                    taskViewModel.hoveredTaskId = root.id
                else if (taskViewModel.hoveredTaskId === root.id)
                    taskViewModel.hoveredTaskId = ""
            }
            onClicked: function(mouse) {
                // Suppress click when a drag just completed — Qt fires onClicked
                // even after a drag, but we treat any movement past the threshold
                // as drag intent, not as an attempt to open the editor.
                if (drag.active) return
                if (mouse.button === Qt.RightButton) {
                    taskCtxMenu.popup()
                } else if (mouse.modifiers & Qt.ControlModifier) {
                    // Ctrl+Click — toggle individual selection
                    taskViewModel.toggleSelection(root.id)
                } else if (mouse.modifiers & Qt.ShiftModifier) {
                    // Shift+Click — range select from first selected to this item
                    if (taskViewModel.selectionCount > 0) {
                        const anchor = taskViewModel.selectedIds[0]
                        taskViewModel.selectRange(anchor, root.id)
                    } else {
                        taskViewModel.selectTask(root.id)
                    }
                } else {
                    // Regular click — clear selection (if any) and open task
                    if (taskViewModel.selectionCount > 0)
                        taskViewModel.clearSelection()
                    root.taskClicked(root.id)
                }
            }
        }

        RowLayout {
            id: mainRowLayout
            anchors {
                fill:        parent
                leftMargin:  root.isSubtask ? Spacing.md + 24 : Spacing.md
                rightMargin: Spacing.md
                topMargin:   _rowVPadding
                bottomMargin: _rowVPadding
            }
            spacing: _compact ? 6 : 10

            // ── Expand/collapse toggle ────────────────────────────────────
            // Only visible for top-level tasks that actually have children.
            // The MouseArea has a NEGATIVE margin to extend the hit-zone
            // 6px outward in every direction (24×24 click target around a
            // 16×16 visual). Without this, clicks on the chevron at small
            // window sizes / non-1x DPI almost always missed and bled into
            // the row-level MouseArea (which opened the task editor
            // instead of toggling subtask expansion). z:5 keeps the click
            // priority above the row MouseArea.
            // Q4.01 pixel-perfect: дизайн TaskRow всегда рисует chevron-right
            // слева от чекбокса (decorative для задач без подзадач, кликабельный
            // — для задач с подзадачами). Ширина обёртки 12, иконка 11, top-выровнена
            // с paddingTop 3 (alignItems: 'flex-start' в дизайне).
            Item {
                id: expandToggleItem
                Layout.preferredWidth:  12
                Layout.preferredHeight: Spacing.md
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 3
                visible: !root.isSubtask
                z: 5

                readonly property bool _hasSub: taskViewModel.hasSubtasks(root.id)

                IconSvg {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    name:    root.expanded ? "chevron-down" : "chevron-right"
                    size:    11
                    color:   root._subColor
                    // Q4.01 pixel-perfect: всегда видим, но затемнён для задач без подзадач
                    // (decorative-only). Дизайн делает то же через opacity: mute ? 0.4 : 1.
                    opacity: expandToggleItem._hasSub ? 1.0 : 0.4
                }

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: expandToggleItem._hasSub ? Qt.PointingHandCursor : Qt.ArrowCursor
                    enabled: expandToggleItem._hasSub
                    // preventStealing + propagateComposedEvents:false so the
                    // row MouseArea (which fills the whole row) doesn't grab
                    // our click during the press phase.
                    preventStealing: true
                    propagateComposedEvents: false
                    onClicked: root.expanded = !root.expanded
                }
            }

            // ── Completion checkbox (circular) ────────────────────────────
            // Q4.01 pixel-perfect: top-align like the design (alignItems: flex-start
            // in TaskRow). topMargin nudges the 18px circle down a hair in non-compact
            // mode so it sits on the typographic baseline of the title row instead
            // of hugging the very top edge.
            Item {
                Layout.preferredWidth:  18
                Layout.preferredHeight: 18
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: _rowVPadding > 8 ? 0 : 4

                Rectangle {
                    id: checkCircle
                    anchors.fill: parent
                    radius:       width / 2
                    color:        isCompleted ? root._priorityColor : "transparent"
                    border.color: root._priorityColor
                    border.width: 1.5

                    // Task 17cea65b iter-3: было unicode "✓".
                    // Q4.01 pixel-perfect: size 10 (дизайн PriorityCheckCircle.check)
                    IconSvg {
                        anchors.centerIn: parent
                        name: "check"
                        size: 10
                        color: "white"
                        visible: isCompleted
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (isCompleted)
                            taskViewModel.uncompleteTask(root.id)
                        else
                            taskViewModel.completeTask(root.id)
                    }
                }
            }

            // ── Content column ────────────────────────────────────────────
            // Q4.01 pixel-perfect: top-aligned (design uses alignItems: flex-start),
            // so the title sits at the top of the row and any wrapped description
            // / meta-chips flow downward instead of being centred against the
            // checkbox/expand-toggle.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 2

                // Wrapper around the title Text + inline-edit TextField. A
                // TapHandler on the wrapper opens the inline editor on double
                // tap; single taps fall through to the row-wide mouseArea
                // underneath (gesturePolicy: ReleaseWithinBounds + listening
                // only to onDoubleTapped means single taps are not consumed).
                Item {
                    id: contentArea
                    Layout.fillWidth: true
                    implicitHeight: root._inlineEditing
                                    ? inlineEditField.implicitHeight
                                    : contentLabel.implicitHeight

                    Text {
                        id: contentLabel
                        anchors.left:   parent.left
                        anchors.right:  parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !root._inlineEditing
                        // ITER3 #9: replace the fixed Layout.maximumWidth offset (Spacing.md*6 = 96px)
                        // which was too narrow on mobile (320px) and broke subtask indenting.
                        // Layout.fillWidth + elide: Text.ElideRight is sufficient — the Text
                        // element already clips correctly once the RowLayout has consumed all
                        // fixed-width siblings (checkbox, drag handle, chip row spacers).
                        //
                        // URL auto-link: when the content contains an http(s) URL we switch
                        // to RichText so Qt renders <a> tags as clickable links. Plain text
                        // is used otherwise — cheaper and avoids any HTML parsing overhead.
                        text:             root._hasUrl(root.content)
                                          ? root._autolinkContent(root.content)
                                          : root.content
                        textFormat:       root._hasUrl(root.content) ? Text.RichText : Text.PlainText
                        onLinkActivated:  function(url) { Qt.openUrlExternally(url) }
                        linkColor:        ThemeManager.accentColor
                        font.pixelSize:   root.isSubtask ? FontSize.caption : FontSize.bodySmall
                        lineHeight:       1.4
                        lineHeightMode:   Text.ProportionalHeight
                        color:            isCompleted ? root._subColor : root._textColor
                        font.strikeout:   isCompleted
                        elide:            Text.ElideRight
                        maximumLineCount: 1
                    }

                    // Double-tap → enter inline-edit mode. Using TapHandler
                    // with gesturePolicy: ReleaseWithinBounds and only the
                    // onDoubleTapped signal means single clicks are NOT
                    // consumed — they propagate to the row-wide mouseArea
                    // which still opens the full editor.
                    TapHandler {
                        id: contentDoubleTap
                        enabled: !root._inlineEditing && !root.isCompleted
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        acceptedButtons: Qt.LeftButton
                        onDoubleTapped: root._beginInlineRename()
                    }

                    TextField {
                        id: inlineEditField
                        anchors.left:   parent.left
                        anchors.right:  parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root._inlineEditing
                        // Match the read-only Label so swapping is visually quiet.
                        font.pixelSize: root.isSubtask ? FontSize.caption : FontSize.bodySmall
                        color:          root._textColor
                        selectByMouse:  true
                        selectionColor: ThemeManager.accentColor
                        // Single-line — Enter commits via onAccepted.
                        background: Rectangle {
                            color: ThemeManager.surfaceColor
                            border.color: ThemeManager.accentColor
                            border.width: 1
                            radius: Radius.sm
                        }
                        // Refresh the field text whenever the editor opens so
                        // we always start from the current task content (not
                        // a stale value from a previous open).
                        onVisibleChanged: {
                            if (visible) text = root.content
                        }
                        onAccepted: root._commitInlineRename(text)
                        Keys.onEscapePressed: root._cancelInlineRename()
                        onActiveFocusChanged: {
                            if (!activeFocus && root._inlineEditing)
                                root._commitInlineRename(text)
                        }
                    }
                }

                // ── Description (inline markdown preview) ─────────────────
                // TODO C++: expose Markdown.toRichText as Q_INVOKABLE for
                // a richer HTML rendering path (handles code blocks, lists, etc.).
                // For now Qt 6's native Text.MarkdownText handles inline formatting.
                //
                // [[Task Title]] cross-references: _resolveTaskRefs() pre-processes
                // the description before MarkdownText sees it, converting resolved
                // references to markdown links [Title](opentodo://task/<id>).
                // onLinkActivated catches the custom URI scheme and navigates to the
                // referenced task; regular http(s) links fall through to the browser.
                Text {
                    Layout.fillWidth: true
                    visible:          root.description !== "" && !root.isCompleted
                    text:             root._resolveTaskRefs(root.description)
                    textFormat:       Text.MarkdownText
                    linkColor:        ThemeManager.accentColor
                    onLinkActivated: function(url) {
                        const prefix = "opentodo://task/"
                        if (typeof url === "string" && url.indexOf(prefix) === 0) {
                            const id = url.substring(prefix.length)
                            const t = dataStore.taskById(id)
                            if (t) taskViewModel.selectedTask = t
                        } else {
                            Qt.openUrlExternally(url)
                        }
                    }
                    font.pixelSize:   FontSize.caption
                    color:            root._subColor
                    elide:            Text.ElideRight
                    maximumLineCount: 2
                    wrapMode:         Text.WordWrap
                }

                // ── Subtask progress bar (2 px, collapsed state only) ─────
                // Shown below the description when subtasks exist but the
                // section is not yet expanded. Adds no visible height delta
                // to mainRow because the ColumnLayout already stretches to
                // at least the chip row height; the bar occupies the same
                // 2 px that would otherwise be empty padding.
                Rectangle {
                    visible: _subtaskTotal > 0 && !root.expanded && !root.isSubtask
                    Layout.fillWidth: true
                    Layout.preferredHeight: 2
                    Layout.topMargin: 2
                    color: Qt.rgba(0, 0, 0, 0.07)
                    radius: 1

                    Rectangle {
                        anchors.top:    parent.top
                        anchors.bottom: parent.bottom
                        anchors.left:   parent.left
                        width: parent.width * (_subtaskTotal > 0 ? _subtaskDone / _subtaskTotal : 0)
                        color: ThemeManager.accentColor
                        radius: 1

                        Behavior on width {
                            NumberAnimation { duration: 200; easing.type: Easing.OutQuad }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    visible: dueDate !== "" || (labels && labels.length > 0)
                             || root._hasDeadline || root._durationLabel !== ""
                             || root._estimateLabel !== ""

                    // Q4.01 (redesign): due-date chip now leads with a stroke
                    // CalendarIcon (size 11) like the design's TaskChip — date
                    // text alone read as "raw timestamp", the icon reads as
                    // "this is a due date" at a glance.
                    Rectangle {
                        visible:  dueDate !== "" && dueDate !== undefined
                        height:   18
                        width:    _dueChipRow.implicitWidth + 12
                        radius:   4
                        color:    "transparent"

                        Row {
                            id: _dueChipRow
                            anchors.centerIn: parent
                            spacing: 4

                            IconSvg {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "calendar"
                                size: 11
                                color: root._isOverdue ? ThemeManager.errorColor : root._subColor
                            }

                            Text {
                                id: dueDateLabel
                                anchors.verticalCenter: parent.verticalCenter
                                text:          root.dueString !== "" ? root.dueString : root.dueDate
                                font.pixelSize: FontSize.caption
                                color:         root._isOverdue ? ThemeManager.errorColor : root._subColor
                            }
                        }
                    }

                    // ── Chip повтора (repeat-icon + дата) ─────────────────
                    // Q4.01 (redesign): glyph "🔁" заменён на IconSvg из
                    // stroke-пака. Показывается только для повторяющихся
                    // задач, у которых есть следующая дата выполнения.
                    Rectangle {
                        id: recurringChip
                        visible: root.isRecurring && root.dueDate !== "" && root.dueDate !== undefined
                        Layout.alignment: Qt.AlignVCenter
                        height: 18
                        // Q4.01 pixel-perfect: дизайн TaskChip height 18, radius 4,
                        // padding 0 6 → 11 icon + 4 gap + text + 12 padding = +27
                        width: recurringChipText.implicitWidth + 27
                        radius: 4
                        color: Qt.rgba(ThemeManager.accentColor.r,
                                       ThemeManager.accentColor.g,
                                       ThemeManager.accentColor.b, 0.12)

                        Row {
                            anchors.centerIn: parent
                            spacing: 4

                            IconSvg {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "repeat"
                                size: 11
                                color: ThemeManager.accentColor
                            }

                            Text {
                                id: recurringChipText
                                anchors.verticalCenter: parent.verticalCenter
                                text: recurringChip._nextOccurrenceShort()
                                font.pixelSize: FontSize.caption
                                color: ThemeManager.accentColor
                            }
                        }

                        // Возвращает локализованную строку для ближайшей даты:
                        // "сегодня" / "завтра" / "12.05" / "12.05.2027"
                        function _nextOccurrenceShort() {
                            if (!root.dueDate) return ""
                            const today    = Qt.formatDate(new Date(), "yyyy-MM-dd")
                            const tomorrow = Qt.formatDate(new Date(Date.now() + 86400000), "yyyy-MM-dd")
                            if (root.dueDate === today)    return qsTr("сегодня")
                            if (root.dueDate === tomorrow) return qsTr("завтра")
                            const d = new Date(root.dueDate + "T00:00:00")
                            if (isNaN(d.getTime())) return root.dueDate
                            const now = new Date()
                            if (d.getFullYear() === now.getFullYear())
                                return Qt.formatDate(d, "dd.MM")
                            return Qt.formatDate(d, "dd.MM.yyyy")
                        }
                    }

                    Repeater {
                        model: root.labels ? root.labels : []

                        delegate: LabelChip {
                            required property string modelData
                            labelName: modelData
                            labelColor: {
                                const lbl = dataStore.labelByName(modelData)
                                return (lbl && lbl.color && lbl.color !== "charcoal")
                                       ? lbl.color : "#808080"
                            }
                        }
                    }

                    // ── Deadline chip ─────────────────────────────────────
                    // Q4.01 (redesign): "⏰" emoji заменён на IconSvg "alarm".
                    Rectangle {
                        visible:  root._hasDeadline
                        height:   18
                        width:    deadlineLabel.implicitWidth + 27
                        radius:   4
                        color:    "transparent"

                        Row {
                            anchors.centerIn: parent
                            spacing: 4

                            IconSvg {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "alarm"
                                size: 11
                                color: root._deadlineColor
                            }

                            Text {
                                id: deadlineLabel
                                anchors.verticalCenter: parent.verticalCenter
                                text:          root.deadline
                                font.pixelSize: FontSize.caption
                                color:         root._deadlineColor
                            }
                        }
                    }

                    // ── Duration chip ─────────────────────────────────────
                    // Q4.01 (redesign): "⏱" emoji заменён на IconSvg "clock".
                    Rectangle {
                        visible:  root._durationLabel !== ""
                        height:   18
                        width:    durationLabel.implicitWidth + 27
                        radius:   4
                        color:    "transparent"

                        Row {
                            anchors.centerIn: parent
                            spacing: 4

                            IconSvg {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "clock"
                                size: 11
                                color: root._subColor
                            }

                            Text {
                                id: durationLabel
                                anchors.verticalCenter: parent.verticalCenter
                                text:          root._durationLabel
                                font.pixelSize: FontSize.caption
                                color:         root._subColor
                            }
                        }
                    }

                    // ── Chip оценки трудозатрат ───────────────────────────
                    // Q4.01 (redesign): "⏱" префикс из _estimateLabel убран,
                    // теперь рендерим IconSvg + чистый текст.
                    Rectangle {
                        visible: root._estimateLabel !== ""
                        Layout.alignment: Qt.AlignVCenter
                        height: 18
                        width:  estimateChipText.implicitWidth + 27
                        radius: 4
                        color:  "transparent"

                        Row {
                            anchors.centerIn: parent
                            spacing: 4

                            IconSvg {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "clock"
                                size: 11
                                color: root._subColor
                            }

                            Text {
                                id: estimateChipText
                                anchors.verticalCenter: parent.verticalCenter
                                text:          root._estimateLabel
                                font.pixelSize: FontSize.caption
                                color:         root._subColor
                            }
                        }
                    }

                    // ── Значок закреплённой задачи ────────────────────────
                    Item {
                        visible: root.isPinned
                        Layout.preferredWidth:  14
                        Layout.preferredHeight: 14

                        // Task 17cea65b iter-3: было unicode emoji "📌".
                        IconSvg {
                            anchors.centerIn: parent
                            name: "pin"
                            size: 12
                            color: ThemeManager.accentColor
                        }
                    }

                    Item { Layout.fillWidth: true }
                }
            }

            // ── Subtask progress badge ─────────────────────────────────────
            // Shown only for top-level tasks that have at least one subtask.
            // Sits between the content column and the drag handle so it does
            // not push the title text aside (the ColumnLayout is fillWidth).
            // Click toggles the row's subtask expand/collapse state, mirroring
            // the ▶/▼ caret on the left so users can act on either affordance.
            Rectangle {
                id: subtaskBadge
                visible: _subtaskTotal > 0 && !root.isSubtask
                Layout.alignment: Qt.AlignVCenter
                height: 16
                width: badgeText.implicitWidth + 10
                radius: 8
                // Three-state tint:
                //   • all done       → green wash (success)
                //   • partial done   → amber wash (warning) — visible progress
                //   • zero done      → neutral surface tint
                // Falls back to accentColor-based washes when ThemeManager
                // does not expose successColor / warningColor (older themes).
                color: {
                    if (subtaskBadge._allDone) {
                        return (typeof ThemeManager.successColor !== "undefined"
                                && ThemeManager.successColor !== "")
                            ? Qt.rgba(ThemeManager.successColor.r,
                                      ThemeManager.successColor.g,
                                      ThemeManager.successColor.b, 0.18)
                            : Qt.rgba(0.30, 0.69, 0.31, 0.18)
                    }
                    if (_subtaskDone > 0) {
                        return (typeof ThemeManager.warningColor !== "undefined"
                                && ThemeManager.warningColor !== "")
                            ? Qt.rgba(ThemeManager.warningColor.r,
                                      ThemeManager.warningColor.g,
                                      ThemeManager.warningColor.b, 0.18)
                            : Qt.rgba(ThemeManager.accentColor.r,
                                      ThemeManager.accentColor.g,
                                      ThemeManager.accentColor.b, 0.15)
                    }
                    // Zero done — neutral surface-variant tint
                    return Qt.rgba(root._subColor.r,
                                   root._subColor.g,
                                   root._subColor.b, 0.12)
                }

                readonly property bool _allDone: _subtaskTotal > 0 && _subtaskDone === _subtaskTotal

                // ── Thin progress fill (overlay, behind the text) ──────────
                // Width scales with completion ratio so the chip itself
                // doubles as a horizontal progress indicator. Uses the
                // accent colour at low alpha so the badge text stays legible
                // on top regardless of theme.
                Rectangle {
                    id: badgeProgressFill
                    anchors.top:    parent.top
                    anchors.bottom: parent.bottom
                    anchors.left:   parent.left
                    width: _subtaskTotal > 0
                        ? parent.width * (_subtaskDone / _subtaskTotal)
                        : 0
                    radius: parent.radius
                    color: subtaskBadge._allDone
                        ? Qt.rgba(0.30, 0.69, 0.31, 0.30)
                        : Qt.rgba(ThemeManager.accentColor.r,
                                  ThemeManager.accentColor.g,
                                  ThemeManager.accentColor.b, 0.28)
                    Behavior on width {
                        NumberAnimation { duration: 200; easing.type: Easing.OutQuad }
                    }
                }

                // Q4.01 (night iter-5): "✓ N/M" замена text-emoji на
                // IconSvg "check" + Text. Раньше галочка рендерилась как
                // Unicode ✓ в font (FontSize.caption=11px) — выглядела
                // тонкой/мелкой и font-style-dependent. Теперь stroke svg
                // 10px рядом с цифрами, единая визуальная семья со всеми
                // галочками в app (priority circle, EmptyState badge).
                Row {
                    id: badgeText
                    anchors.centerIn: parent
                    spacing: 3
                    IconSvg {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "check"
                        size: 10
                        color: subtaskBadge._allDone
                            ? "#2e8b57"
                            : ThemeManager.accentColor
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: _subtaskDone + "/" + _subtaskTotal
                        font.pixelSize: FontSize.caption
                        color: subtaskBadge._allDone
                            ? "#2e8b57"
                            : ThemeManager.accentColor
                        font.bold: subtaskBadge._allDone
                    }
                }

                // Click → toggle expand/collapse, mirroring the ▶/▼ caret.
                MouseArea {
                    id: subtaskBadgeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    // Stop the click from bubbling up to the row-wide
                    // mouseArea, which would open the task editor.
                    propagateComposedEvents: false
                    onClicked: function(mouse) {
                        root.expanded = !root.expanded
                        mouse.accepted = true
                    }
                    // Russian tooltip per spec
                    ToolTip.visible: containsMouse
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("Подзадачи: %1 из %2 выполнено").arg(_subtaskDone).arg(_subtaskTotal)
                }
            }

            // ── Right side: drag handle (shown on hover) ──────────────────
            Item {
                id: dragHandleItem
                Layout.preferredWidth:  Spacing.lg
                Layout.preferredHeight: Spacing.lg
                Layout.alignment: Qt.AlignVCenter
                opacity: (mouseArea.containsMouse || root.dragActive) ? 1.0 : 0.0

                // ITER2 #23: drag handle is icon-only — expose as a button to AT.
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Перетащить задачу")

                Behavior on opacity { NumberAnimation { duration: Duration.fast } }

                // Iter-night-24: было unicode "⋮⋮". Use the dedicated
                // drag-handle SVG (six-dot grip) instead of two stacked
                // ellipses — adapts to theme colour through IconSvg's
                // colorization and renders consistently across high-DPI.
                IconSvg {
                    anchors.centerIn: parent
                    name:  "drag-handle"
                    size:  16
                    color: root.dragActive ? ThemeManager.accentColor : root._subColor
                }

                // The dedicated drag-only MouseArea.  Using drag.target = root
                // would move the whole item out of the ListView; instead we rely
                // on Drag.Automatic + Drag.active so Qt Quick DnD handles the
                // ghost image without reparenting.
                MouseArea {
                    id: dragHandleArea
                    anchors.fill: parent
                    cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    // ITER1 #12: drag.target: root pulled the entire delegate out
                    // of the ListView mid-drag, leaving a hole and corrupting layout.
                    // For Drag.Automatic Qt creates a system-level ghost image — the
                    // delegate must stay in place. Setting drag.target to a tiny
                    // invisible helper item gives MouseArea a "drag" affordance
                    // (so drag.active toggles) without moving the visible row.
                    drag.target: dragGhost
                    drag.axis: Drag.XAndYAxis
                    onPressed: function(mouse) { mouse.accepted = true }
                }
                // Invisible helper used solely as the MouseArea drag.target. Sits
                // outside the layout flow so its movement does not visually shift
                // anything. The real ghost image is provided by Drag.Automatic.
                Item {
                    id: dragGhost
                    width: 1; height: 1
                    visible: false
                }
            }
        }

        // ── Hover-preview popup ───────────────────────────────────────────────
        // Appears after 800 ms of hover when extra info exists and the task
        // is not already open in the editor (isSelected guard).
        Popup {
            id: previewPopup

            // Closed by default; opened by hoverDelayTimer when conditions hold.
            visible: false

            width: 320
            // Height is driven by the content text; add vertical padding.
            height: previewText.implicitHeight + Spacing.md * 2

            // Render above everything else in the window.
            parent: Overlay.overlay

            // Padding so the text does not touch the border.
            leftPadding:   Spacing.sm
            rightPadding:  Spacing.sm
            topPadding:    Spacing.sm
            bottomPadding: Spacing.sm

            // Position relative to the task row, just below it, centred
            // horizontally.  Recompute whenever the popup is about to appear
            // so it reflects the row's current position in the window.
            onAboutToShow: {
                const p = mainRow.mapToItem(Overlay.overlay, 0, mainRow.height)
                const cx = p.x + mainRow.width / 2
                x = Math.max(8,
                             Math.min(cx - width / 2,
                                      Overlay.overlay.width - width - 8))
                y = p.y + 6
            }

            background: Rectangle {
                color:        ThemeManager.surfaceColor
                border.color: ThemeManager.borderColor
                border.width: 1
                radius:       Radius.md
            }

            contentItem: Text {
                id: previewText
                text:       root._previewContent
                // MarkdownText so descriptions with **bold** / *italic* render.
                textFormat: Text.MarkdownText
                wrapMode:   Text.WordWrap
                color:      ThemeManager.textColor
                font.pixelSize: FontSize.caption
            }

            // 800 ms delay before the popup appears (matches spec).
            Timer {
                id: hoverDelayTimer
                interval: 800
                repeat:   false
                onTriggered: {
                    // Re-check conditions at fire time — the cursor may have
                    // moved away in the 800 ms window.
                    if (mouseArea.containsMouse
                            && root._hasPreviewContent
                            && !taskViewModel.isSelected(root.id)) {
                        previewPopup.open()
                    }
                }
            }

            // Watch mouseArea.containsMouse to start/stop the delay timer.
            Connections {
                target: mouseArea
                function onContainsMouseChanged() {
                    if (mouseArea.containsMouse
                            && root._hasPreviewContent
                            && !taskViewModel.isSelected(root.id)) {
                        hoverDelayTimer.restart()
                    } else {
                        hoverDelayTimer.stop()
                        previewPopup.close()
                    }
                }
            }

            // Close immediately when the task becomes selected in the editor.
            Connections {
                target: taskViewModel
                function onSelectionChanged() {
                    if (taskViewModel.isSelected(root.id)) {
                        hoverDelayTimer.stop()
                        previewPopup.close()
                    }
                }
            }
        }

        Menu {
            id: taskCtxMenu

            MenuItem {
                text: qsTr("Открыть")
                onTriggered: root.taskClicked(root.id)
            }
            MenuItem {
                // Q4.01 (redesign): эмодзи-префикс "📋" убран — контекст-меню
                // не несёт иконок (соседние "Открыть"/"В архив" тоже без них).
                text: qsTr("Дублировать")
                // ITER1 #20/#41: use duplicateTask which copies all fields (labels,
                // due date, description, section, color, deadline, etc.) but NOT
                // recurrence and NOT the pinned flag. The duplicate is inserted
                // directly below the source via order-slot claiming.
                onTriggered: {
                    const newId = taskViewModel.duplicateTask(root.id)
                    if (newId && newId !== "")
                        taskViewModel.openTaskById(newId)
                }
            }
            MenuItem {
                text: qsTr("⬆⬆ В начало")
                onTriggered: taskViewModel.moveTaskToTop(root.id)
            }
            MenuItem {
                text: qsTr("⬇⬇ В конец")
                onTriggered: taskViewModel.moveTaskToBottom(root.id)
            }
            // ── «Отложить до даты...» — открывает кастомный DatePicker-popup
            // с явными кнопками «Применить» / «Отмена», в отличие от
            // DatePicker.qml, который применяет выбор сразу по клику.
            // По умолчанию выбирается завтра (или текущий due + 1 день).
            // Скрыто для уже выполненных задач — переносить выполненную задачу
            // вперёд бессмысленно (см. также _deferTaskMinutes выше — он ставит
            // напоминание, тогда как здесь меняется именно поле due).
            MenuItem {
                // Q4.01 (redesign): эмодзи-префикс "📅" убран.
                text: qsTr("Отложить до даты...")
                visible: !root.isCompleted
                height: visible ? implicitHeight : 0
                onTriggered: snoozeDatePicker.openForTask()
            }
            // ── Promote subtask → standalone ────────────────────────────────
            MenuItem {
                text: qsTr("⬅ Поднять")
                visible: root.parentId !== ""
                height: visible ? implicitHeight : 0
                onTriggered: taskViewModel.promoteTask(root.id)
            }
            // ── Demote standalone → subtask ─────────────────────────────────
            // Opens the inline parent-picker Popup defined further down.
            MenuItem {
                text: qsTr("➡ Сделать подзадачей...")
                visible: root.parentId === ""
                height: visible ? implicitHeight : 0
                onTriggered: parentPicker.openForTask(root.id)
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("Добавить выше")
                onTriggered: {
                    const newId = taskViewModel.addTaskAbove(root.id, "")
                    if (newId && newId !== "")
                        taskViewModel.openTaskById(newId)
                }
            }
            MenuItem {
                text: qsTr("Добавить ниже")
                onTriggered: {
                    const newId = taskViewModel.addTaskBelow(root.id, "")
                    if (newId && newId !== "")
                        taskViewModel.openTaskById(newId)
                }
            }
            MenuSeparator {}
            Menu {
                title: qsTr("Перенести в проект")
                Repeater {
                    model: projectViewModel
                    MenuItem {
                        required property var model
                        text: model.name
                        enabled: model.id !== root.projectId
                        onTriggered: taskViewModel.moveTask(root.id, model.id, "")
                    }
                }
            }
            // ── Отложить: 5 настраиваемых длительностей из настроек ──────────
            // Длительности читаются из категории "deferDurations" (см.
            // SettingsSectionDefer.qml). По клику вызывается одноразовое
            // запланированное уведомление через NotificationService — та же
            // семантика, что и у кнопки «Отложить» в InAppBanner.
            Menu {
                title: qsTr("Отложить")
                MenuItem {
                    text: qsTr("Отложить на %1 мин").arg(deferSettings.slot1)
                    onTriggered: root._deferTaskMinutes(deferSettings.slot1)
                }
                MenuItem {
                    text: qsTr("Отложить на %1 мин").arg(deferSettings.slot2)
                    onTriggered: root._deferTaskMinutes(deferSettings.slot2)
                }
                MenuItem {
                    text: qsTr("Отложить на %1 мин").arg(deferSettings.slot3)
                    onTriggered: root._deferTaskMinutes(deferSettings.slot3)
                }
                MenuItem {
                    text: qsTr("Отложить на %1 мин").arg(deferSettings.slot4)
                    onTriggered: root._deferTaskMinutes(deferSettings.slot4)
                }
                MenuItem {
                    text: qsTr("Отложить на %1 мин").arg(deferSettings.slot5)
                    onTriggered: root._deferTaskMinutes(deferSettings.slot5)
                }
            }
            MenuSeparator {}
            MenuItem {
                visible: root.isSubtask
                height: visible ? implicitHeight : 0
                text: qsTr("Повысить до задачи")
                onTriggered: taskViewModel.promoteToTopLevel(root.id)
            }
            MenuItem {
                // Закрепить / открепить задачу наверху текущего списка
                text: root.isPinned ? qsTr("Открепить") : qsTr("Закрепить наверху")
                onTriggered: taskViewModel.updateTask(root.id, "isPinned", !root.isPinned)
            }
            MenuItem {
                text: qsTr("Удалить")
                onTriggered: taskViewModel.deleteTask(root.id)
            }

            // ── Claude Code integration ──────────────────────────────────────
            // Visible only when the task's project has a linkedRepoPath set.
            MenuSeparator {
                visible: {
                    if (typeof claudeCodeLauncher === "undefined" || !claudeCodeLauncher) return false
                    const proj = dataStore.projectById(root.projectId)
                    return !!proj && proj.linkedRepoPath !== ""
                }
                height: visible ? implicitHeight : 0
            }
            MenuItem {
                text: qsTr("Запустить с Claude Code")
                visible: {
                    if (typeof claudeCodeLauncher === "undefined" || !claudeCodeLauncher) return false
                    const proj = dataStore.projectById(root.projectId)
                    return !!proj && proj.linkedRepoPath !== ""
                }
                height: visible ? implicitHeight : 0
                onTriggered: {
                    const proj = dataStore.projectById(root.projectId)
                    if (!proj || proj.linkedRepoPath === "") return
                    const ok = claudeCodeLauncher.launch(root.content, root.description, proj.linkedRepoPath)
                    if (ok) {
                        if (typeof notificationService !== "undefined" && notificationService)
                            notificationService.showInAppBanner(
                                qsTr("Claude Code"),
                                qsTr("Запущено в новом окне"),
                                "")
                    } else {
                        if (typeof notificationService !== "undefined" && notificationService)
                            notificationService.showInAppBanner(
                                qsTr("Claude Code не найден"),
                                qsTr("Установите Claude Code: npm i -g @anthropic-ai/claude-code"),
                                "")
                    }
                }
            }
        }
    }

    // ── Inline subtask section (visible only when expanded & top-level) ───
    Item {
        id: subtaskSection
        anchors.top:   mainRow.bottom
        anchors.left:  parent.left
        anchors.right: parent.right
        visible: root.expanded && !root.isSubtask
        implicitHeight: visible ? subtaskColumn.implicitHeight : 0
        clip: true

        Behavior on implicitHeight {
            NumberAnimation { duration: Duration.fast; easing.type: Easing.InOutQuad }
        }

        Column {
            id: subtaskColumn
            anchors {
                left:   parent.left
                right:  parent.right
                top:    parent.top
            }
            spacing: 0

            // ── Existing subtasks ─────────────────────────────────────────
            // ITER2 #21: decouple the model from a plain binding so subtasksOf()
            // is not invoked on every dataChanged rebuild for every task row.
            // Instead: set the model once when expanded becomes true, and refresh
            // only when dataChanged fires AND the item is currently expanded.
            // Use a tiny refresh-token property that we bump on dataChanged
            // — that forces the model binding below to re-evaluate without
            // making the binding depend on dataChanged directly (which can
            // be too aggressive).
            property int _subtaskRefreshToken: 0

            Connections {
                target: taskViewModel
                function onDataChanged() {
                    if (root.expanded) {
                        subtaskColumn._subtaskRefreshToken =
                            subtaskColumn._subtaskRefreshToken + 1
                    }
                }
            }

            Repeater {
                id: subtaskRepeater
                // Always populate the model — visibility is handled by
                // `subtaskSection.visible: root.expanded && !root.isSubtask`.
                // Tying the model to root.expanded created a chicken-and-egg
                // bug: when expanded was restored from QSettings as true on
                // initial Component construction, the model binding was
                // evaluated BEFORE root.expanded was finalized, returned [],
                // and then no signal fired to repopulate it because the
                // value transitioned 'false → true' too early in setup.
                //
                // We use dataStore.subtasks() because it returns QList<Task*>:
                // each entry is a real QObject with Q_PROPERTY content/etc.
                // accessible reliably from QML. The earlier subtasksOf()
                // path returned QVariantList<QVariantMap> which renders
                // empty delegates in Qt 6.11 AOT-compiled QML — every
                // modelData.X access returns undefined.
                // Use taskViewModel.subtasksOf() — it returns a proper JS
                // array of QVariantMap entries. dataStore.subtasks() returns
                // QList<Task*> which Qt 6.11 doesn't auto-convert to a JS
                // array (length === undefined, isArray === false), so
                // Repeater iterates 0 elements even when there are 2 children.
                model: (typeof taskViewModel !== "undefined"
                        && taskViewModel
                        && root.id
                        && !root.isSubtask
                        && subtaskColumn._subtaskRefreshToken >= 0)
                       ? taskViewModel.subtasksOf(root.id)
                       : []


                delegate: Item {
                    required property var modelData
                    width: subtaskColumn.width
                    height: subtaskRow.implicitHeight


                    Rectangle {
                        anchors.fill: parent
                        color: subtaskMouse.containsMouse ? ThemeManager.hoverColor : "transparent"
                        Behavior on color { ColorAnimation { duration: Duration.fast } }
                    }

                    // Indent separator line
                    Rectangle {
                        anchors.left:   parent.left
                        anchors.leftMargin: Spacing.md + 24
                        anchors.top:    parent.top
                        anchors.bottom: parent.bottom
                        width: 1
                        // Q4.01 (night iter-13): theme-aware subtask connector line
                        color: ThemeManager.borderColor
                    }

                    RowLayout {
                        id: subtaskRow
                        anchors {
                            left:         parent.left
                            right:        parent.right
                            leftMargin:   Spacing.md + 40
                            rightMargin:  Spacing.sm
                            topMargin:    4
                            bottomMargin: 4
                        }
                        spacing: 8

                        // Subtask checkbox
                        Rectangle {
                            width:  14
                            height: 14
                            radius: 7
                            color:  modelData.isCompleted ? ThemeManager.secondaryTextColor : "transparent"
                            border.color: ThemeManager.secondaryTextColor
                            border.width: 1.5

                            // Task 17cea65b iter-3: было unicode "✓".
                            IconSvg {
                                anchors.centerIn: parent
                                name: "check"
                                size: 9
                                color: "white"
                                visible: modelData.isCompleted
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                // ITER1 #47: propagateComposedEvents: false stops the
                                // synthesised "clicked" from bubbling to subtaskMouse.
                                // mouse.accepted = true in onClicked is insufficient
                                // for composed events in Qt 6 — the flag is what works.
                                propagateComposedEvents: false
                                onClicked: (mouse) => {
                                    if (modelData.isCompleted)
                                        taskViewModel.uncompleteTask(modelData.id)
                                    else
                                        taskViewModel.completeTask(modelData.id)
                                }
                            }
                        }

                        // Subtask text
                        Text {
                            Layout.fillWidth: true
                            text:            modelData.content || ""
                            font.pixelSize:  FontSize.caption
                            color:           modelData.isCompleted
                                             ? ThemeManager.secondaryTextColor
                                             : ThemeManager.textColor
                            font.strikeout:  modelData.isCompleted
                            elide:           Text.ElideRight
                            maximumLineCount: 1
                        }
                    }

                    MouseArea {
                        id: subtaskMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.taskClicked(modelData.id)
                        cursorShape: Qt.PointingHandCursor
                    }
                }
            }

            // ── "Add subtask" quick-entry row ─────────────────────────────
            Item {
                width: subtaskColumn.width
                height: addSubtaskRow.implicitHeight + 8

                RowLayout {
                    id: addSubtaskRow
                    anchors {
                        left:   parent.left
                        right:  parent.right
                        top:    parent.top
                        leftMargin:   Spacing.md + 40
                        rightMargin:  Spacing.sm
                        topMargin:    4
                    }
                    spacing: 8

                    // Placeholder circle for consistency with subtask list
                    Rectangle {
                        width:  14
                        height: 14
                        radius: 7
                        color:  "transparent"
                        border.color: ThemeManager.borderColor
                        border.width: 1.5
                    }

                    TextField {
                        id: addSubtaskField
                        Layout.fillWidth: true
                        placeholderText: qsTr("+ Добавить подзадачу")
                        font.pixelSize:  FontSize.caption
                        color:           ThemeManager.textColor
                        background: null
                        leftPadding: 0

                        onAccepted: {
                            const trimmed = text.trim()
                            if (trimmed !== "") {
                                taskViewModel.addSubtask(root.id, trimmed)
                                text = ""
                                // Force the Repeater to pick up the new subtask.
                                // Use dataStore.subtasks() (QList<Task*>) for
                                // consistency with the rest of subtaskRepeater
                                // bindings — taskViewModel.subtasksOf returns
                                // QVariantList<QVariantMap> which renders empty
                                // delegates in Qt 6.11 AOT-compiled QML.
                                subtaskRepeater.model = dataStore.subtasks(root.id)
                            }
                        }

                        // ITER1 #19: Esc clears the field and collapses the subtask section.
                        Keys.onEscapePressed: {
                            text = ""
                            root.expanded = false
                        }
                    }
                }
            }
        }
    }

    // ── «Отложить до даты...» — Popup с календарём и Apply/Cancel ─────────
    // В отличие от компонента DatePicker.qml (который применяет дату сразу
    // по клику и сам закрывается), здесь требуется явное подтверждение
    // через кнопку «Применить»: одиночный клик по дню только меняет выделение
    // (_pendingDate), а отправка в taskViewModel происходит лишь по нажатию
    // «Применить». Это согласуется со спецификацией (см. постановку задачи).
    Popup {
        id: snoozeDatePicker

        // Дата, выбранная в гриде (не отправленная), формат "yyyy-MM-dd".
        property string _pendingDate: ""
        property int    _viewYear:  (new Date()).getFullYear()
        property int    _viewMonth: (new Date()).getMonth()

        readonly property var _today: new Date()

        // weekStart из настроек (0=Mon..6=Sun), как в DatePicker.qml.
        property int _weekStartIdx: (typeof settingsViewModel !== "undefined" && settingsViewModel)
                                    ? settingsViewModel.weekStart : 0
        property int _jsWeekStart:  (_weekStartIdx + 1) % 7
        readonly property var _allLabels: [qsTr("Вс"), qsTr("Пн"), qsTr("Вт"), qsTr("Ср"), qsTr("Чт"), qsTr("Пт"), qsTr("Сб")]
        property var _weekdayLabels: {
            var arr = []
            for (var i = 0; i < 7; i++) arr.push(_allLabels[(_jsWeekStart + i) % 7])
            return arr
        }

        function openForTask() {
            // Дата по умолчанию: завтра, или текущий due + 1 день, если он есть.
            var base
            if (root.dueDate && root.dueDate !== "") {
                var parsed = _parse(root.dueDate)
                base = parsed ? new Date(parsed.getFullYear(),
                                          parsed.getMonth(),
                                          parsed.getDate() + 1)
                              : new Date(Date.now() + 86400000)
            } else {
                base = new Date(Date.now() + 86400000)
            }
            _pendingDate = _format(base)
            _viewYear  = base.getFullYear()
            _viewMonth = base.getMonth()
            open()
        }

        function _format(d) {
            var y  = d.getFullYear()
            var mo = String(d.getMonth() + 1).padStart(2, "0")
            var dy = String(d.getDate()).padStart(2, "0")
            return y + "-" + mo + "-" + dy
        }

        function _parse(s) {
            if (!s || s.length !== 10) return null
            var p = s.split("-")
            if (p.length !== 3) return null
            return new Date(parseInt(p[0]), parseInt(p[1]) - 1, parseInt(p[2]))
        }

        function _navigate(delta) {
            var m = _viewMonth + delta
            var y = _viewYear
            if (m < 0)  { m = 11; y-- }
            if (m > 11) { m = 0;  y++ }
            _viewMonth = m
            _viewYear  = y
        }

        function _monthYearLabel(year, month) {
            var names = [qsTr("Январь"),qsTr("Февраль"),qsTr("Март"),qsTr("Апрель"),qsTr("Май"),qsTr("Июнь"),
                         qsTr("Июль"),qsTr("Август"),qsTr("Сентябрь"),qsTr("Октябрь"),qsTr("Ноябрь"),qsTr("Декабрь")]
            return names[month] + " " + year
        }

        function _buildDays(year, month) {
            var cells = []
            var rawFirstDay = new Date(year, month, 1).getDay()
            var firstDay = (rawFirstDay - _jsWeekStart + 7) % 7
            var daysInM = new Date(year, month + 1, 0).getDate()
            var todayStr = _format(_today)
            for (var i = 0; i < firstDay; i++)
                cells.push({ day: 0, inMonth: false, isToday: false, isSelected: false })
            for (var d = 1; d <= daysInM; d++) {
                var dStr = _format(new Date(year, month, d))
                cells.push({
                    day: d, inMonth: true,
                    isToday: dStr === todayStr,
                    isSelected: dStr === _pendingDate
                })
            }
            while (cells.length < 42)
                cells.push({ day: 0, inMonth: false, isToday: false, isSelected: false })
            return cells
        }

        function _commit() {
            if (!root.id || !_pendingDate) { close(); return }
            taskViewModel.setTaskDue(root.id, _pendingDate)
            if (typeof telemetry !== "undefined" && telemetry) {
                telemetry.trackEvent("task_snooze_to_date",
                                     { task_id: root.id, date: _pendingDate })
            }
            close()
        }

        parent: Overlay.overlay
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        padding: 0

        width: 296
        height: snoozeContent.implicitHeight + 24

        onAboutToShow: {
            var p = mainRow.mapToItem(Overlay.overlay, 0, mainRow.height)
            var cx = p.x + mainRow.width / 2
            x = Math.max(8,
                         Math.min(cx - width / 2,
                                  Overlay.overlay.width - width - 8))
            y = Math.max(8,
                         Math.min(p.y + 6,
                                  Overlay.overlay.height - height - 8))
        }

        background: Rectangle {
            color:        ThemeManager.backgroundColor
            radius:       Radius.md
            border.color: ThemeManager.borderColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            id: snoozeContent
            anchors.margins: 12
            spacing: 8

            // Заголовок popup-а
            Text {
                Layout.fillWidth: true
                text: qsTr("Отложить до...")
                color: ThemeManager.textColor
                font.pixelSize: FontSize.body
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: ThemeManager.borderColor
            }

            // Навигация по месяцам
            RowLayout {
                Layout.fillWidth: true
                spacing: 0

                ToolButton {
                    id: snoozePrevBtn
                    implicitWidth: 28
                    implicitHeight: 28
                    padding: 0
                    contentItem: Text {
                        text: "‹"
                        font.pixelSize: 18
                        color: ThemeManager.textColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 4
                        color: snoozePrevBtn.hovered ? ThemeManager.hoverColor : "transparent"
                    }
                    onClicked: snoozeDatePicker._navigate(-1)
                }

                Text {
                    Layout.fillWidth: true
                    text: snoozeDatePicker._monthYearLabel(snoozeDatePicker._viewYear,
                                                          snoozeDatePicker._viewMonth)
                    color: ThemeManager.textColor
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                ToolButton {
                    id: snoozeNextBtn
                    implicitWidth: 28
                    implicitHeight: 28
                    padding: 0
                    contentItem: Text {
                        text: "›"
                        font.pixelSize: 18
                        color: ThemeManager.textColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 4
                        color: snoozeNextBtn.hovered ? ThemeManager.hoverColor : "transparent"
                    }
                    onClicked: snoozeDatePicker._navigate(1)
                }
            }

            // Заголовки дней недели
            Row {
                Layout.fillWidth: true
                spacing: 0
                Repeater {
                    model: snoozeDatePicker._weekdayLabels
                    delegate: Text {
                        required property string modelData
                        width: snoozeContent.width / 7
                        text: modelData
                        color: ThemeManager.secondaryTextColor
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }

            // Сетка дней — клик меняет _pendingDate, popup НЕ закрывается.
            Grid {
                id: snoozeDayGrid
                Layout.fillWidth: true
                columns: 7
                spacing: 0

                property var days: snoozeDatePicker._buildDays(snoozeDatePicker._viewYear,
                                                               snoozeDatePicker._viewMonth)

                Repeater {
                    model: snoozeDayGrid.days.length
                    delegate: Item {
                        required property int index
                        width:  snoozeContent.width / 7
                        height: 34
                        readonly property var dayInfo: snoozeDayGrid.days[index]

                        Rectangle {
                            anchors.centerIn: parent
                            width: 28; height: 28; radius: 14
                            visible: dayInfo.inMonth
                            color: {
                                if (dayInfo.isSelected) return ThemeManager.accentColor
                                if (snoozeDayMA.containsMouse) return ThemeManager.hoverColor
                                return "transparent"
                            }
                            border.color: {
                                if (dayInfo.isSelected) return ThemeManager.accentColor
                                if (dayInfo.isToday)    return ThemeManager.accentColor
                                return "transparent"
                            }
                            border.width: dayInfo.isToday && !dayInfo.isSelected ? 1 : 0
                        }

                        Text {
                            anchors.centerIn: parent
                            text: dayInfo.inMonth ? dayInfo.day : ""
                            color: {
                                if (!dayInfo.inMonth)   return "transparent"
                                if (dayInfo.isSelected) return "#ffffff"
                                if (dayInfo.isToday)    return ThemeManager.accentColor
                                return ThemeManager.textColor
                            }
                            font.pixelSize: 13
                            font.weight: dayInfo.isToday ? Font.Medium : Font.Normal
                        }

                        MouseArea {
                            id: snoozeDayMA
                            anchors.fill: parent
                            enabled: dayInfo.inMonth
                            cursorShape: Qt.PointingHandCursor
                            hoverEnabled: true
                            onClicked: {
                                var d = new Date(snoozeDatePicker._viewYear,
                                                 snoozeDatePicker._viewMonth,
                                                 dayInfo.day)
                                snoozeDatePicker._pendingDate = snoozeDatePicker._format(d)
                                // Принудительно пересобираем сетку, чтобы isSelected
                                // обновился — Grid не наблюдает за _pendingDate сам.
                                snoozeDayGrid.days = snoozeDatePicker._buildDays(
                                    snoozeDatePicker._viewYear, snoozeDatePicker._viewMonth)
                            }
                        }
                    }
                }

                // Пересборка дней при навигации между месяцами
                Connections {
                    target: snoozeDatePicker
                    function on_ViewYearChanged() {
                        snoozeDayGrid.days = snoozeDatePicker._buildDays(
                            snoozeDatePicker._viewYear, snoozeDatePicker._viewMonth)
                    }
                    function on_ViewMonthChanged() {
                        snoozeDayGrid.days = snoozeDatePicker._buildDays(
                            snoozeDatePicker._viewYear, snoozeDatePicker._viewMonth)
                    }
                }
            }

            // Кнопки «Отмена» / «Применить»
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Spacing.xs
                spacing: 8

                Item { Layout.fillWidth: true }

                Button {
                    id: snoozeCancelBtn
                    text: qsTr("Отмена")
                    flat: true
                    contentItem: Text {
                        text: snoozeCancelBtn.text
                        color: ThemeManager.textColor
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 4
                        color: snoozeCancelBtn.hovered ? ThemeManager.hoverColor : "transparent"
                        border.color: ThemeManager.borderColor
                        border.width: 1
                    }
                    onClicked: snoozeDatePicker.close()
                }

                Button {
                    id: snoozeApplyBtn
                    text: qsTr("Применить")
                    enabled: snoozeDatePicker._pendingDate !== ""
                    contentItem: Text {
                        text: snoozeApplyBtn.text
                        color: "#ffffff"
                        font.pixelSize: 12
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 4
                        color: snoozeApplyBtn.enabled
                               ? ThemeManager.accentColor
                               : Qt.rgba(ThemeManager.accentColor.r,
                                         ThemeManager.accentColor.g,
                                         ThemeManager.accentColor.b, 0.4)
                    }
                    onClicked: snoozeDatePicker._commit()
                }
            }

            Item { height: 2 }
        }
    }

    // ── Parent picker (демоут в подзадачу) ────────────────────────────────
    // Минимальный inline-Popup со списком кандидатов в родители. Кандидаты
    // загружаются через taskViewModel.candidateParents(taskId): тот же
    // проект, без потомков и без подзадач (только верхний уровень).
    // Управление с клавиатуры: ↑/↓ — перемещение, Enter — выбор, Esc — отмена.
    Popup {
        id: parentPicker

        // Идентификатор задачи, для которой выбираем родителя. Устанавливается
        // openForTask() непосредственно перед открытием Popup.
        property string taskId: ""
        property var candidates: []

        function openForTask(tid) {
            taskId = tid
            candidates = taskViewModel.candidateParents(tid)
            parentList.currentIndex = candidates.length > 0 ? 0 : -1
            open()
            Qt.callLater(function() { parentList.forceActiveFocus() })
        }

        function commit(chosenId) {
            if (!taskId || !chosenId) { close(); return }
            taskViewModel.demoteTask(taskId, chosenId)
            close()
        }

        // Render над всем оверлеем, центрируем относительно строки задачи.
        parent: Overlay.overlay
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

        width: 320
        height: Math.min(360, headerLabel.implicitHeight
                              + parentList.contentHeight
                              + Spacing.md * 3)

        onAboutToShow: {
            // Показать прямо под текущей строкой задачи — но если не помещается,
            // прижать к границам оверлея.
            const p = mainRow.mapToItem(Overlay.overlay, 0, mainRow.height)
            const cx = p.x + mainRow.width / 2
            x = Math.max(8,
                         Math.min(cx - width / 2,
                                  Overlay.overlay.width - width - 8))
            y = Math.max(8,
                         Math.min(p.y + 6,
                                  Overlay.overlay.height - height - 8))
        }

        background: Rectangle {
            color:        ThemeManager.surfaceColor
            border.color: ThemeManager.borderColor
            border.width: 1
            radius:       Radius.md
        }

        contentItem: ColumnLayout {
            spacing: Spacing.xs

            Text {
                id: headerLabel
                Layout.fillWidth: true
                text:  qsTr("Выберите родительскую задачу")
                color: ThemeManager.textColor
                font.pixelSize: FontSize.body
                font.bold: true
                elide: Text.ElideRight
            }

            Text {
                Layout.fillWidth: true
                visible: parentPicker.candidates.length === 0
                text:  qsTr("Нет подходящих задач в этом проекте")
                color: ThemeManager.secondaryTextColor
                font.pixelSize: FontSize.caption
                wrapMode: Text.WordWrap
            }

            ListView {
                id: parentList
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: parentPicker.candidates.length > 0
                clip: true
                model: parentPicker.candidates
                focus: true
                keyNavigationEnabled: true
                highlightFollowsCurrentItem: true

                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    width: ListView.view ? ListView.view.width : 0
                    height: 28
                    color: ListView.isCurrentItem
                           ? Qt.rgba(ThemeManager.accentColor.r,
                                     ThemeManager.accentColor.g,
                                     ThemeManager.accentColor.b, 0.18)
                           : (mouseArea.containsMouse
                                ? ThemeManager.hoverColor
                                : "transparent")
                    radius: Radius.sm

                    Text {
                        anchors.fill: parent
                        anchors.leftMargin:  Spacing.sm
                        anchors.rightMargin: Spacing.sm
                        verticalAlignment: Text.AlignVCenter
                        text:  modelData ? (modelData.content || "") : ""
                        color: ThemeManager.textColor
                        font.pixelSize: FontSize.caption
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }

                    MouseArea {
                        id: mouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            parentList.currentIndex = parent.index
                            parentPicker.commit(parent.modelData.id)
                        }
                    }
                }

                Keys.onReturnPressed: {
                    if (currentIndex >= 0 && currentIndex < parentPicker.candidates.length)
                        parentPicker.commit(parentPicker.candidates[currentIndex].id)
                }
                Keys.onEnterPressed: {
                    if (currentIndex >= 0 && currentIndex < parentPicker.candidates.length)
                        parentPicker.commit(parentPicker.candidates[currentIndex].id)
                }
            }
        }
    }

    // ── Bottom border (Q4.01 pixel-perfect) ───────────────────────────────
    // Matches design TaskRow `borderBottom: 1px solid ${t.border}`. Sits as
    // the LAST child of root Item so it paints above any earlier dividers and
    // above the expanded subtaskSection's bottom edge. Hidden for subtasks —
    // the subtask list inside an expanded parent must not be sliced by
    // per-row separators.
    Rectangle {
        anchors.left:   parent.left
        anchors.right:  parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: ThemeManager.borderColor
        visible: !root.isSubtask
    }
}
