import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
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
        // 1) Full-screen task editor open
        if (taskEditorStack.depth > 0) {
            taskEditorStack.pop()
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
        width: sidebarWidth
        height: root.height
        edge: Qt.LeftEdge
        modal: true
        interactive: true

        // Animated open/close is handled natively by Drawer

        background: Rectangle {
            color: contentColor
        }

        Loader {
            id: sidebarContentLoader
            anchors.fill: parent
            source: "../components/SidebarContent.qml"
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
        spacing: 0

        // ── Top bar ───────────────────────────────────────────────────────────
        Rectangle {
            id: topBar
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: contentColor
            z: 2

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
                    leftMargin: Spacing.xs
                    rightMargin: Spacing.xs
                }
                spacing: 0

                // Hamburger button
                // B-6.34: qrc:/icons/menu.svg doesn't exist — use Unicode glyph
                ToolButton {
                    id: hamburgerButton
                    text: "☰"
                    font.pixelSize: FontSize.title
                    contentItem: Text {
                        text: parent.text
                        font: parent.font
                        color: textSecondary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    implicitWidth: 48
                    implicitHeight: 48
                    onClicked: sidebarDrawer.open()
                    KeyNavigation.tab: searchButton  // F-6.96 a11y

                    ToolTip.visible: pressed
                    ToolTip.text: qsTr("Open menu")
                }

                // Current view title
                // ITER2 #2: filterViewModel.currentViewTitle doesn't exist —
                // FilterViewModel is a list model, not a navigation helper. Use
                // the same filter-mode switch as DesktopLayout.
                Label {
                    id: viewTitleLabel
                    text: {
                        switch (taskViewModel.filterMode) {
                        case TaskViewModel.Inbox:     return qsTr("Входящие")
                        case TaskViewModel.Today:     return qsTr("Сегодня")
                        case TaskViewModel.Upcoming:  return qsTr("Предстоящие")
                        case TaskViewModel.Project: {
                            const p = projectViewModel
                                ? projectViewModel.projectById(taskViewModel.currentProjectId) : null
                            return p ? p.name : qsTr("Проект")
                        }
                        case TaskViewModel.Board:     return qsTr("Доска")
                        case TaskViewModel.Calendar:  return qsTr("Календарь")
                        case TaskViewModel.Label:     return qsTr("Метка: %1").arg(taskViewModel.currentFilter || "")
                        case TaskViewModel.Filter:    return qsTr("Фильтр")
                        case TaskViewModel.Completed: return qsTr("Завершённые")
                        case TaskViewModel.Settings:  return qsTr("Настройки")
                        case TaskViewModel.Productivity:  return qsTr("Продуктивность")
                        case TaskViewModel.Notifications: return qsTr("Уведомления")
                        // Q4.01 (redesign): убран префикс "✨" — иконка несётся
                        // нижней нав-панелью.
                        case TaskViewModel.AI:        return qsTr("Помощник")
                        // Q-Android-Audit speculative-finding: Trash was missing
                        // from the title switch; tapping "Корзина" showed
                        // "Задачи" as the fallback title.
                        case TaskViewModel.Trash:     return qsTr("Корзина")
                        default:                      return qsTr("Задачи")
                        }
                    }
                    font.pixelSize: FontSize.title
                    font.weight: Font.DemiBold
                    color: textPrimary
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    leftPadding: Spacing.sm
                }

                // Search icon button — opens the search overlay
                // B-6.34: qrc:/icons/search.svg doesn't exist — use Unicode glyph
                ToolButton {
                    id: searchButton
                    text: "⌕"
                    font.pixelSize: FontSize.title
                    contentItem: Text {
                        text: parent.text
                        font: parent.font
                        color: textSecondary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    implicitWidth: 48
                    implicitHeight: 48
                    onClicked: searchOverlay.open()
                    KeyNavigation.tab: hamburgerButton  // F-6.96 a11y: cycle back

                    ToolTip.visible: pressed
                    ToolTip.text: qsTr("Search")
                }

                // Sync indicator (compact, icon-only on mobile)
                Loader {
                    id: syncIndicatorLoader
                    source: "../components/SyncIndicator.qml"
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 40
                }
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
                case TaskViewModel.Settings:      return "../views/SettingsView.qml"
                case TaskViewModel.Productivity:  return "../views/ProductivityView.qml"
                case TaskViewModel.Notifications: return "../views/NotificationsView.qml"
                case TaskViewModel.AI:            return "../views/AIView.qml"
                case TaskViewModel.Board:         return "../views/BoardView.qml"
                case TaskViewModel.Calendar:      return "../views/CalendarView.qml"
                case TaskViewModel.Trash:         return "../views/TrashView.qml"
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
                    if (!t) return
                    taskViewModel.selectedTask = t
                    taskEditorStack.push(taskEditorPage)
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
            Layout.preferredHeight: quickAddLoader.item ? quickAddLoader.item.height : 48
            color: contentColor
            visible: !root.isFullPageView
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
                    height: taskItem.implicitHeight

                    // ListView injects model roles into required properties on the
                    // delegate root; TaskItem is then bound from here (same pattern
                    // as DesktopLayout — avoids Loader construction-time required-
                    // property failures).
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

                    TaskItem {
                        id: taskItem
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

                        // On mobile, tapping a task opens the full-screen editor
                        // ITER3 #15: guard against null — task may have been deleted
                        // by background sync between render and tap.
                        onTaskClicked: function(taskId) {
                            const t = dataStore.taskById(taskId)
                            if (!t) return
                            taskViewModel.selectedTask = t
                            taskEditorStack.push(taskEditorPage)
                        }
                    }
                    // Bind `id` via Binding{} — assigning to `id:` in an object
                    // initialiser is parsed as the QML id attribute, not a property.
                    Binding {
                        target: taskItem
                        property: "id"
                        value: delegateRoot.id
                    }
                }

                // Empty-state label
                // ITER1 #49: replace English qsTr with literal Russian
                Label {
                    anchors.centerIn: parent
                    visible: taskListView.count === 0
                    text: qsTr("Нет задач — наслаждайтесь днём!")
                    color: textSecondary
                    font.pixelSize: FontSize.body
                }
            }

            // ── Floating Action Button ─────────────────────────────────────────
            Rectangle {
                id: fabButton
                anchors {
                    right:  parent.right
                    bottom: parent.bottom
                    rightMargin:  16
                    bottomMargin: 16
                }
                width:  fabSize
                height: fabSize
                radius: fabSize / 2
                color: accentRed
                z: 10
                clip: true  // B-6.27: clip ripple to FAB radius

                // Elevation shadow
                layer.enabled: true
                layer.effect: null  // F-6.28: DropShadow skipped — Qt5Compat not confirmed available

                // Task 17cea65b iter-3: было unicode "+".
                // Iter-night-23: was hardcoded "#ffffff". The FAB renders
                // on top of the active accentColor; use the theme's
                // accentForeground so the glyph stays readable on every
                // palette (white on red accent works; white on yellow
                // high-contrast accent fails — accentForeground is black
                // there).
                IconSvg {
                    anchors.centerIn: parent
                    name: "plus"
                    size: 28
                    color: ThemeManager.accentForeground
                    // Nudge slightly to optical center
                    anchors.verticalCenterOffset: -2
                }

                // Press scale animation
                scale: fabMouseArea.pressed ? 0.92 : 1.0
                Behavior on scale {
                    NumberAnimation { duration: Duration.fast; easing.type: Easing.InOutQuad }  // B-6.33: instant=0ms, use fast instead
                }

                MouseArea {
                    id: fabMouseArea
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        // Open the quick-add full-screen overlay via StackView
                        taskViewModel.selectedTask = null
                        taskEditorStack.push(taskEditorPage)
                    }
                }

                // Ripple hint on tap. Q4.01 night iter-24: was hardcoded
                // white (1,1,1,0.25) — invisible on sunflower yellow FAB.
                // Use accentForeground so the ripple darkens (or lightens
                // depending on palette) instead of always white.
                Rectangle {
                    id: fabRipple
                    anchors.centerIn: parent
                    width: 0
                    height: 0
                    radius: fabSize / 2
                    color: ThemeManager.accentForeground
                    opacity: 0

                    ParallelAnimation {
                        id: rippleAnimation
                        NumberAnimation {
                            target: fabRipple; property: "width";
                            from: 0; to: fabSize * 1.5; duration: Duration.slow
                        }
                        NumberAnimation {
                            target: fabRipple; property: "height";
                            from: 0; to: fabSize * 1.5; duration: Duration.slow
                        }
                        NumberAnimation {
                            target: fabRipple; property: "opacity";
                            from: 0.4; to: 0; duration: Duration.slow
                        }
                    }

                    Connections {
                        target: fabMouseArea
                        function onPressed() { rippleAnimation.restart() }
                    }
                }
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

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    // Compact editor toolbar
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

                            // Close / back button — task 17cea65b iter-3: было unicode "✕".
                            ToolButton {
                                contentItem: IconSvg {
                                    anchors.centerIn: parent
                                    name: "close"
                                    size: 18
                                    color: textSecondary
                                }
                                onClicked: taskEditorStack.pop()
                            }

                            Label {
                                // ITER1 #50: replace English qsTr with literal Russian
                                text: taskViewModel.selectedTask
                                      ? qsTr("Редактировать задачу")
                                      : qsTr("Добавить задачу")
                                font.pixelSize: 17
                                font.weight: Font.Medium
                                color: textPrimary
                                Layout.fillWidth: true
                                leftPadding: Spacing.sm
                            }
                        }
                    }

                    // The actual editor component
                    Loader {
                        id: editorLoader
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        source: "../components/TaskEditor.qml"

                        onLoaded: {
                            if (item && taskViewModel.selectedTask) {
                                item.task = taskViewModel.selectedTask
                            }
                        }
                    }
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

                    // B-6.34: qrc:/icons/arrow_back.svg doesn't exist — use Unicode glyph
                    ToolButton {
                        text: "←"
                        font.pixelSize: FontSize.title
                        contentItem: Text {
                            text: parent.text
                            font: parent.font
                            color: textSecondary
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
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
