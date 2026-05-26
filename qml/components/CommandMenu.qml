import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

// CommandMenu — VS Code / Linear-style command palette (Ctrl+K / Cmd+K)
Item {
    id: root

    // ── Public API ────────────────────────────────────────────────────────────
    property bool active: false

    // open() / close() convenience
    function open()  { root.active = true  }
    function close() { root.active = false }

    // ── Theme helpers ─────────────────────────────────────────────────────────
    property bool isDarkTheme: ThemeManager.isDark

    readonly property color _bgColor:     ThemeManager.backgroundColor
    readonly property color _overlayBg:   "#80000000"
    readonly property color _textColor:   ThemeManager.textColor
    readonly property color _subColor:    ThemeManager.secondaryTextColor
    readonly property color _borderColor: ThemeManager.borderColor
    readonly property color _itemHover:   ThemeManager.hoverColor
    readonly property color _selColor:    ThemeManager.sidebarActiveColor
    readonly property color _accent:      ThemeManager.accentColor
    readonly property color _hintColor:   isDarkTheme ? "#666666" : "#aaaaaa"
    readonly property color _catColor:    isDarkTheme ? "#444444" : "#bbbbbb"
    readonly property color _divider:     ThemeManager.borderColor

    // Fill the overlay — parent should be ApplicationWindow or root window
    anchors.fill: parent
    visible: active

    // ── Fuzzy match ───────────────────────────────────────────────────────────
    // Returns score >= 0 when all query chars are found in order; -1 otherwise.
    // Higher score = better match (consecutive chars + word-start bonuses).
    function fuzzyMatch(query, target) {
        var q = query.toLowerCase()
        var t = target.toLowerCase()
        var qi = 0
        var score = 0
        for (var ti = 0; ti < t.length && qi < q.length; ti++) {
            if (t[ti] === q[qi]) {
                score += 1
                if (ti === 0 || t[ti - 1] === ' ') score += 2   // word-start bonus
                if (qi > 0) {
                    // consecutive bonus: check if prev matched char was adjacent
                    score += 1
                }
                qi++
            }
        }
        return (qi === q.length) ? score : -1
    }

    // ── Recents (in-memory, max 5) ────────────────────────────────────────────
    property var _recentIds: []

    function _pushRecent(cmdId) {
        var arr = _recentIds.filter(function(id) { return id !== cmdId })
        arr.unshift(cmdId)
        if (arr.length > 5) arr = arr.slice(0, 5)
        _recentIds = arr
    }

    // ── Theme display names ───────────────────────────────────────────────────
    function _themeLabel(key) {
        var map = {
            "system":        qsTr("Системная тема"),
            "classic_light": qsTr("Светлая"),
            "dark":          qsTr("Тёмная"),
            "kale":          "Kale",
            "blueberry":     "Blueberry",
            "lavender":      "Lavender",
            "raspberry":     "Raspberry",
            "graphite":      "Graphite",
            "sunflower":     "Sunflower"
        }
        return map[key] || key
    }

    // ── Command catalog ───────────────────────────────────────────────────────
    // Rebuilt whenever active changes (picks up live projects / themes).
    property var _allCommands: []

    function _buildCatalog() {
        var list = []

        // ── Navigation ──────────────────────────────────────────────────────
        // Q4.01 (redesign): командному меню добавлено поле iconName — имя
        // SVG-иконки из stroke-пака. Делегат рендерит её через IconSvg, а
        // поле `icon` остаётся как fallback-эмодзи и для динамических
        // элементов (проекты-точка "●", темы "🎨" и т.п.), где stroke-
        // иконка не нужна.
        list.push({ id: "nav_inbox",        category: "Navigation", iconName: "inbox",
                    label: qsTr("Перейти во Входящие"),    shortcut: "",
                    action: "inbox" })
        list.push({ id: "nav_today",        category: "Navigation", iconName: "today",
                    label: qsTr("Перейти в Сегодня"),      shortcut: "",
                    action: "today" })
        list.push({ id: "nav_upcoming",     category: "Navigation", iconName: "upcoming",
                    label: qsTr("Перейти в Предстоящие"),  shortcut: "",
                    action: "upcoming" })
        list.push({ id: "nav_settings",     category: "Navigation", iconName: "settings",
                    label: qsTr("Перейти в Настройки"),    shortcut: "",
                    action: "settings" })
        list.push({ id: "nav_productivity", category: "Navigation", iconName: "productivity",
                    label: qsTr("Перейти в Продуктивность"), shortcut: "",
                    action: "productivity" })
        list.push({ id: "nav_notifications", category: "Navigation", iconName: "bell",
                    label: qsTr("Перейти в Уведомления"),  shortcut: "",
                    action: "notifications" })

        // ── Projects (dynamic) ──────────────────────────────────────────────
        // Primary: dataStore.allProjects() — gives full model objects with color.
        // Fallback: projectViewModel row iteration for compatibility.
        var _projectsAdded = false
        try {
            if (typeof dataStore !== "undefined" && dataStore
                && typeof dataStore.allProjects === "function") {
                var allProjects = dataStore.allProjects()
                for (var pi = 0; pi < allProjects.length; pi++) {
                    var project = allProjects[pi]
                    // Skip archived / soft-deleted / inbox pseudo-projects
                    if (!project || !project.id || project.is_deleted || project.is_archived)
                        continue
                    list.push({
                        id:       "project_" + project.id,
                        category: "Projects",
                        icon:     "●",
                        label:    qsTr("Перейти к проекту: %1").arg(project.name),
                        keywords: (project.name || "") + " " + (project.color || ""),
                        shortcut: "",
                        action:   "project:" + project.id
                    })
                }
                _projectsAdded = true
            }
        } catch(e) {}

        // Fallback: projectViewModel model iteration
        if (!_projectsAdded) {
            try {
                if (typeof projectViewModel !== "undefined" && projectViewModel
                    && typeof projectViewModel.rowCount === "function") {
                    for (var pfi = 0; pfi < projectViewModel.rowCount(); pfi++) {
                        var pfidx  = projectViewModel.index(pfi, 0)
                        var pfname = projectViewModel.data(pfidx, Qt.DisplayRole) || ""
                        var pfid   = projectViewModel.data(pfidx, Qt.UserRole)    || ""
                        if (pfname && pfid) {
                            list.push({
                                id:       "project_" + pfid,
                                category: "Projects",
                                icon:     "●",
                                label:    qsTr("Перейти к проекту: %1").arg(pfname),
                                keywords: pfname,
                                shortcut: "",
                                action:   "project:" + pfid
                            })
                        }
                    }
                }
            } catch(e) {}
        }

        // ── Actions ──────────────────────────────────────────────────────────
        list.push({ id: "act_new_task",    category: "Actions", iconName: "projects/pencil",
                    label: qsTr("Новая задача"),       shortcut: "Q",
                    action: "new_task" })
        list.push({ id: "act_add_task",    category: "Actions", iconName: "plus",
                    label: qsTr("Добавить задачу"),   shortcut: "",
                    action: "add_task" })
        list.push({ id: "act_add_project", category: "Actions", iconName: "folder",
                    label: qsTr("Добавить проект"),   shortcut: "",
                    action: "add_project" })
        list.push({ id: "act_add_filter",  category: "Actions", iconName: "filter",
                    label: qsTr("Добавить фильтр"),   shortcut: "",
                    action: "add_filter" })
        list.push({ id: "act_add_label",   category: "Actions", iconName: "label",
                    label: qsTr("Добавить метку"),    shortcut: "",
                    action: "add_label" })
        list.push({ id: "act_search",      category: "Actions", iconName: "search",
                    label: qsTr("Поиск"),             shortcut: "/",
                    action: "search" })
        // ITER1 #45 / ITER2 #5: Undo action wired to undoStack.
        // Only add the entry when undoStack.canUndo is true so the palette does
        // not show an always-enabled undo command when there is nothing to undo.
        // UndoStack exposes Q_PROPERTY(bool canUndo) so this binding is reactive.
        var _canUndo = (typeof undoStack !== "undefined" && undoStack
                        && undoStack.canUndo === true)
        if (_canUndo) {
            var undoLabel = (typeof undoStack !== "undefined" && undoStack
                             && undoStack.lastDescription && undoStack.lastDescription !== "")
                            ? qsTr("Отменить: %1").arg(undoStack.lastDescription)
                            : qsTr("Отменить действие")
            list.push({ id: "action_undo", category: "Actions", iconName: "reset",
                        label: undoLabel, shortcut: "Ctrl+Z",
                        action: "undo" })
        }

        // ── Selected task (if any) ───────────────────────────────────────────
        try {
            var selTask = (typeof taskViewModel !== "undefined" && taskViewModel)
                          ? taskViewModel.selectedTask : null
            if (selTask) {
                list.push({ id: "task_complete", category: "Selected task", iconName: "check",
                             label: qsTr("Завершить задачу"),  shortcut: "",
                             action: "task_complete" })
                list.push({ id: "task_delete",   category: "Selected task", iconName: "trash",
                             label: qsTr("Удалить задачу"),    shortcut: "",
                             action: "task_delete" })
                list.push({ id: "task_edit",     category: "Selected task", iconName: "projects/pencil",
                             label: qsTr("Редактировать задачу"), shortcut: "",
                             action: "task_edit" })
            }
        } catch(e) {}

        // ── Themes (dynamic) ─────────────────────────────────────────────────
        try {
            var themes = (typeof settingsViewModel !== "undefined" && settingsViewModel
                          && typeof settingsViewModel.availableThemes === "function")
                         ? settingsViewModel.availableThemes() : []
            for (var ti = 0; ti < themes.length; ti++) {
                var tk = themes[ti]
                list.push({
                    id: "theme_" + tk,
                    category: "Themes",
                    icon: "🎨",
                    label: qsTr("Переключить тему: %1").arg(_themeLabel(tk)),
                    shortcut: "",
                    action: "set_theme:" + tk
                })
            }
        } catch(e) {}

        // ── Other ─────────────────────────────────────────────────────────────
        list.push({ id: "other_sidebar",  category: "Other", iconName: "sidebar",
                    label: qsTr("Боковая панель вкл/выкл"), shortcut: "",
                    action: "toggle_sidebar" })
        list.push({ id: "other_reload",   category: "Other", iconName: "sync",
                    label: qsTr("Перезагрузить данные"),    shortcut: "",
                    action: "reload" })
        list.push({ id: "other_quit",     category: "Other", iconName: "close",
                    label: qsTr("Выйти из приложения"),     shortcut: "Alt+F4",
                    action: "quit" })

        return list
    }

    // ── Filtered / sorted list ────────────────────────────────────────────────
    property var _filteredCommands: []
    property int _selectedIndex: 0

    function _rebuildList(query) {
        var all = _allCommands
        if (!query || query.trim() === "") {
            // Show recents first, then full catalog
            if (_recentIds.length > 0) {
                var recents = []
                for (var ri = 0; ri < _recentIds.length; ri++) {
                    for (var ai = 0; ai < all.length; ai++) {
                        if (all[ai].id === _recentIds[ri]) {
                            var rc = {}
                            for (var k in all[ai]) rc[k] = all[ai][k]
                            rc.category = "Recent"
                            recents.push(rc)
                            break
                        }
                    }
                }
                // Merge: recents first, then everything else
                var rest = []
                for (var bi = 0; bi < all.length; bi++) {
                    var found = false
                    for (var rj = 0; rj < recents.length; rj++) {
                        if (recents[rj].id === all[bi].id) { found = true; break }
                    }
                    if (!found) rest.push(all[bi])
                }
                _filteredCommands = recents.concat(rest)
            } else {
                _filteredCommands = all
            }
            _selectedIndex = 0
            return
        }

        var scored = []
        for (var i = 0; i < all.length; i++) {
            var cmd = all[i]
            // Score against label + category + optional keywords field
            var s1 = fuzzyMatch(query, cmd.label)
            var s2 = fuzzyMatch(query, cmd.category)
            var s3 = (cmd.keywords) ? fuzzyMatch(query, cmd.keywords) : -1
            var best = Math.max(s1, s2, s3)
            if (best >= 0)
                scored.push({ cmd: cmd, score: best })
        }
        // Sort descending by score
        scored.sort(function(a, b) { return b.score - a.score })

        var out = []
        for (var j = 0; j < scored.length; j++)
            out.push(scored[j].cmd)

        _filteredCommands = out
        _selectedIndex = 0
    }

    function _activateCommand(idx) {
        if (idx < 0 || idx >= _filteredCommands.length) return
        var cmd = _filteredCommands[idx]
        _pushRecent(cmd.id)
        _executeAction(cmd.action)
        root.active = false
    }

    function _executeAction(action) {
        if (action === "inbox") {
            if (typeof sidebarViewModel !== "undefined")
                sidebarViewModel.navigateToInbox()
        } else if (action === "today") {
            if (typeof sidebarViewModel !== "undefined")
                sidebarViewModel.navigateToToday()
        } else if (action === "upcoming") {
            if (typeof sidebarViewModel !== "undefined"
                    && typeof sidebarViewModel.navigateToUpcoming === "function")
                sidebarViewModel.navigateToUpcoming()
        } else if (action === "settings") {
            if (typeof taskViewModel !== "undefined")
                taskViewModel.setFilterMode(TaskViewModel.Settings)
        } else if (action === "productivity") {
            if (typeof taskViewModel !== "undefined")
                taskViewModel.setFilterMode(TaskViewModel.Productivity)
        } else if (action === "notifications") {
            if (typeof taskViewModel !== "undefined")
                taskViewModel.setFilterMode(TaskViewModel.Notifications)
        } else if (action.indexOf("project:") === 0) {
            var pid = action.substring(8)
            if (typeof sidebarViewModel !== "undefined")
                sidebarViewModel.navigateToProject(pid)
        } else if (action === "new_task") {
            // Emit signal — caller wires this to QuickAdd focus
            // TODO main.qml: connect newTaskRequested → focus QuickAdd
            root.newTaskRequested()
        } else if (action === "add_task") {
            // Emit signal — caller connects it to a QuickAdd dialog
            root.addTaskRequested()
        } else if (action === "add_project") {
            if (typeof projectViewModel !== "undefined"
                    && typeof projectViewModel.addProject === "function")
                projectViewModel.addProject("", "#db4c3f")
        } else if (action === "add_filter") {
            root.addFilterRequested()
        } else if (action === "add_label") {
            root.addLabelRequested()
        } else if (action === "search") {
            root.searchRequested()
        } else if (action === "task_complete") {
            try {
                var t = taskViewModel.selectedTask
                if (t && t.id) taskViewModel.completeTask(t.id)
            } catch(e) {}
        } else if (action === "task_delete") {
            try {
                var td = taskViewModel.selectedTask
                if (td && td.id) taskViewModel.deleteTask(td.id)
            } catch(e) {}
        } else if (action === "task_edit") {
            // selectedTask is already showing the editor; nothing extra needed.
        } else if (action.indexOf("set_theme:") === 0) {
            var themeKey = action.substring(10)
            if (typeof settingsViewModel !== "undefined")
                settingsViewModel.theme = themeKey
        } else if (action === "toggle_sidebar") {
            if (typeof sidebarViewModel !== "undefined")
                sidebarViewModel.toggleSidebar()
            else
                root.toggleSidebarRequested()
        } else if (action === "reload") {
            root.reloadRequested()
        } else if (action === "quit") {
            Qt.quit()
        } else if (action === "undo") {
            // ITER1 #45: wire to UndoStack context property
            if (typeof undoStack !== "undefined"
                    && typeof undoStack.undo === "function")
                undoStack.undo()
        }
    }

    // ── Signals for callers ───────────────────────────────────────────────────
    signal searchRequested()
    signal toggleSidebarRequested()
    signal addTaskRequested()
    signal addFilterRequested()
    signal addLabelRequested()
    signal reloadRequested()
    // TODO main.qml: connect newTaskRequested → focus QuickAdd (quickAddLoader.item.forceActiveFocus())
    signal newTaskRequested()

    // ── Open/close logic ──────────────────────────────────────────────────────
    onActiveChanged: {
        if (active) {
            _allCommands = _buildCatalog()
            cmdInput.text = ""
            _rebuildList("")
            _selectedIndex = 0
            cmdInput.forceActiveFocus()
        }
    }

    // ── Semi-transparent backdrop ─────────────────────────────────────────────
    Rectangle {
        id: backdrop
        anchors.fill: parent
        color: root._overlayBg
        opacity: root.active ? 1.0 : 0.0

        Behavior on opacity { NumberAnimation { duration: 150 } }

        MouseArea {
            anchors.fill: parent
            onClicked: root.active = false
        }
    }

    // ── Palette panel ─────────────────────────────────────────────────────────
    Rectangle {
        id: panel
        width: Math.min(640, parent.width - 48)
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Math.max(60, (parent.height - panel.height) / 3)

        height: Math.min(headerArea.height + listArea.implicitHeight, 520)

        color: root._bgColor
        // Q4.01 pixel-perfect: дизайн Card.borderRadius: 10.
        radius: 10
        border.color: root._borderColor
        border.width: 1
        clip: true

        opacity: root.active ? 1.0 : 0.0
        scale:   root.active ? 1.0 : 0.95

        Behavior on opacity { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
        Behavior on scale   { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }

        layer.enabled: true
        layer.effect: null  // Shadow via drop shadow would go here if MultiEffect available

        // ITER1 #24: set z:-1 so this background absorber does not intercept
        // clicks on child items (list rows, text fields) that sit at z:0+.
        MouseArea { anchors.fill: parent; z: -1; onClicked: {} }

        // ── Header ────────────────────────────────────────────────────────────
        Item {
            id: headerArea
            width: parent.width
            height: 52

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: root._borderColor
            }

            RowLayout {
                anchors {
                    fill: parent
                    leftMargin: 16
                    rightMargin: 12
                }
                spacing: 10

                // Q4.01 (redesign): "⌕" unicode → stroke search IconSvg
                IconSvg {
                    name: "search"
                    size: 16
                    color: root._subColor
                    Layout.alignment: Qt.AlignVCenter
                }

                TextField {
                    id: cmdInput
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    placeholderText: qsTr("Введите команду…")
                    font.pixelSize: FontSize.body
                    font.weight: Font.DemiBold
                    color: root._textColor
                    background: Item {}
                    leftPadding: 0
                    rightPadding: 0
                    selectByMouse: true

                    onTextChanged: root._rebuildList(text)

                    Keys.onEscapePressed: root.active = false
                    Keys.onReturnPressed: root._activateCommand(root._selectedIndex)
                    Keys.onEnterPressed:  root._activateCommand(root._selectedIndex)

                    Keys.onUpPressed: {
                        if (root._selectedIndex > 0) {
                            root._selectedIndex--
                            commandList.positionViewAtIndex(root._selectedIndex, ListView.Contain)
                        }
                    }
                    Keys.onDownPressed: {
                        if (root._selectedIndex < root._filteredCommands.length - 1) {
                            root._selectedIndex++
                            commandList.positionViewAtIndex(root._selectedIndex, ListView.Contain)
                        }
                    }
                }

                // Keyboard hint badge [Esc]
                Rectangle {
                    width: escLabel.implicitWidth + 12
                    height: 22
                    radius: Radius.sm
                    color: root._itemHover
                    Layout.alignment: Qt.AlignVCenter

                    Text {
                        id: escLabel
                        anchors.centerIn: parent
                        text: "Esc"
                        font.pixelSize: FontSize.caption
                        color: root._subColor
                    }
                }
            }
        }

        // ── Body ─────────────────────────────────────────────────────────────
        Item {
            id: listArea
            anchors {
                top:    headerArea.bottom
                left:   parent.left
                right:  parent.right
                bottom: parent.bottom
            }
            implicitHeight: Math.min(commandList.contentHeight, 468)

            ListView {
                id: commandList
                anchors.fill: parent
                clip: true
                interactive: true
                boundsBehavior: Flickable.StopAtBounds
                model: root._filteredCommands

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Item {
                    required property var modelData
                    required property int index

                    width: commandList.width

                    // Show category header when category changes (or index == 0)
                    property bool _showCat: {
                        if (index === 0) return true
                        var prev = root._filteredCommands[index - 1]
                        return prev ? prev.category !== modelData.category : true
                    }

                    height: (_showCat ? 28 : 0) + 40

                    // ── Category label ─────────────────────────────────────
                    Text {
                        id: catLabel
                        visible: parent._showCat
                        anchors { top: parent.top; left: parent.left; right: parent.right }
                        height: 28
                        leftPadding: Spacing.md
                        verticalAlignment: Text.AlignVCenter

                        // Translate category keys to Russian display strings
                        text: {
                            var map = {
                                "Recent":        qsTr("НЕДАВНИЕ"),
                                "Navigation":    qsTr("НАВИГАЦИЯ"),
                                "Projects":      qsTr("ПРОЕКТЫ"),
                                "Actions":       qsTr("ДЕЙСТВИЯ"),
                                "Selected task": qsTr("ВЫБРАННАЯ ЗАДАЧА"),
                                "Themes":        qsTr("ТЕМЫ"),
                                "Other":         qsTr("ДРУГОЕ")
                            }
                            return map[modelData.category] || modelData.category.toUpperCase()
                        }
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.8
                        color: root._catColor
                    }

                    // ── Command row ────────────────────────────────────────
                    Rectangle {
                        id: cmdRow
                        anchors {
                            top:         catLabel.visible ? catLabel.bottom : parent.top
                            left:        parent.left
                            right:       parent.right
                            leftMargin:  6
                            rightMargin: 6
                        }
                        height: 40
                        radius: Radius.md

                        color: (root._selectedIndex === index || rowHover.containsMouse)
                               ? root._selColor : "transparent"

                        Behavior on color { ColorAnimation { duration: 80 } }

                        RowLayout {
                            anchors {
                                fill:        parent
                                leftMargin:  10
                                rightMargin: 10
                            }
                            spacing: 10

                            // Icon — Q4.01 (redesign): для команд с
                            // iconName рендерим IconSvg из stroke-пака,
                            // иначе откатываемся к Text (динамические
                            // проекты-точки "●" и темы "🎨").
                            Item {
                                Layout.alignment: Qt.AlignVCenter
                                Layout.preferredWidth: 22
                                Layout.preferredHeight: 22

                                IconSvg {
                                    visible: !!modelData.iconName
                                    anchors.centerIn: parent
                                    name: modelData.iconName || ""
                                    size: 16
                                    color: root._selectedIndex === index
                                           ? root._accent : root._subColor
                                }

                                Text {
                                    visible: !modelData.iconName
                                    anchors.centerIn: parent
                                    text: modelData.icon || "·"
                                    font.pixelSize: 15
                                    color: root._subColor
                                    horizontalAlignment: Text.AlignHCenter
                                }
                            }

                            // Label (fills remaining space)
                            Text {
                                text: modelData.label
                                font.pixelSize: FontSize.bodySmall
                                color: root._selectedIndex === index
                                       ? root._accent : root._textColor
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter

                                Behavior on color { ColorAnimation { duration: 80 } }
                            }

                            // Right-aligned shortcut hint  e.g. [Ctrl+T]
                            Rectangle {
                                visible: modelData.shortcut && modelData.shortcut !== ""
                                width: shortcutText.implicitWidth + 10
                                height: 20
                                radius: 3
                                color: root._itemHover
                                Layout.alignment: Qt.AlignVCenter

                                Text {
                                    id: shortcutText
                                    anchors.centerIn: parent
                                    text: modelData.shortcut || ""
                                    font.pixelSize: FontSize.caption
                                    color: root._hintColor
                                }
                            }
                        }

                        MouseArea {
                            id: rowHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onEntered: root._selectedIndex = index
                            onClicked: root._activateCommand(index)
                        }
                    }
                } // delegate

                // Empty state
                Item {
                    anchors.fill: parent
                    visible: root._filteredCommands.length === 0

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("Команды не найдены")
                        font.pixelSize: FontSize.bodySmall
                        color: root._subColor
                    }
                }
            } // ListView
        } // listArea
    } // panel
}
