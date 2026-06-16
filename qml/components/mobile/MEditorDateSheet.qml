import QtQuick
import OpenTodoUI

// MEditorDateSheet — due-date picker. Ported from m-screens2.jsx date sheet:
// quick chips (Сегодня/Завтра/Выходные/Без срока) + a compact month calendar.
// Emits picked(dueString) — a phrase the DateParser understands, or "" to clear.
MSheet {
    id: root

    signal picked(string dueString)

    property int _y: (new Date()).getFullYear()
    property int _m: (new Date()).getMonth()
    readonly property int _today: (new Date()).getDate()

    function _isoFor(day) { return Qt.formatDate(new Date(_y, _m, day), "yyyy-MM-dd") }
    function _marks() {
        var m = ({})
        var arr = (typeof dataStore !== "undefined" && dataStore.allTasksData) ? (dataStore.allTasksData() || []) : []
        for (var i = 0; i < arr.length; i++) {
            var t = arr[i]
            if (!t || t.isCompleted || t.isDeleted) continue
            var ds = t.dueDate || (t.dueDatetime ? String(t.dueDatetime).substring(0, 10) : "")
            if (!ds) continue
            var d = new Date(ds)
            if (isNaN(d.getTime()) || d.getFullYear() !== _y || d.getMonth() !== _m) continue
            var col = t.priority === 1 ? ThemeManager.priorityP1Color
                    : t.priority === 2 ? ThemeManager.priorityP2Color
                    : t.priority === 3 ? ThemeManager.priorityP3Color : ThemeManager.priorityP4Color
            if (!m[d.getDate()]) m[d.getDate()] = []
            m[d.getDate()].push(col)
        }
        return m
    }

    Item {
        width: parent.width
        height: titleT.implicitHeight + chipsFlow.implicitHeight + 30
        Text {
            id: titleT
            anchors.left: parent.left; anchors.leftMargin: 18
            anchors.top: parent.top; anchors.topMargin: 4
            text: qsTr("Срок"); font.pixelSize: 18; font.weight: Font.Bold
            color: ThemeManager.textColor
        }
        Flow {
            id: chipsFlow
            anchors.top: titleT.bottom; anchors.topMargin: 12
            anchors.left: parent.left; anchors.right: parent.right
            anchors.leftMargin: 18; anchors.rightMargin: 18
            spacing: 9
            Repeater {
                model: [
                    { "l": qsTr("Сегодня"),  "icon": "today",    "color": ThemeManager.successColor,        "due": "сегодня" },
                    { "l": qsTr("Завтра"),   "icon": "upcoming", "color": ThemeManager.priorityP2Color,     "due": "завтра" },
                    { "l": qsTr("Выходные"), "icon": "calendar", "color": ThemeManager.priorityP3Color,     "due": "суббота" },
                    { "l": qsTr("Без срока"),"icon": "circle",   "color": ThemeManager.secondaryTextColor,  "due": "" }
                ]
                delegate: Rectangle {
                    required property var modelData
                    height: 38; radius: 20
                    width: chRow.implicitWidth + 26
                    color: chMa.pressed ? ThemeManager.hoverColor : "transparent"
                    border.width: 1; border.color: ThemeManager.borderColor
                    Row {
                        id: chRow
                        anchors.centerIn: parent; spacing: 7
                        IconSvg { anchors.verticalCenter: parent.verticalCenter; name: modelData.icon; size: 15; color: modelData.color }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: modelData.l; font.pixelSize: 13; font.weight: Font.DemiBold; color: ThemeManager.textColor }
                    }
                    MouseArea { id: chMa; anchors.fill: parent; onClicked: root.picked(modelData.due) }
                }
            }
        }
    }

    Item {
        width: parent.width
        height: cal.implicitHeight + 8
        MCalendar {
            id: cal
            anchors.left: parent.left; anchors.right: parent.right
            anchors.leftMargin: 8; anchors.rightMargin: 8
            anchors.top: parent.top
            compact: true
            year: root._y; month: root._m; today: root._today; selectedDay: -1
            marks: root._marks()
            onDayPicked: function(day) { root.picked(root._isoFor(day)) }
        }
    }

    Item {
        width: parent.width; height: 64
        Rectangle {
            anchors.left: parent.left; anchors.right: parent.right
            anchors.leftMargin: 16; anchors.rightMargin: 16
            anchors.top: parent.top; anchors.topMargin: 4
            height: 50; radius: 14
            color: ThemeManager.accentColor
            Text { anchors.centerIn: parent; text: qsTr("Готово"); color: ThemeManager.accentForeground; font.pixelSize: 15; font.weight: Font.Bold }
            MouseArea { anchors.fill: parent; onClicked: root.closeRequested() }
        }
    }
}
