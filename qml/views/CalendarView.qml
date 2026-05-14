import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

// Rectangle (not Item) so `color: bgColor` is valid — same fix as BoardView.
Rectangle {
    id: root
    anchors.fill: parent

    // ── Design constants ──────────────────────────────────────────────────────
    readonly property color todoistRed:    ThemeManager.accentColor
    readonly property bool  isDarkTheme:   ThemeManager.isDark
    readonly property color bgColor:       ThemeManager.backgroundColor
    readonly property color contentColor:  ThemeManager.surfaceColor
    readonly property color borderColor:   ThemeManager.borderColor
    readonly property color textPrimary:   ThemeManager.textColor
    readonly property color textSecondary: ThemeManager.secondaryTextColor

    // ── State ─────────────────────────────────────────────────────────────────
    property date currentMonth: new Date()
    property date selectedDate: new Date()
    property string viewMode: "month"   // "month" | "week" | "agenda"

    // Agenda refresh kick: incrementing this invalidates all agenda day bindings.
    property int _agendaKick: 0

    color: bgColor

    // F-6.63 — keyboard navigation: arrow keys move selected day, Enter opens popup
    focus: true

    Keys.onLeftPressed:  _moveSelected(-1)
    Keys.onRightPressed: _moveSelected(1)
    Keys.onUpPressed:    _moveSelected(-7)
    Keys.onDownPressed:  _moveSelected(7)
    Keys.onReturnPressed: dayDetailPopup.open()
    Keys.onEnterPressed:  dayDetailPopup.open()

    function _moveSelected(delta) {
        var d = new Date(selectedDate)
        d.setDate(d.getDate() + delta)
        selectedDate = d
        // If moved outside current month view, follow it
        if (d.getMonth() !== currentMonth.getMonth() ||
            d.getFullYear() !== currentMonth.getFullYear()) {
            var m = new Date(d.getFullYear(), d.getMonth(), 1)
            currentMonth = m
        }
    }

    // ── Navigation helpers ────────────────────────────────────────────────────
    function prevMonth() {
        var d = new Date(currentMonth)
        d.setDate(1)
        d.setMonth(d.getMonth() - 1)
        currentMonth = d
    }

    function nextMonth() {
        var d = new Date(currentMonth)
        d.setDate(1)
        d.setMonth(d.getMonth() + 1)
        currentMonth = d
    }

    // ── Period navigation (adapts to viewMode) ────────────────────────────────
    function prevPeriod() {
        if (viewMode === "week") {
            var d = new Date(selectedDate)
            d.setDate(d.getDate() - 7)
            selectedDate = d
            // Keep currentMonth in sync so the month label stays meaningful
            currentMonth = new Date(d.getFullYear(), d.getMonth(), 1)
        } else if (viewMode === "agenda") {
            var da = new Date(agendaStartDate)
            da.setDate(da.getDate() - 14)
            agendaStartDate = da
        } else {
            prevMonth()
        }
    }

    function nextPeriod() {
        if (viewMode === "week") {
            var d = new Date(selectedDate)
            d.setDate(d.getDate() + 7)
            selectedDate = d
            currentMonth = new Date(d.getFullYear(), d.getMonth(), 1)
        } else if (viewMode === "agenda") {
            var da = new Date(agendaStartDate)
            da.setDate(da.getDate() + 14)
            agendaStartDate = da
        } else {
            nextMonth()
        }
    }

    // Agenda window start date (reset to today when switching into agenda mode).
    property date agendaStartDate: {
        var t = new Date(); t.setHours(0,0,0,0); return t
    }

    // Russian plural for задача/задачи/задач
    function _taskWord(n) {
        var mod10  = n % 10
        var mod100 = n % 100
        if (mod10 === 1 && mod100 !== 11)               return "задача"
        if (mod10 >= 2 && mod10 <= 4 &&
            (mod100 < 10 || mod100 >= 20))              return "задачи"
        return "задач"
    }

    // ── Week helpers ──────────────────────────────────────────────────────────

    // Returns an array of 7 Date objects for the Mon-Sun week that contains selectedDate.
    function _weekDates() {
        var ref = new Date(selectedDate)
        ref.setHours(0, 0, 0, 0)
        var dow = (ref.getDay() + 6) % 7   // 0 = Mon, 6 = Sun
        var monday = new Date(ref)
        monday.setDate(ref.getDate() - dow)
        var dates = []
        for (var i = 0; i < 7; i++) {
            var d = new Date(monday)
            d.setDate(monday.getDate() + i)
            dates.push(d)
        }
        return dates
    }

    // Human-readable label for the displayed week, e.g. "12–18 мая 2026"
    function _weekRangeLabel() {
        var dates = _weekDates()
        var mon = dates[0]
        var sun = dates[6]
        var locale = Qt.locale("ru_RU")
        if (mon.getMonth() === sun.getMonth()) {
            return mon.getDate() + "–" + sun.getDate() + " " +
                   locale.monthName(mon.getMonth(), Locale.LongFormat) + " " + mon.getFullYear()
        }
        // Spans two months
        return mon.getDate() + " " + locale.monthName(mon.getMonth(), Locale.ShortFormat) +
               " – " + sun.getDate() + " " + locale.monthName(sun.getMonth(), Locale.ShortFormat) +
               " " + sun.getFullYear()
    }

    // ── Data helpers ──────────────────────────────────────────────────────────

    // Returns the calendar Date for the given 0-based cell index (Mon-first grid)
    function _dateForCell(cellIndex) {
        var first      = new Date(currentMonth.getFullYear(), currentMonth.getMonth(), 1)
        var dow        = first.getDay()                     // 0=Sun … 6=Sat
        var startOff   = (dow === 0) ? 6 : dow - 1         // shift to Mon=0
        var d          = new Date(first)
        d.setDate(d.getDate() - startOff + cellIndex)
        return d
    }

    // B-6.54 — locale-independent same-day comparison
    function _sameDay(a, b) {
        return a.getFullYear() === b.getFullYear() &&
               a.getMonth()   === b.getMonth()    &&
               a.getDate()    === b.getDate()
    }

    // Returns array of task snapshots whose dueDate matches the given date.
    // ITER1 #27: also include tasks where dueDate is empty but dueDatetime
    // starts with the date string (time-stamped tasks with no plain dueDate).
    function _tasksForDate(date) {
        var dateStr = Qt.formatDate(date, "yyyy-MM-dd")
        var tasks   = []
        for (var i = 0; i < taskViewModel.count; i++) {
            var t = taskViewModel.taskAt(i)
            if (!t || t.isCompleted) continue
            var matchesDueDate     = (t.dueDate === dateStr)
            var matchesDatetime    = (t.dueDate === "" &&
                                      typeof t.dueDatetime === "string" &&
                                      t.dueDatetime.startsWith(dateStr))
            if (matchesDueDate || matchesDatetime) {
                tasks.push({
                    id:       t.id,
                    content:  t.content,
                    priority: t.priority,
                    dueDate:  t.dueDate,
                    labels:   t.labels || []
                })
            }
        }
        return tasks
    }

    function _priorityColor(p) {
        switch (p) {
            case 1:  return ThemeManager.priorityP1Color   // replaces hardcoded #db4c3f
            case 2:  return ThemeManager.priorityP2Color
            case 3:  return ThemeManager.priorityP3Color
            default: return ThemeManager.priorityP4Color
        }
    }

    // Fixed 6×7 month grid (42 cells) — matches Todoist / Google Calendar layout.
    // Days from prior/next month fill the leading and trailing slots.
    property int _firstDayOffset: {
        var first = new Date(currentMonth.getFullYear(), currentMonth.getMonth(), 1)
        var dow   = first.getDay()
        return (dow === 0) ? 6 : dow - 1  // Mon=0
    }
    property int _daysInMonth: new Date(currentMonth.getFullYear(),
                                        currentMonth.getMonth() + 1, 0).getDate()
    property int _gridRows:   6
    property int _cellCount:  42  // 6 × 7

    // ── Main layout ───────────────────────────────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Month navigation header ───────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 56
            color:  root.contentColor

            RowLayout {
                anchors.fill:        parent
                anchors.leftMargin:  16
                anchors.rightMargin: 16
                spacing: 12

                ToolButton {
                    text: "◀"   // ◀
                    font.pixelSize: 14
                    Material.foreground: root.textPrimary
                    onClicked: root.prevPeriod()
                    ToolTip.text: root.viewMode === "week"   ? "Предыдущая неделя"
                                : root.viewMode === "agenda" ? "Предыдущие 14 дней"
                                :                              "Предыдущий месяц"
                    ToolTip.visible: hovered
                }

                // B-6.62 — pin locale to ru_RU for consistent Russian month/week names
                Label {
                    text: {
                        if (root.viewMode === "week")   return root._weekRangeLabel()
                        if (root.viewMode === "agenda") {
                            var endDate = new Date(root.agendaStartDate)
                            endDate.setDate(endDate.getDate() + 13)
                            var locale = Qt.locale("ru_RU")
                            var s = root.agendaStartDate
                            var e = endDate
                            if (s.getMonth() === e.getMonth())
                                return s.getDate() + "–" + e.getDate() + " " +
                                       locale.monthName(s.getMonth(), Locale.LongFormat) + " " + s.getFullYear()
                            return s.getDate() + " " + locale.monthName(s.getMonth(), Locale.ShortFormat) +
                                   " – " + e.getDate() + " " + locale.monthName(e.getMonth(), Locale.ShortFormat) +
                                   " " + e.getFullYear()
                        }
                        // BUG-FIX (C-1): Qt 6.11 tightened the 3-arg Qt.formatDate
                        // overload to require a QDate (not QDateTime) for arg 1,
                        // throwing TypeError on the QML `property date` value that
                        // materialises as QDateTime. Use JS Date.toLocaleDateString
                        // instead — it accepts a QML date directly.
                        return root.currentMonth.toLocaleDateString(Qt.locale("ru_RU"), "MMMM yyyy")
                    }
                    font.pixelSize: 20
                    font.bold:      true
                    color:          root.textPrimary
                }

                ToolButton {
                    text: "▶"   // ▶
                    font.pixelSize: 14
                    Material.foreground: root.textPrimary
                    onClicked: root.nextPeriod()
                    ToolTip.text: root.viewMode === "week"   ? "Следующая неделя"
                                : root.viewMode === "agenda" ? "Следующие 14 дней"
                                :                              "Следующий месяц"
                    ToolTip.visible: hovered
                }

                Item { Layout.fillWidth: true }

                // ── View-mode toggle ──────────────────────────────────────────
                RowLayout {
                    spacing: 2

                    ToolButton {
                        text: "Месяц"
                        font.pixelSize: 13
                        checked: root.viewMode === "month"
                        onClicked: root.viewMode = "month"
                        Material.foreground: root.viewMode === "month"
                                             ? root.todoistRed : root.textSecondary
                        ToolTip.text: "Вид: месяц"
                        ToolTip.visible: hovered
                    }

                    ToolButton {
                        text: "Неделя"
                        font.pixelSize: 13
                        checked: root.viewMode === "week"
                        onClicked: root.viewMode = "week"
                        Material.foreground: root.viewMode === "week"
                                             ? root.todoistRed : root.textSecondary
                        ToolTip.text: "Вид: неделя"
                        ToolTip.visible: hovered
                    }

                    ToolButton {
                        text: "Список"
                        font.pixelSize: 13
                        checked: root.viewMode === "agenda"
                        onClicked: {
                            // Reset agenda window to today each time we enter agenda mode
                            var t = new Date(); t.setHours(0,0,0,0)
                            root.agendaStartDate = t
                            root.viewMode = "agenda"
                        }
                        Material.foreground: root.viewMode === "agenda"
                                             ? root.todoistRed : root.textSecondary
                        ToolTip.text: "Вид: список (агенда)"
                        ToolTip.visible: hovered
                    }
                }

                Button {
                    text:   "Сегодня"
                    flat:   true
                    onClicked: {
                        root.currentMonth = new Date()
                        root.selectedDate = new Date()
                    }
                    Material.foreground: root.todoistRed
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

        // ── Day-of-week headers (month mode only) ────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 32
            color:  root.contentColor
            visible: root.viewMode === "month"

            RowLayout {
                anchors.fill: parent
                spacing: 0

                Repeater {
                    model: ["Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"]

                    delegate: Item {
                        Layout.fillWidth: true
                        height: 32
                        required property string modelData

                        Text {
                            anchors.centerIn: parent
                            text:           modelData
                            font.pixelSize: 12
                            font.bold:      true
                            color: {
                                // Highlight weekends
                                if (modelData === "Сб" || modelData === "Вс")
                                    return root.isDarkTheme ? "#888" : "#999"
                                return root.textSecondary
                            }
                        }
                    }
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

        // L-6.52 — calendar grid: dynamic rows (5 or 6) based on month layout
        GridLayout {
            Layout.fillWidth:  true
            Layout.fillHeight: true
            visible: root.viewMode === "month"
            columns:     7
            rows:        root._gridRows
            rowSpacing:  1
            columnSpacing: 1

            Repeater {
                model: root._cellCount

                delegate: Rectangle {
                    id: cell

                    required property int index

                    Layout.fillWidth:  true
                    Layout.fillHeight: true

                    // Computed per-cell state
                    property date cellDate:       root._dateForCell(index)
                    property bool isCurrentMonth: cellDate.getMonth()    === root.currentMonth.getMonth() &&
                                                  cellDate.getFullYear() === root.currentMonth.getFullYear()
                    // B-6.54 — use _sameDay() instead of toDateString() locale compare
                    property bool isToday:        root._sameDay(cellDate, new Date())
                    property bool isSelected:     root._sameDay(cellDate, root.selectedDate)
                    property var  dayTasks:        root._tasksForDate(cellDate)

                    color: {
                        if (isSelected)
                            return root.isDarkTheme ? "#3a2020" : "#fff0ee"
                        if (!isCurrentMonth)
                            return root.isDarkTheme ? "#1a1a1a" : "#f5f5f5"
                        // ~8 % accent tint on days that have at least one task
                        // (subtle hint of activity, doesn't fight today's red border).
                        if (dayTasks.length > 0)
                            return Qt.rgba(root.todoistRed.r,
                                           root.todoistRed.g,
                                           root.todoistRed.b,
                                           0.08)
                        return root.contentColor
                    }

                    border.color: isToday ? root.todoistRed : root.borderColor
                    border.width: isToday ? 2 : 1

                    // ITER3 #4: Refresh task previews when model changes.
                    // Gate with root.visible so the 35×4 connections are no-ops
                    // when CalendarView is not the active view, preventing 17 500+
                    // iterations per change when the user is on another screen.
                    Connections {
                        target: taskViewModel
                        function onDataChanged()  { if (root.visible) cell.dayTasks = root._tasksForDate(cell.cellDate) }
                        function onRowsInserted() { if (root.visible) cell.dayTasks = root._tasksForDate(cell.cellDate) }
                        function onRowsRemoved()  { if (root.visible) cell.dayTasks = root._tasksForDate(cell.cellDate) }
                        function onModelReset()   { if (root.visible) cell.dayTasks = root._tasksForDate(cell.cellDate) }
                    }

                    ColumnLayout {
                        anchors.fill:    parent
                        anchors.margins: 4
                        spacing: 2

                        // Day number
                        Text {
                            text:       cellDate.getDate()
                            font.pixelSize: 13
                            font.bold:  isToday
                            color: {
                                if (isToday)         return root.todoistRed
                                if (!isCurrentMonth) return root.textSecondary
                                return root.textPrimary
                            }
                            Layout.alignment: Qt.AlignRight
                        }

                        // Task previews (up to 3)
                        Repeater {
                            model: Math.min(cell.dayTasks.length, 3)

                            delegate: Rectangle {
                                required property int index
                                Layout.fillWidth: true
                                height: 16; radius: 2
                                color: root.isDarkTheme ? "#333333" : "#f0f0f0"

                                // Priority accent left border
                                Rectangle {
                                    width:  3; height: parent.height; radius: 1
                                    color:  root._priorityColor(cell.dayTasks[index].priority)
                                }

                                Text {
                                    anchors {
                                        left:           parent.left
                                        leftMargin:     6
                                        right:          parent.right
                                        rightMargin:    2
                                        verticalCenter: parent.verticalCenter
                                    }
                                    text:            cell.dayTasks[index].content
                                    font.pixelSize:  10
                                    color:           root.textPrimary
                                    elide:           Text.ElideRight
                                }
                            }
                        }

                        // "+N more" overflow label
                        Text {
                            visible:        cell.dayTasks.length > 3
                            text:           "+ ещё " + (cell.dayTasks.length - 3)
                            font.pixelSize: 10
                            color:          root.todoistRed
                        }

                        // Vertical spacer so day-number stays at top
                        Item { Layout.fillHeight: true }
                    }

                    HoverHandler { id: cellHover }

                    // Hover highlight overlay
                    Rectangle {
                        anchors.fill: parent
                        color:        root.isDarkTheme ? "#ffffff" : "#000000"
                        opacity:      cellHover.containsMouse ? 0.04 : 0
                        Behavior on opacity { NumberAnimation { duration: 80 } }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape:  Qt.PointingHandCursor
                        onClicked: {
                            root.selectedDate = cellDate
                            dayDetailPopup.open()
                        }
                    }

                    // Right-click quick-add (month view)
                    MouseArea {
                        anchors.fill:    parent
                        acceptedButtons: Qt.RightButton
                        onClicked: function(mouse) {
                            quickAddMenu.cellDate = cell.cellDate
                            quickAddMenu.popup()
                        }
                    }
                }
            }
        }

        // ── Week view ─────────────────────────────────────────────────────────
        // One tall row per day (Mon–Sun), listing task titles inline.
        // Tapping a row opens the same dayDetailPopup used by the month grid.
        ListView {
            id: weekListView
            Layout.fillWidth:  true
            Layout.fillHeight: true
            visible: root.viewMode === "week"
            spacing: 1
            clip:    true

            // Refresh when tasks change (same gate pattern as month cells)
            Connections {
                target: taskViewModel
                function onDataChanged()  { if (root.visible && root.viewMode === "week") weekListView.model = root._weekDates() }
                function onRowsInserted() { if (root.visible && root.viewMode === "week") weekListView.model = root._weekDates() }
                function onRowsRemoved()  { if (root.visible && root.viewMode === "week") weekListView.model = root._weekDates() }
                function onModelReset()   { if (root.visible && root.viewMode === "week") weekListView.model = root._weekDates() }
            }

            // Recompute model whenever selectedDate changes (week navigation)
            model: root._weekDates()
            // Ensure model updates when selectedDate or viewMode changes
            Connections {
                target: root
                function onSelectedDateChanged() { if (root.viewMode === "week") weekListView.model = root._weekDates() }
                function onViewModeChanged()     { if (root.viewMode === "week") weekListView.model = root._weekDates() }
            }

            delegate: Rectangle {
                id: weekCell
                required property var modelData   // a Date object
                required property int index

                width:  ListView.view.width
                height: 92
                color: {
                    if (root._sameDay(modelData, root.selectedDate))
                        return root.isDarkTheme ? "#3a2020" : "#fff0ee"
                    return root.contentColor
                }
                border.color: root._sameDay(modelData, new Date()) ? root.todoistRed : root.borderColor
                border.width: root._sameDay(modelData, new Date()) ? 2 : 1

                // Per-cell task list (evaluated once; refreshed via ListView model reassignment)
                readonly property var dayTasks: root._tasksForDate(modelData)
                readonly property bool isToday: root._sameDay(modelData, new Date())

                ColumnLayout {
                    anchors.fill:    parent
                    anchors.margins: 8
                    spacing: 3

                    // ── Day header row ────────────────────────────────────────
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        // Full Russian day name (e.g. "понедельник")
                        Text {
                            text: Qt.locale("ru_RU").dayName(weekCell.modelData.getDay(), Locale.LongFormat)
                            font.pixelSize: 13
                            font.bold:      weekCell.isToday
                            color:          weekCell.isToday ? root.todoistRed : root.textPrimary
                            // Capitalize first letter
                            Component.onCompleted: {
                                var s = text
                                text = s.charAt(0).toUpperCase() + s.slice(1)
                            }
                        }

                        // dd.MM date
                        Text {
                            text:  Qt.formatDate(weekCell.modelData, "dd.MM")
                            font.pixelSize: 12
                            color: root.textSecondary
                        }

                        Item { Layout.fillWidth: true }

                        // Task-count chip
                        Rectangle {
                            visible: weekCell.dayTasks.length > 0
                            width:   chipText.implicitWidth + 10
                            height:  18; radius: 9
                            color:   root.todoistRed

                            Text {
                                id: chipText
                                anchors.centerIn: parent
                                text:            weekCell.dayTasks.length
                                font.pixelSize:  10
                                font.bold:       true
                                color:           "white"
                            }
                        }
                    }

                    // ── Inline task list (up to 3 visible, then "+N ещё") ─────
                    Repeater {
                        model: Math.min(weekCell.dayTasks.length, 3)

                        delegate: RowLayout {
                            required property int index
                            Layout.fillWidth: true
                            spacing: 4

                            // Priority dot
                            Rectangle {
                                width: 6; height: 6; radius: 3
                                color: root._priorityColor(weekCell.dayTasks[index].priority)
                            }

                            Text {
                                text:           weekCell.dayTasks[index].content
                                font.pixelSize: 11
                                color:          root.textPrimary
                                elide:          Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }

                    // Overflow label
                    Text {
                        visible:        weekCell.dayTasks.length > 3
                        text:           "+ ещё " + (weekCell.dayTasks.length - 3)
                        font.pixelSize: 10
                        color:          root.todoistRed
                    }

                    Item { Layout.fillHeight: true }
                }

                HoverHandler { id: weekCellHover }

                Rectangle {
                    anchors.fill: parent
                    color:        root.isDarkTheme ? "#ffffff" : "#000000"
                    opacity:      weekCellHover.containsMouse ? 0.04 : 0
                    Behavior on opacity { NumberAnimation { duration: 80 } }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape:  Qt.PointingHandCursor
                    onClicked: {
                        root.selectedDate = weekCell.modelData
                        dayDetailPopup.open()
                    }
                }

                // Right-click quick-add (week view)
                MouseArea {
                    anchors.fill:    parent
                    acceptedButtons: Qt.RightButton
                    onClicked: function(mouse) {
                        quickAddMenu.cellDate = weekCell.modelData
                        quickAddMenu.popup()
                    }
                }
            }
        }

        // ── Agenda view ───────────────────────────────────────────────────────
        // Flat scrollable list of the next 14 days, each as a section header
        // followed by its tasks. Navigation arrows shift the 14-day window.
        ScrollView {
            id: agendaScroll
            visible: root.viewMode === "agenda"
            Layout.fillWidth:  true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            // Refresh agenda when tasks change (same gate pattern as week view)
            Connections {
                target: taskViewModel
                function onDataChanged()  { if (root.visible && root.viewMode === "agenda") root._agendaKick++ }
                function onRowsInserted() { if (root.visible && root.viewMode === "agenda") root._agendaKick++ }
                function onRowsRemoved()  { if (root.visible && root.viewMode === "agenda") root._agendaKick++ }
                function onModelReset()   { if (root.visible && root.viewMode === "agenda") root._agendaKick++ }
            }

            ColumnLayout {
                width: agendaScroll.availableWidth
                spacing: 0

                readonly property int daysAhead: 14

                Repeater {
                    model: parent.daysAhead

                    delegate: Item {
                        id: agendaDayItem
                        required property int index

                        Layout.fillWidth: true
                        Layout.preferredHeight: agendaDayCol.implicitHeight + 8

                        readonly property var dayDate: {
                            // Depend on _agendaKick so this re-evaluates on data change
                            var _kick = root._agendaKick
                            var d = new Date(root.agendaStartDate)
                            d.setDate(d.getDate() + index)
                            d.setHours(0, 0, 0, 0)
                            return d
                        }
                        readonly property bool isToday: {
                            var today = new Date(); today.setHours(0,0,0,0)
                            return root._sameDay(dayDate, today)
                        }
                        readonly property bool isTomorrow: {
                            var tomorrow = new Date(); tomorrow.setHours(0,0,0,0)
                            tomorrow.setDate(tomorrow.getDate() + 1)
                            return root._sameDay(dayDate, tomorrow)
                        }
                        readonly property var tasks: root._tasksForDate(dayDate) || []

                        ColumnLayout {
                            id: agendaDayCol
                            anchors.left:  parent.left
                            anchors.right: parent.right
                            anchors.top:   parent.top
                            spacing: 2

                            // ── Day section header ────────────────────────────
                            RowLayout {
                                Layout.fillWidth:    true
                                Layout.leftMargin:   16
                                Layout.rightMargin:  16
                                Layout.topMargin:    10
                                spacing: 6

                                Label {
                                    text: {
                                        if (agendaDayItem.isToday)    return "Сегодня"
                                        if (agendaDayItem.isTomorrow) return "Завтра"
                                        var locale = Qt.locale("ru_RU")
                                        var raw = locale.dayName(agendaDayItem.dayDate.getDay(),
                                                                 Locale.LongFormat)
                                        return raw.charAt(0).toUpperCase() + raw.slice(1)
                                    }
                                    font.pixelSize: 14
                                    font.bold: true
                                    color: agendaDayItem.isToday ? root.todoistRed : root.textPrimary
                                }

                                Label {
                                    text: Qt.formatDate(agendaDayItem.dayDate, "dd.MM")
                                    font.pixelSize: 12
                                    color: root.textSecondary
                                }

                                Item { Layout.fillWidth: true }

                                Label {
                                    text: agendaDayItem.tasks.length > 0
                                          ? agendaDayItem.tasks.length + " " +
                                            root._taskWord(agendaDayItem.tasks.length)
                                          : "—"
                                    font.pixelSize: 12
                                    color: root.textSecondary
                                }
                            }

                            // ── Task rows ─────────────────────────────────────
                            Repeater {
                                model: agendaDayItem.tasks

                                delegate: Rectangle {
                                    id: agendaTaskRow
                                    required property var  modelData
                                    required property int  index

                                    Layout.fillWidth:   true
                                    Layout.leftMargin:  32
                                    Layout.rightMargin: 16
                                    height: 28
                                    color: "transparent"

                                    HoverHandler { id: agendaTaskHover }

                                    // Subtle hover highlight
                                    Rectangle {
                                        anchors.fill: parent
                                        color:        root.isDarkTheme ? "#ffffff" : "#000000"
                                        opacity:      agendaTaskHover.containsMouse ? 0.04 : 0
                                        Behavior on opacity { NumberAnimation { duration: 80 } }
                                    }

                                    RowLayout {
                                        anchors.fill:        parent
                                        anchors.leftMargin:  4
                                        anchors.rightMargin: 4
                                        spacing: 6

                                        // Priority dot
                                        Rectangle {
                                            width: 6; height: 6; radius: 3
                                            color: root._priorityColor(agendaTaskRow.modelData.priority)
                                            Layout.alignment: Qt.AlignVCenter
                                        }

                                        Text {
                                            text:             agendaTaskRow.modelData.content || ""
                                            color:            root.textPrimary
                                            elide:            Text.ElideRight
                                            font.pixelSize:   13
                                            Layout.fillWidth: true
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill:    parent
                                        cursorShape:     Qt.PointingHandCursor
                                        // Right-click → quick-add menu for that day
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        onClicked: function(mouse) {
                                            if (mouse.button === Qt.RightButton) {
                                                quickAddMenu.cellDate = agendaDayItem.dayDate
                                                quickAddMenu.popup()
                                            } else {
                                                root.selectedDate = agendaDayItem.dayDate
                                                dayDetailPopup.open()
                                            }
                                        }
                                    }
                                }
                            }

                            // ── Empty-day placeholder ─────────────────────────
                            Label {
                                visible:           agendaDayItem.tasks.length === 0
                                text:              "  Нет задач"
                                Layout.leftMargin: 36
                                color:             root.textSecondary
                                font.pixelSize:    12
                                font.italic:       true
                            }

                            // Section separator
                            Rectangle {
                                Layout.fillWidth:  true
                                Layout.topMargin:  6
                                Layout.bottomMargin: 2
                                height: 1
                                color:  root.borderColor
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Day-detail popup ──────────────────────────────────────────────────────
    Popup {
        id: dayDetailPopup
        x:       (root.width  - width)  / 2
        y:       (root.height - height) / 2
        width:   400
        height:  500
        modal:   true
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        // ITER2 #12: refresh the popup task list when taskViewModel changes
        // while the popup is open (background sync, undo completion, etc.).
        Connections {
            target: taskViewModel
            function onDataChanged()  {
                if (dayDetailPopup.opened)
                    popupTaskList.model = root._tasksForDate(root.selectedDate)
            }
            function onRowsInserted() {
                if (dayDetailPopup.opened)
                    popupTaskList.model = root._tasksForDate(root.selectedDate)
            }
            function onRowsRemoved() {
                if (dayDetailPopup.opened)
                    popupTaskList.model = root._tasksForDate(root.selectedDate)
            }
            function onModelReset() {
                if (dayDetailPopup.opened)
                    popupTaskList.model = root._tasksForDate(root.selectedDate)
            }
        }

        background: Rectangle {
            radius:       Radius.xl
            color:        root.contentColor
            border.color: root.borderColor
            border.width: 1

            // Drop shadow
            layer.enabled: true
            layer.effect: null
        }

        contentItem: Rectangle {
            color:  "transparent"
            radius: Radius.xl

            ColumnLayout {
                anchors.fill:    parent
                anchors.margins: 16
                spacing: 12

                // Header row
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    // B-6.62 — pin locale to ru_RU for Russian day/month names
                    // BUG-FIX (C-1): same Qt 6.11 TypeError as the header label —
                    // `property date` materialises as QDateTime, so the 3-arg
                    // overload throws. Route through JS Date.toLocaleDateString.
                    Label {
                        text:           root.selectedDate.toLocaleDateString(Qt.locale("ru_RU"),
                                                                             "dddd, d MMMM")
                        font.pixelSize: 18
                        font.bold:      true
                        color:          root.textPrimary
                        Layout.fillWidth: true
                    }

                    // Task count badge
                    Rectangle {
                        visible: popupTaskList.count > 0
                        width:   badgeText.implicitWidth + 10
                        height:  20; radius: 10
                        color:   root.todoistRed

                        Text {
                            id:             badgeText
                            anchors.centerIn: parent
                            text:           popupTaskList.count
                            font.pixelSize: 11
                            color:          "white"
                            font.bold:      true
                        }
                    }

                    ToolButton {
                        text:    "✕"
                        font.pixelSize: 14
                        Material.foreground: root.textSecondary
                        onClicked: dayDetailPopup.close()
                    }
                }

                // Separator
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color:  root.borderColor
                }

                // Task list
                ListView {
                    id:               popupTaskList
                    Layout.fillWidth:  true
                    Layout.fillHeight: true
                    model:   root._tasksForDate(root.selectedDate)
                    clip:    true
                    spacing: 0

                    // Empty state
                    Label {
                        anchors.centerIn: parent
                        visible:          popupTaskList.count === 0
                        text:             "На этот день задач нет"
                        font.pixelSize:   14
                        color:            root.textSecondary
                    }

                    delegate: Item {
                        id: taskDelegate
                        width:  ListView.view.width
                        height: 44
                        required property var modelData

                        RowLayout {
                            anchors {
                                left:           parent.left
                                right:          parent.right
                                verticalCenter: parent.verticalCenter
                            }
                            spacing: 10

                            // Priority circle / checkbox
                            Rectangle {
                                width:  18; height: 18; radius: 9
                                border.color: root._priorityColor(modelData.priority)
                                border.width: 1.5
                                color: "transparent"

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape:  Qt.PointingHandCursor
                                    onClicked: {
                                        taskViewModel.completeTask(modelData.id)
                                        // Refresh popup model
                                        popupTaskList.model = root._tasksForDate(root.selectedDate)
                                    }
                                }
                            }

                            // Task content
                            Text {
                                text:            modelData.content
                                font.pixelSize:  14
                                color:           root.textPrimary
                                elide:           Text.ElideRight
                                Layout.fillWidth: true
                            }

                            // Delete button
                            ToolButton {
                                text:    "✕"
                                font.pixelSize: 11
                                Material.foreground: root.textSecondary
                                opacity: 0.6
                                onClicked: {
                                    taskViewModel.deleteTask(modelData.id)
                                    popupTaskList.model = root._tasksForDate(root.selectedDate)
                                }
                            }
                        }

                        // F-6.61 — tap on task row opens editor; sits behind action buttons
                        // z-ordering: this MouseArea is under RowLayout children which have
                        // their own MouseAreas, so those clicks reach the child first.
                        MouseArea {
                            anchors.fill: parent
                            z: -1
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                taskViewModel.openTaskById(modelData.id)
                                dayDetailPopup.close()
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

                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                }

                // Separator
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color:  root.borderColor
                }

                // Quick-add row
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text:           "+"
                        color:          root.todoistRed
                        font.pixelSize: 16
                        font.bold:      true
                    }

                    TextField {
                        id:              popupAddField
                        Layout.fillWidth: true
                        placeholderText: "Добавить задачу…"
                        background:     Item {}
                        color:          root.textPrimary
                        font.pixelSize: 14

                        onAccepted: {
                            var t = text.trim()
                            if (t !== "") {
                                // ITER2 #6: pre-fill the active project so calendar
                                // quick-add doesn't dump the task into Inbox.
                                taskViewModel.addTask(t,
                                    taskViewModel.currentProjectId, 4,
                                    Qt.formatDate(root.selectedDate, "yyyy-MM-dd"), [])
                                text = ""
                                // Refresh popup model
                                popupTaskList.model = root._tasksForDate(root.selectedDate)
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Right-click context menu (month + week views) ─────────────────────────
    Menu {
        id: quickAddMenu
        property date cellDate: new Date()

        MenuItem {
            text: "Добавить задачу на этот день…"
            onTriggered: {
                quickAddDialog.targetDate = quickAddMenu.cellDate
                quickAddDialog.open()
            }
        }
    }

    // ── Quick-add dialog (opened from context menu) ───────────────────────────
    Dialog {
        id: quickAddDialog
        title: "Новая задача"
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel

        property date   targetDate: new Date()
        property string isoDate:    Qt.formatDate(targetDate, "yyyy-MM-dd")

        onOpened: {
            quickAddField.text = ""
            quickAddField.forceActiveFocus()
        }

        onAccepted: {
            var c = quickAddField.text.trim()
            if (c.length === 0) return
            taskViewModel.addTask(c,
                taskViewModel.currentProjectId, 4,
                quickAddDialog.isoDate, [])
        }

        contentItem: ColumnLayout {
            spacing: 8

            Label {
                text:           "Дата: " + Qt.formatDate(quickAddDialog.targetDate,
                                                         "dd.MM.yyyy")
                color:          root.textSecondary
                font.pixelSize: 12
            }

            TextField {
                id:               quickAddField
                Layout.fillWidth: true
                Layout.preferredWidth: 320
                placeholderText:  "Текст задачи"
                color:            root.textPrimary
                // Allow pressing Enter to accept the dialog
                Keys.onReturnPressed: quickAddDialog.accept()
                Keys.onEnterPressed:  quickAddDialog.accept()
            }
        }
    }
}
