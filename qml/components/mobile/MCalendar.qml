import QtQuick
import OpenTodoUI

// MCalendar — Monday-first month grid. Ported from m-pickers.jsx MCalendar.
// `marks` maps day-of-month → array of dot colours (priority colours of tasks
// due that day). today/selectedDay highlight the day circle; dayPicked(day)
// fires on tap.
Column {
    id: root

    property int  year: 2026
    property int  month: 5            // 0-based
    property var  marks: ({})
    property int  selectedDay: -1
    property int  today: -1
    property bool compact: false
    signal dayPicked(int day)

    readonly property var _wd: ["Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"]
    readonly property int _cellH: compact ? 38 : 46
    readonly property var _cells: {
        var first = new Date(year, month, 1)
        var lead = (first.getDay() + 6) % 7   // Mon-first lead blanks
        var days = new Date(year, month + 1, 0).getDate()
        var arr = []
        for (var i = 0; i < lead; i++) arr.push(0)
        for (var dd = 1; dd <= days; dd++) arr.push(dd)
        while (arr.length % 7 !== 0) arr.push(0)
        return arr
    }

    width: parent ? parent.width : 0

    Row {
        width: parent.width
        leftPadding: 6
        rightPadding: 6
        bottomPadding: 6
        Repeater {
            model: root._wd
            delegate: Item {
                required property var modelData
                width: (root.width - 12) / 7
                height: 22
                Text {
                    anchors.centerIn: parent
                    text: modelData
                    font.pixelSize: 11
                    font.weight: Font.Bold
                    color: ThemeManager.secondaryTextColor
                }
            }
        }
    }

    Grid {
        columns: 7
        width: parent.width
        Repeater {
            model: root._cells
            delegate: Item {
                id: cell
                required property var modelData
                readonly property int  dd: modelData
                readonly property bool isToday: dd > 0 && dd === root.today
                readonly property bool isSel:   dd > 0 && dd === root.selectedDay
                readonly property var  dayMarks: (dd > 0 && root.marks && root.marks[dd]) ? root.marks[dd] : []

                width: root.width / 7
                height: root._cellH

                Column {
                    visible: cell.dd > 0
                    anchors.centerIn: parent
                    spacing: 3

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 32; height: 32; radius: 16
                        color: cell.isSel ? ThemeManager.accentColor
                               : (cell.isToday ? ThemeManager.accentContainer : "transparent")
                        Text {
                            anchors.centerIn: parent
                            text: cell.dd > 0 ? cell.dd : ""
                            font.pixelSize: 14
                            font.weight: (cell.isToday || cell.isSel) ? Font.Bold : Font.Medium
                            color: cell.isSel ? ThemeManager.accentForeground
                                   : (cell.isToday ? ThemeManager.accentColor : ThemeManager.textColor)
                        }
                    }
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        height: 5
                        spacing: 3
                        Repeater {
                            model: cell.dayMarks.slice(0, 3)
                            delegate: Rectangle {
                                required property var modelData
                                width: 5; height: 5; radius: 3
                                color: modelData
                            }
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: cell.dd > 0
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.dayPicked(cell.dd)
                }
            }
        }
    }
}
