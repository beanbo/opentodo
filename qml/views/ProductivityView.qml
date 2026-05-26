import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtCore
import OpenTodoUI

Item {
    id: root
    anchors.fill: parent
    readonly property bool isDarkTheme: ThemeManager.isDark

    // Color properties
    property color bgColor: ThemeManager.backgroundColor
    property color contentColor: ThemeManager.surfaceColor
    property color borderColor: ThemeManager.borderColor
    property color textPrimary: ThemeManager.textColor
    property color textSecondary: ThemeManager.secondaryTextColor

    // ── Briefing helpers ──────────────────────────────────────────────────────

    // Russian plural: задача / задачи / задач
    function taskWord(n) {
        var m100 = n % 100
        var m10  = n % 10
        if (m100 >= 11 && m100 <= 14) return qsTr("задач")
        if (m10 === 1) return qsTr("задача")
        if (m10 >= 2 && m10 <= 4) return qsTr("задачи")
        return qsTr("задач")
    }

    // Russian plural: очко / очка / очков
    function _milestoneSuffix(n) {
        var m100 = n % 100
        var m10  = n % 10
        if (m100 >= 11 && m100 <= 14) return qsTr("очков")
        if (m10 === 1) return qsTr("очко")
        if (m10 >= 2 && m10 <= 4) return qsTr("очка")
        return qsTr("очков")
    }

    // Q4.01 (redesign): убрано поле `emoji` — оно никогда не рендерилось
    // (downstream использовался только greet.text). Если когда-нибудь
    // потребуется иконка времени суток, она маппится так:
    //   h<5  → moon, h<12 → sun, h<18 → sun, иначе → moon.
    function _greetingByTime() {
        var h = new Date().getHours()
        if (h < 5)  return { text: qsTr("Доброй ночи") }
        if (h < 12) return { text: qsTr("Доброе утро") }
        if (h < 18) return { text: qsTr("Добрый день") }
        return      { text: qsTr("Добрый вечер") }
    }

    function _briefingMessage(kick) {
        // `kick` is read so the binding re-evaluates when the counter changes.
        void kick
        var todayTasks   = dataStore.tasksForToday()
        var overdueTasks = dataStore.overdueTasks()
        var total        = todayTasks.length
        var overdueCount = overdueTasks.length
        var done         = 0
        for (var i = 0; i < todayTasks.length; i++) {
            if (todayTasks[i].isCompleted) done++
        }
        var remaining = total - done
        var greet = _greetingByTime()

        // Q4.01 (redesign): поле emoji → iconName (имя файла в
        // resources/icons/ui/<iconName>.svg). 🎉 freeday → sparkles,
        // 🚨 overdue → warning, 💪 normal → flame. Делегат рендерит
        // IconSvg на месте старого Text-emoji.
        if (total === 0) {
            return { iconName: "sparkles",
                     title: greet.text + "!",
                     body: qsTr("Сегодня свободный день — можно отдохнуть.") }
        }
        if (done === total) {
            return { iconName: "star",
                     title: qsTr("Ты умница!"),
                     body: qsTr("Все задачи на сегодня выполнены — заслуженный отдых.") }
        }
        if (overdueCount > 0) {
            return { iconName: "warning",
                     title: greet.text + "!",
                     body: qsTr("Сегодня %1 %2, из них %3 просрочено. Самое время сосредоточиться.")
                           .arg(remaining).arg(taskWord(remaining)).arg(overdueCount) }
        }
        return { iconName: "flame",
                 title: greet.text + "!",
                 body: qsTr("Сегодня %1 %2. Вперёд — ты справишься!")
                       .arg(remaining).arg(taskWord(remaining)) }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: Math.min(parent.width, 600)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Spacing.lg

            // Q4.01 pixel-perfect: view-header — icon 22 + title 20/600 как у Settings/Notifications/AI.
            RowLayout {
                Layout.topMargin: Spacing.lg
                spacing: 12
                IconSvg {
                    name: "productivity"
                    size: 22
                    color: textPrimary
                    Layout.alignment: Qt.AlignVCenter
                }
                Label {
                    text: qsTr("Продуктивность")
                    font.pixelSize: FontSize.viewHeader
                    font.weight: Font.DemiBold
                    color: textPrimary
                }
                Item { Layout.fillWidth: true }
            }

            // === Pomodoro Timer Card ===
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: pomodoroCol.implicitHeight + 32
                color: contentColor
                radius: 10
                border.color: borderColor
                border.width: 1

                ColumnLayout {
                    id: pomodoroCol
                    anchors.fill: parent
                    anchors.margins: Spacing.md
                    spacing: 10

                    // Header row: title + daily counter
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Spacing.sm

                        // Q4.01 (redesign): "🍅" префикс убран — заголовок
                        // секции уже опознавателен в контексте Productivity.
                        Text {
                            text: qsTr("Помодоро")
                            font.pixelSize: FontSize.body
                            font.weight: Font.DemiBold
                            color: textPrimary
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            visible: pomodoroService.completedCyclesToday > 0
                            text: qsTr("Сегодня: %1").arg(pomodoroService.completedCyclesToday)
                            color: textSecondary
                            font.pixelSize: FontSize.small
                        }
                    }

                    // Big countdown
                    // Q4.01 pixel-perfect: design-canvas wants 56 px Light with
                    // tabular-nums + letterSpacing -2 for the mm:ss readout.
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        font.pixelSize: 56
                        font.weight: Font.Light
                        font.letterSpacing: -2
                        color: pomodoroService.phase === "break" ? "#2e8b57" : ThemeManager.accentColor
                        text: {
                            var sec = pomodoroService.remainingSeconds
                            var m = Math.floor(sec / 60)
                            var s = sec % 60
                            return m + ":" + (s < 10 ? "0" : "") + s
                        }
                    }

                    // Phase label
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        font.pixelSize: FontSize.body
                        color: textSecondary
                        text: {
                            if (pomodoroService.phase === "work")  return qsTr("Работа")
                            if (pomodoroService.phase === "break") return qsTr("Перерыв")
                            return qsTr("Готов к старту")
                        }
                    }

                    // Progress bar (remaining / cycle length).
                    // Design wants an EMPTY bar in "idle" state ("Готов к старту"):
                    // it represents elapsed time, not time available. When idle we
                    // force 0; during work/break we map remainingSeconds/cycleSeconds
                    // → reverse to show how much has been used.
                    Rectangle {
                        Layout.fillWidth: true
                        height: Spacing.xs
                        radius: Radius.xs
                        color: isDarkTheme ? "#3a3a3a" : "#e0e0e0"
                        Rectangle {
                            width: {
                                if (pomodoroService.phase === "idle") return 0
                                if (pomodoroService.cycleSeconds <= 0) return 0
                                const elapsed = pomodoroService.cycleSeconds - pomodoroService.remainingSeconds
                                return parent.width * (elapsed / pomodoroService.cycleSeconds)
                            }
                            height: parent.height
                            radius: Radius.xs
                            color: pomodoroService.phase === "break" ? "#2e8b57" : ThemeManager.accentColor
                            Behavior on width { NumberAnimation { duration: 800 } }
                        }
                    }

                    // Control buttons
                    // Q4.01 pixel-perfect: design-canvas wants pill buttons —
                    // radius 16, padding 7 18 (primary) / 7 14 (secondary),
                    // fontSize 13, primary filled with accent, secondaries
                    // outlined with border. We override the Material `background`
                    // and content paddings rather than swap to bare Rectangles
                    // so click ripple + focus still work.
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: Spacing.sm

                        Button {
                            id: pomodoroPrimaryBtn
                            text: pomodoroService.running
                                ? qsTr("Пауза")
                                : (pomodoroService.phase === "idle" ? qsTr("Старт") : qsTr("Продолжить"))
                            font.pixelSize: FontSize.bodySmall
                            leftPadding: 18
                            rightPadding: 18
                            topPadding: 7
                            bottomPadding: 7
                            contentItem: Text {
                                text: pomodoroPrimaryBtn.text
                                font: pomodoroPrimaryBtn.font
                                color: "white"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                radius: 16
                                color: pomodoroPrimaryBtn.down
                                       ? Qt.darker(ThemeManager.accentColor, 1.15)
                                       : ThemeManager.accentColor
                                opacity: pomodoroPrimaryBtn.enabled ? 1.0 : 0.55
                            }
                            onClicked: {
                                if (pomodoroService.running)         pomodoroService.pause()
                                else if (pomodoroService.phase === "idle") pomodoroService.start()
                                else                                 pomodoroService.resume()
                            }
                        }
                        Button {
                            id: pomodoroSkipBtn
                            text: qsTr("Пропустить")
                            enabled: pomodoroService.phase !== "idle"
                            font.pixelSize: FontSize.bodySmall
                            leftPadding: 14
                            rightPadding: 14
                            topPadding: 7
                            bottomPadding: 7
                            contentItem: Text {
                                text: pomodoroSkipBtn.text
                                font: pomodoroSkipBtn.font
                                color: textPrimary
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                radius: 16
                                color: pomodoroSkipBtn.down
                                       ? Qt.rgba(textPrimary.r, textPrimary.g, textPrimary.b, 0.08)
                                       : "transparent"
                                border.color: borderColor
                                border.width: 1
                                opacity: pomodoroSkipBtn.enabled ? 1.0 : 0.55
                            }
                            onClicked: pomodoroService.skip()
                        }
                        Button {
                            id: pomodoroResetBtn
                            text: qsTr("Сброс")
                            enabled: pomodoroService.phase !== "idle"
                            font.pixelSize: FontSize.bodySmall
                            leftPadding: 14
                            rightPadding: 14
                            topPadding: 7
                            bottomPadding: 7
                            contentItem: Text {
                                text: pomodoroResetBtn.text
                                font: pomodoroResetBtn.font
                                color: textPrimary
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                radius: 16
                                color: pomodoroResetBtn.down
                                       ? Qt.rgba(textPrimary.r, textPrimary.g, textPrimary.b, 0.08)
                                       : "transparent"
                                border.color: borderColor
                                border.width: 1
                                opacity: pomodoroResetBtn.enabled ? 1.0 : 0.55
                            }
                            onClicked: pomodoroService.reset()
                        }
                    }
                }
            }

            // === Pomodoro Statistics Card ===
            // PomodoroService only persists today's count under
            // `pomodoro/completed_count` + `pomodoro/completed_date` (see
            // src/services/pomodoroservice.cpp). To reach week / total / 7-day
            // history we maintain our own JSON map of {ISODate: count} via
            // Qt.labs.settings, kept in sync with the service's today counter.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: pomodoroStatsCol.implicitHeight + 32
                color: contentColor
                radius: Radius.lg
                border.color: borderColor
                border.width: 1

                // Persisted per-day history. JSON object: {"2026-05-08": 3, ...}
                Settings {
                    id: pomodoroStatsStore
                    property string history: "{}"
                    property int total: 0
                    // Last seen "today" count, so we can detect monotonic
                    // increments emitted by PomodoroService.
                    property string lastSeenDate: ""
                    property int lastSeenCount: 0
                }

                ColumnLayout {
                    id: pomodoroStatsCol
                    anchors.fill: parent
                    anchors.margins: Spacing.md
                    spacing: 12

                    // ── Reactive state ─────────────────────────────────────
                    property int _kick: 0
                    property var _stats: _computeStats()

                    // Today's date in ISO form. Re-read by tickTimer in case
                    // the user keeps the app open across midnight.
                    property string _todayIso: Qt.formatDate(new Date(), "yyyy-MM-dd")

                    // Russian abbreviated day-of-week labels, Mon-first.
                    readonly property var _dayLabels: [qsTr("Пн"), qsTr("Вт"), qsTr("Ср"), qsTr("Чт"), qsTr("Пт"), qsTr("Сб"), qsTr("Вс")]

                    // Last 7 ISO dates, oldest → today.
                    function _last7Dates() {
                        var arr = []
                        var now = new Date()
                        for (var i = 6; i >= 0; i--) {
                            var d = new Date(now.getFullYear(), now.getMonth(), now.getDate() - i)
                            arr.push(Qt.formatDate(d, "yyyy-MM-dd"))
                        }
                        return arr
                    }

                    // Day-of-week label for an ISO date string. Mon-first, so
                    // JS Sunday(0) → 6, others shift by -1.
                    function _dowLabel(iso) {
                        var d = Date.fromLocaleString(Qt.locale("en_US"), iso, "yyyy-MM-dd")
                        if (isNaN(d.getTime())) d = new Date(iso)
                        var jsDow = d.getDay()           // 0=Sun..6=Sat
                        var monIdx = (jsDow + 6) % 7      // 0=Mon..6=Sun
                        return _dayLabels[monIdx]
                    }

                    function _parseHistory() {
                        try {
                            var obj = JSON.parse(pomodoroStatsStore.history || "{}")
                            return (obj && typeof obj === "object") ? obj : {}
                        } catch (e) {
                            return {}
                        }
                    }

                    // Build {today, week, total, daily[7]} from stored history.
                    function _computeStats() {
                        void _kick   // reactive dependency
                        var hist = _parseHistory()
                        var dates = _last7Dates()
                        var daily = []
                        var weekSum = 0
                        for (var i = 0; i < dates.length; i++) {
                            var c = hist[dates[i]] | 0
                            daily.push({ date: dates[i], count: c, label: _dowLabel(dates[i]) })
                            weekSum += c
                        }
                        var todayIso = dates[dates.length - 1]
                        // Today's authoritative count comes from the service,
                        // not the JSON map (which only catches up on increments
                        // we observed in the current session).
                        var todayCount = pomodoroService
                            ? pomodoroService.completedCyclesToday | 0
                            : (hist[todayIso] | 0)
                        // Patch today's value so the chart reflects the live
                        // service count even before the next save.
                        daily[daily.length - 1].count = todayCount
                        weekSum = weekSum - (hist[todayIso] | 0) + todayCount
                        return {
                            today: todayCount,
                            week:  weekSum,
                            total: pomodoroStatsStore.total,
                            daily: daily
                        }
                    }

                    // Sync the persisted map with the service's today counter.
                    // Service only resets to 1 (date rollover) or increments by
                    // 1 (work cycle complete); we mirror that into total + map.
                    function _onCountChanged() {
                        if (!pomodoroService) return
                        var n = pomodoroService.completedCyclesToday | 0
                        var todayIso = Qt.formatDate(new Date(), "yyyy-MM-dd")
                        var prevDate  = pomodoroStatsStore.lastSeenDate
                        var prevCount = pomodoroStatsStore.lastSeenCount | 0

                        var delta = 0
                        if (prevDate === todayIso) {
                            // Same day — only count strictly positive deltas.
                            delta = Math.max(0, n - prevCount)
                        } else {
                            // New day or first-ever observation.
                            // The full `n` represents new completions today.
                            delta = Math.max(0, n)
                        }

                        if (delta > 0) {
                            var hist = _parseHistory()
                            hist[todayIso] = (hist[todayIso] | 0) + delta
                            // Trim entries older than ~60 days to keep the
                            // QSettings string from growing unbounded.
                            var cutoff = new Date()
                            cutoff.setDate(cutoff.getDate() - 60)
                            for (var k in hist) {
                                // BUG-FIX (C-4): keys are "yyyy-MM-dd" pomodoro day
                                // markers. Bare ISO date parses as UTC midnight in JS,
                                // so for UTC- users the day shifts and a key could be
                                // pruned ~24h early near the cutoff boundary. Append
                                // "T00:00:00" so the comparison happens in local time.
                                var kd = new Date(k + "T00:00:00")
                                if (!isNaN(kd.getTime()) && kd < cutoff) {
                                    delete hist[k]
                                }
                            }
                            pomodoroStatsStore.history = JSON.stringify(hist)
                            pomodoroStatsStore.total   = (pomodoroStatsStore.total | 0) + delta
                        }

                        pomodoroStatsStore.lastSeenDate  = todayIso
                        pomodoroStatsStore.lastSeenCount = n

                        _kick = (_kick + 1) % 1000000
                        _stats = _computeStats()
                    }

                    // Force re-evaluation without rewriting persistent state
                    // (used when only the view-side date / display needs to
                    // refresh).
                    function _refresh() {
                        _todayIso = Qt.formatDate(new Date(), "yyyy-MM-dd")
                        _kick = (_kick + 1) % 1000000
                        _stats = _computeStats()
                    }

                    Component.onCompleted: {
                        // First run: ensure the lastSeen* baseline is set so
                        // we don't double-count at startup.
                        if (pomodoroService) {
                            var n = pomodoroService.completedCyclesToday | 0
                            var todayIso = Qt.formatDate(new Date(), "yyyy-MM-dd")
                            if (pomodoroStatsStore.lastSeenDate === "") {
                                // Seed today's history with the service value
                                // and total (best-effort recovery; previous
                                // sessions' totals are unknown).
                                var hist = _parseHistory()
                                if ((hist[todayIso] | 0) < n) {
                                    var delta = n - (hist[todayIso] | 0)
                                    hist[todayIso] = n
                                    pomodoroStatsStore.history = JSON.stringify(hist)
                                    pomodoroStatsStore.total = (pomodoroStatsStore.total | 0) + delta
                                }
                                pomodoroStatsStore.lastSeenDate  = todayIso
                                pomodoroStatsStore.lastSeenCount = n
                            } else {
                                _onCountChanged()
                            }
                        }
                        _refresh()
                    }

                    Connections {
                        target: pomodoroService
                        function onCompletedCyclesTodayChanged() {
                            pomodoroStatsCol._onCountChanged()
                        }
                    }

                    // Periodic refresh: catches midnight rollover and keeps
                    // the chart in sync if the user leaves the app open.
                    Timer {
                        id: tickTimer
                        interval: 60 * 1000
                        running: true
                        repeat: true
                        onTriggered: pomodoroStatsCol._refresh()
                    }

                    // ── Header ─────────────────────────────────────────────
                    // Q4.01 (redesign): "🍅" префикс убран.
                    Text {
                        text: qsTr("Pomodoro статистика")
                        font.pixelSize: FontSize.body
                        font.weight: Font.DemiBold
                        color: textPrimary
                    }

                    // ── 3 stat tiles ───────────────────────────────────────
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Repeater {
                            model: [
                                { label: qsTr("Сегодня"),   value: pomodoroStatsCol._stats.today },
                                { label: qsTr("За неделю"), value: pomodoroStatsCol._stats.week },
                                { label: qsTr("Всего"),     value: pomodoroStatsCol._stats.total }
                            ]
                            // Q4.01 pixel-perfect: design-canvas wants tiles on
                            // the bg surface (not accent-tint), radius 8, 24px
                            // DemiBold number + 11px caption, padding 14/8.
                            delegate: Rectangle {
                                Layout.fillWidth: true
                                radius: Radius.lg
                                color: ThemeManager.backgroundColor
                                border.color: borderColor
                                border.width: 1
                                Layout.preferredHeight: tileCol.implicitHeight + 28

                                ColumnLayout {
                                    id: tileCol
                                    anchors.centerIn: parent
                                    width: parent.width - 16
                                    spacing: 2

                                    Text {
                                        text: modelData.value
                                        font.pixelSize: 24
                                        font.weight: Font.DemiBold
                                        color: textPrimary
                                        horizontalAlignment: Text.AlignHCenter
                                        Layout.alignment: Qt.AlignHCenter
                                    }
                                    Text {
                                        text: modelData.label
                                        font.pixelSize: FontSize.caption
                                        color: textSecondary
                                        horizontalAlignment: Text.AlignHCenter
                                        Layout.alignment: Qt.AlignHCenter
                                        Layout.topMargin: 2
                                    }
                                }
                            }
                        }
                    }

                    // ── Empty state OR mini-bar chart ──────────────────────
                    readonly property bool _isEmpty:
                        pomodoroStatsCol._stats.today === 0 &&
                        pomodoroStatsCol._stats.week  === 0

                    Text {
                        visible: pomodoroStatsCol._isEmpty
                        text: qsTr("Запусти первый Pomodoro в селе")
                        color: textSecondary
                        font.pixelSize: FontSize.small
                        font.italic: true
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        Layout.bottomMargin: 6
                    }

                    // 7-day mini bar chart
                    ColumnLayout {
                        id: chartColumn
                        visible: !pomodoroStatsCol._isEmpty
                        Layout.fillWidth: true
                        Layout.topMargin: Spacing.xs
                        spacing: Spacing.xs

                        readonly property int _maxCount: {
                            var m = 0
                            var d = pomodoroStatsCol._stats.daily
                            for (var i = 0; i < d.length; i++) {
                                if (d[i].count > m) m = d[i].count
                            }
                            return Math.max(1, m)
                        }

                        // Bars
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 80
                            spacing: 6

                            Repeater {
                                model: pomodoroStatsCol._stats.daily

                                delegate: ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    spacing: 2

                                    // Spacer + bar = vertical alignment to the bottom
                                    Item { Layout.fillHeight: true; Layout.fillWidth: true }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: {
                                            var ratio = modelData.count / chartColumn._maxCount
                                            // Always show a faint stub so empty
                                            // bars remain visible on the surface.
                                            return Math.max(2, Math.round(74 * ratio))
                                        }
                                        radius: Radius.xs
                                        color: modelData.count > 0
                                               ? ThemeManager.accentColor
                                               : Qt.rgba(textPrimary.r, textPrimary.g, textPrimary.b, 0.10)
                                        border.color: borderColor
                                        border.width: modelData.count > 0 ? 0 : 1

                                        Behavior on Layout.preferredHeight {
                                            NumberAnimation { duration: 300; easing.type: Easing.OutQuad }
                                        }

                                        // Numeric label on top of bars with count > 0
                                        Text {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            anchors.bottom: parent.top
                                            anchors.bottomMargin: 1
                                            visible: modelData.count > 0
                                            text: modelData.count
                                            font.pixelSize: 9
                                            font.bold: true
                                            color: textPrimary
                                        }
                                    }
                                }
                            }
                        }

                        // Day labels (Пн / Вт / ...) aligned under each bar
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Repeater {
                                model: pomodoroStatsCol._stats.daily

                                delegate: Text {
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    font.pixelSize: 10
                                    color: textSecondary
                                    horizontalAlignment: Text.AlignHCenter
                                }
                            }
                        }
                    }
                }
            }

            // === Per-Project Completion Rate Card ===
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: projectChartCol.implicitHeight + 32
                color: contentColor
                radius: Radius.lg
                border.color: borderColor
                border.width: 1

                ColumnLayout {
                    id: projectChartCol
                    anchors.fill: parent
                    anchors.margins: Spacing.md
                    spacing: 12

                    // Cached chart data: [{ id, name, icon, color, total, completed, ratio }]
                    property var _projectChartData: []

                    function _rebuildProjectChart() {
                        if (!dataStore) {
                            _projectChartData = []
                            return
                        }
                        var tasks = dataStore.allTasks()
                        // Aggregate per-project totals & completion counts.
                        var agg = ({}) // { projectId: { total, completed } }
                        for (var i = 0; i < tasks.length; i++) {
                            var t = tasks[i]
                            if (!t || t.isDeleted) continue
                            var pid = t.projectId
                            if (!pid || pid === "") continue
                            if (!agg[pid]) agg[pid] = { total: 0, completed: 0 }
                            agg[pid].total += 1
                            if (t.isCompleted) agg[pid].completed += 1
                        }
                        // Resolve project metadata and filter projects with >0 tasks.
                        var rows = []
                        for (var pid2 in agg) {
                            var rec = agg[pid2]
                            if (rec.total <= 0) continue
                            var p = dataStore.projectById(pid2)
                            if (!p) continue
                            // Skip soft-deleted/archived projects defensively.
                            if (p.isDeleted || p.isArchived) continue
                            rows.push({
                                id:        pid2,
                                name:      p.name || "",
                                icon:      p.icon || "",
                                color:     (p.color && p.color !== "" && p.color !== "charcoal" && p.color !== "grey")
                                           ? p.color : ThemeManager.accentColor,
                                total:     rec.total,
                                completed: rec.completed,
                                ratio:     rec.total > 0 ? (rec.completed / rec.total) : 0
                            })
                        }
                        // Sort by total desc, then take top 8.
                        rows.sort(function (a, b) { return b.total - a.total })
                        if (rows.length > 8) rows = rows.slice(0, 8)
                        _projectChartData = rows
                    }

                    function _scheduleRebuild() {
                        Qt.callLater(_rebuildProjectChart)
                    }

                    Component.onCompleted: _scheduleRebuild()

                    Connections {
                        target: dataStore
                        function onTasksChanged()    { projectChartCol._scheduleRebuild() }
                        function onProjectsChanged() { projectChartCol._scheduleRebuild() }
                    }

                    // ── Header ─────────────────────────────────────────────
                    // Q4.01 (redesign): "📊" заменён на IconSvg "productivity"
                    // из stroke-пака. Чат-задача — никаких эмодзи в заголовках
                    // разделов.
                    RowLayout {
                        spacing: Spacing.sm

                        IconSvg {
                            name: "productivity"
                            size: 16
                            color: textPrimary
                            Layout.alignment: Qt.AlignVCenter
                        }

                        Text {
                            text: qsTr("Прогресс по проектам")
                            font.pixelSize: FontSize.body
                            font.weight: Font.DemiBold
                            color: textPrimary
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }
                    Text {
                        text: qsTr("Доля выполненных задач")
                        font.pixelSize: FontSize.small
                        color: textSecondary
                        Layout.bottomMargin: Spacing.xs
                    }

                    // ── Empty state ────────────────────────────────────────
                    Text {
                        visible: projectChartCol._projectChartData.length === 0
                        text: qsTr("Нет проектов с задачами")
                        color: textSecondary
                        font.pixelSize: FontSize.small
                        font.italic: true
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        Layout.bottomMargin: 6
                    }

                    // ── Per-project rows ───────────────────────────────────
                    Repeater {
                        model: projectChartCol._projectChartData

                        delegate: RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            // Project icon emoji (or accent dot fallback)
                            Text {
                                text: modelData.icon && modelData.icon !== ""
                                      ? modelData.icon : "•"
                                font.pixelSize: 16
                                color: modelData.icon && modelData.icon !== ""
                                       ? textPrimary : modelData.color
                                Layout.preferredWidth: 20
                                horizontalAlignment: Text.AlignHCenter
                            }

                            // Project name (elided)
                            Text {
                                text: modelData.name
                                font.pixelSize: FontSize.bodySmall
                                color: textPrimary
                                elide: Text.ElideRight
                                Layout.preferredWidth: 110
                                Layout.maximumWidth: 110
                            }

                            // "N из M"
                            Text {
                                text: qsTr("%1 из %2").arg(modelData.completed).arg(modelData.total)
                                font.pixelSize: FontSize.caption
                                color: textSecondary
                                horizontalAlignment: Text.AlignRight
                                Layout.preferredWidth: 60
                            }

                            // Progress bar (fills 60% of remaining width)
                            Rectangle {
                                id: barTrack
                                Layout.fillWidth: true
                                Layout.preferredHeight: 9
                                radius: Radius.sm
                                color: Qt.rgba(ThemeManager.accentColor.r,
                                               ThemeManager.accentColor.g,
                                               ThemeManager.accentColor.b,
                                               0.12)
                                border.width: 0

                                Rectangle {
                                    id: barFill
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    radius: parent.radius
                                    color: modelData.color
                                    width: parent.width * Math.max(0, Math.min(1, modelData.ratio))

                                    Behavior on width {
                                        NumberAnimation { duration: 450; easing.type: Easing.OutQuad }
                                    }
                                }
                            }

                            // Percent text
                            Text {
                                text: Math.round(modelData.ratio * 100) + "%"
                                font.pixelSize: FontSize.small
                                font.bold: true
                                color: textPrimary
                                Layout.preferredWidth: 38
                                horizontalAlignment: Text.AlignRight
                            }
                        }
                    }
                }
            }

            // === Morning Briefing Card ===
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: briefingCol.implicitHeight + 32
                color: contentColor
                radius: 10

                // Accent stripe on the leading edge
                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: Spacing.xs
                    radius: parent.radius
                    color: ThemeManager.accentColor
                }

                ColumnLayout {
                    id: briefingCol
                    anchors.fill: parent
                    anchors.margins: Spacing.md
                    spacing: 6

                    // Kick counter — incremented by Connections to force re-evaluation.
                    property int _msgKick: 0
                    readonly property var _msg: _briefingMessage(_msgKick)

                    RowLayout {
                        spacing: 10

                        // Q4.01 (redesign): emoji-glyph (🎉 / 🚨 / 💪 / 🏆)
                        // заменён на IconSvg из stroke-пака; цвет — accent.
                        IconSvg {
                            name: briefingCol._msg.iconName || "list"
                            size: 28
                            color: ThemeManager.accentColor
                            Layout.alignment: Qt.AlignVCenter
                        }
                        Text {
                            text: briefingCol._msg.title
                            font.pixelSize: 18
                            font.weight: Font.DemiBold
                            color: textPrimary
                        }
                    }

                    Text {
                        text: briefingCol._msg.body
                        color: textSecondary
                        font.pixelSize: FontSize.body
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                // Refresh when any task data changes.
                Connections {
                    target: dataStore
                    function onTasksChanged() {
                        briefingCol._msgKick = (briefingCol._msgKick + 1) % 1000
                    }
                }
            }

            // === Karma Score Hero ===
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: karmaHeroCol.implicitHeight + 32
                radius: 16
                // ITER1 #21: use theme accent instead of hardcoded red
                gradient: Gradient {
                    GradientStop { position: 0.0; color: ThemeManager.accentColor }
                    GradientStop { position: 1.0; color: Qt.darker(ThemeManager.accentColor, 1.2) }
                }

                ColumnLayout {
                    id: karmaHeroCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Spacing.md
                    anchors.topMargin: Spacing.md
                    spacing: Spacing.xs

                    Text {
                        text: karmaViewModel.karmaScore.toString()
                        font.pixelSize: 48
                        font.weight: Font.DemiBold
                        color: "white"
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Text {
                        text: qsTr("Продуктивность · %1").arg(karmaViewModel.karmaLevel)
                        font.pixelSize: 16
                        color: Qt.rgba(1, 1, 1, 0.85)
                        Layout.alignment: Qt.AlignHCenter
                    }

                    // ── Milestone progress bar ──────────────────────────────
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: karmaViewModel.nextMilestone > 0
                        spacing: Spacing.xs
                        Layout.topMargin: Spacing.sm

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: qsTr("До %1 очков:").arg(karmaViewModel.nextMilestone)
                                font.pixelSize: FontSize.small
                                color: Qt.rgba(1, 1, 1, 0.75)
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                readonly property int _remaining: karmaViewModel.nextMilestone - karmaViewModel.karmaScore
                                text: qsTr("%1 %2").arg(_remaining).arg(_milestoneSuffix(_remaining))
                                font.pixelSize: FontSize.small
                                font.bold: true
                                color: "white"
                            }
                        }

                        // Progress bar track
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 6
                            radius: 3
                            color: Qt.rgba(0, 0, 0, 0.20)

                            // Filled portion
                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                radius: parent.radius

                                readonly property int _prev: karmaViewModel.previousMilestone
                                readonly property int _next: karmaViewModel.nextMilestone
                                readonly property int _cur:  karmaViewModel.karmaScore

                                width: {
                                    if (_next <= _prev) return 0
                                    var ratio = Math.max(0, Math.min(1, (_cur - _prev) / (_next - _prev)))
                                    return parent.width * ratio
                                }
                                color: "white"
                                opacity: 0.90
                                Behavior on width { NumberAnimation { duration: 600; easing.type: Easing.OutQuad } }
                            }
                        }
                    }
                }
            }

            // === Daily & Weekly Progress ===
            RowLayout {
                Layout.fillWidth: true
                spacing: Spacing.md

                // Daily card
                Rectangle {
                    Layout.fillWidth: true
                    height: 120
                    radius: 10
                    color: contentColor
                    border.color: borderColor
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Spacing.md
                        spacing: Spacing.sm

                        Text {
                            text: qsTr("Прогресс за день")
                            font.pixelSize: FontSize.body
                            font.weight: Font.DemiBold
                            color: textPrimary
                        }

                        Text {
                            text: karmaViewModel.completedToday + " / " + karmaViewModel.dailyGoal
                            font.pixelSize: 28
                            font.weight: Font.DemiBold
                            color: karmaViewModel.dailyGoalMet ? "#4caf50" : textPrimary
                        }

                        // Progress bar
                        Rectangle {
                            Layout.fillWidth: true
                            height: 6
                            radius: 3
                            color: isDarkTheme ? "#3a3a3a" : "#e0e0e0"
                            Rectangle {
                                width: parent.width * Math.min(karmaViewModel.dailyProgress, 1.0)
                                height: parent.height
                                radius: 3
                                color: karmaViewModel.dailyGoalMet ? "#4caf50" : "#db4c3f"
                                Behavior on width { NumberAnimation { duration: 300 } }
                            }
                        }
                    }
                }

                // Weekly card
                Rectangle {
                    Layout.fillWidth: true
                    height: 120
                    radius: 10
                    color: contentColor
                    border.color: borderColor
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Spacing.md
                        spacing: Spacing.sm

                        Text {
                            text: qsTr("Прогресс за неделю")
                            font.pixelSize: FontSize.body
                            font.weight: Font.DemiBold
                            color: textPrimary
                        }

                        Text {
                            text: karmaViewModel.completedThisWeek + " / " + karmaViewModel.weeklyGoal
                            font.pixelSize: 28
                            font.weight: Font.DemiBold
                            color: karmaViewModel.weeklyGoalMet ? "#4caf50" : textPrimary
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 6
                            radius: 3
                            color: isDarkTheme ? "#3a3a3a" : "#e0e0e0"
                            Rectangle {
                                width: parent.width * Math.min(karmaViewModel.weeklyProgress, 1.0)
                                height: parent.height
                                radius: 3
                                color: karmaViewModel.weeklyGoalMet ? "#4caf50" : "#db4c3f"
                                Behavior on width { NumberAnimation { duration: 300 } }
                            }
                        }
                    }
                }
            }

            // === Streaks ===
            RowLayout {
                Layout.fillWidth: true
                spacing: Spacing.md

                // Daily streak
                Rectangle {
                    Layout.fillWidth: true
                    height: 80
                    radius: 10
                    color: contentColor
                    border.color: borderColor
                    border.width: 1

                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 12

                        // Q4.01 (redesign): "\uD83D\uDD25" emoji \u2192 stroke flame IconSvg
                        IconSvg {
                            name: "flame"
                            size: 28
                            color: ThemeManager.errorColor
                        }

                        ColumnLayout {
                            spacing: 2
                            Text {
                                text: karmaViewModel.dailyStreak.toString()
                                font.pixelSize: 24
                                font.weight: Font.DemiBold
                                color: textPrimary
                            }
                            Text {
                                text: qsTr("дн. подряд")
                                font.pixelSize: FontSize.small
                                color: textSecondary
                            }
                        }
                    }
                }

                // Weekly streak
                Rectangle {
                    Layout.fillWidth: true
                    height: 80
                    radius: 10
                    color: contentColor
                    border.color: borderColor
                    border.width: 1

                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 12

                        // Q4.01 (redesign): unicode star → stroke IconSvg "star"
                        IconSvg {
                            name: "star"
                            size: 28
                            color: "#f7c92f"
                        }

                        ColumnLayout {
                            spacing: 2
                            Text {
                                text: karmaViewModel.weeklyStreak.toString()
                                font.pixelSize: 24
                                font.weight: Font.DemiBold
                                color: textPrimary
                            }
                            Text {
                                text: qsTr("нед. подряд")
                                font.pixelSize: FontSize.small
                                color: textSecondary
                            }
                        }
                    }
                }
            }

            // === Karma Trend Chart (Canvas) ===
            Rectangle {
                Layout.fillWidth: true
                height: 220
                radius: 10
                color: contentColor
                border.color: borderColor
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Spacing.md
                    spacing: Spacing.sm

                    Text {
                        text: qsTr("Динамика продуктивности (30 дней)")
                        font.pixelSize: FontSize.body
                        font.weight: Font.DemiBold
                        color: textPrimary
                    }

                    Canvas {
                        id: trendCanvas
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        property bool darkTheme: isDarkTheme

                        onDarkThemeChanged: requestPaint()

                        onPaint: {
                            var ctx = getContext("2d")
                            var w = width
                            var h = height
                            ctx.clearRect(0, 0, w, h)

                            var data = karmaViewModel.karmaTrend
                            if (!data || data.length === 0) return

                            // Find min/max
                            var scores = []
                            for (var i = 0; i < data.length; i++) {
                                scores.push(data[i].score || 0)
                            }
                            var minVal = Math.min.apply(null, scores)
                            var maxVal = Math.max.apply(null, scores)
                            if (maxVal === minVal) {
                                minVal -= 10
                                maxVal += 10
                            }

                            var padding = 8
                            var chartW = w - padding * 2
                            var chartH = h - padding * 2

                            // Draw grid lines
                            ctx.strokeStyle = darkTheme ? "#363636" : "#e0e0e0"
                            ctx.lineWidth = 0.5
                            for (var g = 0; g < 4; g++) {
                                var gy = padding + (chartH / 3) * g
                                ctx.beginPath()
                                ctx.moveTo(padding, gy)
                                ctx.lineTo(w - padding, gy)
                                ctx.stroke()
                            }

                            // Draw line
                            ctx.strokeStyle = "#db4c3f"
                            ctx.lineWidth = 2
                            ctx.lineJoin = "round"
                            ctx.beginPath()

                            for (var j = 0; j < data.length; j++) {
                                var x = padding + (data.length > 1 ? (chartW / (data.length - 1)) * j : chartW / 2)
                                var y = padding + chartH - ((scores[j] - minVal) / (maxVal - minVal)) * chartH
                                if (j === 0) {
                                    ctx.moveTo(x, y)
                                } else {
                                    ctx.lineTo(x, y)
                                }
                            }
                            ctx.stroke()

                            // Fill area under the line
                            var lastX = padding + (data.length > 1 ? chartW : chartW / 2)
                            ctx.lineTo(lastX, padding + chartH)
                            ctx.lineTo(padding, padding + chartH)
                            ctx.closePath()
                            ctx.fillStyle = "rgba(219, 76, 63, 0.1)"
                            ctx.fill()
                        }

                        Connections {
                            target: karmaViewModel
                            function onKarmaScoreChanged() { trendCanvas.requestPaint() }
                        }
                    }
                }
            }

            // === Activity Heatmap ===
            Rectangle {
                Layout.fillWidth: true
                radius: 10
                color: contentColor
                border.color: borderColor
                border.width: 1
                implicitHeight: heatmapCol.implicitHeight + 32

                ColumnLayout {
                    id: heatmapCol
                    anchors.fill: parent
                    anchors.margins: Spacing.md
                    spacing: 12

                    Text {
                        text: qsTr("Активность за 90 дней")
                        font.pixelSize: FontSize.body
                        font.weight: Font.DemiBold
                        color: textPrimary
                    }

                    // Day-of-week labels (Пн–Вс) aligned above the grid columns
                    // The grid flows column-major (column = week, row = day-of-week)
                    // so we place row labels to the left instead.
                    Row {
                        spacing: 0

                        // Left gutter for row labels
                        Column {
                            spacing: 3
                            Repeater {
                                model: [qsTr("Пн"), qsTr("Вт"), qsTr("Ср"), qsTr("Чт"), qsTr("Пт"), qsTr("Сб"), qsTr("Вс")]
                                delegate: Text {
                                    width: 20
                                    height: 14
                                    text: modelData
                                    font.pixelSize: 9
                                    color: textSecondary
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }

                        // The heatmap grid: 13 columns (weeks) × 7 rows (days Mon–Sun).
                        // data[0] = 89 days ago, data[89] = today.
                        // We need to figure out which column/row each index falls into,
                        // anchoring column 12 row (todayDow-1) to today.
                        Grid {
                            id: heatmapGrid
                            columns: 13
                            rows: 7
                            rowSpacing: 3
                            columnSpacing: 3
                            flow: Grid.TopToBottom   // fill each column top-to-bottom

                            property var heatData: []
                            // todayDow: 0=Mon … 6=Sun (matches our Mon-first grid rows)
                            property int todayDow: (new Date().getDay() + 6) % 7

                            // Total cells = 13 * 7 = 91.
                            // Cell (col, row) with col in [0..12], row in [0..6].
                            // The last column's last filled row = todayDow = today.
                            // dataIndex for cell = 89 - ((12 - col)*7 + (todayDow - row))
                            //                    = 89 - (12-col)*7 - todayDow + row

                            Repeater {
                                // 91 cells, column-major order (flow: TopToBottom)
                                model: 91
                                delegate: Rectangle {
                                    width: 14
                                    height: 14
                                    radius: Radius.xs

                                    // col and row within the grid (column-major)
                                    readonly property int col: Math.floor(index / 7)
                                    readonly property int row: index % 7

                                    // Which index in heatData does this cell map to?
                                    // Anchor: col=12, row=todayDow → dataIndex=89 (today).
                                    readonly property int dataIndex: 89 - (12 - col) * 7 - heatmapGrid.todayDow + row

                                    // Only show cells within the 90-day window.
                                    readonly property bool inRange: dataIndex >= 0 && dataIndex < 90

                                    readonly property int taskCount: (inRange && heatmapGrid.heatData.length > dataIndex)
                                                                     ? (heatmapGrid.heatData[dataIndex].count || 0) : 0
                                    readonly property string cellDate: (inRange && heatmapGrid.heatData.length > dataIndex)
                                                                       ? (heatmapGrid.heatData[dataIndex].date || "") : ""

                                    // Plural helper: Russian task count word
                                    function taskWord(n) {
                                        var mod10 = n % 10
                                        var mod100 = n % 100
                                        if (mod100 >= 11 && mod100 <= 14) return qsTr("задач")
                                        if (mod10 === 1) return qsTr("задача")
                                        if (mod10 >= 2 && mod10 <= 4) return qsTr("задачи")
                                        return qsTr("задач")
                                    }

                                    // Format date from "YYYY-MM-DD" to "DD.MM.YYYY"
                                    function formatDate(iso) {
                                        if (!iso || iso.length < 10) return ""
                                        return iso.slice(8, 10) + "." + iso.slice(5, 7) + "." + iso.slice(0, 4)
                                    }

                                    color: {
                                        if (!inRange)
                                            return "transparent"
                                        var n = taskCount
                                        if (n === 0)
                                            return Qt.rgba(textPrimary.r, textPrimary.g, textPrimary.b, 0.07)
                                        var a = ThemeManager.accentColor
                                        if (n <= 2)  return Qt.rgba(a.r, a.g, a.b, 0.30)
                                        if (n <= 5)  return Qt.rgba(a.r, a.g, a.b, 0.55)
                                        if (n <= 9)  return Qt.rgba(a.r, a.g, a.b, 0.80)
                                        return a
                                    }

                                    HoverHandler { id: cellHover }
                                    ToolTip.visible: cellHover.hovered && inRange && cellDate !== ""
                                    ToolTip.delay: 300
                                    ToolTip.text: qsTr("%1 %2 — %3").arg(taskCount).arg(taskWord(taskCount)).arg(formatDate(cellDate))
                                }
                            }

                            Component.onCompleted: heatData = karmaViewModel.completionsByDay(90)

                            Connections {
                                target: karmaViewModel
                                function onKarmaChanged() {
                                    heatmapGrid.heatData = karmaViewModel.completionsByDay(90)
                                }
                            }
                        }
                    }

                    // Legend
                    RowLayout {
                        spacing: 6
                        Text {
                            text: qsTr("Меньше")
                            font.pixelSize: 10
                            color: textSecondary
                        }
                        Repeater {
                            model: [0, 1, 3, 6, 10]
                            delegate: Rectangle {
                                width: 14
                                height: 14
                                radius: Radius.xs
                                color: {
                                    var n = modelData
                                    if (n === 0)
                                        return Qt.rgba(textPrimary.r, textPrimary.g, textPrimary.b, 0.07)
                                    var a = ThemeManager.accentColor
                                    if (n <= 2)  return Qt.rgba(a.r, a.g, a.b, 0.30)
                                    if (n <= 5)  return Qt.rgba(a.r, a.g, a.b, 0.55)
                                    if (n <= 9)  return Qt.rgba(a.r, a.g, a.b, 0.80)
                                    return a
                                }
                            }
                        }
                        Text {
                            text: qsTr("Больше")
                            font.pixelSize: 10
                            color: textSecondary
                        }
                    }
                }
            }

            // === Tasks by Priority Bar Chart ===
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: priorityChartCol.implicitHeight + 32
                color: contentColor
                radius: 10
                border.color: borderColor
                border.width: 1

                ColumnLayout {
                    id: priorityChartCol
                    anchors.fill: parent
                    anchors.margins: Spacing.md
                    spacing: Spacing.sm

                    Text {
                        text: qsTr("Задачи по приоритету")
                        font.pixelSize: FontSize.body
                        font.weight: Font.DemiBold
                        color: textPrimary
                    }

                    property int _kick: 0

                    property var _counts: _computeCounts()

                    function _computeCounts() {
                        void _kick   // establish reactive dependency
                        const result = { p1: 0, p2: 0, p3: 0, p4: 0 }
                        const all = dataStore.allTasks()
                        for (let i = 0; i < all.length; i++) {
                            const t = all[i]
                            if (!t || t.isDeleted || t.isCompleted) continue
                            const p = t.priority
                            if (p === 1)      result.p1++
                            else if (p === 2) result.p2++
                            else if (p === 3) result.p3++
                            else              result.p4++
                        }
                        return result
                    }

                    Connections {
                        target: dataStore
                        function onTasksChanged() {
                            priorityChartCol._kick = (priorityChartCol._kick + 1) % 10000
                            priorityChartCol._counts = priorityChartCol._computeCounts()
                        }
                    }

                    readonly property int _maxCount: Math.max(
                        1,
                        priorityChartCol._counts.p1,
                        priorityChartCol._counts.p2,
                        priorityChartCol._counts.p3,
                        priorityChartCol._counts.p4
                    )

                    readonly property int _totalCount:
                        priorityChartCol._counts.p1 + priorityChartCol._counts.p2 +
                        priorityChartCol._counts.p3 + priorityChartCol._counts.p4

                    // Empty state
                    Text {
                        visible: priorityChartCol._totalCount === 0
                        text: qsTr("Нет активных задач.")
                        color: textSecondary
                        font.pixelSize: FontSize.small
                    }

                    // Bar rows
                    Repeater {
                        model: priorityChartCol._totalCount > 0 ? [
                            { label: qsTr("P1 — высокий"), count: priorityChartCol._counts.p1, color: ThemeManager.priorityP1Color },
                            { label: qsTr("P2 — средний"), count: priorityChartCol._counts.p2, color: ThemeManager.priorityP2Color },
                            { label: qsTr("P3 — низкий"),  count: priorityChartCol._counts.p3, color: ThemeManager.priorityP3Color },
                            { label: qsTr("P4 — обычный"), count: priorityChartCol._counts.p4, color: ThemeManager.priorityP4Color }
                        ] : []

                        delegate: RowLayout {
                            Layout.fillWidth: true
                            spacing: Spacing.sm

                            Text {
                                text: modelData.label
                                color: textPrimary
                                font.pixelSize: FontSize.bodySmall
                                Layout.preferredWidth: 110
                            }

                            Item {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 18

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    radius: 3
                                    color: modelData.color
                                    opacity: 0.85
                                    width: parent.width * (modelData.count / priorityChartCol._maxCount)
                                    Behavior on width {
                                        NumberAnimation { duration: 400; easing.type: Easing.OutQuad }
                                    }
                                }

                                Text {
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.rightMargin: Spacing.xs
                                    text: modelData.count
                                    font.bold: true
                                    font.pixelSize: FontSize.caption
                                    color: textPrimary
                                }
                            }
                        }
                    }
                }
            }

            // === Recently Completed Tasks ===
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: recentDoneCol.implicitHeight + 32
                color: contentColor
                radius: 10
                border.color: borderColor
                border.width: 1

                ColumnLayout {
                    id: recentDoneCol
                    anchors.fill: parent
                    anchors.margins: Spacing.md
                    spacing: Spacing.sm

                    Text {
                        text: qsTr("Недавно завершённые")
                        font.pixelSize: FontSize.body
                        font.weight: Font.DemiBold
                        color: textPrimary
                    }

                    // Kick counter — forces _recentTasks to re-evaluate on any task change.
                    property int _kick: 0
                    property var _recentTasks: _computeRecent()

                    function _computeRecent() {
                        void _kick   // reactive dependency
                        const all = (typeof dataStore.completedTasks === "function")
                            ? dataStore.completedTasks()
                            : dataStore.allTasks().filter(function(t) {
                                  return t && !t.isDeleted && t.isCompleted
                              })
                        const arr = []
                        for (let i = 0; i < all.length; i++) {
                            const t = all[i]
                            if (!t) continue
                            arr.push({
                                id:          t.id          || "",
                                content:     t.content     || "",
                                completedAt: String(t.completedAt || ""),
                                projectId:   t.projectId   || "",
                                priority:    t.priority    || 4
                            })
                        }
                        // Sort newest-first by ISO completedAt string, then take top 10.
                        arr.sort(function(a, b) { return b.completedAt.localeCompare(a.completedAt) })
                        return arr.slice(0, 10)
                    }

                    // Re-compute whenever any task changes in DataStore.
                    Connections {
                        target: dataStore
                        function onTasksChanged() {
                            recentDoneCol._kick = (recentDoneCol._kick + 1) % 10000
                            recentDoneCol._recentTasks = recentDoneCol._computeRecent()
                        }
                    }

                    // Relative-time formatter (Russian).
                    function _formatRelative(iso) {
                        if (!iso) return ""
                        const d = new Date(iso)
                        if (isNaN(d.getTime())) return ""
                        const diffMs = new Date() - d
                        const min  = 60 * 1000
                        const hour = 60 * min
                        const day  = 24 * hour
                        if (diffMs < min)       return qsTr("только что")
                        if (diffMs < hour)      return qsTr("%1 мин назад").arg(Math.floor(diffMs / min))
                        if (diffMs < day)       return qsTr("%1 ч назад").arg(Math.floor(diffMs / hour))
                        if (diffMs < 7 * day)   return qsTr("%1 дн назад").arg(Math.floor(diffMs / day))
                        return Qt.formatDate(d, "dd.MM.yyyy")
                    }

                    // Empty state — shown when there are no completed tasks.
                    Text {
                        visible: !recentDoneCol._recentTasks || recentDoneCol._recentTasks.length === 0
                        text: qsTr("Пока нет завершённых задач.")
                        color: textSecondary
                        font.pixelSize: FontSize.small
                        Layout.topMargin: Spacing.xs
                    }

                    // One hoverable row per task.
                    Repeater {
                        model: recentDoneCol._recentTasks

                        delegate: Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 44
                            radius: Radius.sm
                            color: rowHover.containsMouse
                                ? Qt.rgba(textPrimary.r, textPrimary.g, textPrimary.b, 0.06)
                                : "transparent"

                            MouseArea {
                                id: rowHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: taskViewModel.openTaskById(modelData.id)
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Spacing.sm
                                anchors.rightMargin: Spacing.sm
                                spacing: Spacing.sm

                                // Checkmark icon — task 17cea65b iter-3: было "✓".
                                // Iter-night-23: was hardcoded "#2e8b57"
                                // (sea green). Bind to the theme's semantic
                                // successColor — same intent ("this task is
                                // done"), correct adaptation across every
                                // palette including high-contrast where
                                // mid-saturation greens become invisible
                                // against pure-black backgrounds.
                                IconSvg {
                                    name: "check"
                                    size: 14
                                    color: ThemeManager.successColor
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                // Task content + time/project line
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    spacing: 0

                                    Text {
                                        text: modelData.content
                                        color: textPrimary
                                        font.pixelSize: FontSize.bodySmall
                                        font.strikeout: true
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        color: textSecondary
                                        font.pixelSize: FontSize.caption
                                        text: {
                                            const rel  = recentDoneCol._formatRelative(modelData.completedAt)
                                            const proj = (typeof projectViewModel.projectById === "function")
                                                ? projectViewModel.projectById(modelData.projectId)
                                                : null
                                            const projName = (proj && proj.name) ? " · #" + proj.name : ""
                                            return rel + projName
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // === Activity Feed (last 30 events) ===
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: activityFeedCol.implicitHeight + 32
                color: contentColor
                radius: Radius.lg
                border.color: borderColor
                border.width: 1

                ColumnLayout {
                    id: activityFeedCol
                    anchors.fill: parent
                    anchors.margins: Spacing.md
                    spacing: Spacing.sm

                    // Header: title + subtitle
                    Text {
                        text: qsTr("Лента активности")
                        font.pixelSize: FontSize.body
                        font.weight: Font.DemiBold
                        color: textPrimary
                    }
                    Text {
                        text: qsTr("Последние 30 событий")
                        color: textSecondary
                        font.pixelSize: FontSize.caption
                        Layout.bottomMargin: Spacing.xs
                    }

                    // Reactive kick: bumped when DataStore signals task changes.
                    property int _kick: 0
                    property var _feed: rebuildFeed()

                    // Russian plural helpers ──────────────────────────────────
                    function _pluralMin(n) {
                        var m100 = n % 100, m10 = n % 10
                        if (m100 >= 11 && m100 <= 14) return qsTr("минут")
                        if (m10 === 1) return qsTr("минуту")
                        if (m10 >= 2 && m10 <= 4) return qsTr("минуты")
                        return qsTr("минут")
                    }
                    function _pluralHour(n) {
                        var m100 = n % 100, m10 = n % 10
                        if (m100 >= 11 && m100 <= 14) return qsTr("часов")
                        if (m10 === 1) return qsTr("час")
                        if (m10 >= 2 && m10 <= 4) return qsTr("часа")
                        return qsTr("часов")
                    }
                    function _pluralDay(n) {
                        var m100 = n % 100, m10 = n % 10
                        if (m100 >= 11 && m100 <= 14) return qsTr("дней")
                        if (m10 === 1) return qsTr("день")
                        if (m10 >= 2 && m10 <= 4) return qsTr("дня")
                        return qsTr("дней")
                    }

                    // Relative-time formatter (Russian).
                    function _formatRelative(date) {
                        if (!date || isNaN(date.getTime())) return ""
                        var diffMs = new Date() - date
                        if (diffMs < 0) diffMs = 0
                        var min  = 60 * 1000
                        var hour = 60 * min
                        var day  = 24 * hour
                        if (diffMs < min) return qsTr("только что")
                        if (diffMs < hour) {
                            var n = Math.floor(diffMs / min)
                            return qsTr("%1 %2 назад").arg(n).arg(_pluralMin(n))
                        }
                        if (diffMs < day) {
                            var n2 = Math.floor(diffMs / hour)
                            return qsTr("%1 %2 назад").arg(n2).arg(_pluralHour(n2))
                        }
                        if (diffMs < 2 * day) return qsTr("вчера")
                        if (diffMs < 7 * day) {
                            var n3 = Math.floor(diffMs / day)
                            return qsTr("%1 %2 назад").arg(n3).arg(_pluralDay(n3))
                        }
                        return Qt.formatDate(date, "dd.MM.yyyy")
                    }

                    // Build [{kind, content, when, sortKey}] for the feed.
                    function rebuildFeed() {
                        void _kick   // reactive dependency
                        var all = dataStore.allTasks()
                        var entries = []
                        for (var i = 0; i < all.length; i++) {
                            var t = all[i]
                            if (!t || t.isDeleted) continue

                            var created   = t.createdAt   ? new Date(t.createdAt)   : null
                            var updated   = t.updatedAt   ? new Date(t.updatedAt)   : null
                            var completed = t.completedAt ? new Date(t.completedAt) : null

                            // Pick the newest meaningful timestamp.
                            var times = []
                            if (created   && !isNaN(created.getTime()))   times.push(created.getTime())
                            if (updated   && !isNaN(updated.getTime()))   times.push(updated.getTime())
                            if (completed && !isNaN(completed.getTime())) times.push(completed.getTime())
                            if (times.length === 0) continue
                            var maxTime = Math.max.apply(null, times)

                            var kind, emoji, prefix
                            if (completed && !isNaN(completed.getTime()) && completed.getTime() === maxTime) {
                                kind = "completed"
                                emoji = "✅"
                                prefix = qsTr("Выполнено: ")
                            } else if (created && updated
                                       && !isNaN(created.getTime()) && !isNaN(updated.getTime())
                                       && Math.abs(updated.getTime() - created.getTime()) <= 5000) {
                                kind = "created"
                                emoji = "➕"
                                prefix = qsTr("Создано: ")
                            } else if (updated && !isNaN(updated.getTime())) {
                                kind = "edited"
                                emoji = "✏"
                                prefix = qsTr("Изменено: ")
                            } else {
                                kind = "created"
                                emoji = "➕"
                                prefix = qsTr("Создано: ")
                            }

                            entries.push({
                                kind:    kind,
                                emoji:   emoji,
                                prefix:  prefix,
                                content: t.content || "",
                                sortKey: maxTime,
                                when:    new Date(maxTime)
                            })
                        }
                        entries.sort(function(a, b) { return b.sortKey - a.sortKey })
                        return entries.slice(0, 30)
                    }

                    Connections {
                        target: dataStore
                        function onTasksChanged() {
                            activityFeedCol._kick = (activityFeedCol._kick + 1) % 10000
                            activityFeedCol._feed = activityFeedCol.rebuildFeed()
                        }
                    }

                    Component.onCompleted: _feed = rebuildFeed()

                    // Empty state
                    Text {
                        visible: !activityFeedCol._feed || activityFeedCol._feed.length === 0
                        text: qsTr("Пока нет активности")
                        color: textSecondary
                        font.pixelSize: FontSize.small
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                        Layout.topMargin: Spacing.sm
                        Layout.bottomMargin: Spacing.sm
                    }

                    // Feed entries
                    Repeater {
                        model: activityFeedCol._feed

                        delegate: RowLayout {
                            Layout.fillWidth: true
                            spacing: Spacing.sm

                            // Emoji prefix (32 px wide)
                            Text {
                                text: modelData.emoji
                                font.pixelSize: 16
                                Layout.preferredWidth: Spacing.xl
                                Layout.alignment: Qt.AlignVCenter
                                horizontalAlignment: Text.AlignHCenter
                            }

                            // Description (kind + content), elided
                            Label {
                                Layout.fillWidth: true
                                text: modelData.prefix + modelData.content
                                color: textPrimary
                                font.pixelSize: FontSize.bodySmall
                                elide: Text.ElideRight
                            }

                            // Relative time
                            Text {
                                text: activityFeedCol._formatRelative(modelData.when)
                                color: textSecondary
                                font.pixelSize: FontSize.caption
                                Layout.alignment: Qt.AlignVCenter
                            }
                        }
                    }
                }
            }

            // === Karma Trend by Day (30 days) ===
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 200
                color: contentColor
                radius: 10
                border.color: borderColor
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Spacing.md
                    spacing: Spacing.sm

                    Text {
                        text: qsTr("Очки за 30 дней")
                        font.pixelSize: FontSize.body
                        font.weight: Font.DemiBold
                        color: textPrimary
                    }

                    Canvas {
                        id: trendCanvas30
                        Layout.fillWidth: true
                        Layout.preferredHeight: 130

                        property var trendData: karmaViewModel.karmaTrendByDay(30)

                        onWidthChanged:      requestPaint()
                        onHeightChanged:     requestPaint()
                        onTrendDataChanged:  requestPaint()

                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            var data = trendData
                            if (!data || data.length === 0) return

                            // Compute max for vertical scale
                            var maxScore = 0
                            for (var i = 0; i < data.length; i++)
                                maxScore = Math.max(maxScore, data[i].score)
                            if (maxScore === 0) maxScore = 1   // avoid /0; flat line at bottom

                            var padX = 30
                            var padY = 20
                            var w = width  - padX * 2
                            var h = height - padY * 2

                            // Y-axis grid lines (4 levels)
                            var accent = ThemeManager.accentColor
                            var txtClr = ThemeManager.textColor
                            ctx.strokeStyle = Qt.rgba(txtClr.r, txtClr.g, txtClr.b, 0.10)
                            ctx.lineWidth = 1
                            for (var g = 0; g <= 3; g++) {
                                var gy = padY + h - (h * g / 3)
                                ctx.beginPath()
                                ctx.moveTo(padX, gy)
                                ctx.lineTo(padX + w, gy)
                                ctx.stroke()
                            }

                            // Draw line
                            ctx.strokeStyle = accent
                            ctx.lineWidth = 2
                            ctx.lineJoin = "round"
                            ctx.beginPath()
                            for (var j = 0; j < data.length; j++) {
                                var x = padX + (data.length === 1 ? w / 2 : w * j / (data.length - 1))
                                var y = padY + h - (h * data[j].score / maxScore)
                                if (j === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                            }
                            ctx.stroke()

                            // Fill area below line (subtle)
                            var lastX = padX + (data.length === 1 ? w / 2 : w)
                            ctx.lineTo(lastX, padY + h)
                            ctx.lineTo(padX,  padY + h)
                            ctx.closePath()
                            ctx.fillStyle = Qt.rgba(accent.r, accent.g, accent.b, 0.18)
                            ctx.fill()

                            // Y-axis labels: max at top, 0 at bottom
                            var sec = ThemeManager.secondaryTextColor
                            ctx.fillStyle = Qt.rgba(sec.r, sec.g, sec.b, 1.0)
                            ctx.font = "11px sans-serif"
                            ctx.textAlign = "right"
                            ctx.fillText(maxScore.toString(), padX - 4, padY + 4)
                            ctx.fillText("0",                padX - 4, padY + h)

                            // X-axis labels: first / middle / last date  (MM-DD)
                            ctx.textAlign = "center"
                            if (data.length > 0) {
                                var firstDate = data[0].date.substring(5)
                                var lastDate  = data[data.length - 1].date.substring(5)
                                ctx.fillText(firstDate, padX,         padY + h + 14)
                                ctx.fillText(lastDate,  padX + w,     padY + h + 14)
                                if (data.length > 2) {
                                    var midIdx  = Math.floor(data.length / 2)
                                    var midDate = data[midIdx].date.substring(5)
                                    ctx.fillText(midDate, padX + w / 2, padY + h + 14)
                                }
                            }
                        }

                        Connections {
                            target: karmaViewModel
                            function onKarmaChanged() {
                                trendCanvas30.trendData = karmaViewModel.karmaTrendByDay(30)
                            }
                        }
                    }

                    // Total karma earned in the last 30 days
                    Text {
                        color: textSecondary
                        font.pixelSize: FontSize.small
                        text: {
                            if (!trendCanvas30.trendData) return ""
                            var sum = 0
                            for (var i = 0; i < trendCanvas30.trendData.length; i++)
                                sum += trendCanvas30.trendData[i].score
                            var word = (sum === 1) ? qsTr("очко")
                                     : (sum % 100 >= 11 && sum % 100 <= 14) ? qsTr("очков")
                                     : (sum % 10 >= 2 && sum % 10 <= 4) ? qsTr("очка")
                                     : qsTr("очков")
                            return qsTr("За 30 дней: +%1 %2").arg(sum).arg(word)
                        }
                    }
                }
            }

            // === Tag Cloud ===
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: tagCloudCol.implicitHeight + 32
                color: contentColor
                radius: 10
                border.color: borderColor
                border.width: 1

                ColumnLayout {
                    id: tagCloudCol
                    anchors.fill: parent
                    anchors.margins: Spacing.md
                    spacing: Spacing.sm

                    Text {
                        text: qsTr("Облако меток")
                        font.pixelSize: FontSize.body
                        font.weight: Font.DemiBold
                        color: textPrimary
                    }

                    // Reactive kick counter — incremented on any relevant dataStore signal
                    // so that _tagData re-evaluates without a two-way binding fight.
                    property int _kick: 0

                    property var _tagData: _computeTagData()

                    // Build [{name, count}] sorted descending by count.
                    function _computeTagData() {
                        void _kick   // establish reactive dependency
                        const counts = {}
                        const all = dataStore.allTasks()
                        for (let i = 0; i < all.length; i++) {
                            const t = all[i]
                            if (!t || t.isDeleted) continue
                            const labels = t.labels || []
                            for (let j = 0; j < labels.length; j++) {
                                counts[labels[j]] = (counts[labels[j]] || 0) + 1
                            }
                        }
                        const result = []
                        for (const name in counts) {
                            result.push({ name: name, count: counts[name] })
                        }
                        result.sort(function(a, b) { return b.count - a.count })
                        return result
                    }

                    // Refresh when tasks or labels change in DataStore.
                    Connections {
                        target: dataStore
                        function onTasksChanged() {
                            tagCloudCol._kick = (tagCloudCol._kick + 1) % 10000
                            tagCloudCol._tagData = tagCloudCol._computeTagData()
                        }
                        function onLabelsChanged() {
                            tagCloudCol._kick = (tagCloudCol._kick + 1) % 10000
                            tagCloudCol._tagData = tagCloudCol._computeTagData()
                        }
                    }

                    // Font size: log scale mapped to [11 .. 22] px.
                    // count=1/maxCount=1 → 14 px (flat guard).
                    // count=maxCount     → 22 px.
                    // count=1            → 11 px (minimum).
                    function _fontSize(count, maxCount) {
                        if (maxCount <= 1) return 14
                        const ratio = Math.log(1 + count) / Math.log(1 + maxCount)
                        return 11 + Math.round(ratio * 11)
                    }

                    // Empty state
                    Text {
                        visible: !tagCloudCol._tagData || tagCloudCol._tagData.length === 0
                        text: qsTr("Метки не используются. Добавьте @метку к задаче, чтобы она появилась здесь.")
                        color: textSecondary
                        font.pixelSize: FontSize.small
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    // Chip flow — visible only when there is at least one labelled task.
                    Flow {
                        visible: tagCloudCol._tagData && tagCloudCol._tagData.length > 0
                        spacing: 6
                        Layout.fillWidth: true

                        // tags are sorted descending, so [0].count is the maximum.
                        readonly property int _maxCount: (tagCloudCol._tagData && tagCloudCol._tagData.length > 0)
                                                         ? tagCloudCol._tagData[0].count : 0

                        Repeater {
                            model: tagCloudCol._tagData

                            delegate: Rectangle {
                                id: tagChip
                                height: chipLabel.implicitHeight + 8
                                width:  chipLabel.implicitWidth  + 16
                                radius: height / 2

                                // Background opacity scales linearly with relative count:
                                // least-used → 10 % alpha, most-used → 30 % alpha.
                                color: Qt.rgba(
                                    ThemeManager.accentColor.r,
                                    ThemeManager.accentColor.g,
                                    ThemeManager.accentColor.b,
                                    0.10 + 0.20 * (modelData.count / Math.max(parent._maxCount, 1))
                                )

                                Text {
                                    id: chipLabel
                                    anchors.centerIn: parent
                                    text: "@" + modelData.name + " (" + modelData.count + ")"
                                    font.pixelSize: tagCloudCol._fontSize(modelData.count, parent._maxCount)
                                    color: textPrimary
                                }

                                HoverHandler {
                                    cursorShape: Qt.PointingHandCursor
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        taskViewModel.filterMode = TaskViewModel.Label
                                        taskViewModel.currentFilter = modelData.name
                                    }
                                }

                                Behavior on color { ColorAnimation { duration: 150 } }
                            }
                        }
                    }
                }
            }

            // === Goals ===
            Rectangle {
                Layout.fillWidth: true
                radius: 10
                color: contentColor
                border.color: borderColor
                border.width: 1
                implicitHeight: goalsCol.implicitHeight + 32

                ColumnLayout {
                    id: goalsCol
                    anchors.fill: parent
                    anchors.margins: Spacing.md
                    spacing: 12

                    Text {
                        text: qsTr("Цели")
                        font.pixelSize: FontSize.body
                        font.weight: Font.DemiBold
                        color: textPrimary
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: qsTr("Дневная цель")
                            font.pixelSize: FontSize.body
                            color: textPrimary
                            Layout.fillWidth: true
                        }
                        // B-6.67 — one-way bind via Component.onCompleted + onValueModified
                        // to avoid the two-way binding fight while the user is typing.
                        SpinBox {
                            id: dailyGoalSpin
                            from: 1
                            to: 50
                            // F-6.96 — KeyNavigation chain: Tab moves to weeklyGoalSpin
                            KeyNavigation.tab: weeklyGoalSpin
                            Component.onCompleted: value = settingsViewModel.dailyGoal
                            onValueModified: settingsViewModel.updateSetting("dailyGoal", value)
                            Connections {
                                target: settingsViewModel
                                function onDailyGoalChanged() {
                                    if (dailyGoalSpin.value !== settingsViewModel.dailyGoal)
                                        dailyGoalSpin.value = settingsViewModel.dailyGoal
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: qsTr("Недельная цель")
                            font.pixelSize: FontSize.body
                            color: textPrimary
                            Layout.fillWidth: true
                        }
                        // B-6.67 — same pattern as dailyGoalSpin
                        SpinBox {
                            id: weeklyGoalSpin
                            from: 1
                            to: 200
                            // F-6.96 — KeyNavigation chain: Tab wraps back to dailyGoalSpin
                            KeyNavigation.tab: dailyGoalSpin
                            Component.onCompleted: value = settingsViewModel.weeklyGoal
                            onValueModified: settingsViewModel.updateSetting("weeklyGoal", value)
                            Connections {
                                target: settingsViewModel
                                function onWeeklyGoalChanged() {
                                    if (weeklyGoalSpin.value !== settingsViewModel.weeklyGoal)
                                        weeklyGoalSpin.value = settingsViewModel.weeklyGoal
                                }
                            }
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: 40 }
        }
    }
}
