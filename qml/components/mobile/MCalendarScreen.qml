import QtQuick
import QtQuick.Controls
import OpenTodoUI

// MCalendarScreen — month calendar view. Ported from m-screens3.jsx
// MCalendarScreen (README §8). Compact top bar + month header with prev/next
// month steppers + the shared MCalendar grid + a list of the selected day's
// tasks. Month marks render up to 3 priority-colour dots per day; the day list
// shows non-completed tasks due on the selected day.
Item {
    id: root
    anchors.fill: parent

    // ── Required screen signals ────────────────────────────────────────────
    signal menuRequested()
    signal taskClicked(string id)

    // ── Displayed month / selected day state (init to current) ─────────────
    property int _y: (new Date()).getFullYear()
    property int _m: (new Date()).getMonth()      // 0-based
    property int _selDay: (new Date()).getDate()

    readonly property var _months: ["Январь", "Февраль", "Март", "Апрель", "Май",
        "Июнь", "Июль", "Август", "Сентябрь", "Октябрь", "Ноябрь", "Декабрь"]
    readonly property var _monthsGen: ["января", "февраля", "марта", "апреля",
        "мая", "июня", "июля", "августа", "сентября", "октября", "ноября", "декабря"]

    // Today's date only highlights when the displayed month is the real month.
    readonly property int _today: {
        var now = new Date()
        return (now.getFullYear() === _y && now.getMonth() === _m) ? now.getDate() : -1
    }

    function _prioColor(p) {
        switch (p) {
        case 1:  return ThemeManager.priorityP1Color
        case 2:  return ThemeManager.priorityP2Color
        case 3:  return ThemeManager.priorityP3Color
        default: return ThemeManager.priorityP4Color
        }
    }

    // Pad a 0-based month to a "yyyy-MM" prefix for dueDate matching.
    readonly property string _ymPrefix: {
        var mm = _m + 1
        return _y + "-" + (mm < 10 ? "0" + mm : "" + mm) + "-"
    }

    // Day-of-month of a task's due (from dueDate "yyyy-MM-dd" or dueDatetime).
    function _taskDay(tk) {
        var dd = tk.dueDate
        if (!dd || dd === "") {
            if (tk.dueDatetime && tk.dueDatetime !== "")
                dd = tk.dueDatetime.substring(0, 10)
            else
                return 0
        }
        if (dd.substring(0, 8) !== root._ymPrefix) return 0
        var n = parseInt(dd.substring(8, 10), 10)
        return isNaN(n) ? 0 : n
    }

    // ── Derived data, recomputed on month / selection / data changes ───────
    property var _marks: ({})
    property var _dayTasks: []

    function _recompute() {
        var marks = {}
        var dayList = []
        if (typeof dataStore === "undefined" || !dataStore) {
            _marks = marks; _dayTasks = dayList; return
        }
        var all = dataStore.allTasksData()
        for (var i = 0; i < all.length; i++) {
            var tk = all[i]
            if (!tk || tk.isDeleted || tk.isCompleted) continue
            var day = root._taskDay(tk)
            if (day <= 0) continue
            ;(marks[day] = marks[day] || []).push(root._prioColor(tk.priority))
            if (day === root._selDay) dayList.push(tk)
        }
        _marks = marks
        _dayTasks = dayList
    }

    Component.onCompleted: _recompute()
    on_YChanged: _recompute()
    on_MChanged: _recompute()
    on_SelDayChanged: _recompute()
    Connections {
        target: dataStore
        ignoreUnknownSignals: true
        function onTasksChanged() { root._recompute() }
    }

    function _shiftMonth(dir) {
        var nm = _m + dir
        if (nm < 0)       { _y -= 1; _m = 11 }
        else if (nm > 11) { _y += 1; _m = 0 }
        else              { _m = nm }
    }

    // ── Density (shared with task rows) ────────────────────────────────────
    MDensity { id: mdens }

    // ── Background ─────────────────────────────────────────────────────────
    Rectangle { anchors.fill: parent; color: ThemeManager.backgroundColor }

    // ── Top bar (compact) ──────────────────────────────────────────────────
    MTopBar {
        id: topBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        big: false
        title: qsTr("Календарь")
        titleIconName: "calendar"
        titleColor: ThemeManager.accentColor
        onMenuClicked: root.menuRequested()

        MIconButton {
            iconName: "search"
            iconColor: ThemeManager.textColor
            onClicked: {}   // no-op (search wired at shell level)
        }
    }

    // ── Scrollable content ─────────────────────────────────────────────────
    Flickable {
        id: flick
        anchors.top: topBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        contentWidth: width
        contentHeight: content.implicitHeight
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: content
            width: flick.width

            // Month header: "<Месяц> <Год>" + prev/next steppers.
            Item {
                width: parent.width
                height: 48

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    text: root._months[root._m] + " " + root._y
                    font.pixelSize: 18
                    font.weight: Font.Bold
                    color: ThemeManager.textColor
                }

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4

                    MIconButton {
                        iconName: "chevron-left"
                        iconColor: ThemeManager.secondaryTextColor
                        iconSize: 20
                        onClicked: root._shiftMonth(-1)
                    }
                    MIconButton {
                        iconName: "chevron-right"
                        iconColor: ThemeManager.secondaryTextColor
                        iconSize: 20
                        onClicked: root._shiftMonth(1)
                    }
                }
            }

            // Month grid.
            Item {
                width: parent.width
                height: cal.implicitHeight
                MCalendar {
                    id: cal
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    year: root._y
                    month: root._m
                    marks: root._marks
                    selectedDay: root._selDay
                    today: root._today
                    onDayPicked: function(day) { root._selDay = day }
                }
            }

            // Selected-day section label: "<day> <monthGenitive> · N задач".
            Item {
                width: parent.width
                height: 44
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 20
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 8
                    text: root._selDay + " " + root._monthsGen[root._m] + " · "
                          + (root._dayTasks.length === 0
                             ? qsTr("нет задач")
                             : root._dayTasks.length + " " + qsTr("задач"))
                    font.pixelSize: 13
                    font.weight: Font.Bold
                    font.letterSpacing: 0.3
                    font.capitalization: Font.AllUppercase
                    color: ThemeManager.secondaryTextColor
                }
            }

            // Day task list.
            Repeater {
                model: root._dayTasks
                delegate: MTaskRow {
                    required property var modelData
                    width: content.width
                    dens: mdens
                    showProject: true
                    taskId:      modelData.id
                    content:     modelData.content
                    priority:    modelData.priority
                    isCompleted: modelData.isCompleted
                    dueDate:     modelData.dueDate
                    deadline:    modelData.deadline
                    labels:      modelData.labels
                    projectId:   modelData.projectId
                    onOpenTask:  root.taskClicked(taskId)
                }
            }

            // Empty state.
            Item {
                width: parent.width
                height: 80
                visible: root._dayTasks.length === 0
                Text {
                    anchors.centerIn: parent
                    width: parent.width - 48
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: qsTr("Свободный день — ничего не запланировано")
                    font.pixelSize: 14
                    color: ThemeManager.secondaryTextColor
                }
            }

            // Bottom padding so the FAB / nav bar never overlaps the last row.
            Item { width: parent.width; height: 96 }
        }
    }
}
