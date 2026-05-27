import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

// Rectangle (not Item) — same Qt strict QML rule as Board/Calendar: `color`
// on a plain Item makes the whole type unavailable.
Rectangle {
    id: root
    anchors.fill: parent

    // ── Design constants ──────────────────────────────────────────────────────
    readonly property color accentRed:     ThemeManager.accentColor
    readonly property bool  isDarkTheme:    ThemeManager.isDark
    readonly property color bgColor:        ThemeManager.backgroundColor
    readonly property color contentColor:   ThemeManager.surfaceColor
    readonly property color borderColor:    ThemeManager.borderColor
    readonly property color textPrimary:    ThemeManager.textColor
    readonly property color textSecondary:  ThemeManager.secondaryTextColor

    // ── View properties ───────────────────────────────────────────────────────
    property string projectId:  sidebarViewModel.currentItemId
    property var    project:    projectViewModel.projectById(projectId)
    property string viewStyle:  project ? project.viewStyle : "list"

    color: bgColor

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    Component.onCompleted: {
        taskViewModel.filterMode       = TaskViewModel.Project
        taskViewModel.currentProjectId = projectId
    }
    onProjectIdChanged: {
        taskViewModel.currentProjectId = projectId
    }

    // ── Helper ────────────────────────────────────────────────────────────────
    function setView(style) {
        if (project) projectViewModel.setViewStyle(projectId, style)
    }

    // ── Weekly goal helpers ───────────────────────────────────────────────────
    function _completedThisWeek() {
        if (!project || !projectId) return 0
        const now    = new Date()
        const day    = now.getDay()           // 0=Sun..6=Sat
        const dayMo  = (day + 6) % 7         // 0=Mon..6=Sun
        const monday = new Date(now)
        monday.setDate(now.getDate() - dayMo)
        monday.setHours(0, 0, 0, 0)
        const isoMonday = monday.toISOString().substring(0, 10)

        const tasks = dataStore.tasksForProject(projectId)
        let done = 0
        for (let i = 0; i < tasks.length; i++) {
            const t = tasks[i]
            if (t && t.isCompleted && t.completedAt) {
                const completedDate = String(t.completedAt).substring(0, 10)
                if (completedDate >= isoMonday) done++
            }
        }
        return done
    }

    // Q3.24: debounced — _completedThisWeek() is O(project tasks); firing it on
    // every tasksChanged (even for unrelated tasks in other projects) was wasteful.
    // 50 ms debounce coalesces rapid edits into a single recompute.
    property int _kick: 0
    Timer {
        id: weeklyGoalDebounce
        interval: 50
        repeat:   false
        onTriggered: root._kick = (root._kick + 1) % 1000
    }
    Connections {
        target: dataStore
        function onTasksChanged() { weeklyGoalDebounce.restart() }
    }

    // ── Layout ────────────────────────────────────────────────────────────────
    // Right-pane editor width — animated so the main column shrinks/grows
    // smoothly when a task is opened/closed.
    readonly property int taskEditorPaneWidth: 400
    readonly property int _editorPaneCurrentWidth:
        taskEditorLoader.active ? taskEditorPaneWidth : 0

    // B-6.42 — section Menu declared once at view root, opened with popup(sectionId).
    // This prevents stale menu state when ListView recycles delegates.
    property string _activeSectionId:   ""
    property string _activeSectionName: ""

    // ── Section reorder helpers ───────────────────────────────────────────────
    // Returns visible (non-archived, non-deleted) sections for the current
    // project sorted by their `order` value. Used by the Up/Down menu items.
    function _siblingSections() {
        const raw = dataStore.sectionsForProject(root.projectId) || []
        // sectionsForProject already excludes deleted + archived; sort by order.
        return raw.slice().sort(function(a, b) { return a.order - b.order })
    }

    // Swap the `order` field of the active section with its neighbour.
    // direction: -1 = move up (swap with predecessor), +1 = move down.
    function _moveSection(sectionId, direction) {
        const siblings = root._siblingSections()
        const idx = siblings.findIndex(function(s) { return s.id === sectionId })
        if (idx < 0) return
        const swapIdx = idx + direction
        if (swapIdx < 0 || swapIdx >= siblings.length) return
        const a = siblings[idx]
        const b = siblings[swapIdx]
        const aOrder = a.order
        const bOrder = b.order
        // Route through DataStore so each side gets a per-field _meta.order
        // stamp — a swap that only mutates the model leaves the merger blind
        // to the change after sync.
        dataStore.setSectionOrder(a.id, bOrder)
        dataStore.setSectionOrder(b.id, aOrder)
    }

    Menu {
        id: globalSectionMenu

        // ── Reorder items ─────────────────────────────────────────────────────
        MenuItem {
            text: qsTr("Переместить вверх")
            enabled: {
                const siblings = root._siblingSections()
                const idx = siblings.findIndex(function(s) { return s.id === root._activeSectionId })
                return idx > 0
            }
            onTriggered: root._moveSection(root._activeSectionId, -1)
        }
        MenuItem {
            text: qsTr("Переместить вниз")
            enabled: {
                const siblings = root._siblingSections()
                const idx = siblings.findIndex(function(s) { return s.id === root._activeSectionId })
                return idx >= 0 && idx < siblings.length - 1
            }
            onTriggered: root._moveSection(root._activeSectionId, +1)
        }
        MenuSeparator {}

        MenuItem {
            text: qsTr("Переименовать раздел…")
            onTriggered: {
                renameSectionDialog.sectionId   = root._activeSectionId
                renameSectionDialog.currentName = root._activeSectionName
                renameSectionDialog.open()
            }
        }
        MenuItem {
            text: qsTr("Добавить раздел ниже…")
            onTriggered: addSectionDialog.open()
        }
        MenuSeparator {}

        // ── Hide section ──────────────────────────────────────────────────────
        // Sets isArchived = true so the section disappears from the list view.
        // TODO: add a "Показать скрытые разделы" toggle to un-hide sections.
        MenuItem {
            text: qsTr("Скрыть раздел")
            onTriggered: {
                dataStore.setSectionArchived(root._activeSectionId, true)
            }
        }
        MenuSeparator {}

        MenuItem {
            text: qsTr("Удалить раздел")
            Material.foreground: root.accentRed
            onTriggered: {
                deleteSectionDialog.sectionId   = root._activeSectionId
                deleteSectionDialog.sectionName = root._activeSectionName
                deleteSectionDialog.open()
            }
        }
    }

    ColumnLayout {
        anchors.left:   parent.left
        anchors.top:    parent.top
        anchors.bottom: parent.bottom
        anchors.right:  parent.right
        anchors.rightMargin: root._editorPaneCurrentWidth
        spacing: 0

        Behavior on anchors.rightMargin {
            NumberAnimation { duration: Duration.normal; easing.type: Easing.InOutQuad }
        }

        // ── Header ────────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 56
            // Q4.01 pixel-perfect: дизайн (view-project.jsx стр. 103-108) не
            // ставит явный bg на project header — он inherit от родителя.
            // Раньше мы рисовали surfaceColor (contentColor) — header был
            // светлее task list area. Теперь bg → всё на одном тоне, разделено
            // только borderBottom.
            color: bgColor

            RowLayout {
                anchors.fill:        parent
                anchors.leftMargin:  Spacing.lg
                // Q4.01 pixel-perfect: дизайн (view-project.jsx стр. 105)
                // — padding 16×24, симметричный L/R. Раньше rightMargin
                // был Spacing.sm = 8, более кнопок к правому краю.
                anchors.rightMargin: Spacing.lg
                spacing: 12

                // Project icon: brand mark for OpenTodo (no explicit icon),
                // stroke SVG (if icon starts with "projects/"), emoji
                // (single-character icon field), or coloured dot fallback.
                Item {
                    width: 22
                    height: 22
                    Layout.alignment: Qt.AlignVCenter

                    // Q4.01 iter-7: auto-detect стрелочного формата иконки.
                    // ProjectIconPicker сохраняет выбранную иконку как
                    // "projects/<glyph>"; SidebarContent умеет это резолвить
                    // (см. _effectiveIconName там), а ProjectView header — нет,
                    // поэтому путь к SVG рендерился как голый Text-литерал
                    // ("projects/OpenTodo" поверх названия). Тот же auto-detect
                    // переносится сюда: префикс "projects/" → IconSvg, иначе
                    // одиночный символ → Text-emoji.
                    readonly property bool _isStrokeIcon:
                        project && project.icon
                        && project.icon.indexOf("projects/") === 0

                    // Q4.01 pixel-perfect: бренд-маркер для проекта OpenTodo.
                    // Когда у проекта нет своей иконки и имя = "OpenTodo" —
                    // рендерим красный круг с белой галочкой (см. дизайн
                    // .design-temp/opentodo/project/icons.jsx → OpenTodoMark).
                    // Используем прямо Image (без IconSvg/MultiEffect),
                    // чтобы галочка осталась белой, а круг — фирменно-красным.
                    Image {
                        visible: project && project.icon === "" && project.name === "OpenTodo"
                        source: "qrc:/icons/resources/icons/ui/opentodo-mark.svg"
                        anchors.fill: parent
                        sourceSize.width: 44
                        sourceSize.height: 44
                        smooth: true
                        mipmap: false
                    }

                    // Stroke-SVG: "projects/briefcase" и подобные пути из
                    // ProjectIconPicker. Цвет = project.color, чтобы иконка
                    // визуально читалась как маркер проекта в шапке.
                    // Q4.01 pixel-perfect: дизайн (view-project.jsx:109) рендерит
                    // OpenTodoMark size 22 в 22×22 контейнере — заполняет всю
                    // площадь. Stroke icon 20 даёт минимальный 1px margin и при
                    // этом визуально весит так же, как brand mark.
                    IconSvg {
                        anchors.centerIn: parent
                        visible: parent._isStrokeIcon
                        name: project ? project.icon : ""
                        size: 20
                        color: project && project.color ? project.color : ThemeManager.textColor
                    }

                    // Emoji-иконка (символьная, без "projects/" префикса) —
                    // legacy путь, сохраняем чтобы старые проекты не сломались.
                    Text {
                        anchors.centerIn: parent
                        visible: project && project.icon !== "" && !parent._isStrokeIcon
                        text: project ? project.icon : ""
                        font.pixelSize: 18
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        visible: project && project.icon === "" && project.name !== "OpenTodo"
                        width: 10
                        height: 10
                        radius: 5
                        color: project ? project.color : "#808080"
                    }
                }

                // Project name
                Label {
                    text:            project ? project.name : ""
                    font.pixelSize:  FontSize.viewHeader
                    font.weight:     Font.DemiBold
                    color:           textPrimary
                    elide:           Text.ElideRight
                    Layout.fillWidth: true
                }

                // ── Weekly goal progress pill ─────────────────────────────────
                Rectangle {
                    visible: project && project.weeklyGoal > 0
                    Layout.preferredHeight: 26
                    Layout.preferredWidth: 120
                    Layout.alignment: Qt.AlignVCenter
                    radius: 13
                    color: Qt.rgba(ThemeManager.textColor.r,
                                   ThemeManager.textColor.g,
                                   ThemeManager.textColor.b, 0.08)

                    readonly property int _completedNow: { root._kick; return root._completedThisWeek() }
                    readonly property bool _achieved: _completedNow >= (project ? project.weeklyGoal : 1)

                    // Fill bar
                    Rectangle {
                        anchors.left:    parent.left
                        anchors.top:     parent.top
                        anchors.bottom:  parent.bottom
                        anchors.margins: 2
                        width: project && project.weeklyGoal > 0
                            ? Math.min(parent.width - 4,
                                       (parent.width - 4) * parent._completedNow / project.weeklyGoal)
                            : 0
                        radius: parent.radius - 2
                        color: parent._achieved ? ThemeManager.successColor : ThemeManager.accentColor
                        Behavior on width { NumberAnimation { duration: 300 } }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("%1/%2 за неделю").arg(parent._completedNow).arg(project ? project.weeklyGoal : 0)
                        font.pixelSize: FontSize.caption
                        color: parent._completedNow > 0 ? ThemeManager.accentForeground : ThemeManager.textColor
                        z: 1
                    }

                    ToolTip.visible: goalHover.containsMouse
                    ToolTip.text: parent._achieved
                        ? qsTr("Цель на эту неделю выполнена!")
                        : qsTr("Осталось: %1").arg((project ? project.weeklyGoal : 0) - parent._completedNow)

                    MouseArea {
                        id: goalHover
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                    }
                }

                // View-style toggles — Q4.01 pixel-perfect (view-project.jsx
                // стр. 7-17 PillIconBtn + стр. 112-114): pill-shape кнопка
                // с rounded background — accent-fill в active, transparent
                // иначе. Раньше использовался дефолтный ToolButton без bg,
                // поэтому active-состояние было видно только по цвету иконки,
                // что не соответствует дизайн-канвасу (там явный фон-чип).
                //   radius 6, padding 5×10, icon size 14.
                Repeater {
                    model: [
                        { style: "list",     iconName: "list",     tip: qsTr("Список") },
                        { style: "board",    iconName: "board",    tip: qsTr("Доска") },
                        { style: "calendar", iconName: "calendar", tip: qsTr("Календарь") }
                    ]
                    delegate: ToolButton {
                        required property var modelData
                        readonly property bool _active: viewStyle === modelData.style

                        checked:   _active
                        onClicked: root.setView(modelData.style)
                        ToolTip.text:    modelData.tip
                        ToolTip.visible: hovered

                        // Pill background — accent fill when active, transparent
                        // otherwise. Padding 5 px vertical / 10 px horizontal
                        // matches PillIconBtn from .design-temp.
                        background: Rectangle {
                            radius: Radius.md
                            color:  parent._active ? accentRed : "transparent"
                            implicitWidth:  28
                            implicitHeight: 24
                        }

                        contentItem: IconSvg {
                            anchors.centerIn: parent
                            name:  modelData.iconName
                            size:  14
                            // Active = white-on-accent (PillIconBtn '#fff'),
                            // inactive = iconColor per design.
                            color: parent._active ? ThemeManager.accentForeground : ThemeManager.iconColor
                        }
                    }
                }

                // Separator
                Rectangle {
                    width:  1
                    height: 20
                    color:  borderColor
                }

                // Add-section button (visible only in list view).
                // Q4.01 pixel-perfect (view-project.jsx стр. 115-121):
                // PlusIcon size 12 + "Раздел" label, оба в subText цвете
                // (НЕ accent). Раньше использовался accent — это привлекало
                // лишнее внимание к не-первичному действию рядом с тоггами.
                ToolButton {
                    id:      addSectionBtn
                    visible: viewStyle === "list"
                    flat:    true
                    padding: 6
                    onClicked: addSectionDialog.open()

                    contentItem: Row {
                        spacing: Spacing.xs
                        anchors.centerIn: parent
                        IconSvg {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "plus"
                            size: 12
                            color: textSecondary
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Раздел")
                            color: textSecondary
                            font.pixelSize: FontSize.bodySmall
                        }
                    }

                    ToolTip.text: qsTr("Добавить раздел в проект")
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                }

                // Q3.16: Edit-project shortcut. Previously "Редактировать
                // проект" was hidden inside the ⋯ menu, which made it
                // discoverable only after a click; users with custom colours,
                // notes or weekly goals to tweak had to open the menu every
                // time. Surfacing a button at toolbar level cuts that to one
                // click. The menu item below stays so keyboard-menu users
                // and "find via dropdown" muscle memory still work.
                //
                // Q4.01 pixel-perfect (view-project.jsx стр. 122): дизайн
                // ставит PaperclipIcon в этом месте, поэтому ✏-эмодзи
                // заменена на stroke paperclip. Семантика "редактировать"
                // не идеально совпадает с paperclip-глифом, но это место в
                // макете зарезервировано именно под paperclip, и весь топбар
                // выглядит однородно (stroke icons size 14, без emoji).
                // Q4.01 pixel-perfect: icon button константы из дизайна
                // (.design-temp/.../app-shell.jsx iconBtn — 28×28 box +
                // borderRadius 6, icon size 14 для toolbar-уровня).
                // Material default ToolButton ~36-40px box ломал ряд.
                ToolButton {
                    id:      editProjectBtn
                    onClicked: editDialog.open()
                    Material.foreground: textSecondary
                    implicitWidth: 28
                    implicitHeight: 28
                    padding: 0

                    contentItem: IconSvg {
                        anchors.centerIn: parent
                        name: "paperclip"
                        size: 14
                        color: textSecondary
                    }

                    ToolTip.text: qsTr("Редактировать проект")
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                }

                // More-options menu button — task 17cea65b iter-3: было "⋯".
                ToolButton {
                    id:      moreBtn
                    onClicked: projectMenu.open()
                    implicitWidth: 28
                    implicitHeight: 28
                    padding: 0
                    contentItem: IconSvg {
                        anchors.centerIn: parent
                        name: "more-horizontal"
                        size: 14
                        color: textSecondary
                    }
                }
            }

            // Bottom border
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left:   parent.left
                anchors.right:  parent.right
                height: 1
                color:  borderColor
            }

            // ── Project context menu ─────────────────────────────────────────
            Menu {
                id: projectMenu
                y: moreBtn.height

                MenuItem {
                    text: qsTr("Редактировать проект")
                    onTriggered: editDialog.open()
                }
                MenuItem {
                    text: qsTr("Добавить раздел")
                    onTriggered: addSectionDialog.open()
                }
                MenuItem {
                    text: (project && project.isFavorite)
                          ? qsTr("Убрать из избранного")
                          : qsTr("В избранное")
                    onTriggered: projectViewModel.setFavorite(projectId,
                                     !(project && project.isFavorite))
                }
                MenuSeparator {}
                MenuItem {
                    text: qsTr("Архивировать проект")
                    onTriggered: projectViewModel.archiveProject(projectId)
                }
                MenuItem {
                    text: qsTr("Дублировать как шаблон")
                    enabled: projectId !== ""
                    onTriggered: {
                        // Wave 3 (F-6.39): copies all non-deleted tasks from
                        // the current project into a new "Шаблон: <name>" project.
                        // C++ side returns the new project id; navigate via
                        // SidebarVM.navigateTo so taskViewModel updates too.
                        const newId = taskViewModel.duplicateProjectAsTemplate(projectId)
                        if (newId && newId !== "")
                            sidebarViewModel.navigateTo("project", newId)
                    }
                }
                MenuItem {
                    text: qsTr("Экспорт проекта (JSON)")
                    enabled: projectId !== ""
                    onTriggered: {
                        const path = taskExporter.exportProjectToJson(projectId, "")
                        const w = root.Window.window
                        if (w && typeof w.showToast === "function") {
                            if (path && path !== "") w.showToast(qsTr("Экспортировано в %1").arg(path), "success")
                            else                     w.showToast(qsTr("Не удалось экспортировать"), "error")
                        }
                    }
                }
                MenuItem {
                    text: qsTr("Поделиться через Google Drive…")
                    enabled: projectId !== "" && shareManager && shareManager.isAvailable()
                    onTriggered: {
                        if (!shareManager || !shareManager.isAvailable()) {
                            const w = root.Window.window
                            if (w && typeof w.showToast === "function")
                                w.showToast(qsTr("Подключите Google Drive в настройках"), "info")
                            return
                        }
                        const projectName = root.project ? root.project.name : qsTr("Проект")
                        shareLinkDialog.startBusy(qsTr("Проект «%1»").arg(projectName))
                        shareManager.shareProject(root.projectId)
                    }
                }
                MenuSeparator {}
                MenuItem {
                    text: qsTr("Удалить все завершённые")
                    enabled: projectId !== ""
                    onTriggered: removeCompletedConfirm.open()
                }
                MenuSeparator {}
                MenuItem {
                    text: qsTr("Удалить проект")
                    Material.foreground: accentRed
                    onTriggered: deleteConfirmDialog.open()
                }
            }
        }

        // ── Notes card — visible only when the project has notes ─────────────
        // Q4.01 (iter-7): полностью переписан под дизайн-канвас
        // (.design-temp/.../view-project.jsx стр. 126-140). Дизайн использует
        // panel-teaser — одну строку с иконкой, заголовком/подзаголовком и
        // chevron-down справа. Тело самой заметки в макете НЕ показывается
        // в шапке проекта; полный текст открывается отдельным разворотом
        // под teaser (по клику на chevron). Раньше тело рендерилось inline,
        // прижатое к левому краю карточки, что ломало пиксель-в-пиксель
        // (3 разных проблемы: тело без отступа, неправильная высота,
        // overlap с quick-add при анимации). Новая структура:
        //   header row (всегда видна, 36 px)
        //   ↓ chevron click
        //   expanded body (Flickable, отдельный padded блок ниже header)
        //
        // Q3.29: third rewrite — the Q3.28 implicit-height pipeline still
        // produced overlap with the «Добавить задачу» bar in list mode.
        // Symptom: the card's drawn area extended past its allocated layout
        // box, covering the top of the quick-add row underneath.
        //
        // Root cause: with `clip:true` plus a Column anchored top/left/right
        // (no bottom anchor) plus `Layout.preferredHeight` bound to that
        // Column's implicitHeight, Qt 6's layout engine occasionally settled
        // on a Rectangle height that was *smaller* than the Column's actual
        // drawn extent. That happened reliably when the project view was
        // first opened (Column laid out before Rectangle width was final) or
        // when the right-pane editor's width animation changed the available
        // width mid-flight. clip:true masked the inside but the OUTER layout
        // still allocated the stale (smaller) preferredHeight, so the next
        // Layout child (the Loader → list view → quick-add bar) was painted
        // a few pixels too high — visually under the notes card border.
        //
        // The bulletproof fix is to stop deriving Rectangle.height from a
        // dynamic implicitHeight chain at all. We give the card a fixed
        // Layout.preferredHeight (compact: 56 px header-only, expanded:
        // 220 px header + scrollable body) and put the long-text body
        // inside a Flickable. No matter how long the notes are, the
        // Rectangle's drawn height equals its allocated layout height —
        // no overlap is geometrically possible.
        Rectangle {
            id: notesCard
            Layout.fillWidth: true
            Layout.leftMargin: Spacing.md
            Layout.rightMargin: Spacing.md
            Layout.topMargin: Spacing.sm
            // Task bd2e7bde: Q3.29 left two latent problems.
            //   1. Collapsed height was 44 px, but the inner ColumnLayout
            //      has 16 px top + 16 px bottom margins and a 24-px header,
            //      i.e. it needs at least 56 px. Children overflowed the
            //      Rectangle's clip area (visually leaking into the
            //      "Добавить задачу" bar below).
            //   2. `Behavior on Layout.preferredHeight` animated only the
            //      preferredHeight — Layout.maximumHeight snapped instantly
            //      to the new target. During the 120 ms animation,
            //      preferredHeight ≠ maximumHeight, and the layout engine
            //      briefly resolved to the (larger) maximum, shifting the
            //      quick-add bar mid-frame. Set the same Behavior on
            //      minimumHeight + maximumHeight so all three move together.
            //
            // 56 px = 24 (header) + 16+16 (Spacing.md margins). Anything
            // smaller forces the header to be clipped.
            //
            // Iter-night-25 (user task 8107255f "Размер заметок должен
            // определяться их наполненеим"): expanded height was a fixed
            // 220 px regardless of content length, so a 2-line note left a
            // 180-px empty band below it while a 100-line note still got
            // truncated to the same 220-px scrolling window. Now compute
            // the expanded height from `notesBodyText.implicitHeight`
            // (Text re-computes this on every width or content change),
            // clamped between the collapsed minimum and an upper cap so a
            // pathological multi-page note doesn't push the task list off
            // screen — beyond the cap the Flickable still scrolls.
            // 400 px = ~18 lines at FontSize.body, a comfortable maximum
            // before the notes card starts dominating the project view.
            readonly property int _collapsedHeight: 56
            readonly property int _expandedHeightCap: 400
            // Iter-night-42 (task bd2e7bde re-open): _expandedHeight USED
            // to be a readonly binding directly dependent on
            // notesBodyText.implicitHeight. During initial render and
            // window-resize the Text widget's implicitHeight settles over
            // several frames as it re-flows under the changing width —
            // each implicitHeight tick re-triggered the Layout.preferred/
            // minimum/maximumHeight Behavior animations (all bound to
            // _targetHeight), which chased a moving target. The animation
            // lag let the Loader below the card briefly mis-align with
            // the card's drawn extent and the "Добавить задачу" bar
            // visually rode over the card's bottom border for several
            // frames before the layout settled.
            //
            // Fix: coalesce implicitHeight transitions via a one-shot
            // 50 ms Timer. _expandedHeight becomes a mutable property
            // that's recomputed at discrete, stable moments — never
            // during the multi-frame layout settle. UX cost: a 50 ms
            // latency between editing notes and the card resizing —
            // imperceptible. Q3.29's anti-overlap invariant
            // (Layout.preferredHeight === Layout.minimumHeight ===
            // Layout.maximumHeight, all three animated by Behavior in
            // lockstep) is preserved unchanged.
            property int _expandedHeight: _collapsedHeight

            Timer {
                id: _heightRecomputeTimer
                interval: 50
                repeat:   false
                onTriggered: {
                    const naturalHeight = 24 + Spacing.xs
                        + notesBodyText.implicitHeight
                        + Spacing.md * 2
                    notesCard._expandedHeight = Math.max(
                        notesCard._collapsedHeight,
                        Math.min(naturalHeight,
                                 notesCard._expandedHeightCap))
                }
            }

            Connections {
                target: notesBodyText
                function onImplicitHeightChanged() {
                    _heightRecomputeTimer.restart()
                }
            }

            readonly property int _targetHeight:
                _expanded ? _expandedHeight : _collapsedHeight
            Layout.preferredHeight: _targetHeight
            Layout.minimumHeight:   _targetHeight
            Layout.maximumHeight:   _targetHeight
            visible: !!project && !!project.notes
            color: ThemeManager.surfaceColor
            border.color: ThemeManager.borderColor
            border.width: 1
            // Q4.01 pixel-perfect: дизайн note panel borderRadius 8 (`Card.borderRadius: 8`).
            radius: Radius.lg
            clip: true

            // Q4.01 (iter-7): дизайн-канвас рендерит note panel ВСЕГДА как
            // teaser-строку (icon + title + subtitle + chevron) без inline
            // тела. Раскрытие — только по клику на chevron. Старая логика
            // авто-разворачивала короткие заметки на стартовом рендере, что
            // ломало пиксель-перфект (видно тело "Тест заметок" сразу при
            // переходе в проект). Сейчас стартуем закрытыми — пользователь
            // открывает теасер один раз, если хочет почитать.
            property bool _expanded: false

            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
            }
            Behavior on Layout.minimumHeight {
                NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
            }
            Behavior on Layout.maximumHeight {
                NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
            }

            // Q4.01 pixel-perfect: дизайн note-panel — padding 12 14, gap 10.
            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin:    12
                anchors.bottomMargin: 12
                anchors.leftMargin:   14
                anchors.rightMargin:  14
                spacing: Spacing.xs

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Spacing.lg
                    spacing: 10  // Q4.01 pixel-perfect: gap icon-text 10

                    // Q4.01 pixel-perfect: icon 16, цвет accent (дизайн).
                    IconSvg {
                        name: "note"
                        size: 16
                        color: ThemeManager.accentColor
                        Layout.alignment: Qt.AlignVCenter
                    }
                    // Q4.01 pixel-perfect: title + subtitle stack
                    // (дизайн view-project.jsx стр. 127-141 — note panel).
                    ColumnLayout {
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 2

                        Label {
                            text: qsTr("Заметки")
                            // Q4.01 pixel-perfect: title fontSize 13 / weight Medium (500).
                            font.pixelSize: FontSize.bodySmall
                            font.weight: Font.Medium
                            color: ThemeManager.textColor
                        }
                        Label {
                            // Q4.01 pixel-perfect: subtitle fontSize 11 / secondary text.
                            text: qsTr("Описание проекта, ссылки, контекст")
                            font.pixelSize: FontSize.caption
                            color: ThemeManager.secondaryTextColor
                        }
                    }
                    Item { Layout.fillWidth: true }
                    ToolButton {
                        flat: true
                        Layout.preferredWidth: Spacing.lg
                        Layout.preferredHeight: Spacing.lg
                        padding: 0
                        ToolTip.text: notesCard._expanded
                                      ? qsTr("Свернуть заметки")
                                      : qsTr("Развернуть заметки")
                        ToolTip.visible: hovered
                        onClicked: notesCard._expanded = !notesCard._expanded
                        // Task 17cea65b iter-2: was unicode "▾"/"▸".
                        // Q4.01 pixel-perfect: chevron 14 (дизайн ChevronDownIcon size 14).
                        contentItem: IconSvg {
                            name: notesCard._expanded ? "chevron-down" : "chevron-right"
                            size: 14
                            color: ThemeManager.secondaryTextColor
                            anchors.centerIn: parent
                        }
                    }
                }

                // Scrollable body — the Text inside can be arbitrarily long;
                // Flickable handles the overflow without affecting the card's
                // outer height.
                Flickable {
                    id: notesFlick
                    Layout.fillWidth:  true
                    Layout.fillHeight: true
                    visible: notesCard._expanded
                    clip: true
                    contentWidth:  width
                    contentHeight: notesBodyText.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {
                        policy: notesFlick.contentHeight > notesFlick.height
                                ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                    }

                    Text {
                        id: notesBodyText
                        width: notesFlick.width
                        text: project ? project.notes : ""
                        textFormat: Text.MarkdownText
                        wrapMode: Text.WordWrap
                        color: ThemeManager.textColor
                        linkColor: ThemeManager.accentColor
                        onLinkActivated: function(url) { Qt.openUrlExternally(url) }
                    }
                }
            }
        }

        // ── Content area — switches between list / board / calendar ──────────
        Loader {
            Layout.fillWidth:  true
            Layout.fillHeight: true
            sourceComponent: {
                if (viewStyle === "board")    return boardComponent
                if (viewStyle === "calendar") return calendarComponent
                return listComponent
            }
        }
    }

    // ── List component ────────────────────────────────────────────────────────
    Component {
        id: listComponent

        Item {
            anchors.fill: parent

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Quick-add bar
                Rectangle {
                    Layout.fillWidth: true
                    // Task bd2e7bde (re-opened): the previous `height: 48`
                    // gave the Rectangle a fixed VISUAL height of 48px but
                    // did NOT tell the parent ColumnLayout — without a
                    // Layout.preferredHeight, the layout engine derived the
                    // child's allocation from implicitHeight, which traces
                    // back to the inner RowLayout's content height (TextField
                    // + small Rectangle = ~25px). So the Layout reserved
                    // ~25px for this bar, but the Rectangle DREW 48px. The
                    // ListView below was positioned at y=25 (next layout
                    // slot), overlapping the bottom 23px of the Quick-add
                    // bar — and, during the notes-card grow/shrink
                    // animation, the bar's drawn extent likewise rode over
                    // the notes card's allocated layout box from above
                    // because the parent's slot math used the wrong height.
                    //
                    // Layout.preferredHeight makes the ColumnLayout actually
                    // reserve 48px for this Rectangle. ListView slots down
                    // by 48px, no overlap geometrically possible.
                    Layout.preferredHeight: 48
                    // Q4.01 pixel-perfect: quick-add bar — view-project.jsx стр.
                    // 142-150 без явного bg. На bgColor родителя, выделен
                    // borderBottom (см. ниже).
                    color: bgColor

                    RowLayout {
                        anchors.fill:        parent
                        anchors.leftMargin:  Spacing.md
                        anchors.rightMargin: Spacing.md
                        spacing: Spacing.sm

                        Rectangle {
                            width: 18; height: 18; radius: 9
                            border.color: accentRed; border.width: 1.5
                            color: "transparent"
                        }

                        TextField {
                            id:               quickAddField
                            Layout.fillWidth: true
                            placeholderText:  qsTr("Добавить задачу…")
                            background:       Item {}
                            color:            textPrimary
                            font.pixelSize:   FontSize.body
                            onAccepted: {
                                if (text.trim() !== "") {
                                    // B-6.41 — addTask returns QString; return value not needed here
                                    taskViewModel.addTask(text.trim(),
                                                          root.projectId, 4, "", [])
                                    text = ""
                                }
                            }
                        }
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.left:   parent.left
                        anchors.right:  parent.right
                        height: 1
                        color:  borderColor
                    }
                }

                // Task list with section grouping (wrapped for empty state)
                Item {
                    Layout.fillWidth:  true
                    Layout.fillHeight: true

                    ListView {
                        id: taskListView
                        anchors.fill: parent
                        visible: taskViewModel.count > 0
                        model:  taskViewModel
                        clip:   true
                        spacing: 0

                        // Use sectionGroupKey instead of sectionId so the empty-section
                        // case routes through "__none__" — Qt's ListView skips section
                        // delegate creation when every visible item shares an empty
                        // section value (verified in tst_listview_section.qml).
                        section.property: "sectionGroupKey"
                        section.criteria: ViewSection.FullString
                        section.labelPositioning: ViewSection.InlineLabels
                        section.delegate: Rectangle {
                            id: sectionHeaderRect
                            required property string section
                            // Translate the sentinel back to "" so downstream code
                            // (drop targets, "+ task" buttons) keeps working with
                            // canonical sectionId semantics.
                            readonly property string sectionId:
                                section === "__none__" ? "" : section

                            // Q3.27: track the underlying Section model so the
                            // chevron + bound bindings (e.g. task delegate
                            // visibility) re-evaluate when isCollapsed flips.
                            readonly property var sectionModel:
                                sectionId === "" ? null
                                                 : dataStore.sectionById(sectionId)
                            readonly property bool isCollapsed:
                                !!sectionModel && sectionModel.isCollapsed

                            width:  ListView.view.width
                            implicitWidth: width
                            height: 36
                            implicitHeight: height
                            // Q4.01 pixel-perfect: дизайн (view-project.jsx
                            // SectionHeader, стр. 82-86) не ставит явный bg —
                            // header сидит на bgColor родителя, отделён только
                            // borderBottom. Раньше использовался hardcoded
                            // "#222"/"#f5f5f5", который давал section header
                            // светлее tasks (теперь tasks тоже на bgColor —
                            // единая палитра).
                            color:  sectionDrop.containsDrag
                                    ? Qt.rgba(root.accentRed.r,
                                              root.accentRed.g,
                                              root.accentRed.b, 0.18)
                                    : ThemeManager.backgroundColor

                            property string sectionName: {
                                if (sectionId === "") return qsTr("(Без раздела)")
                                var s = dataStore.sectionById(sectionId)
                                return s ? s.name : sectionId
                            }

                            RowLayout {
                                anchors.fill:        parent
                                anchors.leftMargin:  Spacing.md
                                anchors.rightMargin: Spacing.sm
                                spacing: 6

                                // Q3.27: collapse/expand chevron. Hidden for the
                                // synthetic "(Без раздела)" header — there's no
                                // Section model to toggle on.
                                // Q4.01 pixel-perfect: SectionHeader chevron
                                // (.design-temp/.../view-project.jsx line 87):
                                // ChevronRightIcon size 12, subText color.
                                ToolButton {
                                    visible:        sectionHeaderRect.sectionId !== ""
                                    flat:           true
                                    Layout.preferredWidth:  28
                                    Layout.preferredHeight: 28
                                    padding: 0
                                    ToolTip.text: sectionHeaderRect.isCollapsed
                                                      ? qsTr("Развернуть раздел")
                                                      : qsTr("Свернуть раздел")
                                    ToolTip.visible: hovered
                                    onClicked: {
                                        const s = sectionHeaderRect.sectionModel
                                        if (s) {
                                            dataStore.setSectionCollapsed(
                                                sectionHeaderRect.sectionId,
                                                !s.isCollapsed)
                                        }
                                    }
                                    // Task 17cea65b iter-2: was unicode "▸"/"▾".
                                    contentItem: IconSvg {
                                        name: sectionHeaderRect.isCollapsed ? "chevron-right" : "chevron-down"
                                        size: 12
                                        color: root.textSecondary
                                        anchors.centerIn: parent
                                    }
                                }

                                Text {
                                    text:           sectionHeaderRect.sectionName
                                    font.pixelSize: FontSize.small
                                    font.bold:      true
                                    color:          root.textSecondary
                                    elide:          Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                // Q4.01 pixel-perfect: SectionHeader +/⋯
                                // (.design-temp/.../view-project.jsx lines 96-97):
                                // PlusIcon/MoreIcon size 14 inside iconBtn 28×28.
                                ToolButton {
                                    Layout.preferredWidth:  28
                                    Layout.preferredHeight: 28
                                    padding: 0
                                    ToolTip.text: qsTr("Добавить задачу в раздел")
                                    ToolTip.visible: hovered
                                    onClicked: {
                                        addTaskToSectionDialog.sectionId = sectionHeaderRect.sectionId
                                        addTaskToSectionDialog.open()
                                    }
                                    // Task 17cea65b iter-3: было unicode "+".
                                    contentItem: IconSvg {
                                        anchors.centerIn: parent
                                        name: "plus"
                                        size: 14
                                        color: root.textSecondary
                                    }
                                }

                                // B-6.42 — ToolButton stores section context in root properties,
                                // then opens the single globalSectionMenu declared at root.
                                // This avoids the recycled-delegate / stale-Menu-state bug.
                                ToolButton {
                                    visible: sectionHeaderRect.sectionId !== ""
                                    Layout.preferredWidth:  28
                                    Layout.preferredHeight: 28
                                    padding: 0
                                    onClicked: {
                                        root._activeSectionId   = sectionHeaderRect.sectionId
                                        root._activeSectionName = sectionHeaderRect.sectionName
                                        globalSectionMenu.popup()
                                    }
                                    // Task 17cea65b iter-3: было unicode "⋯".
                                    contentItem: IconSvg {
                                        anchors.centerIn: parent
                                        name: "more-horizontal"
                                        size: 14
                                        color: root.textSecondary
                                    }
                                }
                            }

                            // Drop zone — drag a task onto a section header to
                            // move it into that section.
                            DropArea {
                                id: sectionDrop
                                anchors.fill: parent
                                keys: ["application/x-opentodo-task"]
                                onDropped: function(drop) {
                                    const tid = drop.getDataAsString("application/x-opentodo-task")
                                    if (tid) taskViewModel.moveTask(tid, root.projectId,
                                                                    sectionHeaderRect.sectionId)
                                    drop.accept(Qt.MoveAction)
                                }
                            }

                            Rectangle {
                                anchors.bottom: parent.bottom
                                anchors.left:   parent.left
                                anchors.right:  parent.right
                                height: 1
                                color:  root.borderColor
                            }
                        }

                        // Delegate uses the shared TaskItem.qml so behaviour
                        // matches Inbox/Today (priority circle click toggles complete,
                        // row click opens the right-pane editor, etc.).
                        delegate: Item {
                            id: delegateRoot
                            width:  taskListView.width
                            // Q3.27: collapse support. When the parent section's
                            // isCollapsed flips true, hide every task row in
                            // that section (height 0 + visible false). Tasks
                            // outside any section ("(Без раздела)") always
                            // show. Re-evaluates automatically because
                            // sectionModel is a QObject* and isCollapsed has a
                            // change signal.
                            readonly property var _sectionModel:
                                sectionId === "" ? null
                                                 : dataStore.sectionById(sectionId)
                            readonly property bool _sectionCollapsed:
                                !!_sectionModel && _sectionModel.isCollapsed
                            visible: !_sectionCollapsed
                            height: _sectionCollapsed ? 0
                                                       : projectTaskItem.implicitHeight

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
                                id: projectTaskItem
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
                                    const t = dataStore.taskById(taskId)
                                    if (t) taskViewModel.selectedTask = t
                                }
                            }

                            // `id` collides with QML's id-attribute syntax inside
                            // an object initialiser, so we bind it via Binding{}
                            // as a sibling (matches DesktopLayout's pattern).
                            Binding {
                                target: projectTaskItem
                                property: "id"
                                value: delegateRoot.id
                            }

                            // Bug 9 fix: per-row drop zones for in-list reorder
                            // and Todoist-style "drop on bottom half → make it a
                            // subtask" gesture. Mirrors the wiring in
                            // DesktopLayout.qml so behaviour is identical across
                            // the Inbox/Today/Upcoming and Project pages.
                            //
                            //   • top  ½ → reorder (drop ABOVE this row)
                            //   • bottom ½ → make the dropped task a SUBTASK
                            //                of this one
                            DropArea {
                                id: projectReorderDrop
                                anchors.left:   parent.left
                                anchors.right:  parent.right
                                anchors.top:    parent.top
                                height: parent.height / 2
                                keys: ["application/x-opentodo-task"]

                                onEntered: function(drag) {
                                    const draggedId = drag.getDataAsString("application/x-opentodo-task")
                                    projectInsertionLine.visible = (draggedId !== projectTaskItem.id)
                                    projectSubtaskHighlight.visible = false
                                }
                                onExited: function() {
                                    projectInsertionLine.visible = false
                                }
                                onDropped: function(drop) {
                                    const draggedId = drop.getDataAsString("application/x-opentodo-task")
                                    projectInsertionLine.visible = false
                                    if (draggedId === projectTaskItem.id) {
                                        drop.accept(Qt.MoveAction)
                                        return
                                    }
                                    taskViewModel.moveTaskOrder(draggedId, projectTaskItem.index)
                                    drop.accept(Qt.MoveAction)
                                }
                            }

                            DropArea {
                                id: projectSubtaskDrop
                                anchors.left:   parent.left
                                anchors.right:  parent.right
                                anchors.bottom: parent.bottom
                                height: parent.height / 2
                                keys: ["application/x-opentodo-task"]

                                onEntered: function(drag) {
                                    const draggedId = drag.getDataAsString("application/x-opentodo-task")
                                    projectSubtaskHighlight.visible = (draggedId !== projectTaskItem.id)
                                    projectInsertionLine.visible = false
                                }
                                onExited: function() {
                                    projectSubtaskHighlight.visible = false
                                }
                                onDropped: function(drop) {
                                    const draggedId = drop.getDataAsString("application/x-opentodo-task")
                                    projectSubtaskHighlight.visible = false
                                    if (draggedId === projectTaskItem.id || !taskViewModel.convertToSubtask) {
                                        drop.accept(Qt.MoveAction)
                                        return
                                    }
                                    taskViewModel.convertToSubtask(draggedId, projectTaskItem.id)
                                    drop.accept(Qt.MoveAction)
                                }
                            }

                            // Visual indicators — thin accent line for reorder,
                            // tinted overlay for subtask. Same look as the
                            // DesktopLayout list so users recognise the gesture.
                            Rectangle {
                                id: projectInsertionLine
                                anchors.top:    parent.top
                                anchors.left:   parent.left
                                anchors.right:  parent.right
                                height: 2
                                color: ThemeManager.accentColor
                                visible: false
                                z: 10
                            }
                            Rectangle {
                                id: projectSubtaskHighlight
                                anchors.fill: parent
                                anchors.leftMargin: Spacing.md + 24
                                color: Qt.rgba(ThemeManager.accentColor.r,
                                               ThemeManager.accentColor.g,
                                               ThemeManager.accentColor.b, 0.18)
                                border.color: ThemeManager.accentColor
                                border.width: 1
                                radius: Radius.sm
                                visible: false
                                z: 9
                            }
                        }

                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    }

                    // Empty state — Q4.01 (redesign): эмодзи "📋" заменён на
                    // stroke-иконку "list" в EmptyState.iconName.
                    EmptyState {
                        anchors.fill: parent
                        visible: taskViewModel.count === 0
                        iconName: "list"
                        title: qsTr("Этот проект пуст")
                        message: qsTr("Начни с одной маленькой задачи. Большие достижения складываются из маленьких шагов.")
                        actionLabel: qsTr("Добавить первую задачу")
                        onActionClicked: quickAddField.forceActiveFocus()
                    }
                }

                // Add-section button
                Rectangle {
                    Layout.fillWidth: true
                    height: 40
                    color:  "transparent"

                    RowLayout {
                        anchors.fill:       parent
                        anchors.leftMargin: Spacing.md
                        spacing: 6

                        // Q4.01 pixel-perfect: PlusIcon size 14 — единый размер
                        // для всех inline quick-add плюсов в дизайне
                        // (view-others.jsx:13, view-project.jsx:148).
                        IconSvg {
                            name: "plus"
                            size: 14
                            color: root.accentRed
                        }
                        Text {
                            text:           qsTr("Добавить раздел")
                            color:          root.textSecondary
                            font.pixelSize: FontSize.body
                        }
                        Item { Layout.fillWidth: true }
                    }

                    MouseArea {
                        anchors.fill:  parent
                        cursorShape:   Qt.PointingHandCursor
                        onClicked:     addSectionDialog.open()
                    }
                }
            }
        }
    }

    // ── Board component ───────────────────────────────────────────────────────
    // Q3.27: hook BoardView's taskClicked signal so that clicking a card in
    // board mode opens the right-pane TaskEditor — matching the list-mode
    // behaviour. Previously the signal was emitted into a void and clicks
    // appeared to do nothing.
    Component {
        id: boardComponent
        BoardView {
            projectId: root.projectId
            onTaskClicked: function(taskId) {
                const t = dataStore.taskById(taskId)
                if (t) taskViewModel.selectedTask = t
            }
        }
    }

    // ── Calendar component ────────────────────────────────────────────────────
    Component {
        id: calendarComponent
        CalendarView {}
    }

    // ── Edit project dialog ───────────────────────────────────────────────────
    Dialog {
        id:      editDialog
        title:   qsTr("Редактировать проект")
        modal:   true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel

        property string editName:        project ? project.name        : ""
        property string editColor:       project ? project.color       : "#808080"
        property string editDescription: project ? project.description : ""
        property string editIcon:        project ? project.icon        : ""
        property int    editWeeklyGoal:  project ? project.weeklyGoal  : 0
        property string editNotes:       project ? project.notes       : ""
        property string editLinkedRepoPath: project ? project.linkedRepoPath : ""
        // Per-project default priority (1=P1, 2=P2, 3=P3, 4=P4/«no priority»).
        // Stored in QSettings, NOT on the Project model — see ProjectViewModel.
        property int    editDefaultPriority: 4
        // Per-project default labels (list of label IDs) — also QSettings only.
        property var    editDefaultLabels: []

        onOpened: {
            editName        = project ? project.name        : ""
            editColor       = project ? project.color       : "#808080"
            editDescription = project ? project.description : ""
            editIcon        = project ? project.icon        : ""
            editWeeklyGoal  = project ? project.weeklyGoal  : 0
            editNotes       = project ? project.notes       : ""
            editLinkedRepoPath = project ? project.linkedRepoPath : ""
            editDefaultPriority = projectId !== ""
                ? projectViewModel.defaultPriorityForProject(projectId)
                : 4
            editDefaultLabels = projectId !== ""
                ? projectViewModel.defaultLabelsForProject(projectId)
                : []
            nameField.text        = editName
            descriptionField.text = editDescription
            iconField.text        = editIcon
            weeklyGoalSpin.value  = editWeeklyGoal
            notesField.text       = editNotes
            linkedRepoPathField.text = editLinkedRepoPath
            // ComboBox layout: 0=«Без приоритета»(=P4), 1=P1, 2=P2, 3=P3.
            defaultPriorityCombo.currentIndex = editDefaultPriority === 4
                ? 0
                : editDefaultPriority
        }

        onAccepted: {
            if (project) {
                projectViewModel.updateProject(projectId, "name",        nameField.text.trim())
                projectViewModel.updateProject(projectId, "color",       editColor)
                projectViewModel.updateProject(projectId, "description", editDialog.editDescription)
                projectViewModel.updateProject(projectId, "icon",        editDialog.editIcon)
                projectViewModel.updateProject(projectId, "weeklyGoal",  editDialog.editWeeklyGoal)
                projectViewModel.updateProject(projectId, "notes",          editDialog.editNotes)
                projectViewModel.updateProject(projectId, "linkedRepoPath", editDialog.editLinkedRepoPath)
                projectViewModel.setDefaultPriorityForProject(projectId, editDialog.editDefaultPriority)
                projectViewModel.setDefaultLabelsForProject(projectId, editDialog.editDefaultLabels)
            }
        }

        ColumnLayout {
            spacing: 12
            width: 320

            Label { text: qsTr("Название проекта"); font.pixelSize: FontSize.small; color: root.textSecondary }
            TextField {
                id:              nameField
                Layout.fillWidth: true
                placeholderText: qsTr("Название проекта")
                color:           root.textPrimary
            }

            Label { text: qsTr("Цвет"); font.pixelSize: FontSize.small; color: root.textSecondary }

            Flow {
                Layout.fillWidth: true
                spacing: Spacing.sm
                Repeater {
                    model: ["#db4c3f","#ff8c00","#fad000","#afb83b","#7ecc49",
                            "#299438","#6accbc","#158fad","#14aaf5","#96c3eb",
                            "#4073ff","#884dff","#af38eb","#eb96eb","#e05194",
                            "#ff8d85","#808080","#b8b8b8"]
                    delegate: Rectangle {
                        required property string modelData
                        width: 24; height: 24; radius: 12
                        color: modelData
                        border.color: editDialog.editColor === modelData
                                      ? root.textPrimary : "transparent"
                        border.width: 2
                        MouseArea {
                            anchors.fill: parent
                            cursorShape:  Qt.PointingHandCursor
                            onClicked:    editDialog.editColor = modelData
                        }
                    }
                }
            }

            Label {
                text: qsTr("Иконка проекта")
                font.pixelSize: FontSize.small
                color: root.textSecondary
                Layout.topMargin: Spacing.sm
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Spacing.sm

                TextField {
                    id: iconField
                    Layout.preferredWidth: 60
                    text: editDialog.editIcon
                    placeholderText: "🐢"
                    font.pixelSize: 22
                    horizontalAlignment: Text.AlignHCenter
                    maximumLength: 8
                    background: Rectangle {
                        color: ThemeManager.surfaceColor
                        border.color: ThemeManager.borderColor
                        border.width: 1
                        radius: Radius.sm
                    }
                    onTextChanged: editDialog.editIcon = text
                }

                // Q4.01 (redesign): эмодзи-набор заменён на 12 stroke-иконок
                // из resources/icons/projects/. Сохраняется как строка вида
                // "projects/<glyph>" — IconSvg уже умеет резолвить subdir,
                // и SidebarItem detects префикс "projects/" чтобы рендерить
                // иконку, а не как Text-emoji.
                RowLayout {
                    spacing: 2
                    Layout.fillWidth: true
                    Repeater {
                        model: ["projects/paw", "projects/heart", "projects/house",
                                "projects/fork-knife", "projects/briefcase", "projects/target",
                                "projects/lightbulb", "projects/book", "projects/rocket",
                                "projects/palette", "projects/cart", "projects/runner"]
                        delegate: ToolButton {
                            required property string modelData
                            flat: true
                            implicitWidth: Spacing.xl
                            implicitHeight: Spacing.xl
                            ToolTip.text: modelData
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            contentItem: IconSvg {
                                anchors.centerIn: parent
                                name: parent.modelData
                                size: 20
                                color: ThemeManager.textColor
                            }
                            onClicked: {
                                iconField.text = modelData
                                editDialog.editIcon = modelData
                            }
                        }
                    }
                }

                ToolButton {
                    flat: true
                    ToolTip.text: qsTr("Убрать иконку")
                    ToolTip.visible: hovered
                    onClicked: {
                        iconField.text = ""
                        editDialog.editIcon = ""
                    }
                    // Task 17cea65b iter-3: было unicode "✕".
                    contentItem: IconSvg {
                        anchors.centerIn: parent
                        name: "close"
                        size: 14
                        color: ThemeManager.secondaryTextColor
                    }
                }
            }

            Label {
                text: qsTr("Описание")
                font.pixelSize: FontSize.small
                color: root.textSecondary
                Layout.topMargin: Spacing.sm
            }
            // Q3.28: TextArea-in-ScrollView width fix.
            // Previously `width: parent.width` referred to ScrollView's
            // internal Flickable.contentItem, whose width is computed from
            // the content (TextArea itself) — a circular dependency that
            // re-allowed the "one word per line, then overflow" behaviour
            // the Q3.13 fix tried to address. Pinning `width` to the
            // ScrollView's `availableWidth` (via explicit id) breaks the
            // cycle: TextArea.width is driven by the ScrollView's external
            // width, the content wraps to that, and the ScrollView never
            // tries to grow horizontally.
            // Q3.25 / task 5fd93bed: bordered Rectangle goes on the ScrollView,
            // not on the inner TextArea. When text overflows the preferredHeight
            // the TextArea grows and scrolls inside the ScrollView's clip area
            // — if the Rectangle is on the TextArea, its top/bottom borders
            // scroll out of view with the content, and the visible "frame"
            // around the field falls apart. Anchoring the frame to the
            // ScrollView keeps it pinned to the visible bounds.
            ScrollView {
                id: descScroll
                Layout.fillWidth: true
                Layout.preferredHeight: 80
                clip: true

                background: Rectangle {
                    color: ThemeManager.surfaceColor
                    border.color: ThemeManager.borderColor
                    border.width: 1
                    radius: Radius.sm
                }

                TextArea {
                    id: descriptionField
                    width: descScroll.availableWidth
                    text: editDialog.editDescription
                    placeholderText: qsTr("Описание проекта (необязательно)")
                    wrapMode: TextEdit.Wrap
                    color: root.textPrimary
                    // Transparent so the ScrollView's bordered background shows
                    // through; the field still gets the TextArea default
                    // padding (6 px) so the text doesn't kiss the border.
                    background: null
                    onTextChanged: {
                        if (text.length > 500) text = text.substring(0, 500)
                        editDialog.editDescription = text
                    }
                }
            }

            // ── Notes ────────────────────────────────────────────────────────
            Label {
                text: qsTr("Заметки")
                font.pixelSize: FontSize.small
                color: root.textSecondary
                Layout.topMargin: Spacing.sm
            }
            // Q3.25 / task 5fd93bed: same frame-pin fix as descScroll above.
            ScrollView {
                id: notesScroll
                Layout.fillWidth: true
                Layout.preferredHeight: 160
                clip: true

                background: Rectangle {
                    color: ThemeManager.surfaceColor
                    border.color: ThemeManager.borderColor
                    border.width: 1
                    radius: Radius.sm
                }

                TextArea {
                    id: notesField
                    width: notesScroll.availableWidth
                    text: editDialog.editNotes
                    placeholderText: qsTr("Длинные заметки, ссылки, ToDo по проекту… (Markdown поддерживается)")
                    wrapMode: TextEdit.Wrap
                    color: root.textPrimary
                    background: null
                    onTextChanged: {
                        if (text.length > 5000) text = text.substring(0, 5000)
                        editDialog.editNotes = text
                    }
                }
            }

            // ── Linked repository (Claude Code integration) ──────────────────
            Label {
                text: qsTr("Связанный репозиторий")
                font.pixelSize: FontSize.small
                color: root.textSecondary
                Layout.topMargin: Spacing.sm
            }
            TextField {
                id: linkedRepoPathField
                Layout.fillWidth: true
                text: editDialog.editLinkedRepoPath
                placeholderText: qsTr("C:\\путь\\к\\репозиторию")
                color: root.textPrimary
                font.pixelSize: FontSize.body
                background: Rectangle {
                    color: ThemeManager.surfaceColor
                    border.color: ThemeManager.borderColor
                    border.width: 1
                    radius: Radius.sm
                }
                onTextChanged: editDialog.editLinkedRepoPath = text
            }
            Label {
                text: qsTr("Полный путь к локальной папке проекта. Если задан, в контекстном меню задач появится пункт «Запустить с Claude Code».")
                font.pixelSize: FontSize.caption
                color: root.textSecondary
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            // ── Weekly goal ───────────────────────────────────────────────────
            Label {
                text: qsTr("Цель на неделю (задач)")
                font.pixelSize: FontSize.small
                color: root.textSecondary
                Layout.topMargin: Spacing.sm
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Spacing.sm

                SpinBox {
                    id: weeklyGoalSpin
                    from: 0
                    to: 100
                    stepSize: 1
                    value: editDialog.editWeeklyGoal
                    editable: true
                    Layout.preferredWidth: 100
                    onValueModified: editDialog.editWeeklyGoal = value
                }
                Label {
                    text: weeklyGoalSpin.value === 0
                        ? qsTr("Без цели")
                        : qsTr("%1 задач в неделю").arg(weeklyGoalSpin.value)
                    color: root.textSecondary
                    font.pixelSize: FontSize.small
                    Layout.fillWidth: true
                }
            }

            // ── Default priority for new tasks ───────────────────────────────
            Label {
                text: qsTr("Приоритет по умолчанию")
                font.pixelSize: FontSize.small
                color: root.textSecondary
                Layout.topMargin: Spacing.sm
            }
            ComboBox {
                id: defaultPriorityCombo
                Layout.fillWidth: true
                font.pixelSize: FontSize.body

                // Index → priority mapping:
                //   0 → 4 (без приоритета)
                //   1 → 1 (P1)
                //   2 → 2 (P2)
                //   3 → 3 (P3)
                model: [qsTr("Без приоритета"), qsTr("P1 — Срочный"), qsTr("P2 — Высокий"), qsTr("P3 — Средний")]

                onActivated: {
                    editDialog.editDefaultPriority = currentIndex === 0 ? 4 : currentIndex
                }

                background: Rectangle {
                    color: ThemeManager.surfaceColor
                    border.color: ThemeManager.borderColor
                    border.width: 1
                    radius: Radius.sm
                }
            }

            // ── Default labels for new tasks ─────────────────────────────────
            Label {
                text: qsTr("Метки по умолчанию для новых задач")
                font.pixelSize: FontSize.small
                color: root.textSecondary
                Layout.topMargin: Spacing.sm
            }

            // Empty-state hint when there are no labels at all in the system.
            Label {
                visible: dataStore.allLabels().length === 0
                text: qsTr("В системе пока нет меток. Создай метки в разделе «Метки», чтобы выбирать их здесь.")
                font.pixelSize: FontSize.caption
                color: root.textSecondary
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            // Wrapping flow of label checkboxes — one per available label.
            Flow {
                id: defaultLabelsFlow
                visible: dataStore.allLabels().length > 0
                Layout.fillWidth: true
                spacing: Spacing.xs

                Repeater {
                    // Re-evaluated every time the dialog opens because
                    // editDefaultLabels resets in onOpened.
                    model: dataStore.allLabels()

                    delegate: Rectangle {
                        id: labelChip
                        required property var modelData

                        readonly property string labelId:    modelData ? modelData.id    : ""
                        readonly property string labelName:  modelData ? modelData.name  : ""
                        readonly property string labelColor: modelData ? modelData.color : "#808080"
                        readonly property bool   isChecked:
                            editDialog.editDefaultLabels.indexOf(labelId) !== -1

                        height: 28
                        // Width hugs the inner row contents.
                        width:  chipRow.implicitWidth + Spacing.sm * 2
                        radius: 14
                        color:  isChecked
                            ? Qt.rgba(ThemeManager.accentColor.r,
                                      ThemeManager.accentColor.g,
                                      ThemeManager.accentColor.b, 0.18)
                            : ThemeManager.surfaceColor
                        border.color: isChecked ? ThemeManager.accentColor : ThemeManager.borderColor
                        border.width: 1

                        RowLayout {
                            id: chipRow
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left:           parent.left
                            anchors.leftMargin:     Spacing.sm
                            anchors.rightMargin:    Spacing.sm
                            spacing: 6

                            // Color dot
                            Rectangle {
                                width: 10
                                height: 10
                                radius: 5
                                color: labelChip.labelColor
                                Layout.alignment: Qt.AlignVCenter
                            }

                            Text {
                                text: labelChip.labelName
                                color: ThemeManager.textColor
                                font.pixelSize: FontSize.small
                                Layout.alignment: Qt.AlignVCenter
                                Layout.rightMargin: Spacing.sm
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                // Toggle membership in the editDefaultLabels array.
                                // Reassign as a fresh array so QML notifies bindings.
                                var copy = editDialog.editDefaultLabels.slice()
                                var idx = copy.indexOf(labelChip.labelId)
                                if (idx === -1) copy.push(labelChip.labelId)
                                else            copy.splice(idx, 1)
                                editDialog.editDefaultLabels = copy
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Delete confirmation dialog ────────────────────────────────────────────
    Dialog {
        id:      deleteConfirmDialog
        title:   qsTr("Удалить проект?")
        modal:   true
        anchors.centerIn: Overlay.overlay
        // Explicit width prevents the wrapping Label from creating an
        // implicitWidth binding loop with the Dialog's auto-sizing.
        width:           Math.min(380, Overlay.overlay.width - Spacing.xl)
        standardButtons: Dialog.Yes | Dialog.No

        ColumnLayout {
            anchors.fill: parent
            spacing: Spacing.sm

            Label {
                text:           qsTr("Проект «%1» и все его задачи будут удалены без возможности восстановления.")
                                .arg(project ? project.name : "")
                wrapMode:       Text.Wrap
                Layout.fillWidth: true
                font.pixelSize: FontSize.body
                color:          root.textPrimary
            }
        }

        onAccepted: projectViewModel.deleteProject(projectId)
    }

    // ── Remove completed tasks confirmation ───────────────────────────────────
    Dialog {
        id: removeCompletedConfirm
        title: qsTr("Удалить все завершённые задачи?")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(380, Overlay.overlay.width - Spacing.xl)
        standardButtons: Dialog.Ok | Dialog.Cancel

        contentItem: Label {
            text: qsTr("Все задачи проекта со статусом «Выполнено» будут удалены. Действие необратимо.")
            wrapMode: Text.WordWrap
            color: ThemeManager.textColor
            width: 320
            padding: Spacing.md
        }

        onAccepted: {
            const n = projectViewModel.removeCompletedTasksInProject(projectId)
            const w = root.Window.window
            if (w && typeof w.showToast === "function") {
                if (n > 0) w.showToast(qsTr("Удалено %1 %2").arg(n).arg(removeCompletedConfirm._wordForm(n)), "success")
                else       w.showToast(qsTr("Завершённых задач не найдено"), "info")
            }
        }

        function _wordForm(n) {
            const m100 = n % 100
            const m10  = n % 10
            if (m100 >= 11 && m100 <= 14) return qsTr("задач")
            if (m10 === 1)                return qsTr("задача")
            if (m10 >= 2 && m10 <= 4)    return qsTr("задачи")
            return qsTr("задач")
        }
    }

    // ── Add section dialog ────────────────────────────────────────────────────
    Dialog {
        id:      addSectionDialog
        title:   qsTr("Добавить раздел")
        modal:   true
        anchors.centerIn: Overlay.overlay
        // Explicit width so the inner ColumnLayout's content (TextField) doesn't
        // overflow the Dialog frame. Material Dialog otherwise computes width
        // from contentItem.implicitWidth which is too small for the placeholder.
        width:           Math.min(360, Overlay.overlay.width - Spacing.xl)
        standardButtons: Dialog.Ok | Dialog.Cancel

        // ITER1 #40: clear text and focus the field when the dialog opens
        onOpened: {
            sectionNameField.text = ""
            sectionNameField.forceActiveFocus()
        }
        onAccepted: {
            var name = sectionNameField.text.trim()
            if (name !== "")
                dataStore.addSection(name, root.projectId)
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: Spacing.sm

            Label {
                text: qsTr("Название раздела")
                Layout.fillWidth: true
                font.pixelSize: FontSize.small
                color: root.textSecondary
            }
            TextField {
                id:              sectionNameField
                Layout.fillWidth: true
                placeholderText: qsTr("Название раздела")
                color:           root.textPrimary
            }
        }
    }

    // ── Rename section dialog ─────────────────────────────────────────────────
    Dialog {
        id: renameSectionDialog
        property string sectionId:  ""
        property string currentName: ""
        title: qsTr("Переименовать раздел")
        modal: true
        anchors.centerIn: Overlay.overlay
        width:           Math.min(360, Overlay.overlay.width - Spacing.xl)
        standardButtons: Dialog.Ok | Dialog.Cancel

        onOpened: renameSectionField.text = currentName
        onAccepted: {
            const n = renameSectionField.text.trim()
            if (n !== "" && sectionId !== "")
                dataStore.renameSection(sectionId, n)
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: Spacing.sm
            Label {
                text: qsTr("Новое название")
                Layout.fillWidth: true
                font.pixelSize: FontSize.small
                color: root.textSecondary
            }
            TextField {
                id: renameSectionField
                Layout.fillWidth: true
                color: root.textPrimary
            }
        }
    }

    // ── Delete section confirmation ──────────────────────────────────────────
    Dialog {
        id: deleteSectionDialog
        property string sectionId:   ""
        property string sectionName: ""
        title: qsTr("Удалить раздел")
        modal: true
        anchors.centerIn: Overlay.overlay
        width:           Math.min(360, Overlay.overlay.width - Spacing.xl)
        standardButtons: Dialog.Yes | Dialog.No

        ColumnLayout {
            anchors.fill: parent
            spacing: Spacing.sm
            Label {
                text: qsTr("Удалить раздел «%1»? Задачи останутся в проекте без раздела.")
                      .arg(deleteSectionDialog.sectionName)
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: root.textPrimary
            }
        }

        onAccepted: {
            if (deleteSectionDialog.sectionId !== "")
                dataStore.removeSection(deleteSectionDialog.sectionId)
        }
    }

    // ── Add task to specific section dialog ───────────────────────────────────
    Dialog {
        id:      addTaskToSectionDialog
        title:   qsTr("Добавить задачу")
        modal:   true
        anchors.centerIn: Overlay.overlay
        width:           Math.min(360, Overlay.overlay.width - Spacing.xl)
        standardButtons: Dialog.Ok | Dialog.Cancel

        property string sectionId: ""

        // ITER1 #40: clear text and focus the field when the dialog opens
        onOpened: {
            taskContentField.text = ""
            taskContentField.forceActiveFocus()
        }
        onAccepted: {
            var t = taskContentField.text.trim()
            if (t !== "") {
                // B-6.41 — addTask returns QString taskId (confirmed from taskviewmodel.h)
                var taskId = taskViewModel.addTask(t, root.projectId, 4, "", [])
                if (taskId && sectionId !== "")
                    taskViewModel.moveTask(taskId, root.projectId, sectionId)
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: Spacing.sm

            Label {
                text: qsTr("Название задачи")
                Layout.fillWidth: true
                font.pixelSize: FontSize.small
                color: root.textSecondary
            }
            TextField {
                id:              taskContentField
                Layout.fillWidth: true
                placeholderText: qsTr("Название задачи")
                color:           root.textPrimary
            }
        }
    }

    // ── Helpers ───────────────────────────────────────────────────────────────
    function _priorityColor(p) {
        switch (p) {
            case 1:  return ThemeManager.priorityP1Color
            case 2:  return ThemeManager.priorityP2Color
            case 3:  return ThemeManager.priorityP3Color
            default: return ThemeManager.priorityP4Color
        }
    }

    function _isOverdue(dateStr) {
        if (!dateStr || dateStr === "") return false
        var due   = new Date(dateStr)
        var today = new Date()
        today.setHours(0, 0, 0, 0)
        return due < today
    }

    // ── Right-pane TaskEditor ────────────────────────────────────────────────
    // Slides in from the right when a task is selected. Mirrors the
    // DesktopLayout pattern so the project view has the same UX as Inbox/Today.
    Rectangle {
        id: taskEditorSeparator
        anchors.top:    parent.top
        anchors.bottom: parent.bottom
        anchors.right:  taskEditorLoader.left
        width:    1
        color:    borderColor
        visible:  taskEditorLoader.active
    }

    Loader {
        id: taskEditorLoader
        anchors.top:    parent.top
        anchors.bottom: parent.bottom
        anchors.right:  parent.right
        width: root.taskEditorPaneWidth
        active: taskViewModel.selectedTask !== null && taskViewModel.selectedTask !== undefined
        visible: active
        source: "../components/TaskEditor.qml"
        onLoaded: {
            if (item && taskViewModel.selectedTask)
                item.task = taskViewModel.selectedTask
        }
        Connections {
            target: taskEditorLoader.item
            ignoreUnknownSignals: true
            function onCloseRequested() {
                taskViewModel.selectedTask = null
            }
        }
        // Keep the editor's task in sync when selectedTask changes (clicking
        // a different row while the editor is already open).
        Connections {
            target: taskViewModel
            function onSelectedTaskChanged() {
                if (taskEditorLoader.item && taskViewModel.selectedTask)
                    taskEditorLoader.item.task = taskViewModel.selectedTask
            }
        }
    }

    // ── Weekly-goal celebration ──────────────────────────────────────────────
    // Confetti burst + toast when the active project's weekly goal is reached.
    // The signal fires from C++ ProjectViewModel::onTasksChanged() once per
    // (projectId, ISO week); we play the overlay only when the celebration
    // belongs to the project the user is currently viewing.
    ConfettiOverlay {
        id: projectConfetti
        anchors.fill: parent
        z: 1000
    }

    Connections {
        target: projectViewModel
        function onWeeklyGoalReached(projectId) {
            if (projectId !== root.projectId) return
            projectConfetti.celebrate()
            const w = root.Window.window
            if (w && typeof w.showToast === "function")
                w.showToast(qsTr("🎉 Цель проекта на неделю достигнута!"), "success")
        }
    }

    // ── Share-link dialog wiring ─────────────────────────────────────────────
    // The "Поделиться через Google Drive…" menu item above puts the dialog
    // into the busy state and calls shareManager.shareProject(); the
    // ShareManager fires shareLinkReady / shareError back here, which we
    // forward into the dialog's state machine.
    ShareLinkDialog {
        id: shareLinkDialog
    }

    Connections {
        target: shareManager
        ignoreUnknownSignals: true

        function onShareLinkReady(url, fileId) {
            if (shareLinkDialog.visible)
                shareLinkDialog.showLink(url, fileId)
        }
        function onShareError(reason) {
            if (shareLinkDialog.visible)
                shareLinkDialog.showError(reason)
        }
        function onShareRevoked(fileId) {
            shareLinkDialog.close()
            const w = root.Window.window
            if (w && typeof w.showToast === "function")
                w.showToast(qsTr("Ссылка отозвана"), "success")
        }
    }
}
