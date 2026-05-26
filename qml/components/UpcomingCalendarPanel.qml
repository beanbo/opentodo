import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

// Compact monthly calendar panel for the Upcoming view.
// Shows a 7-column (Mon-first) month grid with task-count dots.
// Emits dayPicked(date) when the user clicks a day cell.
//
// Layout modes (controlled by parent):
//   • widePanel = true  → fixed 280 px wide right-side panel
//   • widePanel = false → collapsible top strip (240 px tall)

Rectangle {
    id: root

    // ── Public interface ──────────────────────────────────────────────────────
    property date currentMonth:  { var d = new Date(); d.setDate(1); return d }
    property date selectedDay:   new Date()
    signal dayPicked(date picked)

    // True → panel rendered as a right sidebar; false → top strip.
    property bool widePanel: true

    // Collapse state for the top-strip mode.
    property bool collapsed: false

    // ── Design tokens ─────────────────────────────────────────────────────────
    readonly property color _bg:      ThemeManager.surfaceColor
    readonly property color _border:  ThemeManager.borderColor
    readonly property color _text:    ThemeManager.textColor
    readonly property color _dim:     ThemeManager.secondaryTextColor
    readonly property color _accent:  ThemeManager.accentColor
    readonly property bool  _dark:    ThemeManager.isDark

    color:        _bg
    radius:       0
    border.color: _border
    border.width: 1
    clip:         true

    // ── Geometry ──────────────────────────────────────────────────────────────
    // widePanel:  fill the container width (parent sets it to 280)
    // strip:      full width, height toggles between 240 (expanded) and 36 (collapsed)
    implicitWidth:  widePanel ? 280 : -1
    implicitHeight: widePanel
        ? -1
        : (collapsed ? 36 : 240)

    Behavior on implicitHeight {
        NumberAnimation { duration: Duration.normal; easing.type: Easing.InOutQuad }
    }

    // ── Navigation helpers ────────────────────────────────────────────────────
    function _prevMonth() {
        var d = new Date(root.currentMonth)
        d.setDate(1)
        d.setMonth(d.getMonth() - 1)
        root.currentMonth = d
        _rebuildCache()
    }
    function _nextMonth() {
        var d = new Date(root.currentMonth)
        d.setDate(1)
        d.setMonth(d.getMonth() + 1)
        root.currentMonth = d
        _rebuildCache()
    }

    // ── Grid geometry helpers ─────────────────────────────────────────────────
    readonly property int _firstDayOff: {
        var first = new Date(root.currentMonth.getFullYear(),
                             root.currentMonth.getMonth(), 1)
        var dow = first.getDay()          // 0=Sun…6=Sat
        return (dow === 0) ? 6 : dow - 1 // Mon=0
    }
    readonly property int _daysInMonth: new Date(
        root.currentMonth.getFullYear(),
        root.currentMonth.getMonth() + 1, 0).getDate()
    // Minimum rows needed: ceil((offset + daysInMonth) / 7), capped at 6
    readonly property int _rows: Math.min(6,
        Math.ceil((root._firstDayOff + root._daysInMonth) / 7))
    readonly property int _cellCount: _rows * 7

    function _dateForCell(idx) {
        var first = new Date(root.currentMonth.getFullYear(),
                             root.currentMonth.getMonth(), 1)
        var d = new Date(first)
        d.setDate(d.getDate() - root._firstDayOff + idx)
        return d
    }
    function _sameDay(a, b) {
        return a.getFullYear() === b.getFullYear() &&
               a.getMonth()   === b.getMonth()    &&
               a.getDate()    === b.getDate()
    }
    function _isoDate(d) {
        // Returns "yyyy-MM-dd" in local time — same format used by taskViewModel.dueDate
        var y  = d.getFullYear()
        var mo = d.getMonth() + 1
        var dd = d.getDate()
        return y + "-" + (mo < 10 ? "0" + mo : mo) + "-" + (dd < 10 ? "0" + dd : dd)
    }

    // ── Per-month task-count cache ────────────────────────────────────────────
    // Maps ISO date string → count of open (non-completed, non-deleted) tasks.
    // Rebuilt when the month changes or the task model changes.
    property var _taskCache: ({})
    property int _cacheKick: 0   // incrementing invalidates all cell bindings

    function _rebuildCache() {
        var cache = {}
        var tasks = dataStore.allTasks()
        var y  = root.currentMonth.getFullYear()
        var mo = root.currentMonth.getMonth()
        // Build ISO prefix for the displayed month: "yyyy-MM"
        var prefix = y + "-" + ((mo + 1) < 10 ? "0" + (mo + 1) : (mo + 1))
        for (var i = 0; i < tasks.length; i++) {
            var t = tasks[i]
            if (t.isCompleted || t.isDeleted) continue
            var due = t.dueDate || ""
            // Also handle time-stamped tasks with no plain dueDate
            if (due === "" && t.dueDatetime) {
                due = t.dueDatetime.substring(0, 10)
            }
            if (due === "") continue
            // Only cache cells in or adjacent to the current month view
            // (adjacent cells from prev/next month are < 7 days away, so
            //  a 1-month window either side is sufficient)
            cache[due] = (cache[due] || 0) + 1
        }
        root._taskCache = cache
        root._cacheKick++  // forces Repeater delegate re-evaluation
    }

    function _countForDate(d) {
        var iso = _isoDate(d)
        return root._taskCache[iso] || 0
    }

    // Iter-night-39: latch any rebuild request that arrived while the
    // panel was invisible. The visibility-guard at the timer's onTriggered
    // (Q3.23) prevents the rebuild storm but also DROPS the rebuild
    // entirely if the user navigates away mid-debounce. The panel itself
    // is a single long-lived QML object whose `visible` toggles based on
    // taskViewModel.filterMode === Upcoming (DesktopLayout.qml:748) —
    // it's NOT mounted via Loader, so Component.onCompleted only fires
    // once at app start. Without latching, the user can edit a task on
    // Inbox/Today/Project view, navigate back to Upcoming, and see the
    // stale day-count dots until either they change the month or another
    // mutation lands while the panel is visible.
    property bool _cacheStale: false

    // Coalescing timer — any number of signals within 50 ms collapses to
    // one _rebuildCache() call. This prevents the double-fire that was
    // causing O(N) full-list scans on every single task mutation:
    // DataStore emits tasksChanged + dataChanged together; taskViewModel
    // reacts to tasksChanged and then emits its own dataChanged + modelReset,
    // so without coalescing each edit triggered 3-4 consecutive rebuilds.
    Timer {
        id: _rebuildTimer
        interval: 50
        repeat:   false
        onTriggered: {
            if (root.visible) {
                root._rebuildCache()
            } else {
                // Iter-night-39: defer the rebuild — onVisibleChanged
                // below will consume the flag when the panel reappears.
                root._cacheStale = true
            }
        }
    }

    // Rebuild on model changes. taskViewModel covers all task mutations
    // (DataStore.tasksChanged → taskViewModel reload → modelReset/dataChanged).
    // We do NOT also listen on dataStore.dataChanged — that would double-fire.
    Connections {
        target: taskViewModel
        function onDataChanged()  { _rebuildTimer.restart() }
        function onRowsInserted() { _rebuildTimer.restart() }
        function onRowsRemoved()  { _rebuildTimer.restart() }
        function onModelReset()   { _rebuildTimer.restart() }
    }
    // Rebuild when month changes and on initial load
    onCurrentMonthChanged: _rebuildCache()
    Component.onCompleted:  _rebuildCache()

    // Iter-night-39: consume the stale-flag when the panel becomes
    // visible again. Single rebuild on transition, not on every
    // mutation that happened while hidden — preserves the Q3.23 anti-
    // storm guarantee.
    onVisibleChanged: {
        if (visible && _cacheStale) {
            _cacheStale = false
            _rebuildCache()
        }
    }

    // ── Main layout ───────────────────────────────────────────────────────────
    ColumnLayout {
        anchors.fill:       parent
        anchors.topMargin:  0
        anchors.leftMargin: widePanel ? 0 : 0
        spacing: 0

        // ── Header: month label + nav arrows (+ collapse chevron in strip mode) ─
        Rectangle {
            Layout.fillWidth: true
            implicitHeight:   36
            color:            root._bg

            // Bottom border
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left:   parent.left
                anchors.right:  parent.right
                height: 1
                color:  root._border
            }

            RowLayout {
                anchors {
                    fill:         parent
                    leftMargin:   8
                    rightMargin:  8
                }
                spacing: 4

                // Collapse toggle (strip mode only)
                // Iter-night-24: было unicode "▼/▶". Migrate to IconSvg so
                // the chevron renders consistently with the rest of the UI
                // and theme-colour changes propagate without manual rebind.
                ToolButton {
                    visible:        !root.widePanel
                    contentItem: IconSvg {
                        anchors.centerIn: parent
                        name:  root.collapsed ? "chevron-right" : "chevron-down"
                        size:  14
                        color: root._dim
                    }
                    Layout.preferredWidth:  24
                    Layout.preferredHeight: 24
                    onClicked: root.collapsed = !root.collapsed
                    ToolTip.text: root.collapsed ? qsTr("Развернуть календарь") : qsTr("Свернуть календарь")
                    ToolTip.visible: hovered
                }

                // Prev month button
                // Iter-night-24: было unicode "◀". chevron-left.svg added
                // in this commit (mirrors chevron-right.svg).
                ToolButton {
                    visible:  !root.collapsed
                    contentItem: IconSvg {
                        anchors.centerIn: parent
                        name:  "chevron-left"
                        size:  14
                        color: root._dim
                    }
                    Layout.preferredWidth:  24
                    Layout.preferredHeight: 24
                    onClicked: root._prevMonth()
                    ToolTip.text:    qsTr("Предыдущий месяц")
                    ToolTip.visible: hovered
                }

                // Month/year label
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    // BUG-FIX (C-1 pattern): use toLocaleDateString to avoid Qt 6.11
                    // TypeError when `property date` materialises as QDateTime.
                    text: root.currentMonth.toLocaleDateString(
                              Qt.locale("ru_RU"), "MMMM yyyy")
                    font.pixelSize: 13
                    font.bold:      true
                    color:          root._text
                    elide:          Text.ElideRight
                }

                // Next month button
                // Iter-night-24: было unicode "▶".
                ToolButton {
                    visible:  !root.collapsed
                    contentItem: IconSvg {
                        anchors.centerIn: parent
                        name:  "chevron-right"
                        size:  14
                        color: root._dim
                    }
                    Layout.preferredWidth:  24
                    Layout.preferredHeight: 24
                    onClicked: root._nextMonth()
                    ToolTip.text:    qsTr("Следующий месяц")
                    ToolTip.visible: hovered
                }

                // "Сегодня" shortcut
                ToolButton {
                    visible:  !root.collapsed
                    text:     qsTr("•")
                    font.pixelSize: 16
                    Material.foreground: root._accent
                    Layout.preferredWidth:  24
                    Layout.preferredHeight: 24
                    onClicked: {
                        var today = new Date()
                        today.setDate(1)
                        root.currentMonth = today
                        root.selectedDay  = new Date()
                    }
                    ToolTip.text:    qsTr("Перейти к сегодня")
                    ToolTip.visible: hovered
                }
            }
        }

        // ── Day-of-week header row ─────────────────────────────────────────────
        Item {
            Layout.fillWidth:  true
            implicitHeight:    20
            visible:           !root.collapsed

            RowLayout {
                anchors.fill: parent
                spacing:      0

                Repeater {
                    model: [qsTr("Пн"), qsTr("Вт"), qsTr("Ср"),
                            qsTr("Чт"), qsTr("Пт"), qsTr("Сб"), qsTr("Вс")]

                    delegate: Item {
                        required property string modelData
                        required property int    index
                        Layout.fillWidth: true
                        implicitHeight:   20

                        Text {
                            anchors.centerIn: parent
                            text:       modelData
                            font.pixelSize: 10
                            font.bold:  true
                            color: (index >= 5) // Sat, Sun
                                ? (root._dark ? "#888" : "#aaa")
                                : root._dim
                        }
                    }
                }
            }

            // Bottom border
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left:   parent.left
                anchors.right:  parent.right
                height: 1
                color:  root._border
                opacity: 0.5
            }
        }

        // ── Calendar grid ──────────────────────────────────────────────────────
        GridLayout {
            id: calGrid
            Layout.fillWidth:  true
            Layout.fillHeight: root.widePanel
            visible:           !root.collapsed

            columns:      7
            rows:         root._rows
            rowSpacing:   1
            columnSpacing: 1

            // Q3.25: cell height MUST come from the panel root (root.height),
            // not from the GridLayout's own `height`.  When `_cellH` reads
            // `calGrid.height` and every cell's Layout.preferredHeight is
            // bound back to `_cellH`, the layout engine sees the GridLayout's
            // implicitHeight (sum of cell preferredHeights) re-derive from the
            // GridLayout's actual height every polish pass — a one-pixel
            // rounding wobble re-triggers updatePolish() forever.  The error
            // log was filling with thousands of "ColumnLayout called polish()
            // inside updatePolish()" + "GridLayout: Detected recursive
            // rearrange. Aborting after two iterations." warnings (2 085 of
            // them per session before this fix), with the Upcoming view
            // burning a full CPU core continuously while visible.
            //
            // `root` is the panel Rectangle; its height is set externally by
            // the UpcomingView container (anchors / Layout.preferredHeight),
            // so reading `root.height` here does NOT feed back into the inner
            // Layout system and the polish chain converges in one pass.
            //
            // 56 = header (36) + weekday-row (20).
            readonly property real _cellH: {
                if (root.widePanel) return Math.max(24, (root.height - 56) / root._rows)
                return Math.max(24, (184 - 2) / root._rows)
            }

            Repeater {
                model: root._cellCount

                delegate: Item {
                    id: cell
                    required property int index

                    Layout.fillWidth:     true
                    Layout.preferredHeight: calGrid._cellH
                    Layout.minimumHeight:   20

                    // Depend on _cacheKick so the cell refreshes when cache updates
                    property int   _kick:      root._cacheKick
                    property date  _date:      root._dateForCell(index)
                    property bool  _curMonth:  _date.getMonth() === root.currentMonth.getMonth() &&
                                               _date.getFullYear() === root.currentMonth.getFullYear()
                    property bool  _today:     root._sameDay(_date, new Date())
                    property bool  _selected:  root._sameDay(_date, root.selectedDay)
                    property int   _count:     { _kick; root._countForDate(_date) }
                    property bool  _weekend:   {
                        var dow = _date.getDay()
                        return dow === 0 || dow === 6
                    }

                    Rectangle {
                        anchors.fill:   parent
                        anchors.margins: 1
                        radius: Radius.sm

                        color: {
                            if (cell._selected && cell._today)
                                return root._accent
                            if (cell._selected)
                                return Qt.rgba(root._accent.r, root._accent.g,
                                               root._accent.b, 0.18)
                            if (cell._today)
                                return Qt.rgba(root._accent.r, root._accent.g,
                                               root._accent.b, 0.12)
                            return "transparent"
                        }

                        // Today border ring
                        border.color: cell._today  ? root._accent : "transparent"
                        border.width: cell._today  ? 1.5 : 0

                        ColumnLayout {
                            anchors.fill:    parent
                            anchors.margins: 2
                            spacing:         1

                            // Day number
                            Text {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                text: cell._date.getDate()
                                font.pixelSize: Math.min(12, calGrid._cellH * 0.40)
                                font.bold:      cell._today
                                color: {
                                    if (cell._selected && cell._today) return "#ffffff"
                                    if (cell._today)   return root._accent
                                    if (!cell._curMonth) return root._dark ? "#555" : "#ccc"
                                    if (cell._weekend)   return root._dark ? "#999" : "#888"
                                    return root._text
                                }
                            }

                            // Task indicator row: dot(s) or count badge
                            Item {
                                Layout.fillWidth:  true
                                Layout.fillHeight: true
                                visible: cell._count > 0 && calGrid._cellH > 22

                                // Small dot for 1–3 tasks, count badge for 4+
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 2
                                    visible: cell._count > 0 && cell._count <= 3

                                    Repeater {
                                        model: cell._count
                                        delegate: Rectangle {
                                            width:  4; height: 4; radius: 2
                                            color: (cell._selected && cell._today)
                                                   ? "#ffffff"
                                                   : root._accent
                                        }
                                    }
                                }

                                // Count badge (4+)
                                Rectangle {
                                    anchors.centerIn: parent
                                    visible:  cell._count > 3
                                    width:    badgeTxt.implicitWidth + 4
                                    height:   12; radius: Radius.md
                                    color: (cell._selected && cell._today)
                                           ? Qt.rgba(1, 1, 1, 0.3)
                                           : root._accent

                                    Text {
                                        id: badgeTxt
                                        anchors.centerIn: parent
                                        text:       cell._count
                                        font.pixelSize: 8
                                        color: (cell._selected && cell._today)
                                               ? "#ffffff"
                                               : "#ffffff"
                                    }
                                }
                            }
                        }

                        // Hover overlay
                        HoverHandler { id: cellHover }
                        Rectangle {
                            anchors.fill: parent
                            radius:       parent.radius
                            color:        root._dark ? "#ffffff" : "#000000"
                            opacity:      cellHover.containsMouse ? 0.05 : 0
                            Behavior on opacity { NumberAnimation { duration: 80 } }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape:  Qt.PointingHandCursor
                            onClicked: {
                                root.selectedDay = cell._date
                                root.dayPicked(cell._date)
                            }
                        }
                    }
                }
            }
        }

        // Spacer so widePanel mode fills remaining height
        Item {
            Layout.fillWidth:  true
            Layout.fillHeight: !root.widePanel
            visible:           !root.widePanel && !root.collapsed
        }
    }
}
