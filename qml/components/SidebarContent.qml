import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtCore
import OpenTodoUI

Rectangle {
    id: sidebarContent

    property bool isDarkTheme: ThemeManager.isDark

    // Colors
    readonly property color accentRed: ThemeManager.accentColor
    readonly property color bgColor: ThemeManager.backgroundColor
    readonly property color hoverColor: ThemeManager.hoverColor
    readonly property color activeColor: ThemeManager.sidebarActiveColor
    readonly property color activeTextColor: ThemeManager.accentColor
    readonly property color textColor: ThemeManager.textColor
    readonly property color secondaryTextColor: ThemeManager.secondaryTextColor
    readonly property color borderColor: ThemeManager.borderColor
    readonly property color iconColor: isDarkTheme ? "#aaaaaa" : "#555555"

    width: 280
    color: bgColor

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    // Returns true when the given view/item should be highlighted
    function isActive(view, itemId) {
        if (sidebarViewModel.currentView !== view)
            return false
        if (itemId === undefined || itemId === null)
            return true
        return sidebarViewModel.currentItemId === itemId
    }

    // -------------------------------------------------------------------------
    // State: expanded project ids (tree), archive section visibility
    // -------------------------------------------------------------------------

    // Set of project ids that are expanded to show children.
    // Stored as a plain JS object used as a set: { id: true }.
    property var expandedProjects: ({})

    // Explicit signal so the Repeater inside the project-tree Loader can react
    // to expansion changes. Relying on the auto-generated `expandedProjectsChanged`
    // property notify was unreliable across Loader/Component scopes — `var` props
    // sometimes don't emit on object-identity changes through Loader boundaries.
    signal expandedToggled()

    // Toggle expanded state for a project node.
    function toggleExpanded(projectId) {
        const copy = Object.assign({}, sidebarContent.expandedProjects)
        if (copy[projectId])
            delete copy[projectId]
        else
            copy[projectId] = true
        sidebarContent.expandedProjects = copy
        sidebarContent.expandedToggled()
    }

    function isExpanded(projectId) {
        return !!sidebarContent.expandedProjects[projectId]
    }

    // -------------------------------------------------------------------------
    // Scroll area
    // -------------------------------------------------------------------------

    Flickable {
        id: flickable
        anchors.fill: parent
        contentWidth: parent.width
        contentHeight: mainColumn.implicitHeight + 16
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        Column {
            id: mainColumn
            width: parent.width
            topPadding: 8
            bottomPadding: 8

            // I24-D: overdue tasks count for the "Сегодня" badge.
            // Recomputed on dataStore changes and once per minute (so a task
            // that crosses midnight without a manual edit still updates).
            property int _overdueCount: 0

            function _recomputeOverdue() {
                if (typeof dataStore === "undefined" || !dataStore || typeof dataStore.allTasks !== "function") {
                    _overdueCount = 0
                    return
                }
                var now = new Date()
                var todayY = now.getFullYear()
                var todayM = now.getMonth()
                var todayD = now.getDate()
                var n = 0
                var arr = dataStore.allTasks() || []
                for (var i = 0; i < arr.length; ++i) {
                    var t = arr[i]
                    if (!t || t.isCompleted || t.isDeleted) continue
                    // Prefer task.due (the scheduled date); fall back to deadline if no due.
                    var dateStr = ""
                    if (t.due && typeof t.due === "object" && t.due.date) dateStr = t.due.date
                    else if (typeof t.due === "string" && t.due.length > 0) dateStr = t.due
                    if (!dateStr && typeof t.deadline === "string" && t.deadline.length > 0) dateStr = t.deadline
                    if (!dateStr) continue
                    var d = new Date(dateStr)
                    if (isNaN(d.getTime())) continue
                    if (d.getFullYear() < todayY ||
                        (d.getFullYear() === todayY && d.getMonth() < todayM) ||
                        (d.getFullYear() === todayY && d.getMonth() === todayM && d.getDate() < todayD)) {
                        n += 1
                    }
                }
                _overdueCount = n
            }

            function _overduePlural(n) {
                var m100 = n % 100
                var m10 = n % 10
                if (m100 >= 11 && m100 <= 14) return qsTr("задач")
                if (m10 === 1) return qsTr("задача")
                if (m10 >= 2 && m10 <= 4) return qsTr("задачи")
                return qsTr("задач")
            }

            // Note: Component.onCompleted for overdue is hooked into the
            // existing _rebuildPinned handler below; we don't redeclare it.

            Connections {
                target: dataStore
                ignoreUnknownSignals: true
                function onTasksChanged() { Qt.callLater(mainColumn._recomputeOverdue) }
                function onDataChanged() { Qt.callLater(mainColumn._recomputeOverdue) }
            }

            Timer {
                interval: 60000
                running: true
                repeat: true
                onTriggered: mainColumn._recomputeOverdue()
            }

            // -----------------------------------------------------------------
            // Pinned tasks section — закреплённые задачи
            // -----------------------------------------------------------------
            //
            // Сводный список всех задач со снятым `isPinned` (исключая
            // выполненные и удалённые). Раздел сворачивается; состояние
            // expand/collapse сохраняется через Qt.labs.settings. Если
            // закреплённых задач нет, раздел полностью скрыт — заголовок
            // "Закреплённые (0)" не показываем.

            Settings {
                id: pinnedSettings
                category: "sidebar"
                property bool pinnedExpanded: true
            }

            // Кэш: массив { id, content, priority } — пересобирается по
            // сигналам dataStore. Лимит 8 элементов в UI; полное число
            // храним отдельно для счётчика и кнопки "Показать все".
            property var _pinnedTasks: []
            property int _pinnedTotal: 0

            // Русское склонение для слова "задача"
            function _pinnedTaskWord(n) {
                const m100 = n % 100
                const m10  = n % 10
                if (m100 >= 11 && m100 <= 14) return qsTr("задач")
                if (m10 === 1)                 return qsTr("задача")
                if (m10 >= 2 && m10 <= 4)      return qsTr("задачи")
                return qsTr("задач")
            }

            function _priorityColor(p) {
                switch (p) {
                    case 1:  return "#db4c3f"
                    case 2:  return "#f49c18"
                    case 3:  return "#4073ff"
                    default: return "#808080"
                }
            }

            function _rebuildPinned() {
                const out = []
                let total = 0
                if (typeof dataStore !== "undefined" && dataStore) {
                    const all = dataStore.allTasks()
                    if (all) {
                        for (let i = 0; i < all.length; ++i) {
                            const t = all[i]
                            if (!t) continue
                            if (!t.isPinned) continue
                            if (t.isCompleted) continue
                            if (t.isDeleted) continue
                            total++
                            if (out.length < 8) {
                                out.push({
                                    taskId:    t.id,
                                    content:   t.content,
                                    priority:  t.priority
                                })
                            }
                        }
                    }
                }
                mainColumn._pinnedTasks = out
                mainColumn._pinnedTotal = total
            }

            Component.onCompleted: { _rebuildPinned(); Qt.callLater(mainColumn._recomputeOverdue) }

            Connections {
                target: dataStore
                function onTasksChanged() { Qt.callLater(mainColumn._rebuildPinned) }
                function onDataChanged()  { Qt.callLater(mainColumn._rebuildPinned) }
            }

            // Вся секция показывается только когда есть хотя бы одна
            // закреплённая задача. Loader+active гарантирует, что вложенные
            // привязки не вычисляются впустую.
            Loader {
                id: pinnedSectionLoader
                width: parent.width
                active: mainColumn._pinnedTotal > 0
                visible: active

                sourceComponent: Column {
                    width: parent.width

                    // Заголовок-кликабельная строка: иконка + название + (N) +
                    // шеврон. Клик переключает pinnedExpanded.
                    Rectangle {
                        id: pinnedHeader
                        width: parent.width
                        height: 36
                        radius: Radius.md
                        color: pinnedHeaderMouse.containsMouse
                               ? sidebarContent.hoverColor
                               : "transparent"

                        Behavior on color { ColorAnimation { duration: 120 } }

                        // Шеврон — task 17cea65b iter-2: было unicode "▾"/"▸".
                        IconSvg {
                            id: pinnedChevron
                            anchors {
                                left: parent.left
                                leftMargin: 10
                                verticalCenter: parent.verticalCenter
                            }
                            name: pinnedSettings.pinnedExpanded ? "chevron-down" : "chevron-right"
                            size: 12
                            color: sidebarContent.secondaryTextColor
                        }

                        // Task 17cea65b iter-3: было unicode emoji "📌".
                        IconSvg {
                            id: pinnedIcon
                            anchors {
                                left: pinnedChevron.right
                                leftMargin: 4
                                verticalCenter: parent.verticalCenter
                            }
                            name: "pin"
                            size: 16
                            color: ThemeManager.accentColor
                        }

                        // Название
                        Text {
                            id: pinnedTitle
                            anchors {
                                left: pinnedIcon.right
                                leftMargin: 6
                                right: pinnedCountBadge.left
                                rightMargin: 6
                                verticalCenter: parent.verticalCenter
                            }
                            text: qsTr("Закреплённые")
                            font.pixelSize: FontSize.body
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                            color: sidebarContent.textColor
                        }

                        // Счётчик-пилюля
                        Rectangle {
                            id: pinnedCountBadge
                            anchors {
                                right: parent.right
                                rightMargin: 10
                                verticalCenter: parent.verticalCenter
                            }
                            height: 18
                            width: Math.max(18, pinnedCountBadgeText.implicitWidth + 10)
                            radius: 9
                            color: sidebarContent.isDarkTheme ? "#3a3a3a" : "#ebebeb"

                            Text {
                                id: pinnedCountBadgeText
                                anchors.centerIn: parent
                                text: mainColumn._pinnedTotal > 99
                                      ? "99+"
                                      : mainColumn._pinnedTotal.toString()
                                font.pixelSize: FontSize.caption
                                font.weight: Font.Medium
                                color: sidebarContent.isDarkTheme ? "#aaaaaa" : "#666666"
                            }
                        }

                        MouseArea {
                            id: pinnedHeaderMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: pinnedSettings.pinnedExpanded =
                                       !pinnedSettings.pinnedExpanded
                        }
                    }

                    // Список закреплённых — рендерится только при развёрнутом
                    // состоянии. Active=false снимает все биндинги внутри.
                    Loader {
                        width: parent.width
                        active: pinnedSettings.pinnedExpanded
                        visible: active

                        sourceComponent: Column {
                            width: parent.width

                            Repeater {
                                model: mainColumn._pinnedTasks

                                delegate: Rectangle {
                                    id: pinnedRow
                                    required property var modelData

                                    readonly property string taskId:   modelData.taskId   || ""
                                    readonly property string taskText: modelData.content  || ""
                                    readonly property int    taskPrio: modelData.priority || 4

                                    width: parent.width
                                    height: 32
                                    radius: Radius.md
                                    color: pinnedRowMouse.containsMouse
                                           ? sidebarContent.hoverColor
                                           : "transparent"

                                    Behavior on color { ColorAnimation { duration: 120 } }

                                    // Цветная точка приоритета — индентация под
                                    // эмодзи заголовка (12 + 16 ≈ 28).
                                    Rectangle {
                                        id: pinnedPrioDot
                                        x: 30
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 8
                                        height: 8
                                        radius: 4
                                        color: mainColumn._priorityColor(pinnedRow.taskPrio)
                                    }

                                    // Текст задачи (обрезаемый в одну строку)
                                    Text {
                                        anchors {
                                            left: pinnedPrioDot.right
                                            leftMargin: 8
                                            right: parent.right
                                            rightMargin: 12
                                            verticalCenter: parent.verticalCenter
                                        }
                                        text: pinnedRow.taskText
                                        font.pixelSize: FontSize.body
                                        elide: Text.ElideRight
                                        color: sidebarContent.textColor
                                    }

                                    MouseArea {
                                        id: pinnedRowMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (pinnedRow.taskId !== "")
                                                taskViewModel.openTaskById(pinnedRow.taskId)
                                        }
                                    }

                                    // Подсказка по наведению — полный текст
                                    // на случай обрезания
                                    HoverHandler { id: pinnedRowHover }
                                    ToolTip.visible: pinnedRowHover.hovered &&
                                                     pinnedRow.taskText !== ""
                                    ToolTip.delay: 600
                                    ToolTip.text: pinnedRow.taskText
                                }
                            }

                            // Ссылка "Показать все (N задач)" — видна только
                            // когда закреплённых > 8. Открывает первую
                            // закреплённую задачу как fallback (чисто-QML
                            // навигации к "filter:pinned" нет).
                            Item {
                                width: parent.width
                                height: 28
                                visible: mainColumn._pinnedTotal > 8

                                Text {
                                    id: pinnedShowAll
                                    anchors {
                                        left: parent.left
                                        leftMargin: 38
                                        verticalCenter: parent.verticalCenter
                                    }
                                    text: qsTr("Показать все (%1 %2)")
                                            .arg(mainColumn._pinnedTotal)
                                            .arg(mainColumn._pinnedTaskWord(mainColumn._pinnedTotal))
                                    font.pixelSize: FontSize.caption
                                    font.underline: pinnedShowAllMouse.containsMouse
                                    color: sidebarContent.activeTextColor
                                }

                                MouseArea {
                                    id: pinnedShowAllMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        // Фокус на первую закреплённую: открываем
                                        // её в редакторе. Это лучшее, что можно
                                        // сделать без новых API.
                                        const first = mainColumn._pinnedTasks[0]
                                        if (first && first.taskId)
                                            taskViewModel.openTaskById(first.taskId)
                                    }
                                }
                            }

                            // Небольшой нижний отступ перед "Входящие"
                            Item { width: 1; height: 4 }
                        }
                    }
                }
            }

            // -----------------------------------------------------------------
            // Quick views section
            // -----------------------------------------------------------------

            SidebarItem {
                width: parent.width
                iconName: "inbox"
                label: qsTr("Входящие")
                count: settingsViewModel.showInboxCount ? sidebarViewModel.inboxCount : 0
                showCount: settingsViewModel.showInboxCount && sidebarViewModel.inboxCount > 0
                active: isActive("inbox")
                isDarkTheme: sidebarContent.isDarkTheme
                onClicked: sidebarViewModel.navigateToInbox()
            }

            // "Сегодня" entry wrapped so we can layer an overdue badge on top
            // (I24-D). The Rectangle below has no MouseArea so clicks pass
            // through to the SidebarItem's own click handler. The existing
            // todayCount pill keeps its position; the overdue pill sits to its
            // left.
            Item {
                width: parent.width
                height: todaySidebarItem.height

                SidebarItem {
                    id: todaySidebarItem
                    width: parent.width
                    iconName: "today"
                    label: qsTr("Сегодня")
                    count: settingsViewModel.showTodayCount ? sidebarViewModel.todayCount : 0
                    showCount: settingsViewModel.showTodayCount && sidebarViewModel.todayCount > 0
                    active: isActive("today")
                    isDarkTheme: sidebarContent.isDarkTheme
                    onClicked: sidebarViewModel.navigateToToday()
                }

                Rectangle {
                    id: overdueBadge
                    visible: mainColumn._overdueCount > 0
                    anchors.right: parent.right
                    // Reserve room for the existing today-count pill on the
                    // far right when it is shown.
                    anchors.rightMargin: (settingsViewModel.showTodayCount && sidebarViewModel.todayCount > 0)
                                         ? 44 : 12
                    anchors.verticalCenter: parent.verticalCenter
                    // Width budget: 10px warning glyph + 4px gap + text + 12 padding.
                    width: overdueBadgeText.implicitWidth + 26
                    height: 18
                    radius: 9
                    color: ThemeManager.errorColor || "#E74C3C"
                    z: 2

                    // Q4.01 (redesign): badge previously used the "⚠" Unicode
                    // glyph as a prefix. Swap to the stroke-pack warning icon
                    // so it carries the same visual weight as the other nav
                    // icons; pill width was bumped to accommodate it.
                    Row {
                        anchors.centerIn: parent
                        spacing: 4

                        IconSvg {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "warning"
                            size: 10
                            color: "white"
                        }

                        Text {
                            id: overdueBadgeText
                            anchors.verticalCenter: parent.verticalCenter
                            text: mainColumn._overdueCount.toString()
                            color: "white"
                            font.pixelSize: 10
                            font.bold: true
                        }
                    }

                    HoverHandler {
                        id: overdueHover
                        cursorShape: Qt.PointingHandCursor
                    }

                    ToolTip.text: mainColumn._overdueCount + " " + mainColumn._overduePlural(mainColumn._overdueCount) + " просрочено"
                    ToolTip.visible: overdueHover.hovered
                    ToolTip.delay: 400
                }
            }

            SidebarItem {
                width: parent.width
                iconName: "upcoming"
                label: qsTr("Предстоящие")
                count: settingsViewModel.showUpcomingCount ? sidebarViewModel.upcomingCount : 0
                showCount: settingsViewModel.showUpcomingCount && sidebarViewModel.upcomingCount > 0
                active: isActive("upcoming")
                isDarkTheme: sidebarContent.isDarkTheme
                onClicked: sidebarViewModel.navigateToUpcoming()
            }

            // -----------------------------------------------------------------
            // Favorites section
            // -----------------------------------------------------------------

            Loader {
                width: parent.width
                active: sidebarViewModel.favorites && sidebarViewModel.favorites.length > 0
                visible: active

                sourceComponent: Column {
                    width: parent.width

                    SectionHeader {
                        width: parent.width
                        title: qsTr("Избранное")
                        isDarkTheme: sidebarContent.isDarkTheme
                    }

                    Repeater {
                        model: sidebarViewModel.favorites

                        // Wrapping delegate in Item (not bare SidebarItem) so we
                        // can carry sibling MouseArea + Menus next to the row —
                        // same pattern the regular Projects tree delegate uses
                        // for its right-click context menu. Without the wrapper
                        // a bare SidebarItem delegate has nowhere to host the
                        // RightButton MouseArea, which is why right-click
                        // silently did nothing on favorites.
                        delegate: Item {
                            id: favRow
                            required property var modelData

                            width: parent.width
                            height: favItem.height

                            SidebarItem {
                                id: favItem
                                width: parent.width
                                // For project favorites: show emoji icon when set, otherwise color dot.
                                // Q4.01 (redesign): label-фавориты теперь рендерятся через
                                // iconName "label" (stroke-пак) вместо эмодзи "🏷"; для
                                // проектов оставляем user-typed emoji как раньше.
                                iconName: favRow.modelData.type === "label" ? "label" : ""
                                iconText: favRow.modelData.type === "project" && favRow.modelData.icon
                                          ? favRow.modelData.icon : "#"
                                iconIsText: favRow.modelData.type === "project"
                                            && (!favRow.modelData.icon || favRow.modelData.icon === "")
                                dotColor: favRow.modelData.color || sidebarContent.accentRed
                                label: favRow.modelData.name || ""
                                active: {
                                    if (favRow.modelData.type === "project")
                                        return isActive("project", favRow.modelData.id)
                                    if (favRow.modelData.type === "label")
                                        return isActive("label", favRow.modelData.name)
                                    if (favRow.modelData.type === "filter")
                                        return isActive("filter", favRow.modelData.id)
                                    return false
                                }
                                isDarkTheme: sidebarContent.isDarkTheme
                                onClicked: {
                                    if (favRow.modelData.type === "project")
                                        sidebarViewModel.navigateToProject(favRow.modelData.id)
                                    else if (favRow.modelData.type === "label")
                                        sidebarViewModel.navigateToLabel(favRow.modelData.name)
                                    else if (favRow.modelData.type === "filter")
                                        sidebarViewModel.navigateToFilter(favRow.modelData.id)
                                }
                            }

                            // Right-click context menu — routes by favorite
                            // type. Only Qt.RightButton, so left-click + hover
                            // continue to fall through to SidebarItem.
                            MouseArea {
                                anchors.fill: favItem
                                acceptedButtons: Qt.RightButton
                                onClicked: function(mouse) {
                                    if (favRow.modelData.type === "project") {
                                        favProjectCtxMenu.projectId   = favRow.modelData.id
                                        favProjectCtxMenu.projectName = favRow.modelData.name || ""
                                        favProjectCtxMenu.popup()
                                    } else if (favRow.modelData.type === "filter") {
                                        favFilterCtxMenu.filterId   = favRow.modelData.id
                                        favFilterCtxMenu.filterName = favRow.modelData.name || ""
                                        favFilterCtxMenu.popup()
                                    } else if (favRow.modelData.type === "label") {
                                        favLabelCtxMenu.labelName = favRow.modelData.name || ""
                                        favLabelCtxMenu.popup()
                                    }
                                }
                            }

                            // Per-type context menus. Project menu is feature
                            // parity with the Projects-tree projCtxMenu (open,
                            // add section, parent picker, toggle favorite,
                            // archive, delete). Filter menu uses
                            // filterViewModel.setFavorite for un-favoriting.
                            // Label menu only offers "open" + "remove from
                            // favorites" because labels expose isFavorite on
                            // the model but no QML-callable setter — the
                            // remove action falls back to sidebarViewModel.
                            Menu {
                                id: favProjectCtxMenu
                                property string projectId:   ""
                                property string projectName: ""

                                MenuItem {
                                    text: qsTr("Открыть")
                                    onTriggered: sidebarViewModel.navigateToProject(favProjectCtxMenu.projectId)
                                }
                                MenuItem {
                                    text: qsTr("Добавить раздел…")
                                    onTriggered: {
                                        addSectionFromMenuDialog.projectId = favProjectCtxMenu.projectId
                                        addSectionFromMenuDialog.open()
                                    }
                                }
                                MenuItem {
                                    text: qsTr("Родительский проект…")
                                    onTriggered: {
                                        parentPickerDialog.projectId   = favProjectCtxMenu.projectId
                                        parentPickerDialog.projectName = favProjectCtxMenu.projectName
                                        parentPickerDialog.open()
                                    }
                                }
                                MenuSeparator {}
                                MenuItem {
                                    text: qsTr("Убрать из избранного")
                                    onTriggered: projectViewModel.setFavorite(favProjectCtxMenu.projectId, false)
                                }
                                MenuItem {
                                    text: qsTr("Архивировать")
                                    onTriggered: projectViewModel.archiveProject(favProjectCtxMenu.projectId)
                                }
                                MenuSeparator {}
                                MenuItem {
                                    text: qsTr("Удалить проект")
                                    onTriggered: {
                                        deleteProjectDialog.projectId   = favProjectCtxMenu.projectId
                                        deleteProjectDialog.projectName = favProjectCtxMenu.projectName
                                        deleteProjectDialog.open()
                                    }
                                }
                            }

                            Menu {
                                id: favFilterCtxMenu
                                property string filterId:   ""
                                property string filterName: ""

                                MenuItem {
                                    text: qsTr("Открыть")
                                    onTriggered: sidebarViewModel.navigateToFilter(favFilterCtxMenu.filterId)
                                }
                                MenuSeparator {}
                                MenuItem {
                                    text: qsTr("Убрать из избранного")
                                    onTriggered: filterViewModel.setFavorite(favFilterCtxMenu.filterId, false)
                                }
                            }

                            Menu {
                                id: favLabelCtxMenu
                                property string labelName: ""

                                MenuItem {
                                    text: qsTr("Открыть")
                                    onTriggered: sidebarViewModel.navigateToLabel(favLabelCtxMenu.labelName)
                                }
                                MenuSeparator {}
                                MenuItem {
                                    text: qsTr("Убрать из избранного")
                                    enabled: typeof sidebarViewModel.setLabelFavorite === "function"
                                    onTriggered: {
                                        // sidebarViewModel may not expose the
                                        // label-favorite setter yet — leave a
                                        // graceful no-op if so. enabled-binding
                                        // above already grays the item out.
                                        if (typeof sidebarViewModel.setLabelFavorite === "function")
                                            sidebarViewModel.setLabelFavorite(favLabelCtxMenu.labelName, false)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // -----------------------------------------------------------------
            // Projects section
            // -----------------------------------------------------------------

            SectionHeader {
                width: parent.width
                title: qsTr("Проекты")
                isDarkTheme: sidebarContent.isDarkTheme
                showAddButton: true
                showArchiveToggle: true
                archiveActive: projectViewModel.showArchived
                onAddClicked: addProjectDialog.open()
                onArchiveToggleClicked: projectViewModel.showArchived = !projectViewModel.showArchived
            }

            // The project tree is rendered by a Column + Repeater.
            // The Repeater's model is a JS array built by projectTreeColumnRepeater.rebuild().
            // rebuild() collects top-level projects and inserts children immediately after
            // any expanded parent, giving a flat-array representation of the tree.
            Column {
                id: projectTreeColumn
                width: parent.width

                // Q3.25: centralized task-counts cache for the project tree.
                // Previously each delegate held its own Connections{ onTasksChanged }
                // and called dataStore.tasksForProject() — an O(N) scan per
                // project per signal. With 20 projects and 500 tasks that was
                // 10 000 ops on every single task edit (synced storm during
                // startup + ongoing UI lag).  We now compute counts for ALL
                // projects in a single O(N) pass, debounced to 50 ms, and the
                // delegates read O(1) entries out of the resulting map.
                property var projectCountsMap: ({})

                function _rebuildProjectCounts() {
                    const map = {}
                    if (typeof dataStore === "undefined" || !dataStore) {
                        projectTreeColumn.projectCountsMap = map
                        return
                    }
                    const tasks = dataStore.allTasks() || []
                    const todayStr = Qt.formatDate(new Date(), "yyyy-MM-dd")
                    for (let i = 0; i < tasks.length; i++) {
                        const t = tasks[i]
                        if (!t || t.isDeleted) continue
                        // Mirror DataStore::tasksForProject filtering: top-level
                        // (no parent) tasks only — subtasks are not counted as
                        // first-class project rows.
                        if (t.parentId) continue
                        const pid = t.projectId
                        if (!pid) continue
                        let e = map[pid]
                        if (!e) { e = { total: 0, done: 0, overdue: 0, today: 0 }; map[pid] = e }
                        e.total++
                        if (t.isCompleted) { e.done++; continue }
                        if (t.dueDate && t.dueDate < todayStr) e.overdue++
                        if (t.dueDate === todayStr) e.today++
                    }
                    projectTreeColumn.projectCountsMap = map
                }

                Timer {
                    id: projectCountsDebounce
                    interval: 50
                    repeat: false
                    onTriggered: projectTreeColumn._rebuildProjectCounts()
                }

                Component.onCompleted: projectTreeColumn._rebuildProjectCounts()

                // Single Connections at the column level; the dataStore signal
                // fans out to all delegates via the projectCountsMap binding
                // below instead of N independent O(N) handlers.
                Connections {
                    target: dataStore
                    function onTasksChanged() { projectCountsDebounce.restart() }
                    // onDataChanged not needed — tasksChanged is already emitted
                    // alongside dataChanged from every task mutation path in
                    // DataStore.cpp, so listening to both would double-fire the
                    // debounce restart for the same logical event.
                }

                // Rebuild when expanded state or the underlying model changes.
                // Qt.callLater coalesces same-tick bursts (e.g. countChanged +
                // projectChanged both firing from a single project edit) into
                // one rebuild instead of two, cutting delegate-recreation cost
                // in half for the common case.
                Connections {
                    target: projectViewModel
                    function onCountChanged()   { Qt.callLater(projectTreeColumnRepeater.rebuild) }
                    function onProjectChanged() { Qt.callLater(projectTreeColumnRepeater.rebuild) }
                    function onProjectAdded()   { Qt.callLater(projectTreeColumnRepeater.rebuild) }
                    function onProjectRemoved() { Qt.callLater(projectTreeColumnRepeater.rebuild) }
                }

                // Connections to sidebarContent — MUST be at Column scope, NOT
                // inside Repeater {}. Connections placed inside a Repeater are
                // treated as part of the delegate component template and never
                // get instantiated as a singleton — so onExpandedToggled never
                // fired and clicking the chevron toggled the property map but
                // the row list never refreshed. Moving the Connections to the
                // Column where the Repeater lives makes it a proper singleton
                // sibling.
                Connections {
                    target: sidebarContent
                    function onExpandedToggled()         { projectTreeColumnRepeater.rebuild() }
                    function onExpandedProjectsChanged() { projectTreeColumnRepeater.rebuild() }
                }

                Repeater {
                    id: projectTreeColumnRepeater

                    // Rebuild the rows array and re-assign it to force the Repeater
                    // to recreate delegates. We use a plain JS array as model.
                    //
                    // Q3.19: walker is now recursive (was 1-level: root +
                    // children only). Bug report — "Не работает раскрытие
                    // проекта который уже вложен в родительский". Previously
                    // a sub-project's chevron was rendered (hasChildren()
                    // queries `projectViewModel.childProjects` and that
                    // works at any depth) but clicking it toggled the
                    // expansion map without producing any new rows, because
                    // the loop below stopped at depth=1. The recursive walker
                    // pushes one row per visible node at its true depth, so
                    // expansion now works for depth 2, 3, ...
                    //
                    // Recursion is fine: project trees in practice are 2-4
                    // levels deep. The Project model has no built-in depth
                    // cap, and we don't add one here — if a user hand-crafts
                    // a 1000-level tree they'll discover the JS stack limit,
                    // which is a far more useful "you've done something
                    // weird" signal than a silently-truncated rebuild.
                    function rebuild() {
                        const rows = []
                        // Iter-7: defence-in-depth cycle guard. SyncEngine
                        // now rejects parent_id cycles on the inbound path,
                        // but if a state.json ever ships with a pre-existing
                        // cycle (older builds, hand-edited file) the walker
                        // would JS-stack-overflow on expand. Track visited
                        // ids and bail on revisits.
                        const visited = ({})

                        function pushSubtree(node, depth) {
                            if (!node || node.isInbox || node.isArchived) return
                            if (visited[node.id]) return
                            visited[node.id] = true
                            rows.push({
                                projectId:    node.id,
                                projectName:  node.name,
                                projectColor: node.color || sidebarContent.accentRed,
                                projectIcon:  node.icon  || "",
                                depth:        depth
                            })
                            if (!sidebarContent.isExpanded(node.id)) return
                            const children = projectViewModel.childProjects(node.id)
                            if (!children) return
                            for (let i = 0; i < children.length; ++i)
                                pushSubtree(children[i], depth + 1)
                        }

                        const count = projectViewModel.count
                        for (let i = 0; i < count; ++i) {
                            const p = projectViewModel.projectAt(i)
                            if (!p) continue
                            if (p.parentId === "" || p.parentId === null || p.parentId === undefined)
                                pushSubtree(p, 0)
                        }
                        projectTreeColumnRepeater.model = rows
                    }

                    Component.onCompleted: rebuild()

                    // (The Connections that listens for sidebarContent's
                    // expandedToggled signal was moved up one level to the
                    // surrounding Column — Connections placed inside the
                    // Repeater body were being treated as part of the
                    // delegate template and were never instantiated as a
                    // standalone singleton, so the chevron click toggled the
                    // expansion property map but the row list never rebuilt.)

                    delegate: Item {
                        id: projTreeDelegate
                        required property var modelData
                        required property int index

                        readonly property string projectId:    modelData.projectId    || ""
                        readonly property string projectName:  modelData.projectName  || ""
                        readonly property string projectColor: modelData.projectColor || sidebarContent.accentRed
                        readonly property string projectIcon:  modelData.projectIcon  || ""
                        readonly property int    depth:        modelData.depth        || 0

                        // ── Inline rename state ───────────────────────────────
                        property bool _renaming: false

                        width: projectTreeColumn.width
                        height: projTreeItem.height

                        // ── Drag-and-drop для перестановки проектов ────────────
                        // Q3.22: работает для проектов любой глубины.
                        // Ранее был жёсткий гейт depth === 0 (только root),
                        // потому что cross-parent reparent сложен. Сейчас
                        // снимаем ограничение: drop вызывает
                        // projectViewModel.reorderProject(fromIdx, toIdx),
                        // который работает на плоском списке и не меняет
                        // parentId — то есть same-parent reorder работает
                        // корректно, а cross-parent просто меняет позицию
                        // в плоском списке (визуальная перестановка без
                        // reparent'а). Чтобы изменить родителя, есть
                        // отдельный пункт меню «Родительский проект…».
                        //
                        // Iter-night-26 (user task a1a57171 "Перетаскивание
                        // проектов в левой панели… должно быть таким же
                        // лёгким как перетаскивание тасков"): pre-fix the
                        // drag was activated only by `dragHandleArea`, the
                        // 4-16px wide MouseArea hidden inside the
                        // `dragHandle` Item on the left edge — and that
                        // handle was visible-gated on row hover, so users
                        // first had to hover the row, then aim at a sliver
                        // on the left edge to engage drag. Tasks use a
                        // row-wide `DragHandler` (TaskItem.qml:223 — pointer
                        // handler, target: null, dragThreshold: 6) which
                        // lets the user press anywhere on the row and start
                        // dragging past a small threshold without competing
                        // with click handlers. Mirror that design here.
                        property bool dragActive:
                            dragHandleArea.drag.active || projRowDragHandler.active

                        Drag.active:          projTreeDelegate.dragActive
                        Drag.dragType:        Drag.Automatic
                        Drag.supportedActions: Qt.MoveAction
                        Drag.hotSpot.x: width / 2
                        Drag.hotSpot.y: height / 2
                        Drag.mimeData:        ({ "application/x-opentodo-project": projectId })
                        Drag.keys:            ["application/x-opentodo-project"]

                        // Row-wide drag from any point. Engages after
                        // dragThreshold pixels of movement so a click on
                        // the row (no movement) still falls through to
                        // chevron / row MouseArea / right-click menu.
                        // Right-button drag is disabled to keep the
                        // context menu intact.
                        DragHandler {
                            id: projRowDragHandler
                            target: null
                            dragThreshold: 6
                            acceptedButtons: Qt.LeftButton
                            cursorShape: active ? Qt.ClosedHandCursor
                                                : Qt.ArrowCursor
                        }

                        // Check whether this project has VISIBLE children (to show chevron).
                        // Filter must match the rebuild() loop above so chevron only
                        // appears when expansion will actually reveal something.
                        readonly property bool hasChildren: {
                            const ch = projectViewModel.childProjects(projTreeDelegate.projectId)
                            if (!ch) return false
                            for (let i = 0; i < ch.length; ++i) {
                                const c = ch[i]
                                if (c && !c.isArchived && !c.isInbox) return true
                            }
                            return false
                        }

                        // ── Chevron button (expander) ──────────────────────
                        // z: 12 keeps the chevron above sibling DropArea, the
                        // right-click MouseArea, and crucially the dragHandle's
                        // MouseArea (z: 11) which would otherwise swallow clicks
                        // in the x=4..16 overlap region — that was the cause of
                        // "клик по шеврону не разворачивает родительский проект".
                        Rectangle {
                            id: chevronBtn

                            // Объявлен раньше color-привязки, чтобы chevronMouse.containsMouse
                            // не оценивалось как undefined при первом построении.
                            MouseArea {
                                id: chevronMouse
                                // Extend the hit-zone 8px outward in every
                                // direction (32×32 hit-target around a 16×16
                                // visual). Earlier iterations tried -4 margins
                                // (24×24), which were still missed on Windows
                                // high-DPI / touchpad setups. 32×32 is the
                                // platform-recommended minimum for "fat
                                // finger" touch targets. The visual chevron
                                // stays compact — only the hit zone widens.
                                // The accompanying double-click handler on
                                // the SidebarItem row provides a second,
                                // even-larger affordance.
                                anchors.fill: parent
                                anchors.margins: -8
                                hoverEnabled: true
                                // Belt-and-braces against parent MouseAreas / Flickable
                                // intercepting clicks: explicitly accept the press and
                                // mark the click as handled.
                                preventStealing: true
                                propagateComposedEvents: false
                                cursorShape: Qt.PointingHandCursor
                                onClicked: sidebarContent.toggleExpanded(projTreeDelegate.projectId)
                            }

                            // z: 100 — definitively above SidebarItem.hoverMouse
                            // (default z:0) and any future-added sibling
                            // MouseAreas, so the chevron always wins hit-testing.
                            z: 100
                            visible: projTreeDelegate.hasChildren
                            x: projTreeDelegate.depth * 14 + 4
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16
                            height: 16
                            radius: 3
                            color: chevronMouse.containsMouse
                                   ? (sidebarContent.isDarkTheme ? "#444444" : "#e0e0e0")
                                   : "transparent"

                            // Task 17cea65b iter-2: было unicode "▾"/"▸".
                            IconSvg {
                                anchors.centerIn: parent
                                name: sidebarContent.isExpanded(projTreeDelegate.projectId) ? "chevron-down" : "chevron-right"
                                size: 12
                                color: sidebarContent.secondaryTextColor
                            }
                        }

                        // ── Task-count cache for the completion badge ─────────
                        // Q3.25: was per-delegate O(N) dataStore.tasksForProject()
                        // scan on every tasksChanged/dataChanged signal — i.e.
                        // for 20 projects × 500 tasks = 10 000 ops per single
                        // task edit, contributing to global UI lag.  Now read
                        // from the column-level projectCountsMap which is built
                        // ONCE per debounced 50 ms window in a single O(N) pass.
                        readonly property var _projCounts: projectTreeColumn.projectCountsMap[projTreeDelegate.projectId]
                        readonly property int _totalTasks:     _projCounts ? _projCounts.total   : 0
                        readonly property int _completedTasks: _projCounts ? _projCounts.done    : 0
                        readonly property int _overdueTasks:   _projCounts ? _projCounts.overdue : 0
                        readonly property int _todayTasks:     _projCounts ? _projCounts.today   : 0

                        // Russian plural form for "задача/задачи/задач"
                        function _projectTaskWord(n) {
                            const m100 = n % 100
                            const m10  = n % 10
                            if (m100 >= 11 && m100 <= 14) return qsTr("задач")
                            if (m10 === 1)                 return qsTr("задача")
                            if (m10 >= 2 && m10 <= 4)      return qsTr("задачи")
                            return qsTr("задач")
                        }

                        // ── Hover-handler: нужен для показа ручки перетаскивания ──
                        // Объявлен раньше SidebarItem / completionBadge, чтобы их
                        // привязки к rowHoverHandler.hovered не получали undefined
                        // на первой оценке.
                        HoverHandler { id: rowHoverHandler }

                        // ── Sidebar row ────────────────────────────────────
                        SidebarItem {
                            id: projTreeItem
                            // Indent: 14px per depth level + 20px for chevron area
                            x: projTreeDelegate.depth * 14 + 20
                            width: parent.width - (projTreeDelegate.depth * 14 + 20)
                            // Emoji takes precedence over the color dot.
                            iconIsText: projTreeDelegate.projectIcon === ""
                            iconText:   projTreeDelegate.projectIcon !== ""
                                        ? projTreeDelegate.projectIcon : ""
                            dotColor: projTreeDelegate.projectColor
                            label: projTreeDelegate.projectName
                            active: isActive("project", projTreeDelegate.projectId)
                            isDarkTheme: sidebarContent.isDarkTheme
                            border.color: projTreeDropArea.containsDrag
                                          ? ThemeManager.accentColor
                                          : "transparent"
                            border.width: projTreeDropArea.containsDrag ? 2 : 0
                            onClicked: sidebarViewModel.navigateToProject(projTreeDelegate.projectId)
                            // Double-click toggles expand/collapse for parent
                            // projects. This is a more discoverable affordance
                            // than the small chevron — especially on high-DPI
                            // / touchpad setups where 16×16 click targets are
                            // missed. The chevron remains as the primary
                            // explicit affordance.
                            onDoubleClicked: {
                                if (projTreeDelegate.hasChildren)
                                    sidebarContent.toggleExpanded(projTreeDelegate.projectId)
                            }

                            // ── Row tooltip: breakdown on hover ───────────────
                            ToolTip.visible: rowHoverHandler.hovered &&
                                             projTreeDelegate._totalTasks > 0
                            ToolTip.delay: 700
                            ToolTip.text: {
                                if (projTreeDelegate._totalTasks === 0) return ""
                                const lines = []
                                lines.push(qsTr("Всего: %1 %2")
                                           .arg(projTreeDelegate._totalTasks)
                                           .arg(projTreeDelegate._projectTaskWord(projTreeDelegate._totalTasks)))
                                if (projTreeDelegate._completedTasks > 0)
                                    lines.push(qsTr("Завершено: %1").arg(projTreeDelegate._completedTasks))
                                if (projTreeDelegate._overdueTasks > 0)
                                    lines.push(qsTr("Просрочено: %1").arg(projTreeDelegate._overdueTasks))
                                if (projTreeDelegate._todayTasks > 0)
                                    lines.push(qsTr("На сегодня: %1").arg(projTreeDelegate._todayTasks))
                                return lines.join("\n")
                            }
                        }

                        // ── Completion badge (X/Y) ────────────────────────
                        // Floats over the SidebarItem's right margin area.
                        // z:12 keeps it above the right-click MouseArea (no z) and drag
                        // handle (z:11). Hidden on hover so the drag handle can breathe.
                        Text {
                            id: completionBadge
                            z: 12
                            visible: projTreeDelegate._totalTasks > 0 &&
                                     !rowHoverHandler.hovered
                            anchors {
                                right: parent.right
                                rightMargin: 10
                                verticalCenter: parent.verticalCenter
                            }
                            text: projTreeDelegate._completedTasks + "/" + projTreeDelegate._totalTasks
                            font.pixelSize: FontSize.caption
                            color: projTreeDelegate._completedTasks === projTreeDelegate._totalTasks
                                   ? "#2e8b57"
                                   : ThemeManager.secondaryTextColor

                            HoverHandler { id: badgeHoverHandler }
                            ToolTip.visible: badgeHoverHandler.hovered
                            ToolTip.text: qsTr("Выполнено %1 из %2")
                                            .arg(projTreeDelegate._completedTasks)
                                            .arg(projTreeDelegate._totalTasks)
                            ToolTip.delay: 500
                        }

                        // Right-click context menu
                        MouseArea {
                            anchors.fill: projTreeItem
                            acceptedButtons: Qt.RightButton
                            onClicked: function(mouse) {
                                projCtxMenu.projectId   = projTreeDelegate.projectId
                                projCtxMenu.projectName = projTreeDelegate.projectName
                                projCtxMenu.popup()
                            }
                        }

                        // I13-D: double-click → inline rename
                        TapHandler {
                            target: projTreeItem
                            acceptedButtons: Qt.LeftButton
                            gesturePolicy: TapHandler.ReleaseWithinBounds
                            // Don't allow rename of the inbox project (modelData.isInbox)
                            // — inbox names are conventionally fixed.
                            enabled: !(projTreeDelegate.modelData
                                       && projTreeDelegate.modelData.projectIsInbox)
                            onDoubleTapped: {
                                projTreeDelegate._renaming = true
                                Qt.callLater(function() {
                                    inlineRenameField.forceActiveFocus()
                                    inlineRenameField.selectAll()
                                })
                            }
                        }

                        // Overlay TextField for inline rename — appears on top of
                        // the SidebarItem when _renaming is true.
                        Rectangle {
                            id: inlineRenameOverlay
                            visible: projTreeDelegate._renaming
                            anchors.fill: projTreeItem
                            color: ThemeManager.surfaceColor
                            border.color: ThemeManager.accentColor
                            border.width: 1
                            radius: 4
                            z: 50

                            TextField {
                                id: inlineRenameField
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 32
                                anchors.rightMargin: 4
                                background: Item {}
                                text: projTreeDelegate.projectName
                                selectByMouse: true
                                font.pixelSize: 13
                                color: ThemeManager.textColor

                                Keys.onEscapePressed: {
                                    text = projTreeDelegate.projectName
                                    projTreeDelegate._renaming = false
                                }
                                onAccepted: {
                                    const trimmed = text.trim()
                                    if (trimmed !== "" && trimmed !== projTreeDelegate.projectName) {
                                        projectViewModel.updateProject(
                                            projTreeDelegate.projectId, "name", trimmed)
                                        if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                                            telemetry.trackEvent("project_rename_inline", {})
                                    }
                                    projTreeDelegate._renaming = false
                                }
                                onActiveFocusChanged: {
                                    // Save on blur (Notion-style UX)
                                    if (!activeFocus && projTreeDelegate._renaming)
                                        accepted()
                                }
                            }
                        }

                        Menu {
                            id: projCtxMenu
                            property string projectId:   ""
                            property string projectName: ""

                            MenuItem {
                                text: qsTr("Открыть")
                                onTriggered: sidebarViewModel.navigateToProject(projCtxMenu.projectId)
                            }
                            MenuItem {
                                text: qsTr("Добавить раздел…")
                                onTriggered: {
                                    addSectionFromMenuDialog.projectId = projCtxMenu.projectId
                                    addSectionFromMenuDialog.open()
                                }
                            }
                            MenuItem {
                                text: qsTr("Родительский проект…")
                                onTriggered: {
                                    parentPickerDialog.projectId   = projCtxMenu.projectId
                                    parentPickerDialog.projectName = projCtxMenu.projectName
                                    parentPickerDialog.open()
                                }
                            }
                            // Q4.01 iter-7: смена иконки уже существующего
                            // проекта через тот же ProjectIconPicker. Используем
                            // отдельный экземпляр (ctxIconPicker) — так не нужен
                            // дискриминатор контекста в onIconAccepted.
                            MenuItem {
                                text: qsTr("Изменить иконку…")
                                onTriggered: {
                                    const p = projectViewModel.projectById(projCtxMenu.projectId)
                                    ctxIconPicker.targetProjectId = projCtxMenu.projectId
                                    ctxIconPicker.openWith(p ? (p.icon || "") : "")
                                }
                            }
                            MenuSeparator {}
                            MenuItem {
                                text: (projectViewModel.projectById(projCtxMenu.projectId)
                                       && projectViewModel.projectById(projCtxMenu.projectId).isFavorite)
                                      ? qsTr("Убрать из избранного")
                                      : qsTr("В избранное")
                                onTriggered: {
                                    const p = projectViewModel.projectById(projCtxMenu.projectId)
                                    projectViewModel.setFavorite(projCtxMenu.projectId,
                                                                 !(p && p.isFavorite))
                                }
                            }
                            MenuItem {
                                text: qsTr("Архивировать")
                                onTriggered: projectViewModel.archiveProject(projCtxMenu.projectId)
                            }
                            MenuSeparator {}
                            MenuItem {
                                text: qsTr("Удалить проект")
                                onTriggered: {
                                    deleteProjectDialog.projectId   = projCtxMenu.projectId
                                    deleteProjectDialog.projectName = projCtxMenu.projectName
                                    deleteProjectDialog.open()
                                }
                            }
                        }

                        // ── DropArea: задача → проект ─────────────────────────
                        // Принимает только mime-тип задачи; перестановка проектов
                        // обрабатывается отдельным projReorderDropArea ниже.
                        // Для архивированных проектов перетаскивание отключено.
                        DropArea {
                            id: projTreeDropArea
                            anchors.fill: projTreeItem
                            keys: ["application/x-opentodo-task"]
                            // Не принимаем задачи на архивированные проекты
                            enabled: {
                                const p = projectViewModel.projectById(projTreeDelegate.projectId)
                                return !(p && p.isArchived)
                            }

                            onDropped: function(drop) {
                                const taskId  = drop.getDataAsString("application/x-opentodo-task")
                                const projId  = projTreeDelegate.projectId
                                // Двойная защита: не перемещаем без обоих id
                                if (taskId && projId) {
                                    taskViewModel.moveTask(taskId, projId, "")
                                    // Телеметрия (опционально)
                                    if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                                        telemetry.trackEvent("task_drag_to_project_sidebar", {})
                                }
                                drop.accept(Qt.MoveAction)
                            }

                            // Подсветка строки при наведении перетаскиваемой задачи
                            Rectangle {
                                id: taskDropHighlight
                                anchors.fill: parent
                                color: Qt.rgba(ThemeManager.accentColor.r,
                                               ThemeManager.accentColor.g,
                                               ThemeManager.accentColor.b, 0.20)
                                radius: 4
                                visible: projTreeDropArea.containsDrag
                                z: -1   // за содержимым строки, над её фоном
                            }
                        }

                        // ── Ручка перетаскивания (⠿) — видна при наведении ─────────
                        // Q3.28: снят гейт depth === 0. Q3.22 разрешил Drag.active
                        // для любой глубины, но забыл снять гейт на dragHandle.visible
                        // — без видимой ручки MouseArea внутри не получает события
                        // (родительский Item с visible:false блокирует delivery),
                        // так что фактически дочерние проекты по-прежнему было
                        // нельзя начать перетаскивать. Теперь ручка показывается
                        // на всех глубинах. Inbox в дерево не попадает уже на
                        // уровне модели (pushSubtree фильтрует isInbox), так
                        // что отдельная защита здесь не нужна.
                        Item {
                            id: dragHandle
                            visible: rowHoverHandler.hovered
                            // Если у проекта НЕТ дочерних — занимаем штатные 16px (x=0..16),
                            // показываем иконку ⠇ как раньше.
                            // Если у проекта ЕСТЬ дочерние — шеврон занимает x=4..20, и
                            // hit-зона ручки сжимается до x=0..4, чтобы её MouseArea не
                            // перехватывала клик по шеврону. Иконка ⠇ при этом скрывается:
                            // 4px — слишком мало для нормально читаемой пиктограммы, и
                            // важнее не перекрыть шеврон визуально. Курсор SizeVerCursor
                            // в узкой полоске остаётся подсказкой о возможности drag&drop.
                            x: 0
                            anchors.verticalCenter: parent.verticalCenter
                            width: projTreeDelegate.hasChildren ? 4 : 16
                            height: parent.height
                            z: 11  // ниже chevronBtn (z=12), выше других сиблингов

                            Text {
                                anchors.centerIn: parent
                                visible: !projTreeDelegate.hasChildren
                                text: "⠇"   // ⠿ BRAILLE PATTERN DOTS-1234567
                                font.pixelSize: 12
                                color: sidebarContent.secondaryTextColor
                            }

                            // MouseArea-источник перетаскивания.
                            // drag.target: null — визуально делегат не перемещается;
                            // Drag.Automatic создаёт системный drag-курсор.
                            MouseArea {
                                id: dragHandleArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.SizeVerCursor
                                drag.target: null
                                drag.axis: Drag.YAxis
                                // preventStealing нужен, чтобы Flickable не перехватил
                                // вертикальное движение раньше, чем сработает Drag.
                                preventStealing: true
                            }
                        }

                        // ── DropArea для перестановки / вложения проектов ─────
                        // Q3.28: добавили зону «по центру строки» для смены
                        // родителя (drag-to-reparent). Раньше drop работал
                        // только как reorder в плоском списке, поэтому
                        // изменить вложенность через drag было нельзя —
                        // приходилось лезть в меню «Родительский проект…».
                        // Теперь:
                        //   • верхние 30% строки   → вставить ВЫШЕ как sibling
                        //   • нижние 30% строки    → вставить НИЖЕ как sibling
                        //   • средние 40%          → сделать ДОЧЕРНИМ для строки
                        // Indikатор у sibling-режимов — горизонтальная линия,
                        // у режима reparent — подсветка всей строки.
                        DropArea {
                            id: projReorderDropArea
                            anchors.fill: parent
                            keys: ["application/x-opentodo-project"]

                            // 0 = верхняя треть (выше), 1 = средняя (внутрь),
                            // 2 = нижняя треть (ниже).
                            readonly property int dropZone: {
                                if (!containsDrag) return -1
                                const h = projTreeDelegate.height
                                if (drag.y < h * 0.30) return 0
                                if (drag.y > h * 0.70) return 2
                                return 1
                            }
                            readonly property bool dragIsAbove: dropZone === 0
                            readonly property bool dragAsChild: dropZone === 1

                            // Горизонтальная линия-индикатор вставки (sibling).
                            Rectangle {
                                visible: projReorderDropArea.containsDrag
                                         && !projReorderDropArea.dragAsChild
                                width: parent.width - 24 - projTreeDelegate.depth * 14
                                x: 12 + projTreeDelegate.depth * 14
                                height: 2
                                radius: 1
                                color: ThemeManager.accentColor
                                y: projReorderDropArea.dragIsAbove ? 0 : (parent.height - 2)
                            }

                            // Подсветка строки при наведении в режиме «сделать дочерним».
                            Rectangle {
                                anchors.fill: parent
                                visible: projReorderDropArea.containsDrag
                                         && projReorderDropArea.dragAsChild
                                color: Qt.rgba(ThemeManager.accentColor.r,
                                               ThemeManager.accentColor.g,
                                               ThemeManager.accentColor.b, 0.20)
                                radius: 4
                                z: -1
                            }

                            onDropped: function(drop) {
                                const draggedId = drop.getDataAsString("application/x-opentodo-project")
                                if (!draggedId || draggedId === projTreeDelegate.projectId) return

                                if (projReorderDropArea.dragAsChild) {
                                    // Drag-to-reparent. Бэкенд (updateProject
                                    // с field=parentId) сам отбивает циклы и
                                    // попытки переродительствовать Inbox, так
                                    // что доп. проверок здесь не делаем.
                                    projectViewModel.updateProject(
                                        draggedId, "parentId", projTreeDelegate.projectId)
                                    drop.accept(Qt.MoveAction)
                                    return
                                }

                                // Sibling reorder — iter-13 fix.
                                // Q3.22 used reorderProject(rowsIdx, rowsIdx),
                                // but rowsIdx is a position in the tree-walker
                                // array (skips Inbox+archived, depth-first),
                                // while reorderProject expected indices into
                                // the flat m_projects list. Off-by-one drove
                                // root projects around when dragging children.
                                // reorderProjectById renumbers only same-parent
                                // siblings — drag stays local to the level.
                                projectViewModel.reorderProjectById(
                                    draggedId,
                                    projTreeDelegate.projectId,
                                    projReorderDropArea.dragIsAbove)

                                drop.accept(Qt.MoveAction)
                            }
                        }
                    } // delegate
                } // Repeater projectTreeColumnRepeater

                // Iter-task a1a57171: a "root" drop zone below the tree so the
                // user can drag any nested project out of its parent and back
                // to the top level. Without this there was no UI gesture to
                // un-nest a project — the user could go deeper (drag-as-child)
                // and could re-order siblings, but never escape to root.
                Item {
                    id: rootDropArea
                    width: parent.width
                    // Slim by default to avoid wasting sidebar space; grows
                    // to 24 px while a drag is hovering so the user has an
                    // obvious target.
                    height: rootDrop.containsDrag ? 24 : 12

                    DropArea {
                        id: rootDrop
                        anchors.fill: parent
                        keys: ["application/x-opentodo-project"]

                        Rectangle {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            anchors.topMargin: 4
                            anchors.bottomMargin: 4
                            visible: rootDrop.containsDrag
                            radius: 4
                            color: Qt.rgba(ThemeManager.accentColor.r,
                                           ThemeManager.accentColor.g,
                                           ThemeManager.accentColor.b, 0.18)
                            border.color: ThemeManager.accentColor
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: qsTr("К корню")
                                color: ThemeManager.accentColor
                                font.pixelSize: 11
                                font.bold: true
                            }
                        }

                        onDropped: function(drop) {
                            const draggedId = drop.getDataAsString("application/x-opentodo-project")
                            if (!draggedId) return
                            const proj = projectViewModel.projectById(draggedId)
                            // No-op if already root (parentId empty) or Inbox.
                            if (!proj || proj.isInbox || !proj.parentId) {
                                drop.accept(Qt.MoveAction)
                                return
                            }
                            // Backend updateProject(parentId="") clears the
                            // parent. Cycle/Inbox guards in updateProject
                            // accept empty as a valid "no parent" sentinel.
                            projectViewModel.updateProject(draggedId, "parentId", "")
                            drop.accept(Qt.MoveAction)
                        }
                    }
                }
            } // Column projectTreeColumn

            // -----------------------------------------------------------------
            // Archive section (shown when projectViewModel.showArchived)
            // -----------------------------------------------------------------

            Loader {
                width: parent.width
                active: projectViewModel.showArchived
                visible: active

                sourceComponent: Column {
                    width: parent.width

                    SectionHeader {
                        width: parent.width
                        title: qsTr("Архив")
                        isDarkTheme: sidebarContent.isDarkTheme
                        showAddButton: false
                    }

                    Repeater {
                        id: archivedRepeater
                        model: projectViewModel

                        delegate: Loader {
                            id: archDelegateLoader
                            required property int index
                            required property var model

                            width: parent.width
                            active: model.isArchived && !model.isInbox

                            sourceComponent: Item {
                                readonly property string projectId:    archDelegateLoader.model.id    || ""
                                readonly property string projectName:  archDelegateLoader.model.name  || ""
                                readonly property string projectColor: archDelegateLoader.model.color || sidebarContent.accentRed

                                width: archivedRepeater.width
                                height: archItem.height

                                // Greyed-out row for archived projects (simple Rectangle, no SidebarItem)
                                Rectangle {
                                    id: archItem
                                    width: parent.width
                                    height: 36
                                    radius: Radius.md
                                    color: archRowMouse.containsMouse
                                           ? (sidebarContent.isDarkTheme ? "#2a2a2a" : "#f0f0f0")
                                           : "transparent"
                                    opacity: 0.7

                                    // Colored dot
                                    Rectangle {
                                        id: archDot
                                        x: 30
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 8; height: 8; radius: 4
                                        color: parent.parent.projectColor
                                        opacity: 0.4
                                    }

                                    // Greyed label
                                    Text {
                                        anchors {
                                            left: archDot.right
                                            leftMargin: 8
                                            right: parent.right
                                            rightMargin: 8
                                            verticalCenter: parent.verticalCenter
                                        }
                                        text: parent.parent.projectName
                                        font.pixelSize: FontSize.body
                                        elide: Text.ElideRight
                                        color: sidebarContent.isDarkTheme ? "#666666" : "#aaaaaa"
                                    }

                                    MouseArea {
                                        id: archRowMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        acceptedButtons: Qt.LeftButton
                                        // Left click is a no-op for archived projects
                                    }
                                }

                                // Right-click context menu for archived projects
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.RightButton
                                    onClicked: function(mouse) {
                                        archivedCtxMenu.projectId   = parent.projectId
                                        archivedCtxMenu.projectName = parent.projectName
                                        archivedCtxMenu.popup()
                                    }
                                }

                                Menu {
                                    id: archivedCtxMenu
                                    property string projectId:   ""
                                    property string projectName: ""

                                    MenuItem {
                                        text: qsTr("Восстановить")
                                        onTriggered: projectViewModel.unarchiveProject(archivedCtxMenu.projectId)
                                    }
                                    MenuSeparator {}
                                    MenuItem {
                                        text: qsTr("Удалить навсегда")
                                        onTriggered: {
                                            deleteProjectDialog.projectId   = archivedCtxMenu.projectId
                                            deleteProjectDialog.projectName = archivedCtxMenu.projectName
                                            deleteProjectDialog.open()
                                        }
                                    }
                                }
                            } // sourceComponent
                        } // Loader archDelegateLoader
                    } // Repeater archivedRepeater
                } // Column
            } // Loader archive section

            // -----------------------------------------------------------------
            // Filters section
            // -----------------------------------------------------------------

            SectionHeader {
                width: parent.width
                title: qsTr("Фильтры")
                isDarkTheme: sidebarContent.isDarkTheme
                showAddButton: true
                onAddClicked: addFilterDialog.open()
            }

            Repeater {
                id: filterRepeater
                model: filterViewModel

                delegate: Item {
                    id: filterDelegate
                    required property int index
                    required property var model

                    readonly property string filterId:    model.id    || ""
                    readonly property string filterName:  model.name  || ""
                    readonly property string filterColor: model.color || sidebarContent.accentRed
                    readonly property int    filterOrder: model.order || 0

                    width: parent.width
                    height: filterItem.height

                    SidebarItem {
                        id: filterItem
                        width: parent.width
                        iconText: "⊙"
                        dotColor: filterDelegate.filterColor
                        iconIsText: false
                        label: filterDelegate.filterName
                        active: isActive("filter", filterDelegate.filterId)
                        isDarkTheme: sidebarContent.isDarkTheme
                        onClicked: sidebarViewModel.navigateToFilter(filterDelegate.filterId)
                    }

                    // Right-click area for filter context menu
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.RightButton
                        onClicked: function(mouse) {
                            filterCtxMenu.filterId    = filterDelegate.filterId
                            filterCtxMenu.filterName  = filterDelegate.filterName
                            filterCtxMenu.filterIndex = filterDelegate.index
                            filterCtxMenu.filterOrder = filterDelegate.filterOrder
                            filterCtxMenu.popup()
                        }
                    }

                    Menu {
                        id: filterCtxMenu
                        property string filterId:    ""
                        property string filterName:  ""
                        property int    filterIndex: 0
                        property int    filterOrder: 0

                        MenuItem {
                            text: qsTr("Открыть")
                            onTriggered: sidebarViewModel.navigateToFilter(filterCtxMenu.filterId)
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: qsTr("Переименовать")
                            onTriggered: {
                                renameFilterDialog.filterId   = filterCtxMenu.filterId
                                renameFilterDialog.filterName = filterCtxMenu.filterName
                                renameFilterDialog.open()
                            }
                        }
                        MenuItem {
                            text: qsTr("Изменить запрос")
                            onTriggered: {
                                editFilterQueryDialog.filterId    = filterCtxMenu.filterId
                                editFilterQueryDialog.filterName  = filterCtxMenu.filterName
                                const f = filterViewModel.filterById(filterCtxMenu.filterId)
                                editFilterQueryDialog.initialQuery = f ? f.query : ""
                                editFilterQueryDialog.open()
                            }
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: qsTr("Переместить вверх")
                            enabled: filterCtxMenu.filterIndex > 0
                            onTriggered: filterViewModel.reorderFilter(filterCtxMenu.filterId,
                                                                       filterCtxMenu.filterOrder - 1)
                        }
                        MenuItem {
                            text: qsTr("Переместить вниз")
                            enabled: filterCtxMenu.filterIndex < filterViewModel.count - 1
                            onTriggered: filterViewModel.reorderFilter(filterCtxMenu.filterId,
                                                                       filterCtxMenu.filterOrder + 1)
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: qsTr("Удалить фильтр")
                            onTriggered: {
                                deleteFilterDialog.filterId   = filterCtxMenu.filterId
                                deleteFilterDialog.filterName = filterCtxMenu.filterName
                                deleteFilterDialog.open()
                            }
                        }
                    }
                } // Item filterDelegate
            } // Repeater filterRepeater

            // -----------------------------------------------------------------
            // Labels section
            // -----------------------------------------------------------------

            SectionHeader {
                width: parent.width
                title: qsTr("Метки")
                isDarkTheme: sidebarContent.isDarkTheme
                showAddButton: false
            }

            Repeater {
                id: labelRepeater
                model: sidebarViewModel.labels

                // Re-fetch labels when the data store or saved order changes
                Connections {
                    target: sidebarViewModel
                    function onLabelsChanged()     { labelRepeater.model = sidebarViewModel.labels }
                    function onLabelOrderChanged() { labelRepeater.model = sidebarViewModel.labels }
                }
                Connections {
                    target: dataStore
                    function onLabelsChanged() { labelRepeater.model = sidebarViewModel.labels }
                    function onDataChanged()   { labelRepeater.model = sidebarViewModel.labels }
                }

                delegate: Item {
                    id: labelDelegate
                    required property var modelData
                    required property int index

                    readonly property string labelId:    modelData.id    || ""
                    readonly property string labelName:  modelData.name  || ""
                    readonly property color  labelColor: modelData.color || sidebarContent.accentRed

                    width: parent.width
                    height: labelItem.height

                    // ── Drag source: метка → перестановка в списке меток ───────
                    // Активируем drag только когда пользователь схватил ручку.
                    // Drag.Automatic — системный drag, не двигает делегат
                    // (Column держит его в layout-позиции, как у проектов).
                    Drag.active:           labelDragHandleArea.drag.active
                    Drag.dragType:         Drag.Automatic
                    Drag.supportedActions: Qt.MoveAction
                    Drag.mimeData:         ({ "application/x-opentodo-label-reorder": labelDelegate.labelId })
                    Drag.keys:             ["application/x-opentodo-label-reorder"]

                    // Лёгкая прозрачность пока строка перетаскивается
                    opacity: Drag.active ? 0.55 : 1.0

                    SidebarItem {
                        id: labelItem
                        width: parent.width
                        // Q4.01 (redesign): эмодзи "🏷" → IconSvg "label" + цветная
                        // точка (color остаётся для идентификации метки).
                        iconName: "label"
                        iconText: ""
                        iconIsText: false
                        dotColor: labelDelegate.labelColor
                        label: labelDelegate.labelName
                        active: isActive("label", labelDelegate.labelName)
                        isDarkTheme: sidebarContent.isDarkTheme
                        // Highlight border while a task is dragged over this row
                        border.color: labelDropArea.containsDrag
                                      ? ThemeManager.accentColor
                                      : "transparent"
                        border.width: labelDropArea.containsDrag ? 2 : 0
                        onClicked: sidebarViewModel.navigateToLabel(labelDelegate.labelName)
                    }

                    // ── Hover-handler для показа ручки перетаскивания ──────────
                    HoverHandler { id: labelHoverHandler }

                    // ── Ручка перетаскивания (⠇) — видна только при наведении ──
                    // Размещена в самом левом кармане ряда, поверх SidebarItem.
                    Item {
                        id: labelDragHandle
                        visible: labelHoverHandler.hovered || labelDelegate.Drag.active
                        x: 0
                        anchors.verticalCenter: parent.verticalCenter
                        width: 16
                        height: parent.height
                        z: 11

                        Text {
                            anchors.centerIn: parent
                            text: "⠇"
                            font.pixelSize: 12
                            color: sidebarContent.secondaryTextColor
                        }

                        MouseArea {
                            id: labelDragHandleArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.SizeVerCursor
                            // drag.target: null — делегат не двигается визуально
                            // (Drag.Automatic создаёт системный курсор-источник).
                            drag.target: null
                            drag.axis: Drag.YAxis
                            preventStealing: true
                        }
                    }

                    // ── DropArea для перестановки меток ────────────────────────
                    // Принимает только мим-тип "label-reorder". Линия-индикатор
                    // подсвечивает позицию вставки (сверху или снизу ряда).
                    DropArea {
                        id: labelReorderDropArea
                        anchors.fill: parent
                        keys: ["application/x-opentodo-label-reorder"]

                        readonly property bool dragIsAbove: {
                            if (!containsDrag) return false
                            return drag.y < (labelDelegate.height / 2)
                        }

                        // Горизонтальная линия-индикатор вставки
                        Rectangle {
                            visible: labelReorderDropArea.containsDrag
                            width: parent.width - 24
                            x: 12
                            height: 2
                            radius: 1
                            color: ThemeManager.accentColor
                            y: labelReorderDropArea.dragIsAbove ? 0 : (parent.height - 2)
                            z: 20
                        }

                        onDropped: function(drop) {
                            const draggedId = drop.getDataAsString("application/x-opentodo-label-reorder")
                            if (!draggedId || draggedId === labelDelegate.labelId) {
                                drop.accept(Qt.MoveAction)
                                return
                            }

                            // Текущий список меток в порядке отображения
                            const current = sidebarViewModel.labels
                            const ids = []
                            for (let i = 0; i < current.length; ++i)
                                ids.push(current[i].id)

                            const fromIdx = ids.indexOf(draggedId)
                            if (fromIdx < 0) {
                                drop.accept(Qt.MoveAction)
                                return
                            }

                            // Удаляем из исходной позиции
                            ids.splice(fromIdx, 1)

                            // Вычисляем целевую позицию, учитывая, верх/низ ряда
                            let toIdx = labelDelegate.index
                            if (toIdx > fromIdx) toIdx -= 1   // сдвиг после удаления
                            if (!labelReorderDropArea.dragIsAbove) toIdx += 1
                            if (toIdx < 0) toIdx = 0
                            if (toIdx > ids.length) toIdx = ids.length

                            ids.splice(toIdx, 0, draggedId)
                            sidebarViewModel.setLabelOrder(ids)
                            drop.accept(Qt.MoveAction)
                        }
                    }

                    // ── DropArea: задача → метка ──────────────────────────────
                    // Принимает задачу и добавляет метку в её список меток
                    // (дедупликация через indexOf, тихий no-op для уже присвоенных меток).
                    DropArea {
                        id: labelDropArea
                        anchors.fill: labelItem
                        keys: ["application/x-opentodo-task"]

                        onDropped: function(drop) {
                            const taskId = drop.getDataAsString("application/x-opentodo-task")
                            // Edge case: нет id — принимаем drop без мутации
                            if (!taskId) {
                                drop.accept(Qt.MoveAction)
                                return
                            }
                            const lName = labelDelegate.labelName
                            // Edge case: пустое имя метки — тихий no-op
                            if (lName === "") {
                                drop.accept(Qt.MoveAction)
                                return
                            }
                            const task = dataStore.taskById(taskId)
                            if (task) {
                                const existing = task.labels || []
                                // Edge case: метка уже есть — дедупликация
                                if (existing.indexOf(lName) < 0) {
                                    const newLabels = existing.concat([lName])
                                    taskViewModel.setTaskLabels(taskId, newLabels)
                                    if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                                        telemetry.trackEvent("task_drag_to_label_sidebar", {})
                                }
                            }
                            drop.accept(Qt.MoveAction)
                        }

                        // Подсветка строки при наведении перетаскиваемой задачи
                        Rectangle {
                            anchors.fill: parent
                            color: Qt.rgba(ThemeManager.accentColor.r,
                                           ThemeManager.accentColor.g,
                                           ThemeManager.accentColor.b, 0.20)
                            radius: 4
                            visible: labelDropArea.containsDrag
                            z: -1   // за содержимым строки, над её фоном
                        }
                    }
                } // Item labelDelegate
            } // Repeater labelRepeater

            // ── Divider before app-level views ────────────────────────────
            Item { width: 1; height: 16 }
            Rectangle {
                width: parent.width - 24
                x: 12
                height: 1
                color: sidebarContent.borderColor
            }
            Item { width: 1; height: 8 }

            // ── App-level views (settings / productivity / notifications)
            SidebarItem {
                width: parent.width
                iconName: "productivity"
                label: qsTr("Продуктивность")
                active: isActive("productivity")
                isDarkTheme: sidebarContent.isDarkTheme
                onClicked: sidebarViewModel.navigateTo("productivity")
            }

            SidebarItem {
                width: parent.width
                iconName: "bell"
                label: qsTr("Уведомления")
                active: isActive("notifications")
                isDarkTheme: sidebarContent.isDarkTheme
                onClicked: sidebarViewModel.navigateTo("notifications")
            }

            SidebarItem {
                width: parent.width
                iconName: "sparkles"
                label: qsTr("Помощник")
                active: isActive("ai")
                isDarkTheme: sidebarContent.isDarkTheme
                onClicked: sidebarViewModel.navigateTo("ai")
            }

            SidebarItem {
                width: parent.width
                iconName: "settings"
                label: qsTr("Настройки")
                active: isActive("settings")
                isDarkTheme: sidebarContent.isDarkTheme
                onClicked: sidebarViewModel.navigateTo("settings")
            }

            // -----------------------------------------------------------------
            // Корзина — недавно удалённые задачи (мягкое удаление, можно
            // восстановить).  Счётчик обновляется по сигналам dataStore.
            // -----------------------------------------------------------------

            SidebarItem {
                id: trashItem
                width: parent.width
                iconName: "trash"
                label: qsTr("Корзина")
                count: trashItem._trashCount
                showCount: trashItem._trashCount > 0
                active: isActive("trash")
                isDarkTheme: sidebarContent.isDarkTheme
                onClicked: sidebarViewModel.navigateTo("trash")

                // Cached count, refreshed from taskViewModel.trashCount() on
                // every data change.  Storing it as a property avoids calling
                // the C++ method during binding evaluation hot paths.
                property int _trashCount: 0
                function _refresh() { _trashCount = taskViewModel.trashCount() }
                Component.onCompleted: _refresh()

                Connections {
                    target: dataStore
                    function onTasksChanged() { Qt.callLater(trashItem._refresh) }
                    function onDataChanged()  { Qt.callLater(trashItem._refresh) }
                }
            }

            // -----------------------------------------------------------------
            // Streak footer — серия дней с выполненными задачами
            // -----------------------------------------------------------------

            Rectangle {
                id: streakFooter
                width: parent.width
                height: 36
                color: "transparent"
                visible: typeof karmaViewModel !== "undefined" && karmaViewModel !== null
                         && karmaViewModel.dailyStreak > 0

                // Top divider
                Rectangle {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: Spacing.md
                    anchors.rightMargin: Spacing.md
                    height: 1
                    color: ThemeManager.borderColor
                    opacity: 0.3
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.topMargin: 1   // clear the divider line
                    anchors.leftMargin: Spacing.md
                    anchors.rightMargin: Spacing.md
                    spacing: Spacing.xs

                    // Q4.01 (redesign): was "🔥" Unicode emoji; flame glyph
                    // from the stroke pack instead. Tinted accent so the
                    // streak still reads as warm/positive against the dark
                    // and light themes.
                    IconSvg {
                        name: "flame"
                        size: 14
                        color: ThemeManager.accentColor
                        Layout.alignment: Qt.AlignVCenter
                    }

                    Text {
                        id: streakLabel
                        text: {
                            if (typeof karmaViewModel === "undefined" || karmaViewModel === null)
                                return ""
                            const n = karmaViewModel.dailyStreak
                            if (n === 0) return ""
                            // Русское склонение: день / дня / дней
                            const m100 = n % 100
                            const m10  = n % 10
                            let word
                            if (m100 >= 11 && m100 <= 14)      word = qsTr("дней")
                            else if (m10 === 1)                 word = qsTr("день")
                            else if (m10 >= 2 && m10 <= 4)     word = qsTr("дня")
                            else                                word = qsTr("дней")
                            return qsTr("%1 %2 подряд").arg(n).arg(word)
                        }
                        font.pixelSize: FontSize.caption
                        color: ThemeManager.textColor
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                    }

                    // Недельная серия — показывается когда weeklyStreak > 0.
                    // Q4.01 (redesign): "📆" emoji-префикс вынесен в IconSvg
                    // "calendar" + чистый Text. Row сохраняет компоновку с
                    // соседним flame+streakLabel выше.
                    Row {
                        visible: typeof karmaViewModel !== "undefined" && karmaViewModel !== null
                                 && karmaViewModel.weeklyStreak > 0
                        spacing: 3
                        Layout.alignment: Qt.AlignVCenter

                        IconSvg {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "calendar"
                            size: 11
                            color: ThemeManager.secondaryTextColor
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: (typeof karmaViewModel !== "undefined" && karmaViewModel !== null)
                                  ? qsTr("%1 нед.").arg(karmaViewModel.weeklyStreak)
                                  : ""
                            font.pixelSize: FontSize.caption
                            color: ThemeManager.secondaryTextColor
                        }
                    }
                }

                // Подсказка при наведении
                HoverHandler { id: streakHover }
                ToolTip.visible: streakHover.hovered
                ToolTip.delay: 500
                ToolTip.text: qsTr("Серия дней с выполненными задачами")
            }

            // Bottom spacer
            Item { width: 1; height: 8 }
        } // mainColumn
    } // Flickable

    // -------------------------------------------------------------------------
    // Add Project Dialog
    // -------------------------------------------------------------------------

    Dialog {
        id: addProjectDialog
        title: qsTr("Добавить проект")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel

        readonly property var projectColors: [
            "#db4c3f", "#e74c3c", "#ff9933", "#fad000",
            "#7ecc49", "#299438", "#6accbc", "#158fad",
            "#14aaf5", "#a47bf5", "#ad6cf5", "#e05194"
        ]
        property string selectedColor: projectColors[0]

        // Q4.01 iter-6: pre-selected иконка для нового проекта. Строка вида
        // "projects/<glyph>" сохраняется через updateProject сразу после
        // создания (addProject принимает только name + color на C++ стороне).
        property string selectedIcon: ""

        onAccepted: {
            if (newProjectName.text.trim() !== "") {
                const newId = projectViewModel.addProject(
                    newProjectName.text.trim(), addProjectDialog.selectedColor)
                if (newId && newId !== "" && addProjectDialog.selectedIcon !== "")
                    projectViewModel.updateProject(newId, "icon", addProjectDialog.selectedIcon)
            }
            newProjectName.text = ""
            addProjectDialog.selectedColor = addProjectDialog.projectColors[0]
            addProjectDialog.selectedIcon = ""
        }
        onRejected: {
            newProjectName.text = ""
            addProjectDialog.selectedColor = addProjectDialog.projectColors[0]
            addProjectDialog.selectedIcon = ""
        }

        ColumnLayout {
            spacing: 12

            TextField {
                id: newProjectName
                Layout.fillWidth: true
                Layout.preferredWidth: 280
                placeholderText: qsTr("Название проекта")
                focus: true
            }

            // ── Иконка ──────────────────────────────────────────────────
            // Q4.01 iter-6: новая секция — иконка проекта. Кликабельная
            // плитка с превью; по клику открывается ProjectIconPicker.
            Text {
                text: qsTr("Иконка")
                font.pixelSize: FontSize.small
                color: ThemeManager.secondaryTextColor
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredWidth: 280
                Layout.preferredHeight: 40
                radius: Radius.md
                color: addProjectIconArea.containsMouse
                       ? ThemeManager.hoverColor
                       : ThemeManager.surfaceColor
                border.color: ThemeManager.borderColor
                border.width: 1

                Behavior on color { ColorAnimation { duration: 100 } }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Spacing.sm
                    anchors.rightMargin: Spacing.sm
                    spacing: Spacing.sm

                    // Превью: либо IconSvg, либо цветная точка-fallback.
                    IconSvg {
                        visible: addProjectDialog.selectedIcon !== ""
                        name: addProjectDialog.selectedIcon
                        size: 20
                        color: ThemeManager.accentColor
                        Layout.alignment: Qt.AlignVCenter
                    }
                    Rectangle {
                        visible: addProjectDialog.selectedIcon === ""
                        Layout.alignment: Qt.AlignVCenter
                        width: 12
                        height: 12
                        radius: 6
                        color: addProjectDialog.selectedColor
                    }
                    Text {
                        text: addProjectDialog.selectedIcon !== ""
                              ? addProjectDialog.selectedIcon.replace("projects/", "")
                              : qsTr("Выбрать иконку…")
                        color: addProjectDialog.selectedIcon !== ""
                               ? ThemeManager.textColor
                               : ThemeManager.secondaryTextColor
                        font.pixelSize: FontSize.body
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                    }
                    IconSvg {
                        name: "chevron-right"
                        size: 14
                        color: ThemeManager.secondaryTextColor
                        Layout.alignment: Qt.AlignVCenter
                    }
                }

                MouseArea {
                    id: addProjectIconArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: iconPicker.openWith(addProjectDialog.selectedIcon)
                }
            }

            Text {
                text: qsTr("Цвет")
                font.pixelSize: FontSize.small
                color: ThemeManager.secondaryTextColor
            }

            Flow {
                Layout.fillWidth: true
                Layout.preferredWidth: 280
                spacing: 8

                Repeater {
                    model: addProjectDialog.projectColors

                    delegate: Rectangle {
                        required property string modelData

                        width: 24
                        height: 24
                        radius: 12
                        color: modelData
                        border.color: ThemeManager.textColor
                        border.width: addProjectDialog.selectedColor === modelData ? 2 : 0

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: addProjectDialog.selectedColor = parent.modelData
                        }
                    }
                }
            }
        }
    }

    // ── Project Icon Picker (shared) ─────────────────────────────────────────
    // Q4.01 iter-6: единый picker для addProjectDialog и (в следующих
    // итерациях) для контекст-меню «Изменить иконку…». accepted-target
    // выставляется в .openWith() через дополнительные свойства context-id;
    // здесь по сигналу iconAccepted мы знаем, что вызвал addProjectDialog,
    // и сохраняем выбор в addProjectDialog.selectedIcon.
    ProjectIconPicker {
        id: iconPicker
        onIconAccepted: function(icon) {
            addProjectDialog.selectedIcon = icon
        }
    }

    // Q4.01 iter-7: второй экземпляр пикера для контекст-меню "Изменить
    // иконку…". Отдельный экземпляр (а не дискриминатор) — потому что
    // обработчик iconAccepted ходит в разные ViewModel-методы:
    //   • iconPicker      → set local state of addProjectDialog
    //   • ctxIconPicker   → projectViewModel.updateProject(id, "icon", ...)
    ProjectIconPicker {
        id: ctxIconPicker
        property string targetProjectId: ""

        onIconAccepted: function(icon) {
            if (ctxIconPicker.targetProjectId === "") return
            projectViewModel.updateProject(
                ctxIconPicker.targetProjectId, "icon", icon)
            if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                telemetry.trackEvent("project_icon_changed", { icon: icon })
            ctxIconPicker.targetProjectId = ""
        }
    }

    // -------------------------------------------------------------------------
    // Add Filter Dialog
    // -------------------------------------------------------------------------

    Dialog {
        id: addFilterDialog
        title: qsTr("Добавить фильтр")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel

        // Disable OK while name is empty so onAccepted only fires for valid input.
        function _refreshOkState() {
            const okBtn = addFilterDialog.standardButton(Dialog.Ok)
            if (okBtn)
                okBtn.enabled = newFilterName.text.trim() !== ""
        }

        onOpened: _refreshOkState()

        onAccepted: {
            const name = newFilterName.text.trim()
            if (name === "")
                return
            const query = newFilterQuery.query.trim()
            filterViewModel.addFilter(name, query, "")
            newFilterName.text = ""
            newFilterQuery.query = ""
        }
        onRejected: {
            newFilterName.text = ""
            newFilterQuery.query = ""
        }

        ColumnLayout {
            spacing: 12

            TextField {
                id: newFilterName
                Layout.fillWidth: true
                Layout.preferredWidth: 320
                placeholderText: qsTr("Название фильтра")
                focus: true
                onTextChanged: addFilterDialog._refreshOkState()
            }

            Text {
                text: qsTr("Запрос фильтра")
                font.pixelSize: FontSize.small
                color: ThemeManager.secondaryTextColor
            }

            FilterQueryInput {
                id: newFilterQuery
                Layout.fillWidth: true
                Layout.preferredWidth: 320
            }
        }
    }

    // -------------------------------------------------------------------------
    // Per-project context-menu support dialogs
    // -------------------------------------------------------------------------

    Dialog {
        id: deleteProjectDialog
        property string projectId:   ""
        property string projectName: ""
        title: qsTr("Удалить проект")
        modal: true
        anchors.centerIn: Overlay.overlay
        // Explicit width: avoids implicitWidth binding loop with the wrapping Label.
        width: Math.min(380, Overlay.overlay.width - Spacing.xl)
        standardButtons: Dialog.Yes | Dialog.No

        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label {
                text: qsTr("Удалить проект «%1» вместе со всеми задачами и разделами? Действие необратимо.").arg(deleteProjectDialog.projectName)
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: ThemeManager.textColor
            }
        }

        onAccepted: {
            if (deleteProjectDialog.projectId !== "")
                projectViewModel.deleteProject(deleteProjectDialog.projectId)
        }
    }

    Dialog {
        id: addSectionFromMenuDialog
        property string projectId: ""
        title: qsTr("Добавить раздел")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(360, Overlay.overlay.width - Spacing.xl)
        standardButtons: Dialog.Ok | Dialog.Cancel

        onOpened:   newSectionName.text = ""
        onAccepted: {
            const n = newSectionName.text.trim()
            if (n !== "" && addSectionFromMenuDialog.projectId !== "")
                dataStore.addSection(n, addSectionFromMenuDialog.projectId)
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label {
                text: qsTr("Название раздела")
                color: ThemeManager.secondaryTextColor
                font.pixelSize: 13
                Layout.fillWidth: true
            }
            TextField {
                id: newSectionName
                Layout.fillWidth: true
                placeholderText: qsTr("Например: В работе")
                color: ThemeManager.textColor
            }
        }
    }

    Dialog {
        id: parentPickerDialog
        property string projectId:   ""
        property string projectName: ""
        title: qsTr("Родительский проект")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(380, Overlay.overlay.width - Spacing.xl)
        standardButtons: Dialog.Ok | Dialog.Cancel

        onAccepted: {
            if (parentPickerDialog.projectId === "") return
            const newParent = parentCombo.currentValue || ""
            // Disallow self-reference (empty == top-level which is fine).
            if (newParent === parentPickerDialog.projectId) return
            projectViewModel.updateProject(parentPickerDialog.projectId,
                                           "parentId", newParent)
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 12
            Label {
                text: qsTr("Сделать «%1» подпроектом:").arg(parentPickerDialog.projectName)
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: ThemeManager.textColor
            }
            ComboBox {
                id: parentCombo
                Layout.fillWidth: true
                textRole: "name"
                valueRole: "id"

                function _rebuild() {
                    const items = [{ id: "", name: qsTr("(нет — корневой)") }]
                    if (typeof projectViewModel !== "undefined" && typeof dataStore !== "undefined") {
                        const allProjs = dataStore.allProjects()
                        for (let i = 0; i < allProjs.length; ++i) {
                            const p = allProjs[i]
                            if (!p) continue
                            if (p.isInbox || p.isArchived) continue
                            if (p.id === parentPickerDialog.projectId) continue
                            items.push({ id: p.id, name: p.name })
                        }
                    }
                    parentCombo.model = items
                    const cur = projectViewModel.projectById(parentPickerDialog.projectId)
                    if (cur && cur.parentId)
                        parentCombo.currentIndex = parentCombo.indexOfValue(cur.parentId)
                    else
                        parentCombo.currentIndex = 0
                }

                Component.onCompleted: _rebuild()
                Connections {
                    target: parentPickerDialog
                    function onOpened() { parentCombo._rebuild() }
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // Filter context-menu support dialogs
    // -------------------------------------------------------------------------

    Dialog {
        id: renameFilterDialog
        property string filterId:   ""
        property string filterName: ""
        title: qsTr("Переименовать фильтр")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel

        function _refreshOkState() {
            const okBtn = renameFilterDialog.standardButton(Dialog.Ok)
            if (okBtn)
                okBtn.enabled = renameFilterField.text.trim() !== ""
        }

        onOpened: {
            renameFilterField.text = renameFilterDialog.filterName
            _refreshOkState()
        }

        onAccepted: {
            const n = renameFilterField.text.trim()
            if (n !== "" && renameFilterDialog.filterId !== "")
                filterViewModel.updateFilter(renameFilterDialog.filterId, "name", n)
        }

        ColumnLayout {
            spacing: 8
            width: 280
            Label {
                text: qsTr("Новое название")
                color: ThemeManager.secondaryTextColor
                font.pixelSize: 13
            }
            TextField {
                id: renameFilterField
                Layout.fillWidth: true
                placeholderText: qsTr("Название фильтра")
                color: ThemeManager.textColor
                onTextChanged: renameFilterDialog._refreshOkState()
            }
        }
    }

    Dialog {
        id: editFilterQueryDialog
        property string filterId:     ""
        property string filterName:   ""
        property string initialQuery: ""
        title: qsTr("Изменить запрос фильтра")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel

        onOpened: {
            editQueryInput.query = editFilterQueryDialog.initialQuery
        }

        onAccepted: {
            if (editFilterQueryDialog.filterId !== "")
                filterViewModel.updateFilter(editFilterQueryDialog.filterId,
                                             "query", editQueryInput.query.trim())
        }

        ColumnLayout {
            spacing: 12
            width: 360

            Label {
                text: qsTr("Фильтр: %1").arg(editFilterQueryDialog.filterName)
                font.pixelSize: 13
                font.weight: Font.Medium
                color: ThemeManager.textColor
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }

            Text {
                text: qsTr("Запрос")
                font.pixelSize: FontSize.small
                color: ThemeManager.secondaryTextColor
            }

            FilterQueryInput {
                id: editQueryInput
                Layout.fillWidth: true
                Layout.preferredWidth: 360
            }
        }
    }

    Dialog {
        id: deleteFilterDialog
        property string filterId:   ""
        property string filterName: ""
        title: qsTr("Удалить фильтр")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Yes | Dialog.No

        Label {
            text: qsTr("Удалить фильтр «%1»? Действие необратимо.").arg(deleteFilterDialog.filterName)
            wrapMode: Text.Wrap
            width: 300
            color: ThemeManager.textColor
        }

        onAccepted: {
            if (deleteFilterDialog.filterId !== "")
                filterViewModel.deleteFilter(deleteFilterDialog.filterId)
        }
    }

    // =========================================================================
    // Inline component: SectionHeader
    // =========================================================================

    component SectionHeader: Item {
        id: sectionHeader

        property string title: ""
        property bool isDarkTheme: false
        property bool showAddButton: false

        // Archive toggle button (shown next to "+" in Projects header)
        property bool showArchiveToggle: false
        property bool archiveActive: false   // true = archive is visible → button highlighted

        signal addClicked()
        signal archiveToggleClicked()

        height: 40
        // top margin 24px, bottom margin 8px, text ~8px => ~40px total row

        Rectangle {
            anchors.fill: parent
            color: "transparent"
        }

        Text {
            id: headerLabel
            anchors {
                left: parent.left
                leftMargin: Spacing.md
                verticalCenter: parent.verticalCenter
                verticalCenterOffset: 4
            }
            text: sectionHeader.title.toUpperCase()
            font.pixelSize: FontSize.caption
            font.weight: Font.DemiBold
            font.letterSpacing: 0.5
            // Iter-night-31: was hardcoded "#999999" — that's exactly the
            // dark-theme secondaryTextColor (ThemeManager.qml:191), but
            // every other theme uses a different shade: light=#666666,
            // kale=#8ea88c, blueberry=#8a90c8, … so the section labels
            // ("ПРОЕКТЫ", "МЕТКИ", "ФИЛЬТРЫ", "ИЗБРАННОЕ") rendered with
            // bad contrast in 8 of 9 palettes. The neighbouring "+" icon
            // was already bound to ThemeManager.secondaryTextColor in
            // iter-night-23 (see comment at the IconSvg below); this row
            // header was the lone leftover from that sweep.
            color: ThemeManager.secondaryTextColor
        }

        // Archive toggle icon — appears to the left of the "+" button.
        // Q4.01 (redesign): was the "📦" emoji glyph, now an IconSvg from
        // the stroke pack. Tinted accent when active so the toggle still
        // reads as "on", neutral secondaryTextColor otherwise (matches the
        // sibling "+" button next door).
        Rectangle {
            id: archiveToggleBtn
            visible: sectionHeader.showArchiveToggle
            anchors {
                right: addBtn.visible ? addBtn.left : parent.right
                rightMargin: addBtn.visible ? 4 : 8
                verticalCenter: parent.verticalCenter
                verticalCenterOffset: 4
            }
            width: 20
            height: 20
            radius: 10
            color: sectionHeader.archiveActive
                   ? (sectionHeader.isDarkTheme ? "#5a3a1a" : "#fff3e0")
                   : archiveBtnMouse.containsMouse
                     ? (sectionHeader.isDarkTheme ? "#444444" : "#e8e8e8")
                     : "transparent"

            Behavior on color { ColorAnimation { duration: 100 } }

            IconSvg {
                anchors.centerIn: parent
                name: "archive"
                size: 14
                color: sectionHeader.archiveActive
                       ? ThemeManager.accentColor
                       : ThemeManager.secondaryTextColor
            }

            MouseArea {
                id: archiveBtnMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: sectionHeader.archiveToggleClicked()
            }
        }

        // "+" add button on the right
        Rectangle {
            id: addBtn
            visible: sectionHeader.showAddButton
            anchors {
                right: parent.right
                rightMargin: 8
                verticalCenter: parent.verticalCenter
                verticalCenterOffset: 4
            }
            width: 20
            height: 20
            radius: 10
            color: addBtnMouse.containsMouse
                   ? (sectionHeader.isDarkTheme ? "#444444" : "#e8e8e8")
                   : "transparent"

            Behavior on color { ColorAnimation { duration: 100 } }

            // Task 17cea65b iter-3: было unicode "+".
            // Iter-night-23: was hardcoded "#999999" — same shade as the
            // dark theme's secondaryTextColor, but invisible against the
            // light theme's brighter sidebar. Bind to the theme so the
            // glyph adapts to every palette.
            IconSvg {
                anchors.centerIn: parent
                name: "plus"
                size: 16
                color: ThemeManager.secondaryTextColor
            }

            MouseArea {
                id: addBtnMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: sectionHeader.addClicked()
            }
        }
    }

    // =========================================================================
    // Inline component: SidebarItem
    // =========================================================================

    component SidebarItem: Rectangle {
        id: sidebarItem

        property string iconText: ""
        // Q4.01 (redesign): when set, render an IconSvg from the stroke-style
        // pack instead of the iconText emoji glyph. Mutually exclusive with
        // iconText / iconIsText (those still work as fallbacks for user-typed
        // emoji icons on projects). Naming matches the file under
        // resources/icons/ui/<iconName>.svg.
        property string iconName: ""
        property bool iconIsText: false   // true => show colored dot instead of emoji
        property color dotColor: "#db4c3f"
        property string label: ""
        property int count: 0
        property bool showCount: false
        property bool active: false
        property bool isDarkTheme: false

        signal clicked()
        signal doubleClicked()

        // Объявлено раньше color-привязки ниже, чтобы hoverMouse.containsMouse
        // не давало undefined при первом построении компонента.
        MouseArea {
            id: hoverMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            // Double-click is an additional way for users to expand
            // hierarchical sidebar items (projects with sub-projects) when
            // the small chevron hit-zone is hard to target with a touchpad
            // or high-DPI mouse. The single-click handler still navigates.
            onClicked: sidebarItem.clicked()
            onDoubleClicked: sidebarItem.doubleClicked()
        }

        height: 36
        radius: Radius.md
        color: active
               ? (isDarkTheme ? "#3d2020" : "#ffede8")
               : hoverMouse.containsMouse
                 ? (isDarkTheme ? "#333333" : "#f5f5f5")
                 : "transparent"

        Behavior on color { ColorAnimation { duration: 150 } }

        // Left margin spacer
        Item {
            id: leftPad
            width: 8
            height: parent.height
            anchors.left: parent.left
        }

        // Icon / dot
        // Q4.01 iter-5: добавлен auto-detect префикса "projects/" в iconText.
        // Когда Project.icon хранится как "projects/briefcase" (новый формат
        // из ProjectIconPicker), рендерим IconSvg вместо Text-emoji. Совмест-
        // имость: старые проекты с эмодзи продолжают рендериться текстом.
        Item {
            id: iconArea
            width: 22
            height: parent.height
            anchors.left: leftPad.right

            // Резолв эффективного имени иконки: явный iconName или iconText
            // с префиксом "projects/" → IconSvg.
            readonly property string _effectiveIconName: {
                if (sidebarItem.iconName !== "") return sidebarItem.iconName
                if (sidebarItem.iconText.indexOf("projects/") === 0) return sidebarItem.iconText
                return ""
            }

            // Stroke-SVG icon (Q4.01 redesign). Wins over the emoji Text
            // when iconName is set; tinted via MultiEffect in IconSvg so
            // active/idle states animate the colour.
            IconSvg {
                visible: iconArea._effectiveIconName !== ""
                anchors.centerIn: parent
                name: iconArea._effectiveIconName
                size: 16
                color: sidebarItem.active
                       ? ThemeManager.accentColor
                       : (isDarkTheme ? "#aaaaaa" : "#555555")
            }

            // Colored dot for projects / favorites (no SVG icon set, no emoji)
            Rectangle {
                visible: iconArea._effectiveIconName === "" && sidebarItem.iconIsText
                anchors.centerIn: parent
                width: 8
                height: 8
                radius: Radius.sm
                color: sidebarItem.dotColor
            }

            // Emoji icon — user-typed glyph on a project, or legacy default.
            Text {
                visible: iconArea._effectiveIconName === "" && !sidebarItem.iconIsText
                anchors.centerIn: parent
                text: sidebarItem.iconText
                font.pixelSize: FontSize.body
                color: sidebarItem.active ? "#db4c3f" : (isDarkTheme ? "#aaaaaa" : "#555555")
            }
        }

        // Label
        Text {
            id: itemLabel
            anchors {
                left: iconArea.right
                leftMargin: 6
                right: badgeLoader.left
                rightMargin: 4
                verticalCenter: parent.verticalCenter
            }
            text: sidebarItem.label
            font.pixelSize: FontSize.body
            elide: Text.ElideRight
            color: sidebarItem.active
                   ? "#db4c3f"
                   : (isDarkTheme ? "#e0e0e0" : "#202020")

            Behavior on color { ColorAnimation { duration: 150 } }
        }

        // Count badge (pill)
        Loader {
            id: badgeLoader
            anchors {
                right: parent.right
                rightMargin: 10
                verticalCenter: parent.verticalCenter
            }
            active: sidebarItem.showCount && sidebarItem.count > 0
            visible: active

            sourceComponent: Rectangle {
                height: 18
                width: Math.max(18, badgeText.implicitWidth + 10)
                radius: 9
                color: sidebarItem.active
                       ? "#db4c3f"
                       : (isDarkTheme ? "#3a3a3a" : "#ebebeb")

                Behavior on color { ColorAnimation { duration: 150 } }

                Text {
                    id: badgeText
                    anchors.centerIn: parent
                    text: sidebarItem.count > 99 ? "99+" : sidebarItem.count.toString()
                    font.pixelSize: FontSize.caption
                    font.weight: Font.Medium
                    color: sidebarItem.active
                           ? "#ffffff"
                           : (isDarkTheme ? "#aaaaaa" : "#666666")
                }
            }
        }

    } // SidebarItem

} // sidebarContent
