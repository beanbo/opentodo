import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtCore
import OpenTodoUI

Item {
    id: root

    readonly property bool  isDarkTheme:    ThemeManager.isDark
    readonly property color accentRed:     ThemeManager.accentColor
    readonly property color bgColor:        ThemeManager.backgroundColor
    readonly property color contentColor:   ThemeManager.surfaceColor
    readonly property color borderColor:    ThemeManager.borderColor
    readonly property color textPrimary:    ThemeManager.textColor
    readonly property color textSecondary:  ThemeManager.secondaryTextColor

    readonly property int sidebarWidth:      280
    readonly property int sidebarItemHeight: 36
    readonly property int taskItemHeight:    40
    readonly property int taskItemPadding:   Spacing.md
    readonly property int animationDuration: Duration.slow
    readonly property int fabSize:           56

    // ── Mobile redesign: bottom-nav pin state + shell-level computed flags ──
    // Pinned slots persist in QSettings (category "bottomNavPins"); up to 3
    // ids, each one of "sv:today" / "sv:upcoming" / a project id.
    Settings {
        id: navPinSettings
        category: "bottomNavPins"
        property string pinned: ""
    }
    property var navPins: []
    function _defaultPins() {
        // Default: «Сегодня» + the first two real projects (per the handoff),
        // falling back to «Предстоящие» when there aren't enough projects.
        var arr = ["sv:today"]
        if (typeof projectViewModel !== "undefined" && projectViewModel) {
            for (var i = 0; i < projectViewModel.count && arr.length < 3; i++) {
                var p = projectViewModel.projectAt(i)
                if (p && !p.isInbox && !p.isArchived) arr.push(p.id)
            }
        }
        if (arr.length < 3 && arr.indexOf("sv:upcoming") < 0) arr.push("sv:upcoming")
        return arr.slice(0, 3)
    }
    function _loadPins() {
        var s = navPinSettings.pinned
        var arr = (s ? s.split(",") : []).filter(function(x) { return x !== "" })
        navPins = (arr.length === 0) ? _defaultPins() : arr.slice(0, 3)
    }
    function togglePin(id) {
        var arr = navPins.slice()
        var idx = arr.indexOf(id)
        if (idx >= 0)                 arr.splice(idx, 1)          // unpin
        else if (arr.length >= 3)   { arr.shift(); arr.push(id) } // evict oldest
        else                          arr.push(id)
        navPins = arr
        navPinSettings.pinned = arr.join(",")
    }
    // Snooze (defer to tomorrow). setTaskDue does NOT push to the undo stack,
    // so we snapshot the prior due here and restore it from the snackbar.
    property var _pendingSnooze: null
    function _snoozeTask(id) {
        var t = dataStore.taskById(id)
        var prev = t ? ((t.dueString && t.dueString.length) ? t.dueString : (t.dueDate || "")) : ""
        taskViewModel.setTaskDue(id, "завтра")
        root._pendingSnooze = { "id": id, "prev": prev }
        mSnackbar.show(qsTr("Отложено на завтра"), qsTr("Отменить"))
    }

    // Long-press context menu (block 4).
    function _openTaskContext(id) {
        if (taskContextSheet)
            taskContextSheet.openFor(id)
    }
    property var taskContextSheet: null

    // Move-to-project picker (single task from context menu, or bulk selection).
    property var _moveIds: []
    function _buildProjectOptions() {
        var arr = []
        for (var i = 0; i < projectViewModel.count; i++) {
            var p = projectViewModel.projectAt(i)
            if (!p || p.isArchived) continue
            arr.push({ "id": p.id, "label": p.name,
                       "icon": (p.icon && p.icon.length > 0) ? p.icon : "hash",
                       "color": (p.color && p.color.length > 0) ? p.color : "" })
        }
        return arr
    }
    function _openMove(ids) {
        root._moveIds = ids
        movePicker.options = _buildProjectOptions()
        movePicker.subtitle = qsTr("Задач: %1").arg(ids.length)
        movePicker.open = true
    }

    // Dev/preview: jump to a starting view (OPENTODO_MOBILE_START env → context
    // property mobileStartView). No effect when unset.
    function _applyStartView() {
        if (typeof mobileStartView === "undefined" || !mobileStartView || mobileStartView.length === 0)
            return
        var v = mobileStartView
        if (v === "today")             sidebarViewModel.navigateToToday()
        else if (v === "upcoming")     sidebarViewModel.navigateToUpcoming()
        else if (v === "inbox")        sidebarViewModel.navigateToInbox()
        else if (v === "settings")     taskViewModel.filterMode = TaskViewModel.Settings
        else if (v === "productivity") taskViewModel.filterMode = TaskViewModel.Productivity
        else if (v === "notifications")taskViewModel.filterMode = TaskViewModel.Notifications
        else if (v === "trash")        taskViewModel.filterMode = TaskViewModel.Trash
        else if (v === "ai")           taskViewModel.filterMode = TaskViewModel.AI
        else if (v === "board")        taskViewModel.filterMode = TaskViewModel.Board
        else if (v === "calendar")     taskViewModel.filterMode = TaskViewModel.Calendar
        else if (v === "labels")       root.browseScreen = "labels"
        else if (v === "filters")      root.browseScreen = "filters"
        else if (v === "drawer")       Qt.callLater(function() { sidebarDrawer.open() })
        else if (v === "project") {
            // Pick the busiest non-inbox project (most tasks) for verification.
            var best = null, bestN = -1
            for (var i = 0; i < projectViewModel.count; i++) {
                var p = projectViewModel.projectAt(i)
                if (!p || p.isInbox || p.isArchived) continue
                var n = projectViewModel.taskCountForProject(p.id)
                if (n > bestN) { bestN = n; best = p }
            }
            if (best) sidebarViewModel.navigateToProject(best.id)
        }
        else if (v === "editor") {
            // Open the first task of the busiest project in the editor (verify).
            var b2 = null, b2n = -1
            for (var k = 0; k < projectViewModel.count; k++) {
                var pk = projectViewModel.projectAt(k)
                if (!pk || pk.isInbox || pk.isArchived) continue
                var nk = projectViewModel.taskCountForProject(pk.id)
                if (nk > b2n) { b2n = nk; b2 = pk }
            }
            if (b2) {
                sidebarViewModel.navigateToProject(b2.id)
                Qt.callLater(function() { var t = taskViewModel.taskAt(0); if (t) taskViewModel.selectedTask = t })
            }
        }
    }

    Component.onCompleted: { _loadPins(); _applyStartView() }

    // Recompute pins once the project model has loaded (default-pin resolution
    // needs real project ids) and whenever projects are added/removed. When the
    // user has saved pins, this just reloads the same persisted set.
    Connections {
        target: projectViewModel
        function onCountChanged() { root._loadPins() }
    }

    // Density tokens shared with all mobile child components.
    MDensity { id: mdens }

    // Which bottom-nav tab is active, derived from the current filter mode.
    readonly property string navKind: {
        switch (taskViewModel.filterMode) {
        case TaskViewModel.Inbox:    return "inbox"
        case TaskViewModel.Today:    return "today"
        case TaskViewModel.Upcoming: return "upcoming"
        case TaskViewModel.Project:  return "project"
        default:                     return "other"
        }
    }
    // Multi-select mode (block 4) hides the nav + FAB; for now it is simply
    // "any task selected".
    readonly property bool selectionMode: taskViewModel.selectionCount > 0
    readonly property bool navVisible: !selectionMode
    readonly property bool showFab:
        !selectionMode
        && taskEditorStack.depth === 0
        && !searchOverlay.opened
        && taskViewModel.filterMode !== TaskViewModel.Settings
        && taskViewModel.filterMode !== TaskViewModel.Productivity
        && taskViewModel.filterMode !== TaskViewModel.Notifications
        && taskViewModel.filterMode !== TaskViewModel.AI
        && taskViewModel.filterMode !== TaskViewModel.Trash

    // Browse overlay for the Метки / Фильтры list screens ("labels"|"filters"|"").
    property string browseScreen: ""

    // Full-page mobile screens that render their own MTopBar — the shell's own
    // top bar is hidden for these (and the browse overlay) to avoid a double header.
    readonly property bool _ownHeaderView:
        browseScreen !== ""
        || taskViewModel.filterMode === TaskViewModel.Settings
        || taskViewModel.filterMode === TaskViewModel.Productivity
        || taskViewModel.filterMode === TaskViewModel.Notifications
        || taskViewModel.filterMode === TaskViewModel.Calendar
        || taskViewModel.filterMode === TaskViewModel.Trash

    // ── List-view header (big collapsing app bar) data ─────────────────────────
    readonly property var _hdrProj: (taskViewModel.filterMode === TaskViewModel.Project
                                     && taskViewModel.currentProjectId !== "")
                                    ? projectViewModel.projectById(taskViewModel.currentProjectId) : null
    readonly property string _listTitle: {
        switch (taskViewModel.filterMode) {
        case TaskViewModel.Inbox:     return qsTr("Входящие")
        case TaskViewModel.Today:     return qsTr("Сегодня")
        case TaskViewModel.Upcoming:  return qsTr("Предстоящие")
        case TaskViewModel.Project:   return _hdrProj ? _hdrProj.name : qsTr("Проект")
        case TaskViewModel.Board:     return qsTr("Доска")
        case TaskViewModel.Label:     return qsTr("Метка: %1").arg(taskViewModel.currentFilter || "")
        case TaskViewModel.Filter:    return qsTr("Фильтр")
        case TaskViewModel.Completed: return qsTr("Завершённые")
        case TaskViewModel.AI:        return qsTr("Помощник")
        default:                      return qsTr("Задачи")
        }
    }
    readonly property string _listIcon: {
        switch (taskViewModel.filterMode) {
        case TaskViewModel.Inbox:     return "inbox"
        case TaskViewModel.Today:     return "today"
        case TaskViewModel.Upcoming:  return "upcoming"
        case TaskViewModel.Project:   return (_hdrProj && _hdrProj.icon && _hdrProj.icon.length > 0) ? _hdrProj.icon : "hash"
        case TaskViewModel.Board:     return "board"
        case TaskViewModel.Label:     return "tag"
        case TaskViewModel.Filter:    return "filter"
        case TaskViewModel.Completed: return "check-circle"
        case TaskViewModel.AI:        return "sparkles"
        default:                      return "inbox"
        }
    }
    readonly property color _listIconColor:
        (taskViewModel.filterMode === TaskViewModel.Project && _hdrProj && _hdrProj.color && _hdrProj.color.length > 0)
        ? _hdrProj.color : ThemeManager.accentColor
    readonly property string _listSubtitle: {
        switch (taskViewModel.filterMode) {
        case TaskViewModel.Today:    return new Date().toLocaleDateString(Qt.locale("ru_RU"), "d MMMM, dddd")
        case TaskViewModel.Upcoming: return new Date().toLocaleDateString(Qt.locale("ru_RU"), "MMMM yyyy")
        case TaskViewModel.Project:  return _hdrProj ? (projectViewModel.taskCountForProject(taskViewModel.currentProjectId) + " " + _taskWord(projectViewModel.taskCountForProject(taskViewModel.currentProjectId))) : ""
        default:                     return ""
        }
    }
    function _taskWord(n) {
        var m100 = n % 100, m10 = n % 10
        if (m100 >= 11 && m100 <= 14) return qsTr("задач")
        if (m10 === 1) return qsTr("задача")
        if (m10 >= 2 && m10 <= 4) return qsTr("задачи")
        return qsTr("задач")
    }

    // Q-Android-Audit #1.1: mirror DesktopLayout's full-page predicate so the
    // body Loader can swap to Settings / Productivity / Notifications / AI /
    // Board / Calendar / Trash views when the sidebar navigates there. Without
    // this, tapping those entries only updated the top-bar title — the screen
    // kept showing the inbox task list.
    readonly property bool isFullPageView:
        taskViewModel.filterMode === TaskViewModel.Settings
        || taskViewModel.filterMode === TaskViewModel.Productivity
        || taskViewModel.filterMode === TaskViewModel.Notifications
        || taskViewModel.filterMode === TaskViewModel.AI
        || taskViewModel.filterMode === TaskViewModel.Board
        || taskViewModel.filterMode === TaskViewModel.Calendar
        || taskViewModel.filterMode === TaskViewModel.Trash

    // Q-Android-Audit #1.2: back-button consumption ladder, called from
    // ApplicationWindow.onClosing in Main.qml. Returns true when the back press
    // was handled by app-level navigation (close editor stack → close search →
    // close drawer → navigate home); returns false only when the app is in its
    // ground state and the close should be accepted (app exit).
    function handleBack() {
        // 0) Bottom-nav pin-management sheet open
        if (pinSheet.open) {
            pinSheet.open = false
            return true
        }
        // 0a) Quick-add sheet open
        if (quickAdd.open) {
            quickAdd.open = false
            return true
        }
        // 0a2) Labels/Filters browse overlay open
        if (root.browseScreen !== "") {
            root.browseScreen = ""
            return true
        }
        // 0b) Task context menu / move picker open
        if (taskContextSheet && taskContextSheet.open) {
            taskContextSheet.open = false
            return true
        }
        if (movePicker.open) {
            movePicker.open = false
            return true
        }
        // 0c) Multi-select mode active → exit selection
        if (root.selectionMode) {
            taskViewModel.clearSelection()
            return true
        }
        // 1) Full-screen task editor open
        if (taskEditorStack.depth > 0) {
            // clear(), not pop(): the editor is pushed as the StackView's only
            // (root) item, and StackView.pop() is a no-op on the last/root item —
            // so pop() could NEVER close it (✕ and swipe-back both appeared
            // broken). clear() removes all items → depth 0 → editor closes.
            taskEditorStack.clear()
            return true
        }
        // 2) Search overlay open
        if (searchOverlay.opened) {
            searchOverlay.close()
            return true
        }
        // 3) Sidebar drawer open
        if (sidebarDrawer.opened) {
            sidebarDrawer.close()
            return true
        }
        // 4) Not on the home (Inbox) view — navigate home instead of exiting
        if (taskViewModel.filterMode !== TaskViewModel.Inbox) {
            if (typeof sidebarViewModel !== "undefined" && sidebarViewModel)
                sidebarViewModel.navigateToInbox()
            else
                taskViewModel.filterMode = TaskViewModel.Inbox
            return true
        }
        // 5) Already on home — let the OS close the app
        return false
    }

    anchors.fill: parent

    Rectangle {
        anchors.fill: parent
        color: bgColor
    }

    // ── Sidebar Drawer ────────────────────────────────────────────────────────
    Drawer {
        id: sidebarDrawer
        width: Math.min(308, root.width * 0.86)
        height: root.height
        edge: Qt.LeftEdge
        modal: true
        interactive: true

        // Animated open/close is handled natively by Drawer

        background: Rectangle {
            color: ThemeManager.surfaceColor
        }

        // Mobile redesign: native mobile drawer (profile + smart lists + projects
        // with pin toggles + Метки/Фильтры/Корзина + Settings).
        Loader {
            id: sidebarContentLoader
            anchors.fill: parent
            source: "../components/mobile/MDrawerContent.qml"
        }
        Binding {
            target: sidebarContentLoader.item
            property: "pins"
            value: root.navPins
            when: sidebarContentLoader.status === Loader.Ready
        }
        Connections {
            target: sidebarContentLoader.item
            ignoreUnknownSignals: true
            function onPinToggleRequested(id) { root.togglePin(id) }
            function onBrowseRequested(which) { root.browseScreen = which; sidebarDrawer.close() }
        }

        // Keep the view model in sync
        onOpened:  sidebarViewModel.sidebarVisible = true
        onClosed:  sidebarViewModel.sidebarVisible = false
    }

    // Sync drawer open/close to the view model
    Connections {
        target: sidebarViewModel
        function onSidebarVisibleChanged() {
            if (sidebarViewModel.sidebarVisible && !sidebarDrawer.opened) {
                sidebarDrawer.open()
            } else if (!sidebarViewModel.sidebarVisible && sidebarDrawer.opened) {
                sidebarDrawer.close()
            }
        }
        // Q-Android-Audit #1.6: auto-close the drawer on navigation. Other
        // mobile apps (Gmail, Todoist) dismiss their nav drawer when the user
        // picks an entry; previously the drawer stayed open covering the body.
        function onNavigationRequested(view, itemId) {
            if (sidebarDrawer.opened)
                sidebarDrawer.close()
            // Navigating to a label/filter from the browse screen dismisses it.
            root.browseScreen = ""
        }
    }

    // Bug 1 fix (mobile): holds the active NotificationsView instance so the
    // Connections block below can forward openTaskRequested to taskViewModel.
    // Wire this from any Loader that loads NotificationsView on mobile:
    //   notificationsViewItem = loader.item
    property var notificationsViewItem: null

    Connections {
        target: root.notificationsViewItem
        ignoreUnknownSignals: true
        function onOpenTaskRequested(taskId) {
            if (taskId && taskId.length > 0)
                taskViewModel.openTaskById(taskId)
        }
    }

    // ── Main ColumnLayout ─────────────────────────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        // Leave room for the bottom navigation (hidden in multi-select mode).
        anchors.bottomMargin: root.navVisible ? bottomNav.height : 0
        spacing: 0

        // ── Top bar (big collapsing app bar for list views) ──────────────────
        // Redesign: MTopBar renders a large title + icon + subtitle that
        // collapses to a compact 56px row once the list scrolls past 24px.
        // Hidden for mobile screens that carry their own MTopBar (and the
        // Метки/Фильтры browse overlay) to avoid a double header.
        MTopBar {
            id: topBar
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            visible: !root._ownHeaderView
            z: 2

            big: !root.isFullPageView && (taskListView.contentY < 24)
            title: root._listTitle
            titleIconName: root._listIcon
            titleColor: root._listIconColor
            subtitle: root._listSubtitle
            onMenuClicked: sidebarDrawer.open()

            // Show-completed toggle (hidden for board/AI desktop views).
            MIconButton {
                visible: !root.isFullPageView
                iconName: taskViewModel.showCompleted ? "eye" : "eye-off"
                iconColor: ThemeManager.textColor
                onClicked: {
                    taskViewModel.showCompleted = !taskViewModel.showCompleted
                    if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                        telemetry.trackEvent("toggle_show_completed", { "showing": taskViewModel.showCompleted })
                }
            }
            MIconButton {
                iconName: "search"
                iconColor: ThemeManager.textColor
                onClicked: searchOverlay.open()
            }
        }

        // ── Full-page view Loader ─────────────────────────────────────────────
        // Q-Android-Audit #1.1: mirrors DesktopLayout.qml's fullPageLoader so
        // that Settings / Productivity / Notifications / AI / Board / Calendar
        // / Trash actually render on mobile. The Loader's `active` gate keeps
        // the inbox-task-list path zero-cost when no full-page view is selected.
        Loader {
            id: fullPageLoader
            Layout.fillWidth: true
            Layout.fillHeight: true
            active: root.isFullPageView
            visible: active
            source: {
                switch (taskViewModel.filterMode) {
                // Mobile redesign screens:
                case TaskViewModel.Settings:      return "../components/mobile/MSettingsScreen.qml"
                case TaskViewModel.Productivity:  return "../components/mobile/MProductivityScreen.qml"
                case TaskViewModel.Notifications: return "../components/mobile/MNotificationsScreen.qml"
                case TaskViewModel.Calendar:      return "../components/mobile/MCalendarScreen.qml"
                case TaskViewModel.Trash:         return "../components/mobile/MTrashScreen.qml"
                // Not yet redesigned for mobile — fall back to the desktop views:
                case TaskViewModel.AI:            return "../views/AIView.qml"
                case TaskViewModel.Board:         return "../views/BoardView.qml"
                }
                return ""
            }

            // Bridge for the existing notificationsViewItem connection in
            // qml/layouts/MobileLayout.qml. When NotificationsView is the
            // active full-page view, expose its instance so the Connections
            // block can forward openTaskRequested to taskViewModel.
            onLoaded: {
                if (taskViewModel.filterMode === TaskViewModel.Notifications)
                    root.notificationsViewItem = item
                else
                    root.notificationsViewItem = null
            }
            onActiveChanged: {
                if (!active)
                    root.notificationsViewItem = null
            }

            // Q3.27: forward BoardView.taskClicked (board mode on mobile) into
            // the editor stack. Mirrors the inline list delegate at line ~450
            // so a card tap opens the same full-screen editor page.
            Connections {
                target: fullPageLoader.item
                ignoreUnknownSignals: true
                function onTaskClicked(taskId) {
                    if (!taskId || taskId.length === 0) return
                    const t = dataStore.taskById(taskId)
                    if (t) taskViewModel.selectedTask = t   // Connections pushes the editor
                }
                // Mobile screens expose menuRequested (hamburger) + aiRequested.
                function onMenuRequested() { sidebarDrawer.open() }
                function onAiRequested()   { sidebarViewModel.navigateTo("ai") }
            }
        }

        // ── Upcoming calendar strip (mobile) ─────────────────────────────────
        // Shows the compact monthly calendar above the quick-add bar when the
        // user is in Upcoming mode.  Collapsible via the chevron in the header.
        UpcomingCalendarPanel {
            id: mobileCalendarStrip
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight

            visible: taskViewModel.filterMode === TaskViewModel.Upcoming
                     && !root.isFullPageView

            widePanel: false

            onDayPicked: function(picked) {
                // Scroll the mobile task list to the picked date
                var iso = Qt.formatDate(picked, "yyyy-MM-dd")
                for (var i = 0; i < taskViewModel.count; i++) {
                    var t = taskViewModel.taskAt(i)
                    if (t && t.dueDate === iso) {
                        taskListView.positionViewAtIndex(i, ListView.Beginning)
                        return
                    }
                }
                // Fallback: first task on or after picked date
                for (var j = 0; j < taskViewModel.count; j++) {
                    var tj = taskViewModel.taskAt(j)
                    if (tj && tj.dueDate >= iso) {
                        taskListView.positionViewAtIndex(j, ListView.Beginning)
                        return
                    }
                }
            }
        }

        // ── Quick-add bar ─────────────────────────────────────────────────────
        // ITER3 #14: mirror DesktopLayout — bind Layout.preferredHeight to the
        // QuickAdd component's actual height so expanded toolbar is not clipped.
        // Q-Android-Audit #1.1: hide when a full-page view is active.
        Rectangle {
            id: quickAddBar
            Layout.fillWidth: true
            // Mobile redesign: quick-add is now the FAB→MQuickAdd sheet, so the
            // persistent inline bar is hidden (matches the design — no always-on
            // quick-add strip on mobile).
            Layout.preferredHeight: 0
            color: contentColor
            visible: false
            z: 1

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
                    fill: parent
                    leftMargin: taskItemPadding
                    rightMargin: taskItemPadding
                    topMargin: 6
                    bottomMargin: 6
                }
                source: "../components/QuickAdd.qml"
            }
        }

        // ── Task list (fills remaining space) ─────────────────────────────────
        // Q-Android-Audit #1.1: hidden when a full-page view is rendered above
        // — fullPageLoader already takes the same row-Layout slot via
        // Layout.fillHeight, so we simply zero this out.
        Item {
            id: taskListContainer
            Layout.fillWidth: true
            Layout.fillHeight: !root.isFullPageView
            Layout.preferredHeight: root.isFullPageView ? 0 : -1
            visible: !root.isFullPageView

            ListView {
                id: taskListView
                anchors.fill: parent
                model: taskViewModel
                clip: true
                spacing: 0
                boundsBehavior: Flickable.StopAtBounds

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }

                // ITER1 #3: old Loader set item.taskData and connected item.taskSelected —
                // neither exists on TaskItem. Replaced with the same Item+TaskItem pattern
                // used by DesktopLayout so all properties and signals are correctly bound.
                delegate: Item {
                    id: delegateRoot
                    width: taskListView.width
                    height: mRow.implicitHeight

                    required property int    index
                    required property string id
                    required property string content
                    required property int    priority
                    required property var    labels
                    required property bool   isCompleted
                    required property string dueDate
                    required property string dueString
                    required property bool   isRecurring
                    required property string deadline
                    required property string projectId

                    // Mobile redesign: swipeable MTaskRow (replaces the desktop
                    // TaskItem on mobile). Swipe right → complete, left → snooze/
                    // delete, long-press → context menu, tap → editor.
                    MTaskRow {
                        id: mRow
                        width: parent.width
                        dens: mdens

                        taskId:      delegateRoot.id
                        content:     delegateRoot.content
                        priority:    delegateRoot.priority
                        labels:      delegateRoot.labels
                        isCompleted: delegateRoot.isCompleted
                        dueDate:     delegateRoot.dueDate
                        dueString:   delegateRoot.dueString
                        isRecurring: delegateRoot.isRecurring
                        deadline:    delegateRoot.deadline
                        projectId:   delegateRoot.projectId
                        showProject: taskViewModel.filterMode !== TaskViewModel.Project
                        selectionMode: root.selectionMode
                        // selectionCount touch → re-evaluate isSelected reactively.
                        selected: (taskViewModel.selectionCount, taskViewModel.isSelected(delegateRoot.id))

                        onToggleComplete: {
                            if (delegateRoot.isCompleted) taskViewModel.uncompleteTask(delegateRoot.id)
                            else                          taskViewModel.completeTask(delegateRoot.id)
                        }
                        onOpenTask: {
                            const t = dataStore.taskById(delegateRoot.id)
                            if (t) taskViewModel.selectedTask = t   // Connections pushes the editor
                        }
                        onSnooze: root._snoozeTask(delegateRoot.id)
                        onDeleteTask: taskViewModel.deleteTask(delegateRoot.id)
                        onSelectToggle: taskViewModel.toggleSelection(delegateRoot.id)
                        onLongPressed: root._openTaskContext(delegateRoot.id)
                    }
                }

                // Per-view empty state (icon badge + title + text).
                MEmptyState {
                    anchors.centerIn: parent
                    visible: taskListView.count === 0
                    iconName: {
                        switch (taskViewModel.filterMode) {
                        case TaskViewModel.Today:    return "check-circle"
                        case TaskViewModel.Inbox:    return "inbox"
                        case TaskViewModel.Upcoming: return "upcoming"
                        case TaskViewModel.Project:  return root._listIcon
                        default:                     return "list"
                        }
                    }
                    iconColor: taskViewModel.filterMode === TaskViewModel.Today
                               ? ThemeManager.successColor : root._listIconColor
                    title: {
                        switch (taskViewModel.filterMode) {
                        case TaskViewModel.Today:    return qsTr("На сегодня всё")
                        case TaskViewModel.Inbox:    return qsTr("Входящие пусты")
                        case TaskViewModel.Upcoming: return qsTr("Ничего не запланировано")
                        case TaskViewModel.Project:  return qsTr("Пока пусто")
                        default:                     return qsTr("Список пуст")
                        }
                    }
                    text: {
                        switch (taskViewModel.filterMode) {
                        case TaskViewModel.Today:    return qsTr("Задач на сегодня нет.\nОтличная работа!")
                        case TaskViewModel.Inbox:    return qsTr("Новые задачи будут падать сюда.\nНажмите + чтобы добавить.")
                        case TaskViewModel.Upcoming: return qsTr("Поставьте задачам сроки —\nи они появятся здесь.")
                        case TaskViewModel.Project:  return qsTr("Добавьте первую задачу\nв этот проект.")
                        default:                     return qsTr("Здесь пока нет задач.")
                        }
                    }
                }
            }

            // FAB moved to root level (mobile redesign → MFab, floats above the
            // bottom navigation). See `mobileFab` below, near the editor stack.
        }
    }

    // ── Bottom navigation + FAB + pin sheet (mobile redesign) ──────────────────
    MBottomNav {
        id: bottomNav
        anchors.left:   parent.left
        anchors.right:  parent.right
        anchors.bottom: parent.bottom
        z: 15
        visible: root.navVisible
        pins: root.navPins
        activeKind: root.navKind
        activeProjectId: taskViewModel.currentProjectId
        searchOpen: searchOverlay.opened
        onTabSelected: function(kind, projectId) {
            taskViewModel.clearSelection()
            if (kind === "inbox")         sidebarViewModel.navigateToInbox()
            else if (kind === "today")    sidebarViewModel.navigateToToday()
            else if (kind === "upcoming") sidebarViewModel.navigateToUpcoming()
            else if (kind === "project")  sidebarViewModel.navigateToProject(projectId)
        }
        onManagePins: pinSheet.open = true
    }

    MFab {
        id: mobileFab
        anchors.right:  parent.right
        anchors.rightMargin: 18
        anchors.bottom: parent.bottom
        anchors.bottomMargin: (root.navVisible ? bottomNav.height : 0) + 18
        z: 16
        visible: root.showFab
        onClicked: quickAdd.openAdd()
    }

    // ── Quick-add sheet (mobile redesign) ──────────────────────────────────────
    MQuickAdd {
        id: quickAdd
        anchors.fill: parent
        z: 105
        onCloseRequested: open = false
        onCreated: function(message) { root._pendingSnooze = null; mSnackbar.show(message, "") }
    }

    // Single editor-open mechanism: setting taskViewModel.selectedTask (from a
    // row tap, context menu, search, or notification) pushes the editor page.
    // The StackView clears selectedTask on close, so this never re-fires.
    Connections {
        target: taskViewModel
        function onSelectedTaskChanged() {
            if (taskViewModel.selectedTask && taskEditorStack.depth === 0)
                taskEditorStack.push(taskEditorPage)
        }
    }

    // ── Метки / Фильтры browse overlay (opened from the drawer) ───────────────
    Loader {
        id: browseLoader
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.navVisible ? bottomNav.height : 0
        z: 17
        active: root.browseScreen !== ""
        visible: active
        source: root.browseScreen === "labels"  ? "../components/mobile/MLabelsScreen.qml"
              : root.browseScreen === "filters" ? "../components/mobile/MFiltersScreen.qml" : ""
        Connections {
            target: browseLoader.item
            ignoreUnknownSignals: true
            function onMenuRequested() { root.browseScreen = ""; sidebarDrawer.open() }
        }
    }

    MPinSheet {
        id: pinSheet
        anchors.fill: parent
        z: 100
        pins: root.navPins
        onTogglePin: function(id) { root.togglePin(id) }
        onCloseRequested: open = false
    }

    // ── Undo snackbar (mobile) ─────────────────────────────────────────────────
    MSnackbar {
        id: mSnackbar
        anchors.fill: parent
        z: 95
        bottomMargin: root.navVisible ? (bottomNav.height + 12) : 18
        onActionTriggered: {
            if (root._pendingSnooze) {
                taskViewModel.setTaskDue(root._pendingSnooze.id, root._pendingSnooze.prev)
                root._pendingSnooze = null
            } else {
                undoStack.undo()
            }
        }
    }

    // ── Multi-select toolbar (replaces the top bar while selecting) ────────────
    Rectangle {
        id: selectionBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 58
        z: 18
        visible: root.selectionMode
        color: ThemeManager.accentContainer

        MIconButton {
            id: selClose
            anchors.left: parent.left
            anchors.leftMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            iconName: "close"; iconColor: ThemeManager.accentColor; iconSize: 22
            onClicked: taskViewModel.clearSelection()
        }
        Text {
            anchors.left: selClose.right
            anchors.right: selActions.left
            anchors.verticalCenter: parent.verticalCenter
            leftPadding: 6
            text: qsTr("%1 выбрано").arg(taskViewModel.selectionCount)
            font.pixelSize: 18; font.weight: Font.Bold
            color: ThemeManager.accentColor
            elide: Text.ElideRight
        }
        Row {
            id: selActions
            anchors.right: parent.right
            anchors.rightMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            MIconButton {
                iconName: "check-circle"; iconColor: ThemeManager.accentColor; iconSize: 22
                onClicked: {
                    var n = taskViewModel.selectionCount
                    taskViewModel.bulkComplete()
                    mSnackbar.show(qsTr("Выполнено: %1").arg(n), qsTr("Отменить"))
                    taskViewModel.clearSelection()
                }
            }
            MIconButton {
                iconName: "folder"; iconColor: ThemeManager.accentColor; iconSize: 21
                onClicked: root._openMove(taskViewModel.selectedIds.slice())
            }
            MIconButton {
                iconName: "trash"; iconColor: ThemeManager.errorColor; iconSize: 21
                onClicked: {
                    taskViewModel.bulkDelete()
                    taskViewModel.clearSelection()
                }
            }
        }
    }

    // ── Task context menu (long-press) ─────────────────────────────────────────
    MContextSheet {
        id: contextSheet
        anchors.fill: parent
        z: 100
        Component.onCompleted: root.taskContextSheet = contextSheet
        onCloseRequested: open = false
        onOpenRequested: function(id) {
            var t = dataStore.taskById(id)
            if (t) taskViewModel.selectedTask = t
        }
        onToggleCompleteRequested: function(id) {
            var t = dataStore.taskById(id)
            if (t) { if (t.isCompleted) taskViewModel.uncompleteTask(id); else taskViewModel.completeTask(id) }
        }
        onSelectMultipleRequested: function(id) { taskViewModel.selectTask(id) }
        onMoveRequested: function(id) { root._openMove([id]) }
        onDuplicateRequested: function(id) { taskViewModel.duplicateTask(id) }
        onDeleteRequested: function(id) { taskViewModel.deleteTask(id) }
    }

    // ── Move-to-project picker ─────────────────────────────────────────────────
    MOptionSheet {
        id: movePicker
        anchors.fill: parent
        z: 100
        title: qsTr("Переместить в проект")
        onCloseRequested: open = false
        onSelected: function(id) {
            if (root._moveIds.length === 1) taskViewModel.moveTask(root._moveIds[0], id, "")
            else                            taskViewModel.bulkMoveToProject(id)
            movePicker.open = false
            var p = projectViewModel.projectById(id)
            root._pendingSnooze = null
            mSnackbar.show(qsTr("Перемещено → %1").arg(p ? p.name : ""), qsTr("Отменить"))
            taskViewModel.clearSelection()
        }
    }

    // Drive the mobile snackbar from task mutations (single source of undo).
    Connections {
        target: taskViewModel
        function onTaskRemoved(id) {
            root._pendingSnooze = null
            mSnackbar.show(undoStack.lastDescription || qsTr("Задача удалена"), qsTr("Отменить"))
        }
        function onTasksRemoved(ids) {
            var n = ids ? ids.length : 0
            if (n <= 0) return
            root._pendingSnooze = null
            mSnackbar.show(undoStack.lastDescription || qsTr("Удалено задач: %1").arg(n), qsTr("Отменить"))
        }
        function onTaskChanged(id) {
            var t = dataStore.taskById(id)
            if (t && t.isCompleted) {
                root._pendingSnooze = null
                mSnackbar.show(undoStack.lastDescription || qsTr("Задача завершена"), qsTr("Отменить"))
            }
        }
    }

    // ── Full-screen Task Editor overlay (StackView) ───────────────────────────
    StackView {
        id: taskEditorStack
        anchors.fill: parent
        visible: depth > 0
        z: 20

        // ITER2 #25: clear taskViewModel.selectedTask when the stack is fully
        // popped so the global selection state matches the visible UI.
        onDepthChanged: {
            if (depth === 0)
                taskViewModel.selectedTask = null
        }

        // Slide-up push / slide-down pop transitions
        pushEnter: Transition {
            NumberAnimation {
                property: "y"
                from: taskEditorStack.height
                to: 0
                duration: animationDuration
                easing.type: Easing.OutCubic
            }
        }
        pushExit: Transition {
            // Underlying content stays in place
        }
        popEnter: Transition {}
        popExit: Transition {
            NumberAnimation {
                property: "y"
                from: 0
                to: taskEditorStack.height
                duration: animationDuration
                easing.type: Easing.InCubic
            }
        }

        // Component used as a page inside the StackView
        Component {
            id: taskEditorPage

            Rectangle {
                color: contentColor

                // Mobile redesign: full-screen MEditorScreen (its own toolbar,
                // property pickers, subtasks, comments, history).
                MEditorScreen {
                    anchors.fill: parent
                    onCloseRequested: taskEditorStack.clear()
                }
            }
        }
    }

    // ── Search overlay (full-screen popup) ────────────────────────────────────
    Popup {
        id: searchOverlay
        anchors.centerIn: Overlay.overlay
        // B-6.29: use Overlay.overlay dimensions so the popup fills the actual overlay, not root
        width: Overlay.overlay ? Overlay.overlay.width : root.width
        height: Overlay.overlay ? Overlay.overlay.height : root.height
        padding: 0
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle { color: contentColor }

        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Duration.normal }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Duration.fast }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                color: contentColor

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
                        leftMargin: 4
                        rightMargin: 12
                    }

                    // B-9.9: was a literal "←" (U+2190) text glyph — tofu box on
                    // Android (Roboto lacks it). Use IconSvg "chevron-left".
                    ToolButton {
                        contentItem: IconSvg {
                            anchors.centerIn: parent
                            name: "chevron-left"
                            size: 22
                            color: textSecondary
                        }
                        onClicked: searchOverlay.close()
                    }

                    Loader {
                        source: "../components/SearchBar.qml"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                    }
                }
            }

            // Search results
            //
            // C++ side (SearchViewModel::performSearch) emits each entry as a
            // QVariantMap with keys: {type, id, title, subtitle, projectId,
            // isCompleted, color, taskId, object}. Earlier versions of this
            // delegate tried to bind task-style fields (content, description,
            // priority, dueDate…) that the C++ side never produces — every
            // task row rendered as an empty TaskItem with priority 4 and no
            // text. Mirror the desktop SearchBar.qml shape: a compact row
            // with a type icon, highlighted title, and subtitle.
            ListView {
                id: searchResultsView
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: searchViewModel.results
                clip: true

                delegate: Rectangle {
                    id: searchDelegateRoot
                    width: searchResultsView.width
                    height: 56

                    // modelData is the plain JS object produced by
                    // SearchViewModel::performSearch — see keys above.
                    required property var modelData
                    required property int index

                    color: rowHover.containsMouse
                           ? Qt.rgba(accentRed.r, accentRed.g, accentRed.b,
                                     isDarkTheme ? 0.10 : 0.06)
                           : "transparent"

                    Behavior on color { ColorAnimation { duration: Duration.instant } }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin:  Spacing.md
                        anchors.rightMargin: Spacing.md
                        spacing: Spacing.sm

                        // Type glyph mirrors the desktop search bar.
                        // Q4.01 (redesign): "🏷"/"💬" эмодзи заменены на
                        // IconSvg ("label"/"note") из stroke-пака; остальные
                        // типы держат однознаковые маркеры — единый визуал.
                        Item {
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18

                            IconSvg {
                                visible: searchDelegateRoot.modelData
                                         && (searchDelegateRoot.modelData.type === "label"
                                             || searchDelegateRoot.modelData.type === "comment")
                                anchors.centerIn: parent
                                name: searchDelegateRoot.modelData && searchDelegateRoot.modelData.type === "comment"
                                      ? "note" : "label"
                                size: 14
                                color: textSecondary
                            }

                            Label {
                                visible: !searchDelegateRoot.modelData
                                         || (searchDelegateRoot.modelData.type !== "label"
                                             && searchDelegateRoot.modelData.type !== "comment")
                                anchors.centerIn: parent
                                font.pixelSize: FontSize.body
                                color: textSecondary
                                text: {
                                    if (!searchDelegateRoot.modelData) return ""
                                    switch (searchDelegateRoot.modelData.type) {
                                        case "task":    return "✓"
                                        case "project": return "#"
                                        case "filter":  return "⊙"
                                        default:        return "•"
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            spacing: 2

                            Label {
                                Layout.fillWidth: true
                                text: searchDelegateRoot.modelData
                                      ? (searchDelegateRoot.modelData.title || "")
                                      : ""
                                color: textPrimary
                                font.pixelSize: FontSize.body
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: searchDelegateRoot.modelData
                                         && searchDelegateRoot.modelData.subtitle
                                         && searchDelegateRoot.modelData.subtitle !== ""
                                text: searchDelegateRoot.modelData
                                      ? (searchDelegateRoot.modelData.subtitle || "")
                                      : ""
                                color: textSecondary
                                font.pixelSize: FontSize.caption
                                elide: Text.ElideRight
                            }
                        }
                    }

                    MouseArea {
                        id: rowHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            const md = searchDelegateRoot.modelData
                            if (!md) return
                            // For task results open the editor; for projects,
                            // labels and filters fall back to closing the
                            // overlay (mobile navigation lands on the relevant
                            // view through other paths).
                            if (md.type === "task" && md.id) {
                                taskViewModel.openTaskById(md.id)
                            } else if (md.type === "comment" && md.taskId) {
                                taskViewModel.openTaskById(md.taskId)
                            } else if (md.type === "comment" && md.projectId) {
                                // iter-night-6: project-level comments now
                                // route to the project view rather than
                                // silently doing nothing.
                                sidebarViewModel.navigateToProject(md.projectId)
                            } else if (md.type === "project" && md.id) {
                                sidebarViewModel.navigateToProject(md.id)
                            } else if (md.type === "label" && md.title) {
                                sidebarViewModel.navigateToLabel(md.title)
                            } else if (md.type === "filter" && md.id) {
                                sidebarViewModel.navigateToFilter(md.id)
                            }
                            searchOverlay.close()
                        }
                    }

                    // Hairline divider between rows
                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.left:   parent.left
                        anchors.right:  parent.right
                        height: 1
                        color: borderColor
                        opacity: 0.4
                    }
                }

                // Empty state — Russian copy, matches the rest of the UI.
                // Fix: previous build referenced `searchViewModel.query`, which
                // does not exist — the Q_PROPERTY is `searchQuery`.
                Label {
                    anchors.centerIn: parent
                    visible: searchResultsView.count === 0
                             && searchViewModel.searchQuery.length > 0
                    text: qsTr("Ничего не найдено")
                    color: textSecondary
                    font.pixelSize: FontSize.body
                }
            }
        }
    }
}
