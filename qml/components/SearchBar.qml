import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtCore
import OpenTodoUI

// SearchBar — expandable search field with results/recent dropdown
Rectangle {
    id: root

    // ── Public properties ────────────────────────────────────────────────────
    property bool isDarkTheme: ThemeManager.isDark

    // ── Recent-search history (persisted via Qt.labs.settings) ───────────────
    // Хранится как JSON-массив строк под ключом search/recentJson.
    // Кэш в historyEntries нужен, чтобы Repeater реагировал на изменения.
    Settings {
        id: searchHistory
        category: "search"
        property string recentJson: "[]"
    }

    property var historyEntries: []
    Component.onCompleted: root.historyEntries = root._loadHistory()

    // Парсит JSON, возвращает массив строк (с защитой от повреждённых данных).
    function _loadHistory() {
        try {
            var arr = JSON.parse(searchHistory.recentJson)
            if (!Array.isArray(arr)) return []
            return arr.filter(function(e) { return typeof e === "string" })
        } catch (e) {
            return []
        }
    }

    // Добавляет запрос в начало истории (с дедупликацией, ограничение 5).
    function _pushHistory(query) {
        if (!query) return
        var trimmed = query.trim()
        if (trimmed.length < 2) return
        var arr = root._loadHistory()
        // Удаляем существующие совпадения (case-insensitive), оставляем последний ввод.
        var lower = trimmed.toLowerCase()
        arr = arr.filter(function(e) { return e.toLowerCase() !== lower })
        arr.unshift(trimmed)
        if (arr.length > 5) arr = arr.slice(0, 5)
        searchHistory.recentJson = JSON.stringify(arr)
        root.historyEntries = arr
    }

    // Удаляет конкретную запись из истории.
    function _removeHistory(query) {
        var arr = root._loadHistory().filter(function(e) { return e !== query })
        searchHistory.recentJson = JSON.stringify(arr)
        root.historyEntries = arr
    }

    // Очищает всю историю.
    function _clearHistory() {
        searchHistory.recentJson = "[]"
        root.historyEntries = []
    }

    // ── Dimensions ───────────────────────────────────────────────────────────
    // Q4.01 pixel-perfect: дизайн topbar search 260×28, borderRadius 6.
    height: 28
    radius: Radius.md

    // ── Colors ───────────────────────────────────────────────────────────────
    readonly property color _bgNormal:    ThemeManager.backgroundColor
    readonly property color _bgHover:     ThemeManager.hoverColor
    readonly property color _bgExpanded:  ThemeManager.backgroundColor
    readonly property color _textColor:   ThemeManager.textColor
    readonly property color _subColor:    ThemeManager.secondaryTextColor
    readonly property color _borderColor: ThemeManager.borderColor
    readonly property color _dropBg:      ThemeManager.backgroundColor
    readonly property color _itemHover:   ThemeManager.hoverColor
    readonly property color _accent:      ThemeManager.accentColor

    // ── State ────────────────────────────────────────────────────────────────
    property bool expanded: false

    color: expanded
           ? _bgExpanded
           : (hoverArea.containsMouse ? _bgHover : _bgNormal)

    border.color: expanded ? _accent : _borderColor
    border.width: expanded ? 1.5 : 1

    Behavior on color       { ColorAnimation { duration: Duration.fast } }
    Behavior on border.color { ColorAnimation { duration: Duration.fast } }

    // ── 300 ms debounce timer ────────────────────────────────────────────────
    Timer {
        id: debounceTimer
        interval: 300
        repeat: false
        onTriggered: {
            var q = searchField.text.trim()
            if (q.length > 0) {
                searchViewModel.search(q)
                // Сохраняем введённый запрос в историю (debounced submit).
                root._pushHistory(q)
            }
        }
    }

    // ── Collapsed / collapsed-hover mouse area ───────────────────────────────
    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.IBeamCursor
        visible: !root.expanded
        onClicked: {
            root.expanded = true
            searchField.forceActiveFocus()
        }
    }

    // ── Collapsed row: magnifying glass + placeholder ─────────────────────────
    RowLayout {
        anchors {
            fill: parent
            leftMargin: 10
            rightMargin: 10
            topMargin: 0
            bottomMargin: 0
        }
        spacing: 6
        visible: !root.expanded

        // Q4.01 (redesign): "⌕" unicode glyph → stroke search IconSvg so the
        // search affordance matches the rest of the chrome glyphs.
        IconSvg {
            name: "search"
            // Q4.01 pixel-perfect: дизайн (.design-temp/.../app-shell.jsx:176)
            // указывает SearchIcon size 13 в search-боксе 260×28 топ-бара.
            size: 13
            color: root._subColor
            Layout.alignment: Qt.AlignVCenter
        }

        Text {
            text: qsTr("Поиск")
            font.pixelSize: FontSize.body
            color: root._subColor
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
        }
    }

    // ── Expanded row: search icon + TextField + clear/spinner ─────────────────
    RowLayout {
        anchors {
            fill: parent
            leftMargin: 10
            rightMargin: 10
        }
        spacing: 6
        visible: root.expanded

        // Q4.01 (redesign): "⌕" unicode glyph → stroke search IconSvg so the
        // search affordance matches the rest of the chrome glyphs.
        IconSvg {
            name: "search"
            // Q4.01 pixel-perfect: дизайн (.design-temp/.../app-shell.jsx:176)
            // указывает SearchIcon size 13 в search-боксе 260×28 топ-бара.
            size: 13
            color: root._subColor
            Layout.alignment: Qt.AlignVCenter
        }

        TextField {
            id: searchField
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            placeholderText: qsTr("Поиск…")
            font.pixelSize: FontSize.body
            color: root._textColor
            background: Item {}   // transparent — outer Rectangle is the background
            leftPadding: 0
            rightPadding: 0

            onTextChanged: {
                debounceTimer.restart()
                if (text.trim().length === 0)
                    searchViewModel.clearSearch()
                // Перепроверяем видимость popup (при очистке поля без истории — закрываем).
                root._refreshDropdownVisibility()
            }

            Keys.onEscapePressed: {
                root._closeAndClear()
            }

            Keys.onReturnPressed: {
                // Сохраняем явный submit в историю (даже до debounce-таймера).
                var q = searchField.text.trim()
                if (q.length > 0)
                    root._pushHistory(q)
                // Select the highlighted result if dropdown is open
                if (dropdown.visible && resultsView.count > 0)
                    root._activateResult(resultsView.currentIndex)
            }

            Keys.onUpPressed: {
                if (resultsView.currentIndex > 0)
                    resultsView.currentIndex--
            }

            Keys.onDownPressed: {
                if (resultsView.currentIndex < resultsView.count - 1)
                    resultsView.currentIndex++
            }
        }

        // Loading spinner
        BusyIndicator {
            id: spinner
            visible: searchViewModel.isSearching
            running: visible
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20
            Layout.alignment: Qt.AlignVCenter
            palette.dark: root._accent
        }

        // Clear button — task 17cea65b iter-3: было unicode "✕".
        IconSvg {
            visible: !searchViewModel.isSearching && searchField.text.length > 0
            name: "close"
            size: 12
            color: root._subColor
            Layout.alignment: Qt.AlignVCenter

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root._closeAndClear()
            }
        }
    }

    // ── Helper functions ─────────────────────────────────────────────────────

    // Экранирование HTML-спецсимволов перед вставкой в RichText
    function _escapeHtml(s) {
        return s.replace(/&/g, "&amp;")
                .replace(/</g, "&lt;")
                .replace(/>/g, "&gt;")
                .replace(/"/g, "&quot;")
    }

    // Возвращает HTML-строку с подсвеченным вхождением query в text.
    // Совпадение регистронезависимо; оригинальный регистр символов сохраняется.
    function highlightMatch(text, query) {
        if (!text) return ""
        var safe = _escapeHtml(text)
        if (!query || query.length === 0) return safe
        var safeQ = _escapeHtml(query)
        // Экранируем метасимволы RegExp, чтобы спецсимволы в запросе не ломали выражение
        var escaped = safeQ.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")
        var re = new RegExp("(" + escaped + ")", "gi")
        return safe.replace(re,
            '<span style="font-weight:600;color:' + root._accent + '">$1</span>')
    }

    function _closeAndClear() {
        searchField.text = ""
        searchViewModel.clearSearch()
        root.expanded = false
        searchField.focus = false
        dropdown.close()
    }

    function _activateResult(idx) {
        if (idx < 0 || idx >= searchViewModel.results.length) return
        var item = searchViewModel.results[idx]
        if (!item) return
        switch (item.type) {
            // A task result used to just jump to the Inbox (wrong for tasks in
            // other projects, and the task itself was never opened); comment
            // results did nothing at all.
            case "task":    root._openTask(item.id, item.projectId); break
            case "project": sidebarViewModel.navigateToProject(item.id); break
            case "label":   sidebarViewModel.navigateToLabel(item.title); break
            case "filter":  sidebarViewModel.navigateToFilter(item.id); break
            case "comment":
                if (item.taskId)         root._openTask(item.taskId, "")
                else if (item.projectId) sidebarViewModel.navigateToProject(item.projectId)
                break
        }
        root._closeAndClear()
    }

    // Navigate to the task's project (or Inbox) and open it in the editor.
    // Navigation runs first: switching views is synchronous and must not
    // clobber the editor selection we set right after.
    function _openTask(taskId, projectId) {
        if (!taskId) return
        var pid = projectId
        if (!pid && typeof dataStore !== "undefined") {
            var t = dataStore.taskById(taskId)
            pid = t ? t.projectId : ""
        }
        var proj = (pid && typeof projectViewModel !== "undefined")
                   ? projectViewModel.projectById(pid) : null
        if (proj && !proj.isInbox)
            sidebarViewModel.navigateToProject(pid)
        else
            sidebarViewModel.navigateToInbox()
        taskViewModel.openTaskById(taskId)
    }

    // ── Manage dropdown open/close with expanded state ────────────────────────
    // Условия видимости popup:
    //  • поле в фокусе (expanded);
    //  • если ввод непустой — показываем результаты;
    //  • если ввод пустой — popup открываем ТОЛЬКО при наличии истории.
    function _refreshDropdownVisibility() {
        if (!root.expanded) {
            dropdown.close()
            return
        }
        var q = searchField.text.trim()
        if (q.length > 0) {
            dropdown.open()
            return
        }
        // Пустой ввод: открываем только если есть история.
        if (root.historyEntries && root.historyEntries.length > 0)
            dropdown.open()
        else
            dropdown.close()
    }

    onExpandedChanged: root._refreshDropdownVisibility()
    onHistoryEntriesChanged: root._refreshDropdownVisibility()

    // Close if user clicks elsewhere
    Connections {
        target: searchField
        function onActiveFocusChanged() {
            // B-7.15: dropdown.contentItem is the Item wrapper — the named
            // property containsMouse is exposed on dropContent (the inner Item
            // that owns dropMA). Use that directly.
            if (!searchField.activeFocus && !dropContent.containsMouse) {
                // small delay so click-in-dropdown is processed first
                closeDelayTimer.restart()
            }
        }
    }

    Timer {
        id: closeDelayTimer
        interval: 150
        repeat: false
        onTriggered: {
            if (!searchField.activeFocus)
                root._closeAndClear()
        }
    }

    // ── Dropdown Popup ────────────────────────────────────────────────────────
    Popup {
        id: dropdown
        parent: root
        x: 0
        y: root.height + 4
        width: Math.max(root.width, 320)
        padding: 0
        modal: false
        closePolicy: Popup.NoAutoClose   // we manage closing ourselves

        background: Rectangle {
            color: root._dropBg
            radius: Radius.lg
            border.color: root._borderColor
            border.width: 1

            layer.enabled: true
            layer.effect: null  // shadow via drop-shadow on the item if available
        }

        // Cap height with a scrollable container
        contentItem: Item {
            id: dropContent
            property bool containsMouse: dropMA.containsMouse
            implicitHeight: Math.min(innerCol.implicitHeight, 400)
            clip: true

            MouseArea {
                id: dropMA
                anchors.fill: parent
                hoverEnabled: true
                // Qt 6 requires explicit signal parameter declaration.
                // Bare `mouse.accepted = ...` used the Qt 5 implicit `mouse`
                // parameter that no longer exists in Qt 6, producing
                // "ReferenceError: mouse is not defined" on every click.
                onClicked: function(mouse) { mouse.accepted = false }
            }

            // ── RECENT SEARCHES (empty query) ─────────────────────────────────
            // Видим только если поле пустое и история не пуста — само открытие
            // popup также блокируется в _refreshDropdownVisibility().
            Column {
                id: innerCol
                width: parent.width
                visible: searchField.text.trim().length === 0
                         && root.historyEntries && root.historyEntries.length > 0

                // Header row
                RowLayout {
                    width: parent.width
                    height: 36
                    spacing: 4

                    Text {
                        text: qsTr("Недавние запросы")
                        font.pixelSize: FontSize.caption
                        font.weight: Font.DemiBold
                        color: root._subColor
                        Layout.leftMargin: Spacing.md
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                    }

                    Text {
                        text: qsTr("Очистить")
                        font.pixelSize: FontSize.caption
                        color: root._accent
                        visible: root.historyEntries.length > 0
                        Layout.alignment: Qt.AlignVCenter

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root._clearHistory()
                        }
                    }

                    Item { width: 8 }
                }

                // Recent search entries — максимум 5 (slice заранее, на случай легаси).
                Repeater {
                    model: root.historyEntries.slice(0, 5)

                    delegate: Rectangle {
                        required property string modelData
                        required property int index

                        width: innerCol.width
                        height: 36
                        color: recentMouse.containsMouse ? root._itemHover : "transparent"

                        Behavior on color { ColorAnimation { duration: Duration.instant } }

                        RowLayout {
                            anchors {
                                fill: parent
                                leftMargin: 12
                                rightMargin: 8
                            }
                            spacing: 8

                            // Q4.01 (redesign): "🕒" emoji → stroke clock icon
                            IconSvg {
                                name: "clock"
                                size: 13
                                color: root._subColor
                                Layout.alignment: Qt.AlignVCenter
                            }

                            Text {
                                text: modelData
                                font.pixelSize: FontSize.body
                                color: root._textColor
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                            }

                            // Task 17cea65b iter-3: было unicode "✕".
                            IconSvg {
                                name: "close"
                                size: 10
                                color: root._subColor
                                opacity: recentMouse.containsMouse ? 1.0 : 0.0
                                Layout.alignment: Qt.AlignVCenter

                                Behavior on opacity { NumberAnimation { duration: Duration.instant } }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    // Russian tooltip per spec
                                    ToolTip.visible: containsMouse
                                    ToolTip.delay: 400
                                    ToolTip.text: qsTr("Убрать из истории")
                                    onClicked: function(mouse) {
                                        mouse.accepted = true
                                        root._removeHistory(modelData)
                                    }
                                }
                            }
                        }

                        MouseArea {
                            id: recentMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                // Применяем выбранный запрос: ставим текст,
                                // запускаем поиск, поднимаем в истории и закрываем popup.
                                searchField.text = modelData
                                searchViewModel.search(modelData)
                                root._pushHistory(modelData)
                                dropdown.close()
                            }
                        }
                    }
                }
            } // Column (recent)

            // ── SEARCH RESULTS (query present) ───────────────────────────────
            Flickable {
                id: resultsFlick
                anchors.fill: parent
                visible: searchField.text.trim().length > 0
                contentWidth: width
                contentHeight: resultsView.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                // Grouped list using a ListView over the flat results array
                // We build a synthetic model that interleaves section headers.
                ListView {
                    id: resultsView
                    width: parent.width
                    implicitHeight: contentHeight
                    interactive: false   // Flickable handles scrolling
                    currentIndex: 0

                    model: searchViewModel.results

                    // Track category headers rendered
                    property string _lastCategory: ""

                    delegate: Item {
                        required property var modelData
                        required property int index

                        width: resultsView.width
                        // header height (28) + row height (44) — show header only when category changes
                        property string _cat: modelData ? modelData.type : ""
                        property bool _showHeader: {
                            if (index === 0) return true
                            var prev = searchViewModel.results[index - 1]
                            return prev ? prev.type !== _cat : true
                        }
                        height: (_showHeader ? 28 : 0) + 44

                        // Category header
                        Text {
                            id: catHeader
                            visible: parent._showHeader
                            anchors { top: parent.top; left: parent.left; right: parent.right }
                            height: 28
                            leftPadding: 12
                            verticalAlignment: Text.AlignVCenter
                            text: {
                                switch (parent._cat) {
                                    case "task":    return qsTr("ЗАДАЧИ")
                                    case "project": return qsTr("ПРОЕКТЫ")
                                    case "label":   return qsTr("МЕТКИ")
                                    case "filter":  return qsTr("ФИЛЬТРЫ")
                                    default:        return parent._cat.toUpperCase()
                                }
                            }
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                            font.letterSpacing: 0.6
                            color: root._subColor
                        }

                        // Result row
                        Rectangle {
                            id: resultRow
                            anchors {
                                top: catHeader.visible ? catHeader.bottom : parent.top
                                left: parent.left
                                right: parent.right
                            }
                            height: 44
                            color: (resultsView.currentIndex === index || rowMouse.containsMouse)
                                   ? root._itemHover : "transparent"

                            Behavior on color { ColorAnimation { duration: Duration.instant } }

                            RowLayout {
                                anchors {
                                    fill: parent
                                    leftMargin: 12
                                    rightMargin: 12
                                }
                                spacing: 10

                                // Type icon — Q4.01 (redesign): "🏷" emoji
                                // мешал визуальному весу рядом с символьными
                                // глифами других типов. Метка теперь рендерится
                                // через IconSvg "label", остальные типы — те
                                // же текстовые маркеры что раньше.
                                Item {
                                    Layout.alignment: Qt.AlignVCenter
                                    Layout.preferredWidth: 18
                                    Layout.preferredHeight: 18

                                    IconSvg {
                                        visible: modelData && modelData.type === "label"
                                        anchors.centerIn: parent
                                        name: "label"
                                        size: 14
                                        color: root._subColor
                                    }

                                    Text {
                                        visible: !modelData || modelData.type !== "label"
                                        anchors.centerIn: parent
                                        font.pixelSize: 15
                                        color: root._subColor
                                        text: {
                                            if (!modelData) return ""
                                            switch (modelData.type) {
                                                case "task":    return "✓"
                                                case "project": return "#"
                                                case "filter":  return "⊙"
                                                default:        return "•"
                                            }
                                        }
                                    }
                                }

                                // Title + subtitle
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    spacing: 2

                                    Text {
                                        // Подсветка совпадения поискового запроса в заголовке
                                        text: modelData
                                              ? root.highlightMatch(modelData.title || "", searchField.text.trim())
                                              : ""
                                        textFormat: Text.RichText
                                        font.pixelSize: FontSize.body
                                        font.weight: Font.Medium
                                        color: root._textColor
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        visible: modelData && modelData.subtitle && modelData.subtitle !== ""
                                        // Подсветка совпадения и в строке описания
                                        text: modelData
                                              ? root.highlightMatch(modelData.subtitle || "", searchField.text.trim())
                                              : ""
                                        textFormat: Text.RichText
                                        font.pixelSize: 13
                                        color: root._subColor
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }
                            }

                            MouseArea {
                                id: rowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root._activateResult(index)
                                onEntered: resultsView.currentIndex = index
                            }
                        }
                    } // delegate
                } // ListView
            } // Flickable (results)
        } // contentItem
    } // Popup
}
