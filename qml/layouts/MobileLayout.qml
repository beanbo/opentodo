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

    // ── Sectioned list model (collapsible groups) ──────────────────────────────
    // The flat list is grouped per the design: Today → Просрочено/Сегодня,
    // Upcoming → by date, Project → by section, Inbox → "Задачи". Built client-
    // side as a flattened [header, task, task, …] array so collapse simply omits
    // a group's tasks. taskViewModel is already filtered per filterMode.
    property var _collapsedSections: ({})
    property var _flatModel: []
    function _humanDate(iso) {
        if (!iso || iso === "zzz") return qsTr("Без даты")
        var tomorrow = Qt.formatDate(new Date(Date.now() + 86400000), "yyyy-MM-dd")
        if (iso === ThemeManager.todayStr) return qsTr("Сегодня")
        if (iso === tomorrow)              return qsTr("Завтра")
        return DateFormatter.format(iso)
    }
    function _toggleSection(sid) {
        var c = Object.assign({}, root._collapsedSections)
        if (c[sid]) delete c[sid]; else c[sid] = true
        root._collapsedSections = c
        _rebuildSections()
    }
    function _rebuildSections() {
        if (typeof taskViewModel === "undefined") { root._flatModel = []; return }
        var fm = taskViewModel.filterMode
        function td(tk) {
            return { "kind": "task", "id": tk.id, "content": tk.content, "priority": tk.priority,
                     "isCompleted": tk.isCompleted, "dueDate": tk.dueDate, "dueString": tk.dueString,
                     "isRecurring": tk.dueIsRecurring, "deadline": tk.deadline, "labels": tk.labels,
                     "projectId": tk.projectId }
        }
        var groups = []
        var i, tk
        if (fm === TaskViewModel.Today) {
            var over = [], tod = []
            for (i = 0; i < taskViewModel.count; i++) {
                tk = taskViewModel.taskAt(i); if (!tk) continue
                if (tk.dueDate && tk.dueDate < ThemeManager.todayStr) over.push(td(tk)); else tod.push(td(tk))
            }
            if (over.length) groups.push({ "sid": "over", "title": qsTr("Просрочено"), "tasks": over })
            if (tod.length)  groups.push({ "sid": "tod",  "title": qsTr("Сегодня"),   "tasks": tod })
        } else if (fm === TaskViewModel.Upcoming) {
            var map = ({}), order = []
            for (i = 0; i < taskViewModel.count; i++) {
                tk = taskViewModel.taskAt(i); if (!tk) continue
                var d = tk.dueDate || "zzz"
                if (!map[d]) { map[d] = []; order.push(d) }
                map[d].push(td(tk))
            }
            order.sort()
            for (var k = 0; k < order.length; k++)
                groups.push({ "sid": order[k], "title": _humanDate(order[k]), "tasks": map[order[k]] })
        } else if (fm === TaskViewModel.Project) {
            var bySec = ({}), noSec = []
            for (i = 0; i < taskViewModel.count; i++) {
                tk = taskViewModel.taskAt(i); if (!tk) continue
                var sec = tk.sectionId || ""
                if (sec === "") noSec.push(td(tk))
                else { if (!bySec[sec]) bySec[sec] = []; bySec[sec].push(td(tk)) }
            }
            if (noSec.length) groups.push({ "sid": "__none__", "title": qsTr("Задачи"), "tasks": noSec })
            var secs = dataStore.sectionsForProject(taskViewModel.currentProjectId) || []
            var ordered = secs.slice().sort(function(a, b) { return (a.order || 0) - (b.order || 0) })
            for (var s = 0; s < ordered.length; s++)
                if (bySec[ordered[s].id]) groups.push({ "sid": ordered[s].id, "title": ordered[s].name, "tasks": bySec[ordered[s].id] })
        } else {
            var all = []
            for (i = 0; i < taskViewModel.count; i++) { tk = taskViewModel.taskAt(i); if (tk) all.push(td(tk)) }
            if (all.length) groups.push({ "sid": "all", "title": qsTr("Задачи"), "tasks": all })
        }
        var showHeaders = (fm === TaskViewModel.Today || fm === TaskViewModel.Upcoming
                           || fm === TaskViewModel.Project || fm === TaskViewModel.Inbox)
        var flat = []
        for (var g = 0; g < groups.length; g++) {
            var grp = groups[g]
            if (showHeaders) flat.push({ "kind": "header", "sid": grp.sid, "title": grp.title, "count": grp.tasks.length })
            if (!showHeaders || !root._collapsedSections[grp.sid])
                for (var ti = 0; ti < grp.tasks.length; ti++) flat.push(grp.tasks[ti])
        }
        root._flatModel = flat
    }
    Connections {
        target: taskViewModel
        function onCountChanged()            { Qt.callLater(root._rebuildSections) }
        function onTaskChanged()             { Qt.callLater(root._rebuildSections) }
        function onFilterModeChanged()       { root._collapsedSections = ({}); Qt.callLater(root._rebuildSections) }
        function onCurrentProjectIdChanged() { Qt.callLater(root._rebuildSections) }
        function onShowCompletedChanged()    { Qt.callLater(root._rebuildSections) }
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
        else if (v === "board") {
            // Open the busiest project in board mode (verification).
            var bb = null, bbn = -1
            for (var j = 0; j < projectViewModel.count; j++) {
                var pj = projectViewModel.projectAt(j)
                if (!pj || pj.isInbox || pj.isArchived) continue
                var nj = projectViewModel.taskCountForProject(pj.id)
                if (nj > bbn) { bbn = nj; bb = pj }
            }
            if (bb) sidebarViewModel.navigateToProject(bb.id)
            Qt.callLater(function() { taskViewModel.filterMode = TaskViewModel.Board })
        }
        else if (v === "calendar")     taskViewModel.filterMode = TaskViewModel.Calendar
        else if (v === "labels")       root.browseScreen = "labels"
        else if (v === "filters")      root.browseScreen = "filters"
        else if (v === "drawer")       Qt.callLater(function() { sidebarDrawer.open() })
        else if (v === "search")       Qt.callLater(function() { searchOverlay.open(); searchViewModel.search("на") })
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

    Component.onCompleted: { _loadPins(); _applyStartView(); _rebuildSections() }

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
        || taskViewModel.filterMode === TaskViewModel.Board

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

        // ── Project view-mode segment (Список / Доска / Календарь) ────────────
        MSeg {
            id: projectSegment
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 2
            Layout.bottomMargin: 8
            Layout.preferredHeight: 40
            visible: taskViewModel.filterMode === TaskViewModel.Project
            value: "list"
            options: [
                { "value": "list",  "label": qsTr("Список"),    "icon": "list" },
                { "value": "board", "label": qsTr("Доска"),     "icon": "board" },
                { "value": "cal",   "label": qsTr("Календарь"),  "icon": "calendar" }
            ]
            onChanged: function(m) {
                if (m === "board")    taskViewModel.filterMode = TaskViewModel.Board
                else if (m === "cal") taskViewModel.filterMode = TaskViewModel.Calendar
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
                case TaskViewModel.Board:         return "../components/mobile/MBoardScreen.qml"
                // Not yet redesigned for mobile — fall back to the desktop view:
                case TaskViewModel.AI:            return "../views/AIView.qml"
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
                // MBoardScreen: + button → quick-add; search → overlay; segment → view mode.
                function onAddRequested()    { quickAdd.openAdd() }
                function onSearchRequested() { searchOverlay.open() }
                function onViewModeChanged(mode) {
                    if (mode === "list")      taskViewModel.filterMode = TaskViewModel.Project
                    else if (mode === "board") taskViewModel.filterMode = TaskViewModel.Board
                    else if (mode === "cal")   taskViewModel.filterMode = TaskViewModel.Calendar
                }
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
                model: root._flatModel
                clip: true
                spacing: 0
                boundsBehavior: Flickable.StopAtBounds
                topMargin: mdens.listTopGap

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }

                // Mobile redesign: the model is a flattened [header, task, …]
                // array (see root._rebuildSections). Each entry renders either an
                // MSection collapsible header or a swipeable MTaskRow.
                delegate: Item {
                    id: delegateRoot
                    required property var modelData
                    width: taskListView.width
                    readonly property bool isHeader: modelData.kind === "header"
                    height: isHeader ? mdens.sectionH
                                     : (rowLoader.item ? rowLoader.item.implicitHeight : mdens.rowMinH)

                    Loader {
                        id: rowLoader
                        anchors.fill: parent
                        sourceComponent: delegateRoot.isHeader ? headerComp : taskRowComp
                    }

                    Component {
                        id: headerComp
                        MSection {
                            dens: mdens
                            title: delegateRoot.modelData.title
                            count: delegateRoot.modelData.count
                            collapsed: !!root._collapsedSections[delegateRoot.modelData.sid]
                            onToggled: root._toggleSection(delegateRoot.modelData.sid)
                        }
                    }

                    Component {
                        id: taskRowComp
                        MTaskRow {
                            dens: mdens
                            taskId:      delegateRoot.modelData.id
                            content:     delegateRoot.modelData.content
                            priority:    delegateRoot.modelData.priority
                            labels:      delegateRoot.modelData.labels
                            isCompleted: delegateRoot.modelData.isCompleted
                            dueDate:     delegateRoot.modelData.dueDate
                            dueString:   delegateRoot.modelData.dueString
                            isRecurring: delegateRoot.modelData.isRecurring
                            deadline:    delegateRoot.modelData.deadline
                            projectId:   delegateRoot.modelData.projectId
                            showProject: taskViewModel.filterMode !== TaskViewModel.Project
                            selectionMode: root.selectionMode
                            selected: (taskViewModel.selectionCount, taskViewModel.isSelected(delegateRoot.modelData.id))

                            onToggleComplete: {
                                if (delegateRoot.modelData.isCompleted) taskViewModel.uncompleteTask(delegateRoot.modelData.id)
                                else                                    taskViewModel.completeTask(delegateRoot.modelData.id)
                            }
                            onOpenTask: {
                                var t = dataStore.taskById(delegateRoot.modelData.id)
                                if (t) taskViewModel.selectedTask = t   // Connections pushes the editor
                            }
                            onSnooze: root._snoozeTask(delegateRoot.modelData.id)
                            onDeleteTask: taskViewModel.deleteTask(delegateRoot.modelData.id)
                            onSelectToggle: taskViewModel.toggleSelection(delegateRoot.modelData.id)
                            onLongPressed: root._openTaskContext(delegateRoot.modelData.id)
                        }
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

        onOpened: searchScreen.focusInput()
        onClosed: searchViewModel.clearSearch()

        background: Rectangle { color: contentColor }

        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Duration.normal }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Duration.fast }
        }

        // Mobile redesign: full search screen (recent searches + task results).
        MSearchScreen {
            id: searchScreen
            anchors.fill: parent
            onCloseRequested: searchOverlay.close()
            onTaskClicked: function(id) {
                var t = dataStore.taskById(id)
                if (t) taskViewModel.selectedTask = t   // Connections pushes the editor
                searchOverlay.close()
            }
        }
    }
}
