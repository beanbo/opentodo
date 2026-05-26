import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

Popup {
    id: root

    // ─── Public API ────────────────────────────────────────────────────────────
    property string selectedDate: ""
    signal dateSelected(string dateString)

    // ─── Internals ─────────────────────────────────────────────────────────────
    readonly property bool isDark: ThemeManager.isDark

    readonly property color accentRed:   ThemeManager.accentColor
    readonly property color bgColor:   ThemeManager.backgroundColor
    readonly property color borderCol: ThemeManager.borderColor
    readonly property color textCol:   ThemeManager.textColor
    readonly property color subCol:    ThemeManager.secondaryTextColor
    readonly property color hoverCol:  ThemeManager.hoverColor

    // ─── Calendar state ────────────────────────────────────────────────────────
    property int  viewYear:  today.getFullYear()
    property int  viewMonth: today.getMonth()   // 0-based

    readonly property var today: new Date()

    // Currently selected date as a JS Date (or null)
    property var pickedDate: selectedDate !== "" ? parseDate(selectedDate) : null

    // B-7.22: weekStart from settings (0=Mon..6=Sun per SettingsViewModel).
    // Falls back to 0 (Monday) when settingsViewModel is not available.
    property int weekStartIdx: (typeof settingsViewModel !== "undefined" && settingsViewModel)
                               ? settingsViewModel.weekStart : 0

    // Day-of-week header labels rotated to start on weekStartIdx.
    // JS getDay() is 0=Sun; SettingsViewModel weekStart is 0=Mon..6=Sun,
    // so we map: settings 0→JS 1(Mon), settings 6→JS 0(Sun).
    // Labels are in JS weekday order (0=Sun): ["Вс","Пн","Вт","Ср","Чт","Пт","Сб"]
    readonly property var _allLabels: [qsTr("Вс"), qsTr("Пн"), qsTr("Вт"), qsTr("Ср"), qsTr("Чт"), qsTr("Пт"), qsTr("Сб")]
    // Convert SettingsViewModel 0=Mon..6=Sun to JS 0=Sun index:
    property int _jsWeekStart: (weekStartIdx + 1) % 7  // 0(Mon)->1, 6(Sun)->0
    property var weekdayLabels: {
        var arr = []
        for (var i = 0; i < 7; i++) arr.push(_allLabels[(_jsWeekStart + i) % 7])
        return arr
    }

    // ─── Popup geometry ────────────────────────────────────────────────────────
    width:  268
    height: contentCol.implicitHeight + 24
    padding: 0
    modal:  false
    focus:  true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // ─── Background ────────────────────────────────────────────────────────────
    background: Rectangle {
        color:        root.bgColor
        radius:       Radius.lg
        border.color: root.borderCol
        border.width: 1

        layer.enabled: true
        layer.effect: null   // shadow can be added via Qt.labs.effects if desired
    }

    // ─── Content ───────────────────────────────────────────────────────────────
    ColumnLayout {
        id: contentCol
        anchors {
            left:    parent.left
            right:   parent.right
            top:     parent.top
            margins: 12
        }
        spacing: 8

        // ── Quick options ──────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Repeater {
                model: [
                    { label: qsTr("Сегодня"),       value: "today"     },
                    { label: qsTr("Завтра"),        value: "tomorrow"  },
                    { label: qsTr("След. неделя"),  value: "next week" },
                    { label: qsTr("Без даты"),      value: ""          }
                ]

                delegate: Button {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.label
                    font.pixelSize: FontSize.small
                    flat: true

                    contentItem: Text {
                        text:  parent.text
                        color: root.textCol
                        font:  parent.font
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment:   Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: Radius.sm
                        color:  parent.hovered ? root.hoverCol : root.borderCol
                    }

                    onClicked: {
                        root.selectedDate = modelData.value
                        root.dateSelected(modelData.value)
                        root.close()
                    }
                }
            }
        }

        // Divider
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color:  root.borderCol
        }

        // ── Month navigation header ────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 0

            // Left arrow
            ToolButton {
                id: prevBtn
                implicitWidth:  28
                implicitHeight: 28
                padding: 0

                // Q4.01 (redesign): "‹" unicode → stroke chevron-left IconSvg
                contentItem: IconSvg {
                    name:                "chevron-left"
                    size:                14
                    color:               root.textCol
                    anchors.centerIn:    parent
                }

                background: Rectangle {
                    radius: Radius.sm
                    color:  prevBtn.hovered ? root.hoverCol : "transparent"
                }

                onClicked: root.navigateMonth(-1)
            }

            // Month + year label
            Text {
                Layout.fillWidth: true
                text: root.monthYearLabel(root.viewYear, root.viewMonth)
                color: root.textCol
                font { pixelSize: FontSize.body; weight: Font.DemiBold }
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment:   Text.AlignVCenter
            }

            // Right arrow
            ToolButton {
                id: nextBtn
                implicitWidth:  28
                implicitHeight: 28
                padding: 0

                // Q4.01 (redesign): "›" unicode → stroke chevron-right IconSvg
                contentItem: IconSvg {
                    name:                "chevron-right"
                    size:                14
                    color:               root.textCol
                    anchors.centerIn:    parent
                }

                background: Rectangle {
                    radius: Radius.sm
                    color:  nextBtn.hovered ? root.hoverCol : "transparent"
                }

                onClicked: root.navigateMonth(1)
            }
        }

        // ── Day-of-week headers ────────────────────────────────────────────────
        Row {
            Layout.fillWidth: true
            spacing: 0

            Repeater {
                // B-7.22: weekday headers rotated by weekStartIdx
                model: root.weekdayLabels
                delegate: Text {
                    required property string modelData
                    width:  (contentCol.width) / 7
                    text:   modelData
                    color:  root.subCol
                    font.pixelSize: FontSize.caption
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        // ── Day grid ──────────────────────────────────────────────────────────
        Grid {
            id: dayGrid
            Layout.fillWidth: true
            columns: 7
            spacing: 0

            property var days: root.buildDays(root.viewYear, root.viewMonth)

            Repeater {
                model: dayGrid.days.length

                delegate: Item {
                    required property int index
                    width:  (contentCol.width) / 7
                    height: 34

                    readonly property var dayInfo: dayGrid.days[index]

                    // Highlight circle
                    Rectangle {
                        id: highlight
                        anchors.centerIn: parent
                        width:  28
                        height: 28
                        radius: 14

                        visible: dayInfo.inMonth
                        // B-7.23: binding-safe hover — no imperative onEntered/onExited
                        // color mutation; all state derived from dayMA.containsMouse.
                        color: {
                            if (dayInfo.isSelected) return root.accentRed
                            if (dayMA.containsMouse) return root.hoverCol
                            return "transparent"
                        }
                        border.color: {
                            if (dayInfo.isSelected) return root.accentRed
                            if (dayInfo.isToday)    return root.accentRed
                            return "transparent"
                        }
                        border.width: dayInfo.isToday && !dayInfo.isSelected ? 1 : 0
                    }

                    Text {
                        anchors.centerIn: parent
                        text:  dayInfo.inMonth ? dayInfo.day : ""
                        color: {
                            if (!dayInfo.inMonth)  return "transparent"
                            if (dayInfo.isSelected) return "#ffffff"
                            if (dayInfo.isToday)    return root.accentRed
                            return root.textCol
                        }
                        font.pixelSize: FontSize.bodySmall
                        font.weight:    dayInfo.isToday ? Font.Medium : Font.Normal
                    }

                    MouseArea {
                        id: dayMA
                        anchors.fill: parent
                        enabled: dayInfo.inMonth
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true

                        // B-7.23: state-driven binding instead of imperative color
                        // assignment in onEntered/onExited which breaks declarative
                        // bindings. highlight.color is now purely computed.

                        onClicked: {
                            let d = new Date(root.viewYear, root.viewMonth, dayInfo.day)
                            let str = root.formatDate(d)
                            root.selectedDate = str
                            root.pickedDate   = d
                            root.dateSelected(str)
                            root.close()
                        }
                    }
                }
            }
        }

        // Bottom padding spacer
        Item { height: 4 }
    }

    // ─── Helper functions ──────────────────────────────────────────────────────

    function navigateMonth(delta) {
        let m = root.viewMonth + delta
        let y = root.viewYear
        if (m < 0)  { m = 11; y-- }
        if (m > 11) { m = 0;  y++ }
        root.viewMonth = m
        root.viewYear  = y
    }

    function monthYearLabel(year, month) {
        const names = [qsTr("Январь"),qsTr("Февраль"),qsTr("Март"),qsTr("Апрель"),qsTr("Май"),qsTr("Июнь"),
                       qsTr("Июль"),qsTr("Август"),qsTr("Сентябрь"),qsTr("Октябрь"),qsTr("Ноябрь"),qsTr("Декабрь")]
        return names[month] + " " + year
    }

    // Build array of 42 cell objects for the calendar grid
    function buildDays(year, month) {
        let cells    = []
        // B-7.22: shift firstDay to align with user's preferred week start.
        // getDay() returns 0=Sun. _jsWeekStart is also 0=Sun-based.
        let rawFirstDay = new Date(year, month, 1).getDay()   // 0=Sun
        let firstDay = (rawFirstDay - root._jsWeekStart + 7) % 7
        let daysInM  = new Date(year, month + 1, 0).getDate()

        let t = root.today
        let todayStr  = root.formatDate(t)
        let pickedStr = root.pickedDate ? root.formatDate(root.pickedDate) : ""

        // Leading empty cells
        for (let i = 0; i < firstDay; i++) {
            cells.push({ day: 0, inMonth: false, isToday: false, isSelected: false })
        }

        for (let d = 1; d <= daysInM; d++) {
            let dStr = root.formatDate(new Date(year, month, d))
            cells.push({
                day:        d,
                inMonth:    true,
                isToday:    dStr === todayStr,
                isSelected: dStr === pickedStr
            })
        }

        // Trailing cells to fill 42 slots
        while (cells.length < 42) {
            cells.push({ day: 0, inMonth: false, isToday: false, isSelected: false })
        }

        return cells
    }

    function formatDate(d) {
        let y  = d.getFullYear()
        let mo = String(d.getMonth() + 1).padStart(2, "0")
        let dy = String(d.getDate()).padStart(2, "0")
        return y + "-" + mo + "-" + dy
    }

    function parseDate(str) {
        // Handles "YYYY-MM-DD"; quick strings ("today" etc.) return null
        if (!str || str.length !== 10) return null
        let parts = str.split("-")
        if (parts.length !== 3) return null
        return new Date(parseInt(parts[0]), parseInt(parts[1]) - 1, parseInt(parts[2]))
    }

    // Reset view to today's month when opened
    onOpened: {
        root.viewYear  = root.today.getFullYear()
        root.viewMonth = root.today.getMonth()
        if (root.selectedDate.length === 10) {
            let d = root.parseDate(root.selectedDate)
            if (d) {
                root.viewYear  = d.getFullYear()
                root.viewMonth = d.getMonth()
            }
        }
    }
}
