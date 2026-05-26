import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window
import QtCore
import OpenTodoUI

Item {
    id: root

    // All colors come from ThemeManager — switching the active theme
    // re-binds every consumer automatically.
    readonly property bool  isDarkTheme:    ThemeManager.isDark
    readonly property color accentRed:     ThemeManager.accentColor
    readonly property color bgColor:        ThemeManager.backgroundColor
    readonly property color contentColor:   ThemeManager.surfaceColor
    readonly property color borderColor:    ThemeManager.borderColor
    readonly property color textPrimary:    ThemeManager.textColor
    readonly property color textSecondary:  ThemeManager.secondaryTextColor

    readonly property int sidebarWidth:      220
    readonly property int taskItemHeight:    36
    readonly property int taskItemPadding:   Spacing.md
    readonly property int taskEditorWidth:   400
    readonly property int animationDuration: Duration.normal

    // ── Responsive breakpoints ───────────────────────────────────────────────
    //   ≥ 800 px            full layout — inline sidebar, full top bar
    //   500 ≤ w < 800 px    "compact" — inline sidebar hidden; hamburger opens
    //                       sidebar as overlay drawer; top bar drops the
    //                       expand/collapse buttons & the today-goal pill but
    //                       keeps search + sync indicator.
    //   < 500 px            "ultra-compact" — same overlay drawer; top bar
    //                       drops the search bar (search reachable via Ctrl+F
    //                       palette path) and the sync indicator label so only
    //                       hamburger + view title remain. Window minimumWidth
    //                       in Main.qml allows down to 200 px.
    //
    // We keep `isNarrow` as the alias the existing drawer-overlay code uses:
    // ANY width below 800 px should trigger the overlay-sidebar codepath.
    readonly property int  narrowBreakpoint:      800
    readonly property int  ultraCompactBreakpoint: 500
    readonly property bool _narrowUltraCompact:   width > 0 && width < ultraCompactBreakpoint
    readonly property bool _narrowCompact:        width >= ultraCompactBreakpoint && width < narrowBreakpoint
    // Backwards-compatible alias used by the overlay-drawer code below; covers
    // both compact bands (anything < 800 px).
    readonly property bool isNarrow:              width > 0 && width < narrowBreakpoint

    // ── Upcoming calendar panel breakpoint ───────────────────────────────────
    // At ≥ 900 px the calendar is a fixed right-side panel; below that it
    // collapses to a strip above the task list.
    readonly property int  calendarPanelBreakpoint: 900
    readonly property bool _calendarWide: width >= calendarPanelBreakpoint

    // ITER2 #8: track whether the last sidebar close was auto-triggered by
    // entering narrow mode. Only restore on exit if WE closed it — not if the
    // user closed it manually on a wide screen before resizing down and back.
    property bool _autoCollapsed: false

    // ── Режим фокуса ─────────────────────────────────────────────────────────
    // F11 toggles a distraction-free reading mode that hides the sidebar and
    // the top toolbar so only the task list remains visible. Persisted in
    // QSettings under "ui/focusMode" so the state survives restarts.
    property bool _focusMode: false

    Settings {
        id: focusSettings
        category: "ui"
        property bool focusMode: false
    }

    Component.onCompleted: {
        root._focusMode = focusSettings.focusMode
    }

    function _toggleFocusMode() {
        root._focusMode = !root._focusMode
        focusSettings.focusMode = root._focusMode
        if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
            telemetry.trackEvent("focus_mode_toggled", { "enabled": root._focusMode })
    }

    // Auto-collapse the inline sidebar when entering narrow mode and
    // auto-restore on exit. The user can still toggle manually via the
    // hamburger.
    onIsNarrowChanged: {
        if (isNarrow && sidebarViewModel.sidebarVisible) {
            _autoCollapsed = true
            sidebarViewModel.toggleSidebar()
        } else if (!isNarrow && !sidebarViewModel.sidebarVisible && _autoCollapsed) {
            // ITER3 #2: toggle first; the flag is cleared in onSidebarVisibleChanged
            // after confirming the sidebar is now visible — prevents the flag from
            // being lost if toggleSidebar() fails before the state change.
            sidebarViewModel.toggleSidebar()
        }
    }

    // ITER3 #2: reset _autoCollapsed only after the sidebar reports it is visible,
    // so the guard is not lost if toggleSidebar() fails partway through.
    Connections {
        target: sidebarViewModel
        function onSidebarVisibleChanged() {
            if (sidebarViewModel.sidebarVisible && _autoCollapsed)
                _autoCollapsed = false
        }
    }

    // True when the user navigated to a full-page view (Settings / Productivity /
    // Notifications / AI / Project / Board / Calendar). ITER2 #24: Board and
    // Calendar were missing — switching to Calendar showed the plain inbox list.
    readonly property bool isFullPageView:
        taskViewModel.filterMode === TaskViewModel.Settings
        || taskViewModel.filterMode === TaskViewModel.Productivity
        || taskViewModel.filterMode === TaskViewModel.Notifications
        || taskViewModel.filterMode === TaskViewModel.AI
        || taskViewModel.filterMode === TaskViewModel.Project
        || taskViewModel.filterMode === TaskViewModel.Board
        || taskViewModel.filterMode === TaskViewModel.Calendar
        || taskViewModel.filterMode === TaskViewModel.Trash

    // ── Quick filter chip state ───────────────────────────────────────────────
    // Tracks which chip is currently active ("today" | "overdue" | "no_date" | "").
    property string _quickChip: ""
    // Mode/filter that were active before the chip was applied, so we can
    // restore them when the chip is toggled off.
    property int    _chipSavedMode:   TaskViewModel.Inbox
    property string _chipSavedFilter: ""
    // Guard flag: true while we are ourselves mutating filterMode/currentFilter
    // to apply or clear a chip.  Prevents the filterModeChanged handler below
    // from treating our own change as external navigation.
    property bool   _chipApplying: false

    // Maps chip id → DSL token understood by FilterEvaluator.
    function _chipDslToken(chipId) {
        if (chipId === "today")   return "today"
        if (chipId === "overdue") return "overdue"
        if (chipId === "no_date") return "no date"
        return ""
    }

    // Apply a chip: save current mode/filter, then switch to Filter mode with
    // the appropriate DSL token.
    function _applyChip(chipId) {
        _chipApplying = true
        _chipSavedMode   = taskViewModel.filterMode
        _chipSavedFilter = taskViewModel.currentFilter
        taskViewModel.filterMode    = TaskViewModel.Filter
        taskViewModel.currentFilter = _chipDslToken(chipId)
        _quickChip   = chipId
        _chipApplying = false
    }

    // Clear the active chip and restore the previous view.
    function _clearChip() {
        if (_quickChip === "") return
        _chipApplying = true
        taskViewModel.filterMode    = _chipSavedMode
        taskViewModel.currentFilter = _chipSavedFilter
        _quickChip    = ""
        _chipApplying = false
    }

    // Toggle: clicking the active chip clears it; clicking another activates it.
    function _toggleChip(chipId) {
        if (_quickChip === chipId)
            _clearChip()
        else
            _applyChip(chipId)
    }

    // ── Upcoming calendar → task list scroll ──────────────────────────────────
    // Called when the user picks a day in the UpcomingCalendarPanel.
    // Finds the first row in taskListView whose section matches the ISO date
    // and positions the list to that row.  If no task falls on that date the
    // day is still highlighted in the panel (no scroll needed).
    function _scrollToUpcomingDate(picked) {
        var iso = Qt.formatDate(picked, "yyyy-MM-dd")
        // Walk the visible model rows looking for a matching dueDate.
        for (var i = 0; i < taskViewModel.count; i++) {
            var t = taskViewModel.taskAt(i)
            if (t && t.dueDate === iso) {
                taskListView.positionViewAtIndex(i, ListView.Beginning)
                return
            }
        }
        // No exact match — try the first task on or after the picked date.
        for (var j = 0; j < taskViewModel.count; j++) {
            var tj = taskViewModel.taskAt(j)
            if (tj && tj.dueDate >= iso) {
                taskListView.positionViewAtIndex(j, ListView.Beginning)
                return
            }
        }
    }

    // When the sidebar (or any other external code) drives a navigation, the
    // filterMode changes without going through our _applyChip/_clearChip paths.
    // Detect this and silently reset chip state so the bar doesn't look stuck.
    Connections {
        target: taskViewModel
        function onFilterModeChanged() {
            if (!root._chipApplying && root._quickChip !== "")
                root._quickChip = ""
            // Reset inbox-zero tracking on view switch so we don't fire for a
            // view we never observed in a non-empty state.
            root._lastVisibleCount = -1
            root._lastFilterMode = taskViewModel.filterMode + ""
        }
    }

    // ── Inbox-zero / Today-zero celebration ───────────────────────────────────
    // When the visible task list in Inbox or Today drops to 0 after having
    // been >0, fire the confetti overlay and a Russian toast.  Throttled to
    // once per (filterMode, day) via persistent QSettings so reopening the
    // app on the same day doesn't replay the burst.
    property int    _lastVisibleCount: -1
    property string _lastFilterMode:   ""

    Settings {
        id: zeroCelebSettings
        category: "celebrations"
        property string lastInboxZeroDate: ""
        property string lastTodayZeroDate: ""
    }

    Connections {
        target: taskViewModel
        function onCountChanged()    { root._checkInboxZero() }
        function onTaskChanged(id)   { root._checkInboxZero() }
        function onDataChanged()     { root._checkInboxZero() }
    }

    function _checkInboxZero() {
        const mode = taskViewModel.filterMode
        if (mode !== TaskViewModel.Inbox && mode !== TaskViewModel.Today) {
            root._lastVisibleCount = -1
            return
        }

        const currentCount = taskViewModel.count
        const prev = root._lastVisibleCount
        // Always update tracker first so re-entrancy is harmless.
        root._lastVisibleCount = currentCount
        root._lastFilterMode = mode + ""

        if (!(prev > 0 && currentCount === 0))
            return

        const today = Qt.formatDate(new Date(), "yyyy-MM-dd")
        const isInbox = (mode === TaskViewModel.Inbox)

        if (isInbox) {
            if (zeroCelebSettings.lastInboxZeroDate === today) return
            zeroCelebSettings.lastInboxZeroDate = today
        } else {
            if (zeroCelebSettings.lastTodayZeroDate === today) return
            zeroCelebSettings.lastTodayZeroDate = today
        }

        inboxZeroConfetti.celebrate()

        const msg = isInbox ? qsTr("🎉 Все задачи выполнены!")
                            : qsTr("🎉 Всё на сегодня сделано!")
        const w = root.Window.window
        if (w && typeof w.showToast === "function")
            w.showToast(msg, "success")

        if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled) {
            telemetry.trackEvent("inbox_zero_celebrated",
                                 { "mode": isInbox ? "inbox" : "today" })
        }
    }

    // ── Today-goal refresh kick ───────────────────────────────────────────────
    // Q3.24: debounced via a 50 ms Timer so that rapid task edits (e.g. bulk
    // complete, drag-reorder) coalesce into a single _completedToday recompute.
    // Previously this fired on every tasksChanged, causing an O(N) dataStore
    // scan on each individual keystroke / checkbox tap.
    property int todayGoalKick: 0
    Timer {
        id: todayGoalDebounce
        interval: 50
        repeat:   false
        onTriggered: root.todayGoalKick = (root.todayGoalKick + 1) % 10000
    }
    Connections {
        target: dataStore
        function onTasksChanged() { todayGoalDebounce.restart() }
    }

    // ── Keyboard navigation state ─────────────────────────────────────────────
    property int currentNavIndex: -1

    function navigateSelection(delta) {
        const total = taskListView.count
        if (total === 0) return
        let newIdx = (currentNavIndex < 0) ? (delta > 0 ? 0 : total - 1)
                                           : currentNavIndex + delta
        newIdx = Math.max(0, Math.min(total - 1, newIdx))
        currentNavIndex = newIdx
        // B-6.25: QAbstractListModel::index() is protected and unreachable from QML;
        // use the Q_INVOKABLE taskAt() instead.
        const task = taskViewModel.taskAt(newIdx)
        if (task) taskViewModel.hoveredTaskId = task.id
        taskListView.positionViewAtIndex(newIdx, ListView.Contain)
    }

    function focusSearch() {
        if (searchBarLoader.item) {
            searchBarLoader.item.expanded = true
            searchBarLoader.item.forceActiveFocus()
        }
    }

    anchors.fill: parent

    Rectangle {
        anchors.fill: parent
        color: bgColor
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ── Sidebar (inline, only used in wide mode) ─────────────────────────
        Item {
            id: sidebarContainer
            objectName: "sidebarContainer"
            Layout.fillHeight: true
            // Hidden in narrow mode (BOTH compact bands) — narrow mode uses the
            // overlay drawer. Также полностью скрывается в режиме фокуса (F11).
            // visible is bound to (preferredWidth > 0) so the test that probes
            // the visible flag at width 200 / 500 / 800 sees the correct value.
            Layout.preferredWidth: (root._focusMode || root.isNarrow || !sidebarViewModel.sidebarVisible)
                                   ? 0 : sidebarWidth
            visible: !root._focusMode && Layout.preferredWidth > 0
            clip: true

            Behavior on Layout.preferredWidth {
                NumberAnimation {
                    duration: animationDuration
                    easing.type: Easing.InOutQuad
                }
            }

            Rectangle {
                anchors.fill: parent
                color: contentColor

                Loader {
                    id: sidebarLoader
                    anchors.fill: parent
                    // Width is clamped so the loader never renders a squashed component
                    visible: sidebarContainer.Layout.preferredWidth > 0
                    source: "../components/SidebarContent.qml"
                }
            }
        }

        // ── Vertical separator ────────────────────────────────────────────────
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            color: borderColor
            visible: !root._focusMode
            // Fade out alongside the sidebar
            opacity: sidebarViewModel.sidebarVisible ? 1.0 : 0.0
            Behavior on opacity {
                NumberAnimation { duration: animationDuration }
            }
        }

        // ── Main content area ─────────────────────────────────────────────────
        Item {
            id: mainContent
            objectName: "mainContent"
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // ── Top bar ───────────────────────────────────────────────────
                Rectangle {
                    id: topBar
                    objectName: "topBar"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    color: bgColor
                    // Скрывается в режиме фокуса (F11) — задачи без отвлекающих элементов.
                    visible: !root._focusMode

                    // Bottom border
                    Rectangle {
                        anchors.left:   parent.left
                        anchors.right:  parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: borderColor
                    }

                    RowLayout {
                        anchors {
                            fill: parent
                            leftMargin: taskItemPadding
                            rightMargin: taskItemPadding
                        }
                        spacing: 14

                        // Hamburger / sidebar toggle
                        ToolButton {
                            id: sidebarToggleButton
                            objectName: "sidebarToggleButton"
                            // Q4.01 pixel-perfect: дизайн использует stroke SVG MenuIcon
                            // вместо юникод-символа ☰ (см. .design-temp/.../app-shell.jsx
                            // строка 120). Иконка size 18, цвет — themed iconColor.
                            contentItem: IconSvg {
                                anchors.centerIn: parent
                                name: "menu"
                                size: 18
                                color: ThemeManager.iconColor
                            }
                            onClicked: sidebarViewModel.toggleSidebar()
                            KeyNavigation.tab: searchBarLoader  // F-6.96 a11y

                            // ITER2 #23: icon-only button — provide accessible role + name.
                            Accessible.role: Accessible.Button
                            Accessible.name: qsTr("Меню")

                            ToolTip.visible: hovered
                            // In narrow modes (compact + ultra-compact) the hamburger opens the
                            // overlay drawer; in wide mode it toggles the inline sidebar.
                            ToolTip.text: root.isNarrow
                                          ? qsTr("Открыть боковую панель")
                                          : (sidebarViewModel.sidebarVisible
                                             ? qsTr("Скрыть боковую панель")
                                             : qsTr("Показать боковую панель"))
                        }

                        // Q4.01 (redesign): icon prefix for the view title.
                        // Mirrors the icon highlighted in the sidebar so that the
                        // top bar (which is the only chrome the user sees in
                        // ultra-compact mode) still communicates "where am I"
                        // without forcing them to open the drawer.
                        IconSvg {
                            id: viewTitleIcon
                            name: {
                                switch (taskViewModel.filterMode) {
                                case TaskViewModel.Inbox:         return "inbox"
                                case TaskViewModel.Today:         return "today"
                                case TaskViewModel.Upcoming:      return "upcoming"
                                case TaskViewModel.Calendar:      return "upcoming"
                                case TaskViewModel.Label:         return "label"
                                case TaskViewModel.Filter:        return "filter"
                                case TaskViewModel.Completed:     return "check-circle"
                                case TaskViewModel.Settings:      return "settings"
                                case TaskViewModel.Productivity:  return "productivity"
                                case TaskViewModel.Notifications: return "bell"
                                case TaskViewModel.AI:            return "sparkles"
                                case TaskViewModel.Trash:         return "trash"
                                default:                          return ""
                                }
                            }
                            size: 18
                            color: ThemeManager.iconColor
                            visible: name !== "" && taskViewModel.filterMode !== TaskViewModel.Project
                        }

                        // Current view title — derived from the active filter mode.
                        Label {
                            id: viewTitleLabel
                            text: {
                                switch (taskViewModel.filterMode) {
                                case TaskViewModel.Inbox:     return qsTr("Входящие")
                                case TaskViewModel.Today:     return qsTr("Сегодня")
                                case TaskViewModel.Upcoming:  return qsTr("Предстоящие")
                                // Q3.27: standalone Calendar = calendar display
                                // of Upcoming. Reuse the same title so the
                                // toggle feels like one logical view.
                                case TaskViewModel.Calendar:
                                    return taskViewModel.currentProjectId === ""
                                           ? qsTr("Предстоящие")
                                           : qsTr("Календарь")
                                case TaskViewModel.Project: {
                                    const p = projectViewModel ? projectViewModel.projectById(taskViewModel.currentProjectId) : null
                                    return p ? p.name : qsTr("Проект")
                                }
                                // ITER1 #25: resolve internal label name to display name via dataStore
                                case TaskViewModel.Label: {
                                    const lbl = dataStore.labelByName(taskViewModel.currentFilter)
                                    return qsTr("Метка: %1").arg(lbl ? lbl.name : (taskViewModel.currentFilter || ""))
                                }
                                case TaskViewModel.Filter:        return qsTr("Фильтр")
                                case TaskViewModel.Completed:     return qsTr("Завершённые")
                                case TaskViewModel.Settings:      return qsTr("Настройки")
                                case TaskViewModel.Productivity:  return qsTr("Продуктивность")
                                case TaskViewModel.Notifications: return qsTr("Уведомления")
                                // Q4.01 (redesign): эмодзи-префиксы убраны —
                                // иконку секции уже несёт сайдбар, в шапке
                                // достаточно чистого названия.
                                case TaskViewModel.AI:            return qsTr("Помощник")
                                case TaskViewModel.Trash:         return qsTr("Корзина")
                                default:                          return qsTr("Задачи")
                                }
                            }
                            font.pixelSize: FontSize.subtitleSmall
                            font.weight: Font.Medium
                            color: textPrimary
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            Layout.minimumWidth: 50  // allow shrinking before clipping siblings
                        }

                        // ── Upcoming view-style toggle (list ↔ calendar) ──────
                        // Q3.27: surfaces only when the user is in Upcoming or
                        // standalone Calendar (project-less). Mirrors the
                        // list/board/calendar toggle in ProjectView so the two
                        // displays of the same task set ("upcoming, undone") are
                        // one click apart.
                        Row {
                            visible: !root.isNarrow
                                     && (taskViewModel.filterMode === TaskViewModel.Upcoming
                                         || (taskViewModel.filterMode === TaskViewModel.Calendar
                                             && taskViewModel.currentProjectId === ""))
                            spacing: 2
                            Layout.alignment: Qt.AlignVCenter

                            ToolButton {
                                text: "≡"
                                font.pixelSize: 16
                                checked: taskViewModel.filterMode === TaskViewModel.Upcoming
                                ToolTip.text: qsTr("Список")
                                ToolTip.visible: hovered
                                Material.foreground: checked ? accentRed : textSecondary
                                onClicked: taskViewModel.filterMode = TaskViewModel.Upcoming
                            }
                            ToolButton {
                                checked: taskViewModel.filterMode === TaskViewModel.Calendar
                                ToolTip.text: qsTr("Календарь")
                                ToolTip.visible: hovered
                                // Task 17cea65b iter-3: было unicode "📅".
                                contentItem: IconSvg {
                                    anchors.centerIn: parent
                                    name: "calendar"
                                    size: 18
                                    color: parent.checked ? accentRed : textSecondary
                                }
                                onClicked: {
                                    // Drop project context so the C++ filter takes the
                                    // upcoming-tasks branch (taskviewmodel.cpp Q3.27).
                                    taskViewModel.currentProjectId = ""
                                    taskViewModel.filterMode = TaskViewModel.Calendar
                                }
                            }
                        }

                        // ── Today goal progress pill ──────────────────────────
                        // Hidden in any narrow band — the pill needs ~110 px and
                        // would crowd out the view title at sub-800 widths.
                        Rectangle {
                            id: todayGoalIndicator
                            visible: settingsViewModel.dailyGoal > 0
                                     && !root.isFullPageView
                                     && !root.isNarrow
                            Layout.preferredWidth: 110
                            Layout.preferredHeight: 24
                            Layout.alignment: Qt.AlignVCenter
                            radius: 12
                            color: Qt.rgba(ThemeManager.textColor.r,
                                           ThemeManager.textColor.g,
                                           ThemeManager.textColor.b, 0.06)

                            readonly property int _completedToday: {
                                // Read kick to register dependency on tasksChanged
                                const _ = root.todayGoalKick
                                if (typeof dataStore === "undefined" || !dataStore) return 0
                                const tasks = dataStore.tasksForToday()
                                let done = 0
                                for (let i = 0; i < tasks.length; i++)
                                    if (tasks[i] && tasks[i].isCompleted) done++
                                return done
                            }
                            readonly property int  _goal:     settingsViewModel.dailyGoal
                            readonly property bool _achieved: _completedToday >= _goal

                            // Progress fill bar
                            Rectangle {
                                anchors.left:   parent.left
                                anchors.top:    parent.top
                                anchors.bottom: parent.bottom
                                anchors.margins: 2
                                width: parent._goal > 0
                                    ? Math.min(parent.width - 4,
                                               (parent.width - 4) * parent._completedToday / parent._goal)
                                    : 0
                                radius: parent.radius - 2
                                color: parent._achieved ? "#2e8b57" : ThemeManager.accentColor
                                Behavior on width { NumberAnimation { duration: 300 } }
                            }

                            // Q4.01 (redesign): emoji "🎯 " prefix replaced with stroke IconSvg
                            // so the badge reads consistently across themes (Material's
                            // default emoji font rendered the target as a flat orange disk on
                            // the dark themes, breaking the streak feel).
                            Row {
                                anchors.centerIn: parent
                                spacing: 4
                                IconSvg {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: "flame"
                                    size: 12
                                    color: todayGoalIndicator._completedToday > 0 ? "#ffffff" : "#e05050"
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: todayGoalIndicator._completedToday + " / " + todayGoalIndicator._goal
                                    font.pixelSize: FontSize.caption
                                    font.bold: todayGoalIndicator._completedToday > 0
                                    color: todayGoalIndicator._completedToday > 0 ? "#ffffff" : ThemeManager.textColor
                                }
                            }

                            HoverHandler { id: tdgHover }
                            ToolTip.visible: tdgHover.hovered
                            ToolTip.delay: 500
                            ToolTip.text: parent._achieved
                                ? qsTr("Дневная цель выполнена! 🎉")
                                : qsTr("До дневной цели осталось: %1").arg(parent._goal - parent._completedToday)
                        }

                        // Search bar — shrinks at narrow widths, hidden in ultra-compact.
                        // The search palette (Ctrl+F / Ctrl+K) remains reachable.
                        // Q4.01 pixel-perfect: дизайн search 260×28.
                        Loader {
                            id: searchBarLoader
                            source: "../components/SearchBar.qml"
                            visible: !root._narrowUltraCompact
                            Layout.preferredWidth: root._narrowCompact ? 140 : 260
                            Layout.minimumWidth: root._narrowCompact ? 100 : 120
                            Layout.preferredHeight: 28
                        }

                        // Hide-completed toggle — not shown in full-page views or compact modes.
                        // Q4.01 (redesign): was "👁" / "🙈" emoji, now stroke
                        // IconSvg eye/eye-off pair so the toggle matches the rest of the topbar
                        // glyphs and re-tints under each theme.
                        ToolButton {
                            id: hideCompletedBtn
                            visible: !root.isFullPageView && !root.isNarrow
                            flat: true
                            contentItem: IconSvg {
                                name: taskViewModel.showCompleted ? "eye" : "eye-off"
                                size: 18
                                color: ThemeManager.iconColor
                                anchors.centerIn: parent
                            }
                            ToolTip.text: taskViewModel.showCompleted
                                ? qsTr("Скрыть выполненные")
                                : qsTr("Показать выполненные")
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            onClicked: {
                                taskViewModel.showCompleted = !taskViewModel.showCompleted
                                if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                                    telemetry.trackEvent("toggle_show_completed",
                                                         { "showing": taskViewModel.showCompleted })
                            }
                        }

                        // Bulk expand/collapse subtasks — bumps tokens on the
                        // shared "TaskItemExpansion" Settings category. Each
                        // TaskItem listens via Connections and reacts.
                        Settings {
                            id: bulkExpandSettings
                            category: "TaskItemExpansion"
                            property int expandAllToken: 0
                            property int collapseAllToken: 0
                        }
                        // Iter-night-24: было unicode "📂" (companion to the
                        // already-migrated 📁 collapse-all button below).
                        ToolButton {
                            id: expandAllBtn
                            visible: !root.isFullPageView && !root.isNarrow
                            flat: true
                            contentItem: IconSvg {
                                anchors.centerIn: parent
                                name: "folder"
                                size: 18
                                color: textSecondary
                            }
                            ToolTip.text: qsTr("Развернуть подзадачи у всех задач")
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            onClicked: {
                                bulkExpandSettings.expandAllToken = bulkExpandSettings.expandAllToken + 1
                                if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                                    telemetry.trackEvent("subtasks_bulk_expand", {})
                            }
                        }
                        ToolButton {
                            id: collapseAllBtn
                            visible: !root.isFullPageView && !root.isNarrow
                            flat: true
                            ToolTip.text: qsTr("Свернуть подзадачи у всех задач")
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            // Task 17cea65b iter-3: было unicode "📁".
                            contentItem: IconSvg {
                                anchors.centerIn: parent
                                name: "folder"
                                size: 18
                                color: textSecondary
                            }
                            onClicked: {
                                bulkExpandSettings.collapseAllToken = bulkExpandSettings.collapseAllToken + 1
                                if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                                    telemetry.trackEvent("subtasks_bulk_collapse", {})
                            }
                        }

                        // Sync indicator: pin to a width that fits the longest
                        // localized status string ("Синхронизировано" ~ 110 px +
                        // 16 icon + 6 spacing + 16 padding ≈ 148 px). Capping at
                        // 110 made the content overflow the layout cell and
                        // collide with the right window edge.
                        // Hidden entirely in ultra-compact mode (< 500 px) to make
                        // room for the title; the user can still see sync status
                        // via the global toast/error banner.
                        Loader {
                            id: syncIndicatorLoader
                            source: "../components/SyncIndicator.qml"
                            visible: !root._narrowUltraCompact
                            Layout.preferredWidth: root._narrowCompact ? 100 : 160
                            Layout.maximumWidth: root._narrowCompact ? 100 : 160
                            Layout.preferredHeight: 32
                            Layout.rightMargin: Spacing.sm
                        }
                    }
                }

                // ── Main content: either task area, or a full-page view ─────
                Loader {
                    id: fullPageLoader
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    active: isFullPageView
                    visible: active
                    source: {
                        switch (taskViewModel.filterMode) {
                        case TaskViewModel.Settings:      return "../views/SettingsView.qml"
                        case TaskViewModel.Productivity:  return "../views/ProductivityView.qml"
                        case TaskViewModel.Notifications: return "../views/NotificationsView.qml"
                        case TaskViewModel.AI:            return "../views/AIView.qml"
                        case TaskViewModel.Project:       return "../views/ProjectView.qml"
                        // ITER2 #24: Board and Calendar were unreachable — they
                        // need the same full-page treatment as ProjectView.
                        case TaskViewModel.Board:         return "../views/BoardView.qml"
                        case TaskViewModel.Calendar:      return "../views/CalendarView.qml"
                        case TaskViewModel.Trash:         return "../views/TrashView.qml"
                        }
                        return ""
                    }

                    // Bug 1 fix: forward openTaskRequested from NotificationsView
                    // to taskViewModel so clicking a notification navigates to its task.
                    // Q3.27: also forward taskClicked from BoardView so a click on a
                    // card opens the right-pane editor (matches list-mode behaviour).
                    Connections {
                        target: fullPageLoader.item
                        ignoreUnknownSignals: true
                        function onOpenTaskRequested(taskId) {
                            if (taskId && taskId.length > 0)
                                taskViewModel.openTaskById(taskId)
                        }
                        function onTaskClicked(taskId) {
                            if (taskId && taskId.length > 0)
                                taskViewModel.openTaskById(taskId)
                        }
                    }
                }

                // ── Task area (list + optional editor) ───────────────────────
                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0
                    visible: !isFullPageView

                    // ── Task list column ──────────────────────────────────────
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 0

                        // ── Upcoming calendar strip (narrow mode, < 900 px) ───
                        // In wide mode the calendar is a right-side panel (added
                        // further down in this RowLayout).  In narrow mode it
                        // sits here as a collapsible horizontal strip above the
                        // quick-add bar.
                        UpcomingCalendarPanel {
                            id: upcomingCalendarStrip
                            Layout.fillWidth: true
                            Layout.preferredHeight: implicitHeight

                            // Only visible in Upcoming + narrow mode
                            visible: taskViewModel.filterMode === TaskViewModel.Upcoming
                                     && !root._calendarWide

                            widePanel: false

                            onDayPicked: function(picked) {
                                root._scrollToUpcomingDate(picked)
                            }
                        }

                        // Quick-add bar — height is reactive: tracks the
                        // QuickAdd's animated height (56 collapsed → 176
                        // expanded) so the toolbar row is visible when the
                        // user focuses the input.
                        Rectangle {
                            id: quickAddBar
                            Layout.fillWidth: true
                            Layout.preferredHeight: (quickAddLoader.item
                                                     ? quickAddLoader.item.height
                                                     : 56) + 12
                            color: contentColor

                            Behavior on Layout.preferredHeight {
                                NumberAnimation {
                                    duration: animationDuration
                                    easing.type: Easing.InOutQuad
                                }
                            }

                            Rectangle {
                                anchors.left:   parent.left
                                anchors.right:  parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                color: borderColor
                            }

                            Loader {
                                id: quickAddLoader
                                anchors {
                                    left:        parent.left
                                    right:       parent.right
                                    top:         parent.top
                                    leftMargin:  taskItemPadding
                                    rightMargin: taskItemPadding
                                    topMargin:   6
                                }
                                // Don't fill — let the inner QuickAdd own its height.
                                height: item ? item.height : 56
                                source: "../components/QuickAdd.qml"
                            }
                        }

                        // ── Quick filter chips ────────────────────────────────
                        // Visible in list-based views only (not full-page views).
                        // Shows three one-tap filter chips: Сегодня / Просрочено /
                        // Без даты. Tapping the active chip deactivates it.
                        Rectangle {
                            id: quickFilterBar
                            Layout.fillWidth: true
                            Layout.preferredHeight: 40
                            color: ThemeManager.surfaceColor
                            visible: !root.isFullPageView

                            // Bottom border
                            Rectangle {
                                anchors.left:   parent.left
                                anchors.right:  parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                color: ThemeManager.borderColor
                            }

                            RowLayout {
                                anchors {
                                    fill: parent
                                    leftMargin: taskItemPadding
                                    rightMargin: taskItemPadding
                                }
                                spacing: Spacing.sm

                                // Three chip definitions — modelData exposed by Repeater.
                                // Q4.01 (redesign): добавлено поле iconName — имя SVG
                                // в resources/icons/ui/. Делегат рендерит IconSvg если
                                // iconName есть, иначе fallback на icon-emoji.
                                Repeater {
                                    // Q4.01 (redesign): icons match design canvas filter chips —
                                    // calendar / red flame / clock. iconColor lets "Просрочено"
                                    // render its flame in design's "#e05050" red instead of the
                                    // default themed icon colour.
                                    model: [
                                        { chipId: "today",   label: qsTr("Сегодня"),    icon: "📅", iconName: "today", iconColor: "" },
                                        { chipId: "overdue", label: qsTr("Просрочено"), icon: "🔥", iconName: "flame", iconColor: "#e05050" },
                                        { chipId: "no_date", label: qsTr("Без даты"),   icon: "🕒", iconName: "clock", iconColor: "" }
                                    ]

                                    delegate: Rectangle {
                                        required property var modelData

                                        readonly property bool _active: root._quickChip === modelData.chipId

                                        Layout.preferredHeight: 24
                                        Layout.preferredWidth:  _chipLabel.implicitWidth + _chipIcon.implicitWidth + Spacing.sm + 24
                                        radius: 16

                                        // Q4.01 pixel-perfect: chip bg всегда surface (см.
                                        // .design-temp/.../view-others.jsx InboxView). Active
                                        // state выделяется tinted border, а не сменой bg.
                                        color: ThemeManager.surfaceColor
                                        border.color: _active ? ThemeManager.accentColor : ThemeManager.borderColor
                                        border.width: 1

                                        Behavior on color {
                                            ColorAnimation { duration: Duration.fast }
                                        }

                                        RowLayout {
                                            anchors.centerIn: parent
                                            spacing: 6

                                            // Q4.01 (redesign): IconSvg для chip-ов
                                            // с iconName, fallback на эмодзи иначе.
                                            IconSvg {
                                                id: _chipIcon
                                                visible: modelData.iconName !== ""
                                                name: modelData.iconName
                                                size: 12
                                                // Q4.01 pixel-perfect: icon сохраняет свой
                                                // цвет даже в active (red flame остаётся red).
                                                color: modelData.iconColor && modelData.iconColor !== ""
                                                    ? modelData.iconColor
                                                    : ThemeManager.textColor
                                                Layout.alignment: Qt.AlignVCenter
                                            }

                                            Text {
                                                visible: modelData.iconName === ""
                                                text: modelData.icon
                                                font.pixelSize: FontSize.small
                                                verticalAlignment: Text.AlignVCenter
                                            }

                                            Text {
                                                id: _chipLabel
                                                text: modelData.label
                                                font.pixelSize: FontSize.small
                                                font.bold: parent.parent._active
                                                color: ThemeManager.textColor
                                                verticalAlignment: Text.AlignVCenter

                                                Behavior on color {
                                                    ColorAnimation { duration: Duration.fast }
                                                }
                                            }
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root._toggleChip(modelData.chipId)
                                        }

                                        HoverHandler { id: _chipHover }

                                        // HoverHandler exposes 'hovered', not 'containsMouse' (the
                                        // latter is a MouseArea property). Using the wrong name
                                        // returned undefined and emitted "Unable to assign
                                        // [undefined] to bool" 3× per Repeater chip on every load.
                                        ToolTip.visible: _chipHover.hovered
                                        ToolTip.text: _active
                                            ? qsTr("Сбросить фильтр «%1»").arg(modelData.label)
                                            : qsTr("Показать только: %1").arg(modelData.label)
                                    }
                                }

                                // Flexible spacer — pushes the clear button to the right
                                Item { Layout.fillWidth: true }

                                // "Сбросить" button — only visible while a chip is active.
                                // Task 17cea65b iter-3: было unicode "✕".
                                ToolButton {
                                    visible: root._quickChip !== ""
                                    flat: true
                                    Layout.preferredHeight: 26
                                    Layout.preferredWidth: 28

                                    contentItem: IconSvg {
                                        anchors.centerIn: parent
                                        name: "close"
                                        size: 12
                                        color: ThemeManager.secondaryTextColor
                                    }

                                    onClicked: root._clearChip()

                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Сбросить быстрый фильтр")

                                    // Subtle fade-in/out
                                    opacity: visible ? 1.0 : 0.0
                                    Behavior on opacity {
                                        NumberAnimation { duration: Duration.fast }
                                    }
                                }
                            }
                        }

                        // Task list
                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            Rectangle {
                                anchors.fill: parent
                                color: bgColor

                                ListView {
                                    id: taskListView
                                    anchors {
                                        fill: parent
                                        leftMargin: 0
                                        rightMargin: 0
                                    }
                                    model: taskViewModel
                                    clip: true
                                    spacing: 0
                                    boundsBehavior: Flickable.StopAtBounds

                                    // ── Day-group section headers (Upcoming mode) ─────────────
                                    // In Upcoming mode, group by dueDate (ISO "yyyy-MM-dd").
                                    // In all other modes, set section.property to "" which
                                    // disables grouping entirely (Qt ListView skips delegate
                                    // creation when section.property is empty).
                                    section.property: taskViewModel.filterMode === TaskViewModel.Upcoming
                                                      ? "dueDate" : ""
                                    section.criteria: ViewSection.FullString
                                    // CurrentLabelAtStart pins the current day header to the
                                    // top of the viewport while scrolling within that day's
                                    // tasks (true sticky behaviour). InlineLabels keeps normal
                                    // in-flow headers at each day boundary.
                                    section.labelPositioning: ViewSection.CurrentLabelAtStart
                                                              | ViewSection.InlineLabels

                                    section.delegate: Rectangle {
                                        id: dayGroupHeader
                                        required property string section

                                        width:          ListView.view.width
                                        implicitWidth:  width
                                        height:         36
                                        implicitHeight: height

                                        // Semi-opaque surface colour so the sticky header
                                        // visually separates from the list rows beneath it
                                        // without being fully opaque (blends with bg on scroll).
                                        color: Qt.rgba(ThemeManager.surfaceColor.r,
                                                       ThemeManager.surfaceColor.g,
                                                       ThemeManager.surfaceColor.b, 0.97)

                                        // Compute the Russian display label from the ISO date.
                                        // Edge cases:
                                        //   • Empty string  → tasks have no due date; show "Без даты"
                                        //   • Non-date junk → pass-through raw value as fallback
                                        //   • Today / Tomorrow → localised aliases
                                        //   • Everything else → "12 мая (среда)" format
                                        readonly property string _label: {
                                            const iso = section
                                            if (!iso || iso === "") return qsTr("Без даты")

                                            // Build today and tomorrow as ISO strings for
                                            // comparison, forcing midnight so getTime() is stable.
                                            const today = new Date()
                                            today.setHours(0, 0, 0, 0)
                                            const isoToday = Qt.formatDate(today, "yyyy-MM-dd")
                                            const tomorrow = new Date(today)
                                            tomorrow.setDate(today.getDate() + 1)
                                            const isoTomorrow = Qt.formatDate(tomorrow, "yyyy-MM-dd")

                                            if (iso === isoToday)    return qsTr("Сегодня")
                                            if (iso === isoTomorrow) return qsTr("Завтра")

                                            // Parse as local midnight — append T00:00:00 so the
                                            // Date constructor does not apply UTC offset on
                                            // platforms where bare "yyyy-MM-dd" is treated as UTC.
                                            const d = new Date(iso + "T00:00:00")
                                            if (isNaN(d.getTime())) return iso   // not a valid date

                                            const loc = Qt.locale("ru_RU")
                                            // monthName(month, Locale.LongFormat) → "мая", "июня", …
                                            // dayName(weekday, Locale.LongFormat) → "среда", …
                                            // Qt.locale weekday: 0=Sunday…6=Saturday (JS convention)
                                            return d.getDate()
                                                   + " " + loc.monthName(d.getMonth(), Locale.LongFormat)
                                                   + " (" + loc.dayName(d.getDay(), Locale.LongFormat) + ")"
                                        }

                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.left:           parent.left
                                            anchors.leftMargin:     Spacing.md
                                            text:           dayGroupHeader._label
                                            font.pixelSize: FontSize.body
                                            font.weight:    Font.DemiBold
                                            color:          ThemeManager.textColor
                                        }

                                        // Bottom separator line
                                        Rectangle {
                                            anchors.bottom: parent.bottom
                                            anchors.left:   parent.left
                                            anchors.right:  parent.right
                                            height: 1
                                            color:  ThemeManager.borderColor
                                        }
                                    }

                                    ScrollBar.vertical: ScrollBar {
                                        policy: ScrollBar.AsNeeded
                                    }

                                    delegate: Item {
                                        id: delegateRoot
                                        width: taskListView.width
                                        height: taskItem.implicitHeight

                                        // ListView injects model roles into REQUIRED properties on
                                        // the delegate's root. They live on delegateRoot here, not
                                        // on the inner TaskItem (TaskItem is loaded as a regular
                                        // child and gets its values bound from delegateRoot below).
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
                                            id: taskItem
                                            width: parent.width

                                            // The `id` property name collides with QML's id
                                            // attribute when used as a binding key in object
                                            // initialiser. Bind it via Binding{} (a regular
                                            // assignment to `id:` is parsed as the QML id syntax).

                                            index:       delegateRoot.index
                                            content:     delegateRoot.content
                                            description: delegateRoot.description
                                            projectId:   delegateRoot.projectId
                                            sectionId:   delegateRoot.sectionId
                                            parentId:    delegateRoot.parentId
                                            order:       delegateRoot.order
                                            priority:    delegateRoot.priority
                                            labels:      delegateRoot.labels
                                            isCompleted: delegateRoot.isCompleted
                                            completedAt: delegateRoot.completedAt
                                            dueDate:     delegateRoot.dueDate
                                            dueString:   delegateRoot.dueString
                                            isRecurring: delegateRoot.isRecurring
                                            deadline:    delegateRoot.deadline
                                            durationAmount: delegateRoot.durationAmount
                                            durationUnit:   delegateRoot.durationUnit
                                            dayOrder:       delegateRoot.dayOrder
                                            color:          delegateRoot.color

                                            onTaskClicked: function(taskId) {
                                                const t = dataStore.taskById(taskId)
                                                if (t) taskViewModel.selectedTask = t
                                            }
                                        }
                                        Binding {
                                            target: taskItem
                                            property: "id"
                                            value: delegateRoot.id
                                        }

                                        // Two stacked DropAreas split the row vertically:
                                        //   • top  ½ → reorder (drop ABOVE this row)
                                        //   • bottom ½ → make the dropped task a SUBTASK
                                        //                of this one (Todoist parity).
                                        // The visual indicators differ to make intent obvious.

                                        DropArea {
                                            id: reorderDrop
                                            anchors.left:   parent.left
                                            anchors.right:  parent.right
                                            anchors.top:    parent.top
                                            height: parent.height / 2
                                            keys: ["application/x-opentodo-task"]

                                            onEntered: function(drag) {
                                                const draggedId = drag.getDataAsString("application/x-opentodo-task")
                                                insertionLine.visible = (draggedId !== taskItem.id)
                                                subtaskHighlight.visible = false
                                            }
                                            onExited: function() {
                                                insertionLine.visible = false
                                            }
                                            onDropped: function(drop) {
                                                const draggedId = drop.getDataAsString("application/x-opentodo-task")
                                                insertionLine.visible = false
                                                if (draggedId === taskItem.id) {
                                                    drop.accept(Qt.MoveAction)
                                                    return
                                                }
                                                taskViewModel.moveTaskOrder(draggedId, taskItem.index)
                                                drop.accept(Qt.MoveAction)
                                            }
                                        }

                                        DropArea {
                                            id: subtaskDrop
                                            anchors.left:   parent.left
                                            anchors.right:  parent.right
                                            anchors.bottom: parent.bottom
                                            height: parent.height / 2
                                            keys: ["application/x-opentodo-task"]

                                            onEntered: function(drag) {
                                                const draggedId = drag.getDataAsString("application/x-opentodo-task")
                                                subtaskHighlight.visible = (draggedId !== taskItem.id)
                                                insertionLine.visible = false
                                            }
                                            onExited: function() {
                                                subtaskHighlight.visible = false
                                            }
                                            onDropped: function(drop) {
                                                const draggedId = drop.getDataAsString("application/x-opentodo-task")
                                                subtaskHighlight.visible = false
                                                if (draggedId === taskItem.id || !taskViewModel.convertToSubtask) {
                                                    drop.accept(Qt.MoveAction)
                                                    return
                                                }
                                                taskViewModel.convertToSubtask(draggedId, taskItem.id)
                                                drop.accept(Qt.MoveAction)
                                            }
                                        }

                                        // Visual indicators — a thin accent-coloured line
                                        // for reorder and a tinted overlay for subtask.
                                        Rectangle {
                                            id: insertionLine
                                            anchors.top:    parent.top
                                            anchors.left:   parent.left
                                            anchors.right:  parent.right
                                            height: 2
                                            color: ThemeManager.accentColor
                                            visible: false
                                            z: 10
                                        }
                                        Rectangle {
                                            id: subtaskHighlight
                                            anchors.fill: parent
                                            anchors.leftMargin: Spacing.md + 24
                                            color: Qt.rgba(ThemeManager.accentColor.r,
                                                           ThemeManager.accentColor.g,
                                                           ThemeManager.accentColor.b, 0.18)
                                            border.color: ThemeManager.accentColor
                                            border.width: 1
                                            radius: 4
                                            visible: false
                                            z: 9
                                        }
                                    }

                                    // Empty state
                                    EmptyState {
                                        anchors.fill: parent
                                        visible: taskListView.count === 0
                                        iconName: "check-bold"
                                        successBadge: true
                                        title: qsTr("Задач нет")
                                        message: qsTr("Всё сделано! Самое время добавить что-то новое или просто отдохнуть.")
                                        actionLabel: qsTr("Добавить задачу")
                                        onActionClicked: {
                                            if (quickAddLoader.item)
                                                quickAddLoader.item.focusInput()
                                        }
                                    }
                                }
                            }

                            // ── Bulk action bar — floats above list when tasks are selected ──
                            Rectangle {
                                id: bulkBar
                                anchors.bottom: parent.bottom
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.bottomMargin: Spacing.lg
                                width: Math.min(bulkBarRow.implicitWidth + Spacing.md*2, parent.width - 2*Spacing.lg)  // L-6.15
                                height: 48
                                radius: Radius.lg
                                color: ThemeManager.accentColor
                                visible: taskViewModel.selectionCount > 0
                                z: 100

                                opacity: visible ? 1.0 : 0.0
                                Behavior on opacity {
                                    NumberAnimation { duration: Duration.fast }
                                }

                                // ── Total estimated time chip ──────────────────────────────
                                // Sum of Task.estimateMinutes across selected tasks. Recomputed
                                // on selectionChanged; chip hidden when total is 0.
                                property int _totalEstimate: 0
                                property int _estimateContributors: 0
                                function _recomputeEstimate() {
                                    var total = 0
                                    var contributors = 0
                                    var ids = taskViewModel.selectedIds
                                    if (ids && ids.length > 0) {
                                        for (var i = 0; i < ids.length; i++) {
                                            var t = dataStore.taskById(ids[i])
                                            if (t && t.estimateMinutes > 0) {
                                                total += t.estimateMinutes
                                                contributors += 1
                                            }
                                        }
                                    }
                                    _totalEstimate = total
                                    _estimateContributors = contributors
                                }
                                function _fmtMinutes(total) {
                                    if (total <= 0) return ""
                                    var h = Math.floor(total / 60)
                                    var m = total % 60
                                    if (h === 0) return qsTr("⏱ %1 мин").arg(m)
                                    if (m === 0) return qsTr("⏱ %1 ч").arg(h)
                                    return qsTr("⏱ %1 ч %2 мин").arg(h).arg(m)
                                }
                                // Russian plural: "1 задаче", "2/3/4 задачам", "5+ задачам"
                                // (dative plural). Tooltip uses dative since it follows "Сумма по N".
                                function _ruTaskDative(n) {
                                    var abs = Math.abs(n)
                                    var mod10 = abs % 10
                                    var mod100 = abs % 100
                                    if (mod10 === 1 && mod100 !== 11) return qsTr("выделенной задаче")
                                    return qsTr("выделенным задачам")
                                }

                                Connections {
                                    target: taskViewModel
                                    function onSelectionChanged() { bulkBar._recomputeEstimate() }
                                }
                                // Recompute also if any task's estimate changes while selected.
                                Connections {
                                    target: dataStore
                                    function onDataChanged() {
                                        if (bulkBar.visible) bulkBar._recomputeEstimate()
                                    }
                                }
                                Component.onCompleted: _recomputeEstimate()

                                // ── Priority popup (PriorityPicker reuse) ────────────────────
                                PriorityPicker {
                                    id: bulkPriorityPicker
                                    // Open upward so it doesn't disappear below the screen
                                    y: -height - Spacing.xs
                                    // B-6.16: use mapToItem to convert sibling coords to bulkBar coords
                                    x: bulkPriorityBtn.mapToItem(bulkBar, 0, 0).x - (width - bulkPriorityBtn.width) / 2
                                    onPrioritySelected: function(priority) {
                                        taskViewModel.bulkSetPriority(priority)
                                        // clearSelection is called inside bulkSetPriority
                                    }
                                }

                                // ── Label popup ──────────────────────────────────────────────
                                Popup {
                                    id: bulkLabelPopup
                                    y: -height - Spacing.xs
                                    // B-6.16: use mapToItem for sibling-relative coords
                                    x: bulkLabelBtn.mapToItem(bulkBar, 0, 0).x - (width - bulkLabelBtn.width) / 2
                                    width: 220
                                    // Height adapts to content up to 260px
                                    height: Math.min(bulkLabelList.contentHeight + Spacing.md * 2, 260)
                                    padding: 0
                                    modal: false
                                    focus: true
                                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                                    background: Rectangle {
                                        color: ThemeManager.backgroundColor
                                        radius: 8
                                        border.color: ThemeManager.borderColor
                                        border.width: 1
                                    }

                                    ListView {
                                        id: bulkLabelList
                                        anchors {
                                            fill: parent
                                            topMargin: Spacing.xs
                                            bottomMargin: Spacing.xs
                                        }
                                        clip: true
                                        // B-6.17: load once on open, not on every visible-binding re-eval
                                        model: []
                                        Connections {
                                            target: bulkLabelPopup
                                            function onOpened() { bulkLabelList.model = dataStore.allLabels() }
                                        }

                                        delegate: Rectangle {
                                            required property var modelData
                                            width: bulkLabelList.width
                                            height: 36
                                            color: "transparent"
                                            radius: 4

                                            RowLayout {
                                                anchors {
                                                    fill: parent
                                                    leftMargin: 12
                                                    rightMargin: 12
                                                }
                                                spacing: 8

                                                // Colour swatch
                                                Rectangle {
                                                    width: 12
                                                    height: 12
                                                    radius: 6
                                                    // modelData is a Label* — use its color property.
                                                    // Many label colours are CSS names; Qt handles them.
                                                    color: modelData.color || ThemeManager.accentColor
                                                    Layout.alignment: Qt.AlignVCenter
                                                }

                                                Text {
                                                    Layout.fillWidth: true
                                                    text: modelData.name
                                                    color: ThemeManager.textColor
                                                    font.pixelSize: FontSize.body
                                                    elide: Text.ElideRight
                                                    verticalAlignment: Text.AlignVCenter
                                                }
                                            }

                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                hoverEnabled: true
                                                onEntered: parent.color = ThemeManager.hoverColor
                                                onExited:  parent.color = "transparent"
                                                onClicked: {
                                                    // bulkAddLabel takes a label name (see taskviewmodel.cpp:867)
                                                    taskViewModel.bulkAddLabel(modelData.name)
                                                    bulkLabelPopup.close()
                                                    // clearSelection is called inside bulkAddLabel
                                                }
                                            }
                                        }

                                        // Shown when there are no labels yet
                                        Text {
                                            anchors.centerIn: parent
                                            visible: bulkLabelList.count === 0
                                            text: qsTr("Нет меток")
                                            color: ThemeManager.secondaryTextColor
                                            font.pixelSize: FontSize.bodySmall
                                        }
                                    }
                                }

                                // ── Project popup (with search) ──────────────────────────────
                                Popup {
                                    id: bulkProjectPopup
                                    y: -height - Spacing.xs
                                    // B-6.16: use mapToItem for sibling-relative coords
                                    x: bulkProjectBtn.mapToItem(bulkBar, 0, 0).x - (width - bulkProjectBtn.width) / 2
                                    width: 260
                                    height: 300
                                    padding: 0
                                    modal: false
                                    focus: true
                                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                                    onAboutToShow: bulkProjectSearch.text = ""

                                    background: Rectangle {
                                        color: ThemeManager.backgroundColor
                                        radius: 8
                                        border.color: ThemeManager.borderColor
                                        border.width: 1
                                    }

                                    ColumnLayout {
                                        anchors.fill: parent
                                        spacing: 0

                                        // Search bar
                                        Rectangle {
                                            Layout.fillWidth: true
                                            height: 40
                                            color: "transparent"

                                            TextField {
                                                id: bulkProjectSearch
                                                anchors {
                                                    fill: parent
                                                    margins: Spacing.xs
                                                }
                                                placeholderText: qsTr("Поиск проекта…")
                                                font.pixelSize: FontSize.bodySmall
                                                background: Rectangle {
                                                    color: ThemeManager.surfaceColor
                                                    radius: 6
                                                    border.color: ThemeManager.borderColor
                                                    border.width: 1
                                                }
                                                color: ThemeManager.textColor
                                                placeholderTextColor: ThemeManager.secondaryTextColor
                                            }
                                        }

                                        // Thin divider
                                        Rectangle {
                                            Layout.fillWidth: true
                                            height: 1
                                            color: ThemeManager.borderColor
                                        }

                                        // Project list
                                        ListView {
                                            id: bulkProjectList
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            clip: true

                                            // B-6.19: load once on open, not on every visible-binding re-eval
                                            property var allProjects: []
                                            Connections {
                                                target: bulkProjectPopup
                                                function onOpened() { bulkProjectList.allProjects = dataStore.allProjects() }
                                            }

                                            // Filter: exclude deleted/archived, apply search term
                                            model: {
                                                const q = bulkProjectSearch.text.toLowerCase().trim()
                                                return allProjects.filter(function(p) {
                                                    if (p.isDeleted || p.isArchived) return false
                                                    if (q === "") return true
                                                    return p.name.toLowerCase().indexOf(q) !== -1
                                                })
                                            }

                                            delegate: Rectangle {
                                                required property var modelData
                                                width: bulkProjectList.width
                                                height: 36
                                                color: "transparent"
                                                radius: 4

                                                RowLayout {
                                                    anchors {
                                                        fill: parent
                                                        leftMargin: 12
                                                        rightMargin: 12
                                                    }
                                                    spacing: 8

                                                    // Project colour dot
                                                    Rectangle {
                                                        width: 10
                                                        height: 10
                                                        radius: 5
                                                        color: modelData.color || ThemeManager.accentColor
                                                        Layout.alignment: Qt.AlignVCenter
                                                    }

                                                    Text {
                                                        Layout.fillWidth: true
                                                        text: modelData.name
                                                        color: ThemeManager.textColor
                                                        font.pixelSize: FontSize.body
                                                        elide: Text.ElideRight
                                                        verticalAlignment: Text.AlignVCenter
                                                    }
                                                }

                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: Qt.PointingHandCursor
                                                    hoverEnabled: true
                                                    onEntered: parent.color = ThemeManager.hoverColor
                                                    onExited:  parent.color = "transparent"
                                                    onClicked: {
                                                        // bulkMoveToProject takes a project id
                                                        taskViewModel.bulkMoveToProject(modelData.id)
                                                        bulkProjectPopup.close()
                                                        // clearSelection is called inside bulkMoveToProject
                                                    }
                                                }
                                            }

                                            Text {
                                                anchors.centerIn: parent
                                                visible: bulkProjectList.count === 0
                                                text: qsTr("Нет проектов")
                                                color: ThemeManager.secondaryTextColor
                                                font.pixelSize: FontSize.bodySmall
                                            }
                                        }
                                    }
                                }

                                // ── Color popup ──────────────────────────────────────────────
                                Popup {
                                    id: bulkColorPopup
                                    y: -height - Spacing.xs
                                    x: bulkColorBtn.mapToItem(bulkBar, 0, 0).x - (width - bulkColorBtn.width) / 2
                                    width: 132
                                    padding: Spacing.sm
                                    modal: false
                                    focus: true
                                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                                    background: Rectangle {
                                        color: ThemeManager.backgroundColor
                                        radius: 8
                                        border.color: ThemeManager.borderColor
                                        border.width: 1
                                    }

                                    contentItem: GridLayout {
                                        columns: 4
                                        rowSpacing: 4
                                        columnSpacing: 4

                                        Repeater {
                                            model: [
                                                "",         // очистить цвет
                                                "#db4c3f", "#f49c18", "#fad000",
                                                "#7ecc49", "#299438", "#6accbc",
                                                "#158fad", "#14aaf5", "#96c3eb",
                                                "#4073ff", "#884dff"
                                            ]
                                            delegate: Rectangle {
                                                required property string modelData
                                                width: 24; height: 24; radius: 12
                                                color: modelData === "" ? "transparent" : modelData
                                                border.color: modelData === "" ? ThemeManager.borderColor : "transparent"
                                                border.width: modelData === "" ? 1 : 0

                                                // Task 17cea65b iter-3: было unicode "✕".
                                                IconSvg {
                                                    anchors.centerIn: parent
                                                    visible: modelData === ""
                                                    name: "close"
                                                    size: 10
                                                    color: ThemeManager.secondaryTextColor
                                                }

                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: {
                                                        taskViewModel.bulkSetColor(modelData)
                                                        bulkColorPopup.close()
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                // ── Due-date popup ───────────────────────────────────────────
                                Popup {
                                    id: bulkDuePopup
                                    y: -height - Spacing.xs
                                    x: bulkDueBtn.mapToItem(bulkBar, 0, 0).x - (width - bulkDueBtn.width) / 2
                                    width: 200
                                    padding: Spacing.sm
                                    modal: false

                                    contentItem: Column {
                                        spacing: 0

                                        Repeater {
                                            model: [
                                                { label: qsTr("Сегодня"),             phrase: "сегодня" },
                                                { label: qsTr("Завтра"),              phrase: "завтра" },
                                                { label: qsTr("Через 3 дня"),         phrase: "через 3 дня" },
                                                { label: qsTr("На следующей неделе"), phrase: "следующая неделя" },
                                                { label: qsTr("В понедельник"),       phrase: "понедельник" },
                                                { label: qsTr("В выходные"),          phrase: "суббота" },
                                                { label: "──",                        phrase: "__sep__" },
                                                { label: qsTr("Убрать срок"),         phrase: "" }
                                            ]

                                            delegate: Item {
                                                width: 176
                                                height: modelData.phrase === "__sep__" ? 8 : 28

                                                Rectangle {
                                                    visible: modelData.phrase === "__sep__"
                                                    anchors.centerIn: parent
                                                    width: parent.width - 8
                                                    height: 1
                                                    color: ThemeManager.borderColor
                                                }

                                                Rectangle {
                                                    visible: modelData.phrase !== "__sep__"
                                                    anchors.fill: parent
                                                    color: dueItemHover.containsMouse
                                                        ? Qt.rgba(ThemeManager.accentColor.r,
                                                                  ThemeManager.accentColor.g,
                                                                  ThemeManager.accentColor.b, 0.10)
                                                        : "transparent"
                                                    radius: 4

                                                    Text {
                                                        anchors.left: parent.left
                                                        anchors.leftMargin: Spacing.sm
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        text: modelData.label
                                                        color: modelData.phrase === ""
                                                            ? ThemeManager.secondaryTextColor
                                                            : ThemeManager.textColor
                                                        font.pixelSize: FontSize.body
                                                    }

                                                    MouseArea {
                                                        id: dueItemHover
                                                        anchors.fill: parent
                                                        hoverEnabled: true
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: {
                                                            taskViewModel.bulkSetDue(modelData.phrase)
                                                            bulkDuePopup.close()
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                // ── Deadline picker (reuses the existing DatePicker component) ─
                                // Opened by bulkDeadlineBtn; on dateSelected we feed a QDate to
                                // taskViewModel.bulkSetDeadline. The DatePicker emits an ISO
                                // "yyyy-MM-dd" string (or "" for the "Без даты" quick option),
                                // which we convert here via Qt.formatDate / new Date(...).
                                DatePicker {
                                    id: bulkDeadlinePicker
                                    y: -height - Spacing.xs
                                    // B-6.16-style: sibling-relative coords for centering above the button
                                    x: bulkDeadlineBtn.mapToItem(bulkBar, 0, 0).x - (width - bulkDeadlineBtn.width) / 2
                                    selectedDate: ""
                                    onDateSelected: function(dateString) {
                                        if (dateString === "") {
                                            // "Без даты" / clear option — pass an invalid QDate.
                                            taskViewModel.bulkSetDeadline(new Date(NaN))
                                        } else if (dateString === "today") {
                                            taskViewModel.bulkSetDeadline(new Date())
                                        } else if (dateString === "tomorrow") {
                                            const t = new Date()
                                            t.setDate(t.getDate() + 1)
                                            taskViewModel.bulkSetDeadline(t)
                                        } else if (dateString === "next week") {
                                            const t = new Date()
                                            t.setDate(t.getDate() + 7)
                                            taskViewModel.bulkSetDeadline(t)
                                        } else {
                                            // Plain ISO date "YYYY-MM-DD".
                                            const parts = dateString.split("-")
                                            taskViewModel.bulkSetDeadline(new Date(
                                                parseInt(parts[0]),
                                                parseInt(parts[1]) - 1,
                                                parseInt(parts[2])))
                                        }
                                        // bulkSetDeadline calls clearSelection internally;
                                        // DatePicker auto-closes on selection.
                                    }
                                }

                                // Brief confirmation toast for "Copy as Markdown".
                                // Anchored inside bulkBar so it floats just above the bar.
                                Popup {
                                    id: bulkCopyMdToast
                                    y: -height - Spacing.xs
                                    x: (parent.width - width) / 2
                                    width: implicitWidth + Spacing.md * 2
                                    padding: Spacing.sm
                                    modal: false
                                    closePolicy: Popup.NoAutoClose
                                    background: Rectangle {
                                        color: ThemeManager.surfaceColor
                                        radius: Radius.sm
                                        border.color: ThemeManager.borderColor
                                        border.width: 1
                                    }
                                    contentItem: Text {
                                        text: qsTr("Скопировано в буфер обмена")
                                        color: ThemeManager.textColor
                                        font.pixelSize: FontSize.body
                                    }
                                    Timer {
                                        running: bulkCopyMdToast.visible
                                        interval: 2000
                                        onTriggered: bulkCopyMdToast.close()
                                    }
                                }

                                RowLayout {
                                    id: bulkBarRow
                                    anchors.fill: parent
                                    anchors.margins: Spacing.sm
                                    spacing: Spacing.xs

                                    Text {
                                        text: qsTr("Выделено: %1").arg(taskViewModel.selectionCount)
                                        color: "white"
                                        font.pixelSize: FontSize.body
                                        Layout.leftMargin: Spacing.sm
                                        Layout.fillWidth: true
                                        verticalAlignment: Text.AlignVCenter
                                    }

                                    // ── Закрепить ────────────────────────────────────────────
                                    ToolButton {
                                        id: bulkPinBtn

                                        // Smart toggle: если ВСЕ выделенные уже закреплены — откреплять, иначе закреплять.
                                        readonly property bool allPinned: {
                                            const ids = taskViewModel.selectedIds
                                            if (!ids || ids.length === 0) return false
                                            for (let i = 0; i < ids.length; i++) {
                                                const t = dataStore.taskById(ids[i])
                                                if (!t || !t.isPinned) return false
                                            }
                                            return true
                                        }

                                        contentItem: Item {
                                            implicitHeight: bulkBtnRowPin.implicitHeight
                                            implicitWidth: bulkBtnRowPin.implicitWidth
                                            Row {
                                                id: bulkBtnRowPin
                                                anchors.centerIn: parent
                                                spacing: 6
                                                IconSvg {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    name: "pin"
                                                    size: 14
                                                    color: "white"
                                                }
                                                Text {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    text: bulkPinBtn.allPinned ? qsTr("Открепить") : qsTr("Закрепить")
                                                    color: "white"
                                                    font.pixelSize: FontSize.body
                                                }
                                            }
                                        }
                                        onClicked: {
                                            if (bulkPriorityPicker.opened) bulkPriorityPicker.close()
                                            if (bulkLabelPopup.opened)     bulkLabelPopup.close()
                                            if (bulkProjectPopup.opened)   bulkProjectPopup.close()
                                            if (bulkColorPopup.opened)     bulkColorPopup.close()
                                            taskViewModel.bulkSetPinned(!allPinned)
                                        }
                                        ToolTip.visible: hovered
                                        ToolTip.text: allPinned ? qsTr("Открепить выделенные задачи") : qsTr("Закрепить выделенные задачи наверху")
                                        background: Rectangle {
                                            color: parent.hovered ? Qt.rgba(1,1,1,0.18) : "transparent"
                                            radius: Radius.sm
                                        }
                                    }

                                    // ── Цвет ─────────────────────────────────────────────────
                                    ToolButton {
                                        id: bulkColorBtn
                                        contentItem: Item {
                                            implicitHeight: bulkBtnRowColor.implicitHeight
                                            implicitWidth: bulkBtnRowColor.implicitWidth
                                            Row {
                                                id: bulkBtnRowColor
                                                anchors.centerIn: parent
                                                spacing: 6
                                                IconSvg {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    name: "projects/palette"
                                                    size: 14
                                                    color: "white"
                                                }
                                                Text {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    text: qsTr("Цвет")
                                                    color: "white"
                                                    font.pixelSize: FontSize.body
                                                }
                                            }
                                        }
                                        onClicked: {
                                            if (bulkPriorityPicker.opened) bulkPriorityPicker.close()
                                            if (bulkLabelPopup.opened)     bulkLabelPopup.close()
                                            if (bulkProjectPopup.opened)   bulkProjectPopup.close()
                                            bulkColorPopup.open()
                                        }
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Установить цвет для выделенных задач")
                                        background: Rectangle {
                                            color: parent.hovered ? Qt.rgba(1,1,1,0.18) : "transparent"
                                            radius: Radius.sm
                                        }
                                    }

                                    // ── Срок ─────────────────────────────────────────────────
                                    ToolButton {
                                        id: bulkDueBtn
                                        contentItem: Item {
                                            implicitHeight: bulkBtnRowDue.implicitHeight
                                            implicitWidth: bulkBtnRowDue.implicitWidth
                                            Row {
                                                id: bulkBtnRowDue
                                                anchors.centerIn: parent
                                                spacing: 6
                                                IconSvg {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    name: "calendar"
                                                    size: 14
                                                    color: "white"
                                                }
                                                Text {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    text: qsTr("Срок")
                                                    color: "white"
                                                    font.pixelSize: FontSize.body
                                                }
                                            }
                                        }
                                        onClicked: {
                                            if (bulkPriorityPicker.opened) bulkPriorityPicker.close()
                                            if (bulkLabelPopup.opened)     bulkLabelPopup.close()
                                            if (bulkProjectPopup.opened)   bulkProjectPopup.close()
                                            if (bulkColorPopup.opened)     bulkColorPopup.close()
                                            bulkDuePopup.open()
                                        }
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Срок")
                                        background: Rectangle {
                                            color: parent.hovered ? Qt.rgba(1,1,1,0.18) : "transparent"
                                            radius: Radius.sm
                                        }
                                    }

                                    // ── Дедлайн (Task.deadline) ──────────────────────────────
                                    // Distinct from "Срок" (due date): deadline is a hard cutoff
                                    // shown as a separate badge in TaskItem. Label uses "Дедлайн"
                                    // to match codebase terminology (EditorMetaRow's "Установить
                                    // дедлайн") and to disambiguate from bulkDueBtn above.
                                    ToolButton {
                                        id: bulkDeadlineBtn
                                        contentItem: Item {
                                            implicitHeight: bulkBtnRowDeadline.implicitHeight
                                            implicitWidth: bulkBtnRowDeadline.implicitWidth
                                            Row {
                                                id: bulkBtnRowDeadline
                                                anchors.centerIn: parent
                                                spacing: 6
                                                IconSvg {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    name: "alarm"
                                                    size: 14
                                                    color: "white"
                                                }
                                                Text {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    text: qsTr("Дедлайн")
                                                    color: "white"
                                                    font.pixelSize: FontSize.body
                                                }
                                            }
                                        }
                                        onClicked: {
                                            if (bulkPriorityPicker.opened) bulkPriorityPicker.close()
                                            if (bulkLabelPopup.opened)     bulkLabelPopup.close()
                                            if (bulkProjectPopup.opened)   bulkProjectPopup.close()
                                            if (bulkColorPopup.opened)     bulkColorPopup.close()
                                            if (bulkDuePopup.opened)       bulkDuePopup.close()
                                            bulkDeadlinePicker.open()
                                        }
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Установить срок")
                                        background: Rectangle {
                                            color: parent.hovered ? Qt.rgba(1,1,1,0.18) : "transparent"
                                            radius: Radius.sm
                                        }
                                    }

                                    // ── Архивировать ─────────────────────────────────────────
                                    ToolButton {
                                        id: bulkArchiveBtn
                                        contentItem: Item {
                                            implicitHeight: bulkBtnRowArchive.implicitHeight
                                            implicitWidth: bulkBtnRowArchive.implicitWidth
                                            Row {
                                                id: bulkBtnRowArchive
                                                anchors.centerIn: parent
                                                spacing: 6
                                                IconSvg {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    name: "archive"
                                                    size: 14
                                                    color: "white"
                                                }
                                                Text {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    text: qsTr("Архив")
                                                    color: "white"
                                                    font.pixelSize: FontSize.body
                                                }
                                            }
                                        }
                                        onClicked: taskViewModel.bulkArchive()
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Архивировать выделенные задачи")
                                        background: Rectangle {
                                            color: parent.hovered ? Qt.rgba(1,1,1,0.18) : "transparent"
                                            radius: Radius.sm
                                        }
                                    }

                                    // ── Скопировать как Markdown ──────────────────────────────
                                    ToolButton {
                                        id: bulkCopyMdBtn
                                        contentItem: Item {
                                            implicitHeight: bulkBtnRowCopyMd.implicitHeight
                                            implicitWidth: bulkBtnRowCopyMd.implicitWidth
                                            Row {
                                                id: bulkBtnRowCopyMd
                                                anchors.centerIn: parent
                                                spacing: 6
                                                IconSvg {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    name: "note"
                                                    size: 14
                                                    color: "white"
                                                }
                                                Text {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    text: "MD"
                                                    color: "white"
                                                    font.pixelSize: FontSize.body
                                                }
                                            }
                                        }
                                        onClicked: {
                                            const txt = taskViewModel.bulkCopyAsMarkdown()
                                            if (txt && txt !== "")
                                                bulkCopyMdToast.open()
                                        }
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Скопировать как Markdown")
                                        background: Rectangle {
                                            color: parent.hovered ? Qt.rgba(1,1,1,0.18) : "transparent"
                                            radius: Radius.sm
                                        }
                                    }

                                    // ── Завершить ────────────────────────────────────────────
                                    ToolButton {
                                        contentItem: Text {
                                            text: qsTr("✓ Завершить")
                                            color: "white"
                                            font.pixelSize: FontSize.body
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        onClicked: taskViewModel.bulkComplete()
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Завершить выделенные задачи")
                                        background: Rectangle {
                                            color: parent.hovered ? Qt.rgba(1,1,1,0.18) : "transparent"
                                            radius: Radius.sm
                                        }
                                    }

                                    // ── Удалить ──────────────────────────────────────────────
                                    // ITER1 #48: show confirmation dialog before bulk-delete
                                    ToolButton {
                                        contentItem: Item {
                                            implicitHeight: bulkBtnRowDelete.implicitHeight
                                            implicitWidth: bulkBtnRowDelete.implicitWidth
                                            Row {
                                                id: bulkBtnRowDelete
                                                anchors.centerIn: parent
                                                spacing: 6
                                                IconSvg {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    name: "trash"
                                                    size: 14
                                                    color: "white"
                                                }
                                                Text {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    text: qsTr("Удалить")
                                                    color: "white"
                                                    font.pixelSize: FontSize.body
                                                }
                                            }
                                        }
                                        onClicked: bulkDeleteConfirmDialog.open()
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Удалить выделенные задачи")
                                        background: Rectangle {
                                            color: parent.hovered ? Qt.rgba(1,1,1,0.18) : "transparent"
                                            radius: Radius.sm
                                        }
                                    }

                                    // ── Приоритет ────────────────────────────────────────────
                                    ToolButton {
                                        id: bulkPriorityBtn
                                        contentItem: Text {
                                            text: qsTr("⚑ Приоритет")
                                            color: "white"
                                            font.pixelSize: FontSize.body
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        onClicked: {
                                            if (bulkLabelPopup.opened)   bulkLabelPopup.close()
                                            if (bulkProjectPopup.opened) bulkProjectPopup.close()
                                            bulkPriorityPicker.open()
                                        }
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Установить приоритет для выделенных задач")
                                        background: Rectangle {
                                            color: parent.hovered ? Qt.rgba(1,1,1,0.18) : "transparent"
                                            radius: Radius.sm
                                        }
                                    }

                                    // ── Метка ────────────────────────────────────────────────
                                    ToolButton {
                                        id: bulkLabelBtn
                                        contentItem: Text {
                                            text: qsTr("@ Метка")
                                            color: "white"
                                            font.pixelSize: FontSize.body
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        onClicked: {
                                            if (bulkPriorityPicker.opened) bulkPriorityPicker.close()
                                            if (bulkProjectPopup.opened)   bulkProjectPopup.close()
                                            bulkLabelPopup.open()
                                        }
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Добавить метку к выделенным задачам")
                                        background: Rectangle {
                                            color: parent.hovered ? Qt.rgba(1,1,1,0.18) : "transparent"
                                            radius: Radius.sm
                                        }
                                    }

                                    // ── Проект ───────────────────────────────────────────────
                                    ToolButton {
                                        id: bulkProjectBtn
                                        contentItem: Text {
                                            text: qsTr("# Проект")
                                            color: "white"
                                            font.pixelSize: FontSize.body
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        onClicked: {
                                            if (bulkPriorityPicker.opened) bulkPriorityPicker.close()
                                            if (bulkLabelPopup.opened)     bulkLabelPopup.close()
                                            bulkProjectPopup.open()
                                        }
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Переместить выделенные задачи в проект")
                                        background: Rectangle {
                                            color: parent.hovered ? Qt.rgba(1,1,1,0.18) : "transparent"
                                            radius: Radius.sm
                                        }
                                    }

                                    // ── Сумма оценок по времени (информационный chip) ────────
                                    // Pinned to the right edge of the bulk toolbar, just before
                                    // the close button. Hidden when total estimate is 0.
                                    Item {
                                        id: bulkEstimateChip
                                        visible: bulkBar._totalEstimate > 0
                                        Layout.preferredWidth: estimateLabel.implicitWidth + Spacing.sm * 2
                                        Layout.preferredHeight: estimateLabel.implicitHeight + Spacing.xs * 2
                                        Layout.leftMargin: Spacing.xs
                                        Layout.rightMargin: Spacing.xs

                                        Rectangle {
                                            anchors.fill: parent
                                            radius: Radius.sm
                                            color: Qt.rgba(1, 1, 1, 0.14)
                                        }

                                        Label {
                                            id: estimateLabel
                                            anchors.centerIn: parent
                                            text: bulkBar._fmtMinutes(bulkBar._totalEstimate)
                                            color: ThemeManager.secondaryTextColor
                                            font.pixelSize: FontSize.small
                                            verticalAlignment: Text.AlignVCenter
                                        }

                                        MouseArea {
                                            id: estimateMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            acceptedButtons: Qt.NoButton
                                        }

                                        ToolTip.visible: estimateMouse.containsMouse
                                                         && bulkBar._estimateContributors > 0
                                        ToolTip.text: qsTr("Сумма по %1 %2 с оценкой времени")
                                                      .arg(bulkBar._estimateContributors)
                                                      .arg(bulkBar._ruTaskDative(bulkBar._estimateContributors))
                                    }

                                    // ── Снять выделение ──────────────────────────────────────
                                    // Task 17cea65b iter-3: было unicode "✕".
                                    // Iter-night-23: was hardcoded "white".
                                    // The close icon sits on top of the
                                    // bulk-select bar (accentColor fill),
                                    // so the foreground must follow the
                                    // theme's own accentForeground.
                                    ToolButton {
                                        contentItem: IconSvg {
                                            anchors.centerIn: parent
                                            name: "close"
                                            size: 14
                                            color: ThemeManager.accentForeground
                                        }
                                        onClicked: {
                                            if (bulkPriorityPicker.opened) bulkPriorityPicker.close()
                                            if (bulkLabelPopup.opened)     bulkLabelPopup.close()
                                            if (bulkProjectPopup.opened)   bulkProjectPopup.close()
                                            if (bulkColorPopup.opened)     bulkColorPopup.close()
                                            if (bulkDuePopup.opened)       bulkDuePopup.close()
                                            taskViewModel.clearSelection()
                                        }
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Снять выделение")
                                        background: Rectangle {
                                            color: parent.hovered ? Qt.rgba(1,1,1,0.18) : "transparent"
                                            radius: Radius.sm
                                        }
                                    }
                                }
                            }
                        }

                        // ── Bottom quick-add row ──────────────────────────────
                        // Floats below the task list in list-based views only.
                        // Placeholder text → click → inline TextField for fast
                        // title-only adds. Enter confirms and stays editing for
                        // multi-add; Esc or losing focus collapses back.
                        Item {
                            id: bottomQuickAdd
                            Layout.fillWidth: true
                            Layout.preferredHeight: 36
                            visible: !root.isFullPageView

                            property bool editing: false

                            // Collapse when the view switches away from a list mode
                            Connections {
                                target: taskViewModel
                                function onFilterModeChanged() {
                                    bottomQuickAdd.editing = false
                                    bottomQuickAddInput.text = ""
                                }
                            }

                            // ── Placeholder row (collapsed state) ─────────────
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Spacing.md
                                spacing: Spacing.xs
                                visible: !bottomQuickAdd.editing

                                // Task 17cea65b iter-3: было unicode "+".
                                IconSvg {
                                    name: "plus"
                                    size: 16
                                    color: ThemeManager.secondaryTextColor
                                    Layout.alignment: Qt.AlignVCenter
                                }
                                Text {
                                    text: qsTr("Добавить задачу")
                                    color: ThemeManager.secondaryTextColor
                                    font.pixelSize: FontSize.body
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    opacity: quickAddHover.containsMouse ? 1.0 : 0.6
                                    Behavior on opacity {
                                        NumberAnimation { duration: Duration.fast }
                                    }
                                }
                            }

                            // Sibling of the RowLayout — anchored to the parent
                            // Item so it covers the whole placeholder hit-area
                            // without fighting layout management.
                            MouseArea {
                                id: quickAddHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                visible: !bottomQuickAdd.editing
                                onClicked: {
                                    bottomQuickAdd.editing = true
                                    Qt.callLater(function() { bottomQuickAddInput.forceActiveFocus() })
                                }
                            }

                            // ── Edit row (expanded state) ──────────────────────
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Spacing.md
                                anchors.rightMargin: Spacing.md
                                visible: bottomQuickAdd.editing
                                spacing: Spacing.sm

                                // Circular task checkbox outline — cosmetic only
                                Rectangle {
                                    width: 18; height: 18; radius: 9
                                    border.color: ThemeManager.secondaryTextColor
                                    border.width: 1.5
                                    color: "transparent"
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                TextField {
                                    id: bottomQuickAddInput
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("Название задачи")
                                    color: ThemeManager.textColor
                                    placeholderTextColor: ThemeManager.secondaryTextColor
                                    background: Item {}
                                    font.pixelSize: FontSize.body
                                    verticalAlignment: Text.AlignVCenter

                                    Keys.onEscapePressed: {
                                        text = ""
                                        bottomQuickAdd.editing = false
                                    }

                                    onActiveFocusChanged: {
                                        if (!activeFocus && bottomQuickAdd.editing) {
                                            text = ""
                                            bottomQuickAdd.editing = false
                                        }
                                    }

                                    onAccepted: {
                                        const t = text.trim()
                                        if (t.length > 0) {
                                            const pid = taskViewModel.currentProjectId
                                            taskViewModel.addTask(t, pid, 4, "", [])
                                            if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                                                telemetry.trackEvent("task_added_bottom_quickadd", {})
                                            text = ""
                                            forceActiveFocus()
                                        } else {
                                            bottomQuickAdd.editing = false
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ── Upcoming calendar panel (wide mode, ≥ 900 px) ────────
                    // Left separator + fixed-width panel.  Rendered only in
                    // Upcoming mode; animates in/out with the same pattern used
                    // by the task editor panel on the right.
                    Rectangle {
                        Layout.fillHeight: true
                        Layout.preferredWidth: (_calPanelVisible && root._calendarWide) ? 1 : 0
                        color: borderColor
                        clip:  true

                        readonly property bool _calPanelVisible:
                            taskViewModel.filterMode === TaskViewModel.Upcoming

                        Behavior on Layout.preferredWidth {
                            NumberAnimation {
                                duration: animationDuration
                                easing.type: Easing.InOutQuad
                            }
                        }
                    }

                    Item {
                        id: upcomingCalendarPanelContainer
                        Layout.fillHeight: true
                        Layout.preferredWidth: (_upcomingCalendarPanel.visible && root._calendarWide) ? 280 : 0
                        clip: true

                        Behavior on Layout.preferredWidth {
                            NumberAnimation {
                                duration: animationDuration
                                easing.type: Easing.InOutQuad
                            }
                        }

                        UpcomingCalendarPanel {
                            id: _upcomingCalendarPanel
                            anchors.fill: parent

                            visible: taskViewModel.filterMode === TaskViewModel.Upcoming
                                     && root._calendarWide

                            widePanel: true

                            onDayPicked: function(picked) {
                                root._scrollToUpcomingDate(picked)
                            }
                        }
                    }

                    // ── Vertical separator before editor ──────────────────────
                    Rectangle {
                        Layout.fillHeight: true
                        Layout.preferredWidth: taskViewModel.selectedTask ? 1 : 0
                        color: borderColor
                        clip: true

                        Behavior on Layout.preferredWidth {
                            NumberAnimation {
                                duration: animationDuration
                                easing.type: Easing.InOutQuad
                            }
                        }
                    }

                    // ── Task editor panel (slides in from right) ──────────────
                    Item {
                        id: taskEditorPanel
                        Layout.fillHeight: true
                        Layout.preferredWidth: taskViewModel.selectedTask ? taskEditorWidth : 0
                        clip: true

                        Behavior on Layout.preferredWidth {
                            NumberAnimation {
                                duration: animationDuration
                                easing.type: Easing.InOutQuad
                            }
                        }

                        Rectangle {
                            anchors.fill: parent
                            color: contentColor

                            Loader {
                                id: taskEditorLoader
                                anchors.fill: parent
                                active: taskViewModel.selectedTask !== null
                                        && taskViewModel.selectedTask !== undefined
                                source: "../components/TaskEditor.qml"

                                onLoaded: {
                                    if (item && taskViewModel.selectedTask) {
                                        item.task = taskViewModel.selectedTask
                                    }
                                }

                                Connections {
                                    target: taskEditorLoader.item
                                    ignoreUnknownSignals: true
                                    function onCloseRequested() {
                                        taskViewModel.selectedTask = null
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Narrow-mode sidebar drawer (overlay) ──────────────────────────────────
    // When the window is narrower than the breakpoint, the inline sidebar
    // disappears. Tapping the hamburger reveals THIS overlay drawer that
    // covers the main content from the left edge. Tap outside to dismiss.
    // Width inside the panel is clamped so a 200 px window still shows
    // ~144 px of sidebar (parent.width - 56), enough for project rows.
    Item {
        id: drawerOverlay
        objectName: "drawerOverlay"
        anchors.fill: parent
        z: 50
        visible: root.isNarrow && sidebarViewModel.sidebarVisible

        // Dim background — clicking it closes the drawer.
        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.45)
            MouseArea {
                anchors.fill: parent
                onClicked: sidebarViewModel.toggleSidebar()
            }
        }

        // Sliding sidebar panel.
        Rectangle {
            id: drawerPanel
            objectName: "drawerPanel"
            anchors.top:    parent.top
            anchors.bottom: parent.bottom
            // Floor lowered to 140 so the drawer still fits inside a 200 px window
            // while leaving a tappable strip (~40 px) of dim background to dismiss.
            // B-6.20: floor at a sensible minimum to prevent underflow.
            width: Math.max(140, Math.min(sidebarWidth, parent.width - 40))
            color: contentColor

            Loader {
                anchors.fill: parent
                source: "../components/SidebarContent.qml"
            }

            // Right-edge separator
            Rectangle {
                anchors.top:    parent.top
                anchors.bottom: parent.bottom
                anchors.right:  parent.right
                width: 1
                color: borderColor
            }
        }
    }

    // ── Command palette (Ctrl+K) ──────────────────────────────────────────────
    // Loaded eagerly so it can receive focus immediately on first invocation.
    Loader {
        id: commandMenuLoader
        anchors.fill: parent
        source: "../components/CommandMenu.qml"
        active: true

        // Forward sidebar-toggle signal to the ViewModel
        Connections {
            target: commandMenuLoader.item
            ignoreUnknownSignals: true
            function onToggleSidebarRequested() {
                sidebarViewModel.toggleSidebar()
            }
            function onSearchRequested() {
                // Focus the search bar if it is loaded
                if (searchBarLoader.item
                        && typeof searchBarLoader.item.forceActiveFocus === "function")
                    searchBarLoader.item.forceActiveFocus()
            }
            function onNewTaskRequested() {
                // I2-A: forward CommandMenu's "Новая задача" action to QuickAdd.
                if (quickAddLoader.item && typeof quickAddLoader.item.focusInput === "function")
                    quickAddLoader.item.focusInput()
            }
        }
    }

    // ── Quick jump picker (Ctrl+P) ────────────────────────────────────────────
    // Lazy-loaded on first invocation; the Popup itself lives inside Overlay so
    // it correctly centers over the window regardless of layout nesting.
    Loader {
        id: quickJumpLoader
        active: false
        sourceComponent: QuickJumpDialog {}

        function open() {
            if (!active) active = true
            if (item) item.open()
        }
    }

    // ── Move-to-project dialog (Ctrl+M) ───────────────────────────────────────
    // Same lazy-load pattern as the quick-jump picker. Operates on the current
    // multi-selection or, falling back, on the single open editor task.
    Loader {
        id: taskMoveLoader
        active: false
        sourceComponent: TaskMoveDialog {}

        function open() {
            if (!active) active = true
            if (item) item.open()
        }
    }

    // ── Global keyboard shortcuts ─────────────────────────────────────────────
    // Convenience: true when the task list is the active context
    // (no full-page view, no text editor overlay open).
    readonly property bool _taskListActive: !isFullPageView && taskViewModel.selectedTask === null

    // ── Vim-style g-prefix navigation ────────────────────────────────────────
    // Press G, then within 1s press one of i/t/u/s/p/b to jump to that view.
    // The follow-up shortcuts are only enabled while _gPrefix === "g", so
    // plain i/t/u/s/p/b keystrokes outside the prefix window are not swallowed.
    property string _gPrefix: ""

    Timer {
        id: gTimer
        interval: 1000
        repeat: false
        onTriggered: root._gPrefix = ""
    }

    // True when keyboard focus is inside a TextField/TextArea (or any item
    // that visibly accepts text input). Used to suppress the G-prefix start
    // so the user can still type the letter "g" inside QuickAdd, search,
    // editor description, etc.
    function _textInputFocused() {
        const obj = Qt.application.focusObject
        if (!obj)
            return false
        // Heuristic: TextField/TextArea expose `inputMethodHints`,
        // `cursorPosition` and `selectedText` properties; checking a couple
        // of them is enough to distinguish them from list items / buttons.
        if (obj.hasOwnProperty("cursorPosition") && obj.hasOwnProperty("selectedText"))
            return true
        if (obj.hasOwnProperty("inputMethodHints") && obj.hasOwnProperty("text")
                && obj.hasOwnProperty("cursorVisible"))
            return true
        return false
    }

    Shortcut {
        sequence: "G"
        context: Qt.WindowShortcut
        enabled: !root._textInputFocused()
                 && !(commandMenuLoader.item && commandMenuLoader.item.active)
                 && !(quickAddLoader.item && quickAddLoader.item.expanded)
        onActivated: {
            root._gPrefix = "g"
            gTimer.restart()
        }
    }

    Shortcut {
        sequence: "I"
        context: Qt.WindowShortcut
        enabled: root._gPrefix === "g"
        onActivated: {
            sidebarViewModel.navigateToInbox()
            root._gPrefix = ""
            gTimer.stop()
        }
    }
    Shortcut {
        sequence: "T"
        context: Qt.WindowShortcut
        enabled: root._gPrefix === "g"
        onActivated: {
            sidebarViewModel.navigateToToday()
            root._gPrefix = ""
            gTimer.stop()
        }
    }
    Shortcut {
        sequence: "U"
        context: Qt.WindowShortcut
        enabled: root._gPrefix === "g"
        onActivated: {
            sidebarViewModel.navigateToUpcoming()
            root._gPrefix = ""
            gTimer.stop()
        }
    }
    Shortcut {
        sequence: "S"
        context: Qt.WindowShortcut
        enabled: root._gPrefix === "g"
        onActivated: {
            sidebarViewModel.navigateTo("settings")
            root._gPrefix = ""
            gTimer.stop()
        }
    }
    Shortcut {
        sequence: "P"
        context: Qt.WindowShortcut
        enabled: root._gPrefix === "g"
        onActivated: {
            sidebarViewModel.navigateTo("productivity")
            root._gPrefix = ""
            gTimer.stop()
        }
    }
    Shortcut {
        sequence: "B"
        context: Qt.WindowShortcut
        enabled: root._gPrefix === "g"
        onActivated: {
            sidebarViewModel.navigateTo("trash")
            root._gPrefix = ""
            gTimer.stop()
        }
    }

    // Q / + → Quick Add: expand the QuickAdd bar and focus its text field
    Shortcut {
        sequence: "Q"
        enabled: root._taskListActive && !(commandMenuLoader.item && commandMenuLoader.item.active)
        onActivated: {
            if (quickAddLoader.item) quickAddLoader.item.focusInput()
        }
    }
    Shortcut {
        sequence: "+"
        enabled: root._taskListActive && !(commandMenuLoader.item && commandMenuLoader.item.active)
        onActivated: {
            if (quickAddLoader.item) quickAddLoader.item.focusInput()
        }
    }

    // Enter → Open editor for hovered task
    Shortcut {
        sequence: "Return"
        enabled: root._taskListActive && taskViewModel.hoveredTaskId !== ""
        onActivated: {
            const task = dataStore.taskById(taskViewModel.hoveredTaskId)
            if (task) taskViewModel.selectedTask = task
        }
    }

    // Space → Toggle complete (task list context only, not global)
    Shortcut {
        sequence: "Space"
        enabled: root._taskListActive && taskViewModel.hoveredTaskId !== ""
        onActivated: {
            const id = taskViewModel.hoveredTaskId
            const task = dataStore.taskById(id)
            if (!task) return
            if (task.isCompleted) taskViewModel.uncompleteTask(id)
            else taskViewModel.completeTask(id)
        }
    }

    // Delete → Delete hovered or bulk-delete selection
    // ITER2 #4: bulk path now opens the confirmation dialog so the keyboard
    // and on-screen button paths agree.
    // ITER2 #20: disable the shortcut while the task editor is open so
    // pressing Delete inside a description TextArea doesn't fall through
    // and delete the hovered task.
    Shortcut {
        sequence: "Delete"
        enabled: !isFullPageView
                 && taskViewModel.selectedTask === null
                 && (taskViewModel.selectionCount > 0 || taskViewModel.hoveredTaskId !== "")
        onActivated: {
            if (taskViewModel.selectionCount > 0)
                bulkDeleteConfirmDialog.open()
            else if (taskViewModel.hoveredTaskId !== "")
                taskViewModel.deleteTask(taskViewModel.hoveredTaskId)
        }
    }

    // 1 / 2 / 3 / 4 → Set priority (Todoist-parity hotkeys)
    // Guard: task list must be active, a task must be hovered, and QuickAdd
    // must not be expanded (otherwise digits typed in the input fall through).
    // When a selection exists, the bulk-priority shortcuts below take precedence
    // (gated via selectionCount === 0 here, selectionCount > 0 below).
    Shortcut {
        sequence: "1"
        enabled: root._taskListActive
                 && taskViewModel.selectionCount === 0
                 && taskViewModel.hoveredTaskId !== ""
                 && !(quickAddLoader.item && quickAddLoader.item.expanded)
        onActivated: {
            taskViewModel.setTaskPriority(taskViewModel.hoveredTaskId, 1)
            if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                telemetry.trackEvent("task_priority_via_hotkey", { "priority": 1 })
        }
    }
    Shortcut {
        sequence: "2"
        enabled: root._taskListActive
                 && taskViewModel.selectionCount === 0
                 && taskViewModel.hoveredTaskId !== ""
                 && !(quickAddLoader.item && quickAddLoader.item.expanded)
        onActivated: {
            taskViewModel.setTaskPriority(taskViewModel.hoveredTaskId, 2)
            if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                telemetry.trackEvent("task_priority_via_hotkey", { "priority": 2 })
        }
    }
    Shortcut {
        sequence: "3"
        enabled: root._taskListActive
                 && taskViewModel.selectionCount === 0
                 && taskViewModel.hoveredTaskId !== ""
                 && !(quickAddLoader.item && quickAddLoader.item.expanded)
        onActivated: {
            taskViewModel.setTaskPriority(taskViewModel.hoveredTaskId, 3)
            if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                telemetry.trackEvent("task_priority_via_hotkey", { "priority": 3 })
        }
    }
    Shortcut {
        sequence: "4"
        enabled: root._taskListActive
                 && taskViewModel.selectionCount === 0
                 && taskViewModel.hoveredTaskId !== ""
                 && !(quickAddLoader.item && quickAddLoader.item.expanded)
        onActivated: {
            taskViewModel.setTaskPriority(taskViewModel.hoveredTaskId, 4)
            if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                telemetry.trackEvent("task_priority_via_hotkey", { "priority": 4 })
        }
    }

    // 1 / 2 / 3 / 4 / 0 → Bulk set priority for selected tasks
    // Fires only when there is an active selection (selectionCount > 0).
    // Same QuickAdd / command palette / full-page-view guards as above so
    // typing a digit inside an input never falls through to the shortcut.
    // Key 0 maps to priority 4 (no priority / "снять приоритет").
    Repeater {
        model: [
            { seq: "1", priority: 1 },
            { seq: "2", priority: 2 },
            { seq: "3", priority: 3 },
            { seq: "4", priority: 4 },
            { seq: "0", priority: 4 }   // 0 = clear priority (P4)
        ]
        delegate: Item {
            // Wrapper Item satisfies Qt 6's "Delegate must be of Item type"
            // requirement; the actual hotkey is the child Shortcut. Item has
            // zero default size so it adds no visual footprint.
            Shortcut {
                sequence: modelData.seq
                context: Qt.WindowShortcut
                enabled: root._taskListActive
                         && taskViewModel.selectionCount > 0
                         && !(commandMenuLoader.item && commandMenuLoader.item.active)
                         && !(quickAddLoader.item && quickAddLoader.item.expanded)
                onActivated: {
                    const count = taskViewModel.selectionCount
                    taskViewModel.bulkSetPriority(modelData.priority)
                    if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                        telemetry.trackEvent("bulk_priority_keyboard",
                            { "priority": modelData.priority, "count": count })
                }
            }
        }
    }

    // Ctrl+D → Duplicate hovered task
    // Same gates as priority 1–4: task list active, a task hovered, no
    // QuickAdd expansion, no command palette open.
    Shortcut {
        sequence: "Ctrl+D"
        enabled: root._taskListActive
                 && taskViewModel.hoveredTaskId !== ""
                 && !(commandMenuLoader.item && commandMenuLoader.item.active)
                 && !(quickAddLoader.item && quickAddLoader.item.expanded)
        onActivated: {
            const newId = taskViewModel.duplicateTask(taskViewModel.hoveredTaskId)
            if (newId && newId !== "") {
                taskViewModel.hoveredTaskId = newId
                if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                    telemetry.trackEvent("task_duplicate_via_hotkey", {})
            }
        }
    }

    // F2 → Open task editor for the hovered task
    // Same gates as Ctrl+D: task list active, a task hovered, no command
    // palette open, no QuickAdd expansion.
    Shortcut {
        sequences: ["F2"]
        enabled: root._taskListActive
                 && taskViewModel.hoveredTaskId !== ""
                 && !(commandMenuLoader.item && commandMenuLoader.item.active)
                 && !(quickAddLoader.item && quickAddLoader.item.expanded)
        onActivated: {
            const t = dataStore.taskById(taskViewModel.hoveredTaskId)
            if (t) taskViewModel.selectedTask = t
        }
    }

    // Shift+T → Defer hovered task to tomorrow
    // Same gates as priority 1–4: task list active, a task hovered, no
    // command palette, no QuickAdd expansion.
    Shortcut {
        sequences: ["Shift+T"]
        enabled: root._taskListActive
                 && taskViewModel.hoveredTaskId !== ""
                 && !(commandMenuLoader.item && commandMenuLoader.item.active)
                 && !(quickAddLoader.item && quickAddLoader.item.expanded)
        onActivated: {
            taskViewModel.setTaskDue(taskViewModel.hoveredTaskId, "завтра")
            if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                telemetry.trackEvent("task_defer_tomorrow", {})
        }
    }

    // Shift+N → Defer hovered task to next week
    Shortcut {
        sequences: ["Shift+N"]
        enabled: root._taskListActive
                 && taskViewModel.hoveredTaskId !== ""
                 && !(commandMenuLoader.item && commandMenuLoader.item.active)
                 && !(quickAddLoader.item && quickAddLoader.item.expanded)
        onActivated: {
            taskViewModel.setTaskDue(taskViewModel.hoveredTaskId, "следующая неделя")
            if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                telemetry.trackEvent("task_defer_next_week", {})
        }
    }

    // Ctrl+R → Jump hover to a random uncompleted task in the current list
    Shortcut {
        sequences: ["Ctrl+R"]
        enabled: root._taskListActive
                 && taskViewModel.count > 0
                 && !(commandMenuLoader.item && commandMenuLoader.item.active)
                 && !(quickAddLoader.item && quickAddLoader.item.expanded)
        onActivated: {
            const total = taskViewModel.count
            if (total === 0) return
            // Pick random index, prefer non-completed
            let attempts = 0
            let idx = -1
            while (attempts < 10) {
                const candidate = Math.floor(Math.random() * total)
                const task = taskViewModel.taskAt(candidate)
                if (task && !task.isCompleted) {
                    idx = candidate
                    break
                }
                attempts++
            }
            // Fallback: just pick the first task if all 10 random tries hit completed ones
            if (idx < 0) idx = 0
            const task = taskViewModel.taskAt(idx)
            if (task) {
                taskViewModel.hoveredTaskId = task.id
                taskListView.positionViewAtIndex(idx, ListView.Center)
                if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                    telemetry.trackEvent("task_random_pick", {})
            }
        }
    }

    // ↑ ↓ → Keyboard navigation through the task list
    Shortcut {
        sequence: "Down"
        enabled: root._taskListActive && !(commandMenuLoader.item && commandMenuLoader.item.active)
        onActivated: root.navigateSelection(+1)
    }
    Shortcut {
        sequence: "Up"
        enabled: root._taskListActive && !(commandMenuLoader.item && commandMenuLoader.item.active)
        onActivated: root.navigateSelection(-1)
    }

    // J / K → Vim-style navigation (same gates as priority 1-4 so typing
    // in QuickAdd input is not intercepted)
    Shortcut {
        sequences: [Qt.Key_J]
        enabled: root._taskListActive
                 && !(commandMenuLoader.item && commandMenuLoader.item.active)
                 && !(quickAddLoader.item && quickAddLoader.item.expanded)
        onActivated: navigateSelection(1)
    }
    Shortcut {
        sequences: [Qt.Key_K]
        enabled: root._taskListActive
                 && !(commandMenuLoader.item && commandMenuLoader.item.active)
                 && !(quickAddLoader.item && quickAddLoader.item.expanded)
        onActivated: navigateSelection(-1)
    }

    // Ctrl+F / "/" → Focus search bar
    Shortcut {
        sequences: [StandardKey.Find]   // Ctrl+F + platform aliases
        onActivated: root.focusSearch()
    }
    Shortcut {
        sequence: "/"
        enabled: root._taskListActive && !(commandMenuLoader.item && commandMenuLoader.item.active)
        onActivated: root.focusSearch()
    }

    // Ctrl+K → Open command palette
    Shortcut {
        sequence: "Ctrl+K"
        onActivated: {
            if (commandMenuLoader.item)
                commandMenuLoader.item.open()
        }
    }

    // Ctrl+P → Quick jump task picker
    Shortcut {
        sequences: ["Ctrl+P"]
        enabled: !(commandMenuLoader.item && commandMenuLoader.item.active)
                 && !(quickAddLoader.item && quickAddLoader.item.expanded)
        onActivated: {
            quickJumpLoader.open()
            if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                telemetry.trackEvent("quick_jump_open", {})
        }
    }

    // Ctrl+M → Move selected task(s) to a project (fuzzy picker).
    // Only fires when there's something to move: bulk selection or an open editor.
    Shortcut {
        sequence: "Ctrl+M"
        context: Qt.WindowShortcut
        enabled: !(commandMenuLoader.item && commandMenuLoader.item.active)
                 && !(quickAddLoader.item && quickAddLoader.item.expanded)
                 && (taskViewModel.selectionCount > 0 || !!taskViewModel.selectedTask)
        onActivated: taskMoveLoader.open()
    }

    // ITER1 #34: Esc → Contextual: close palette → close editor → close QuickAdd
    //                  → clear selection → clear hover.
    // context: Qt.ApplicationShortcut ensures the shortcut fires even when a
    // TextArea inside TaskEditor has keyboard focus (TextArea normally consumes
    // the Escape key event before it can bubble up to a Window-level handler).
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (commandMenuLoader.item && commandMenuLoader.item.active) {
                commandMenuLoader.item.active = false
            } else if (!!taskViewModel.selectedTask) {  // B-6.22: null+undefined safe
                taskViewModel.selectedTask = null
            } else if (quickAddLoader.item && quickAddLoader.item.expanded) {
                quickAddLoader.item.collapse()
            } else if (taskViewModel.selectionCount > 0) {
                taskViewModel.clearSelection()
            } else {
                taskViewModel.hoveredTaskId = ""
                currentNavIndex = -1
            }
        }
    }

    // Ctrl+Z → Undo (F-6.8: wire to UndoStack context property)
    Shortcut {
        sequences: [StandardKey.Undo]   // Ctrl+Z + platform aliases (Alt+Backspace etc.)
        // ITER2 #10: guard against undefined context property on platforms /
        // test harnesses where the full main.cpp wiring is absent.
        onActivated: {
            if (typeof undoStack !== "undefined" && undoStack
                    && typeof undoStack.undo === "function")
                undoStack.undo()
        }
    }

    // I16-A: Ctrl+1..6 numeric navigation hotkeys
    Repeater {
        model: [
            { key: "Ctrl+1", view: "inbox",         filterMode: TaskViewModel.Inbox },
            { key: "Ctrl+2", view: "today",         filterMode: TaskViewModel.Today },
            { key: "Ctrl+3", view: "upcoming",      filterMode: TaskViewModel.Upcoming },
            { key: "Ctrl+4", view: "settings",      filterMode: TaskViewModel.Settings },
            { key: "Ctrl+5", view: "productivity",  filterMode: TaskViewModel.Productivity },
            { key: "Ctrl+6", view: "notifications", filterMode: TaskViewModel.Notifications }
        ]
        delegate: Item {
            // Wrapper Item satisfies Qt 6's "Delegate must be of Item type"
            // requirement; the actual hotkey is the child Shortcut.
            Shortcut {
                sequences: [modelData.key]
                enabled: !(commandMenuLoader.item && commandMenuLoader.item.active)
                         && !(quickAddLoader.item && quickAddLoader.item.expanded)
                onActivated: {
                    sidebarViewModel.setCurrentView(modelData.view)
                    taskViewModel.setFilterMode(modelData.filterMode)
                    if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                        telemetry.trackEvent("nav_hotkey", { "view": modelData.view })
                }
            }
        }
    }

    // ITER2 #22: Ctrl+A → Select all visible tasks (Todoist standard behaviour)
    // ITER3 #7: clearSelection() first wipes any stale m_selectionAnchorId
    // from a prior Ctrl+click; otherwise selectRange uses the old anchor and
    // silently selects only the sub-range starting at the previous click.
    Shortcut {
        sequence: "Ctrl+A"
        enabled: !isFullPageView && taskViewModel.count > 0
        onActivated: {
            taskViewModel.clearSelection()
            const first = taskViewModel.taskAt(0)
            const last  = taskViewModel.taskAt(taskViewModel.count - 1)
            if (first && last) taskViewModel.selectRange(first.id, last.id)
        }
    }

    // Ctrl+Shift+L → Toggle dark / light theme
    // No _taskListActive guard — theme toggle should work from any view.
    // Uses ThemeManager.isDark to detect the current polarity and switches
    // between the two baseline themes: "dark" (dark) and "classic_light" (light).
    Shortcut {
        sequences: ["Ctrl+Shift+L"]
        enabled: !(commandMenuLoader.item && commandMenuLoader.item.active)
                 && !(quickAddLoader.item && quickAddLoader.item.expanded)
        onActivated: {
            const target = ThemeManager.isDark ? "classic_light" : "dark"
            settingsViewModel.theme = target
            if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                telemetry.trackEvent("theme_quick_toggle", { "to": target })
        }
    }

    // ? / F1 → Show hotkeys help dialog
    Shortcut {
        sequence: "?"
        enabled: root._taskListActive && !(commandMenuLoader.item && commandMenuLoader.item.active)
        onActivated: hotkeysHelpDialog.open()
    }
    Shortcut {
        sequence: "F1"
        onActivated: hotkeysHelpDialog.open()
    }

    // F11 → Режим фокуса (скрыть боковую панель и верхнюю панель)
    Shortcut {
        sequence: "F11"
        context: Qt.WindowShortcut
        onActivated: root._toggleFocusMode()
    }

    // ── ITER1 #48: Bulk-delete confirmation dialog ───────────────────────────
    // Explicit Dialog.width breaks the implicitWidth feedback loop between
    // contentItem and the wrapping Label's wrapMode (same pattern as the
    // ProjectView confirmation dialogs).
    Dialog {
        id: bulkDeleteConfirmDialog
        title: qsTr("Удалить задачи")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(380, Overlay.overlay.width - Spacing.xl)
        standardButtons: Dialog.Ok | Dialog.Cancel

        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label {
                text: qsTr("Удалить %1 задач? Действие нельзя отменить через корзину.")
                      .arg(taskViewModel.selectionCount)
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                color: ThemeManager.textColor
                font.pixelSize: FontSize.bodySmall
            }
        }

        onAccepted: taskViewModel.bulkDelete()
    }

    // ── Hotkeys help dialog ───────────────────────────────────────────────────
    Dialog {
        id: hotkeysHelpDialog
        title: qsTr("Горячие клавиши")
        modal: true
        anchors.centerIn: Overlay.overlay  // B-6.26: parent resolves to DesktopLayout Item, not overlay
        standardButtons: Dialog.Close

        readonly property var hotkeys: [
            { key: "Q  /  +",         desc: qsTr("Открыть быстрое добавление задачи") },
            { key: "Enter",            desc: qsTr("Открыть редактор выделенной задачи") },
            { key: "Space",            desc: qsTr("Переключить выполнение задачи") },
            { key: "Delete",           desc: qsTr("Удалить задачу / удалить выделенные") },
            { key: "1  2  3  4",       desc: qsTr("Установить приоритет P1..P4") },
            { key: qsTr("1..4 (выделение)"), desc: qsTr("Установить приоритет P1..P4 для выделенных задач") },
            { key: qsTr("0 (выделение)"),    desc: qsTr("Снять приоритет с выделенных задач") },
            { key: "↑ / ↓ / J / K",   desc: qsTr("Перемещение по списку задач") },
            { key: "Ctrl+F  /  /",     desc: qsTr("Фокус на строке поиска") },
            { key: "Ctrl+K",           desc: qsTr("Открыть палитру команд") },
            { key: "Ctrl+P",           desc: qsTr("Перейти к задаче (поиск)") },
            { key: "Ctrl+M",           desc: qsTr("Переместить выделенные задачи в проект") },
            { key: "Esc",              desc: qsTr("Закрыть редактор / снять выделение") },
            { key: "Ctrl+Z",           desc: qsTr("Отменить действие") },  // F-6.9
            { key: "Ctrl+D",           desc: qsTr("Дублировать задачу") },
            { key: "?  /  F1",         desc: qsTr("Показать список горячих клавиш") },
            { key: "F11",              desc: qsTr("Режим фокуса — скрыть панели") },
            { key: "Shift+T",           desc: qsTr("Отложить на завтра") },
            { key: "Shift+N",           desc: qsTr("Отложить на следующую неделю") },
            { key: "Ctrl+R",            desc: qsTr("Случайная задача") },
            { key: "F2",               desc: qsTr("Открыть задачу в редакторе") },
            { key: "Ctrl+Shift+L",     desc: qsTr("Переключить тёмную/светлую тему") },
            { key: "Ctrl+1..6",        desc: qsTr("Перейти к разделу: Входящие / Сегодня / Предстоящие / Настройки / Продуктивность / Уведомления") },
            { key: "g, i",             desc: qsTr("Перейти ко Входящим") },
            { key: "g, t",             desc: qsTr("Перейти к Сегодня") },
            { key: "g, u",             desc: qsTr("Перейти к Предстоящим") },
            { key: "g, s",             desc: qsTr("Перейти к Настройкам") },
            { key: "g, p",             desc: qsTr("Перейти к Продуктивности") },
            { key: "g, b",             desc: qsTr("Перейти к Корзине") }
        ]

        ColumnLayout {
            spacing: 0
            width: 440

            Repeater {
                model: hotkeysHelpDialog.hotkeys

                delegate: Rectangle {
                    required property var modelData
                    required property int index

                    Layout.fillWidth: true
                    height: 36
                    // R-6.10: use theme tokens instead of hardcoded hex stripes
                    color: index % 2 === 0 ? ThemeManager.surfaceColor : ThemeManager.cardColor

                    RowLayout {
                        anchors {
                            fill: parent
                            leftMargin: 12
                            rightMargin: 12
                        }
                        spacing: 16

                        Rectangle {
                            implicitWidth: keyLabel.implicitWidth + 16
                            height: 22
                            radius: 4
                            color: ThemeManager.isDark ? "#333333" : "#e8e8e8"
                            Layout.alignment: Qt.AlignVCenter
                            Layout.minimumWidth: 110

                            Text {
                                id: keyLabel
                                anchors.centerIn: parent
                                text: modelData.key
                                font.pixelSize: FontSize.small
                                color: ThemeManager.accentColor
                            }
                        }

                        Text {
                            text: modelData.desc
                            font.pixelSize: FontSize.bodySmall
                            color: ThemeManager.textColor
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }

    // ── Inbox-zero / Today-zero confetti overlay ──────────────────────────────
    // Local instance so the burst draws on top of the desktop layout (the
    // ApplicationWindow's confettiOverlay is reserved for the daily-karma
    // goal; both can fire independently on the same day).
    ConfettiOverlay {
        id: inboxZeroConfetti
        anchors.fill: parent
    }

    // ── Vim g-prefix indicator ────────────────────────────────────────────────
    // Small pill in the bottom-right corner that appears for ~1s while the
    // user has pressed G and is waiting for the follow-up letter.
    Rectangle {
        id: gPrefixIndicator
        visible: root._gPrefix === "g"
        anchors {
            right: parent.right
            bottom: parent.bottom
            rightMargin: 16
            bottomMargin: 16
        }
        width: gPrefixLabel.implicitWidth + 16
        height: 24
        radius: 4
        color: ThemeManager.isDark ? "#333333" : "#e8e8e8"
        border.width: 1
        border.color: ThemeManager.borderColor
        opacity: 0.92
        z: 1000

        Text {
            id: gPrefixLabel
            anchors.centerIn: parent
            text: "g..."
            font.pixelSize: FontSize.small
            font.bold: true
            color: ThemeManager.accentColor
        }
    }

    // ── Плавающая «таблетка» выхода из режима фокуса ─────────────────────────
    // Видна только в режиме фокуса. Верхний правый угол; клик выключает режим.
    Rectangle {
        id: focusExitPill

        // Объявлен раньше property-привязок и ToolTip.* ниже, чтобы их
        // ссылки на focusExitMouse.containsMouse не получали undefined
        // на первой оценке.
        MouseArea {
            id: focusExitMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root._toggleFocusMode()
        }

        visible: root._focusMode
        anchors {
            right: parent.right
            top: parent.top
            rightMargin: 16
            topMargin: 16
        }
        width: focusExitRow.implicitWidth + 20
        height: 32
        radius: 16
        color: ThemeManager.isDark ? Qt.rgba(0, 0, 0, 0.55)
                                   : Qt.rgba(0, 0, 0, 0.08)
        border.width: 1
        border.color: ThemeManager.borderColor
        opacity: focusExitMouse.containsMouse ? 1.0 : 0.85
        z: 1000

        Behavior on opacity {
            NumberAnimation { duration: 120 }
        }

        Row {
            id: focusExitRow
            anchors.centerIn: parent
            spacing: 8

            // Task 17cea65b iter-3: было unicode "✕".
            IconSvg {
                anchors.verticalCenter: parent.verticalCenter
                name: "close"
                size: 13
                color: ThemeManager.textColor
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Выйти из режима фокуса")
                font.pixelSize: FontSize.small
                color: ThemeManager.textColor
            }
        }

        ToolTip.visible: focusExitMouse.containsMouse
        ToolTip.text: "F11"
        ToolTip.delay: 400
    }
}
