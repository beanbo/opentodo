import QtQuick
import OpenTodoUI

// MContextSheet — task long-press context menu. Ported from m-pickers.jsx
// MContextSheet: task preview header + action rows (Открыть / Выполнить /
// Выделить несколько / Переместить / Дублировать / Удалить).
MSheet {
    id: root

    property string taskId: ""
    property string taskText: ""
    property int    taskPriority: 4
    property bool   taskCompleted: false

    signal openRequested(string id)
    signal toggleCompleteRequested(string id)
    signal selectMultipleRequested(string id)
    signal moveRequested(string id)
    signal duplicateRequested(string id)
    signal deleteRequested(string id)

    function openFor(id) {
        var t = dataStore.taskById(id)
        if (!t) return
        root.taskId = id
        root.taskText = t.content
        root.taskPriority = t.priority
        root.taskCompleted = t.isCompleted
        root.open = true
    }

    readonly property color _prio: {
        switch (taskPriority) {
        case 1: return ThemeManager.priorityP1Color
        case 2: return ThemeManager.priorityP2Color
        case 3: return ThemeManager.priorityP3Color
        default: return ThemeManager.priorityP4Color
        }
    }

    // Preview header
    Item {
        width: parent.width
        height: prevText.implicitHeight + 24
        Rectangle {
            anchors.bottom: parent.bottom; width: parent.width; height: 1
            color: ThemeManager.hairlineColor
        }
        Rectangle {
            id: prevDot
            anchors.left: parent.left; anchors.leftMargin: 20
            anchors.top: parent.top; anchors.topMargin: 2
            width: 18; height: 18; radius: 9
            color: "transparent"
            border.width: 2; border.color: root._prio
        }
        Text {
            id: prevText
            anchors.left: prevDot.right; anchors.leftMargin: 12
            anchors.right: parent.right; anchors.rightMargin: 20
            anchors.top: parent.top
            text: root.taskText
            color: ThemeManager.textColor
            font.pixelSize: 15; font.weight: Font.Medium
            lineHeight: 1.3; lineHeightMode: Text.ProportionalHeight
            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
        }
    }

    // Actions
    Column {
        width: parent.width
        leftPadding: 10; rightPadding: 10; topPadding: 8; bottomPadding: 10

        Component {
            id: actionRow
            Rectangle {
                property string actIcon: ""
                property string actLabel: ""
                property color  actColor: ThemeManager.secondaryTextColor
                property color  actTextColor: ThemeManager.textColor
                property var    onTrigger
                width: parent ? parent.width - 20 : 0
                height: 52; radius: 12
                color: am.pressed ? ThemeManager.hoverColor : "transparent"
                Row {
                    anchors.left: parent.left; anchors.leftMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 16
                    IconSvg { anchors.verticalCenter: parent.verticalCenter; name: actIcon; size: 21; color: actColor }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: actLabel; font.pixelSize: 15; font.weight: Font.Medium; color: actTextColor }
                }
                MouseArea { id: am; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: if (onTrigger) onTrigger() }
            }
        }

        Loader { sourceComponent: actionRow; width: parent.width - 20
            onLoaded: { item.actIcon = "note"; item.actLabel = qsTr("Открыть"); item.onTrigger = function(){ root.openRequested(root.taskId); root.open = false } } }
        Loader { sourceComponent: actionRow; width: parent.width - 20
            // actLabel via Qt.binding: a plain assignment froze the label at the
            // first-loaded task's state, so a completed task still said "Выполнить".
            onLoaded: { item.actIcon = "check-circle"; item.actColor = ThemeManager.successColor; item.actLabel = Qt.binding(function() { return root.taskCompleted ? qsTr("Снять отметку") : qsTr("Выполнить") }); item.onTrigger = function(){ root.toggleCompleteRequested(root.taskId); root.open = false } } }
        Loader { sourceComponent: actionRow; width: parent.width - 20
            onLoaded: { item.actIcon = "list"; item.actLabel = qsTr("Выделить несколько"); item.onTrigger = function(){ root.selectMultipleRequested(root.taskId); root.open = false } } }
        Loader { sourceComponent: actionRow; width: parent.width - 20
            onLoaded: { item.actIcon = "folder"; item.actLabel = qsTr("Переместить в проект"); item.onTrigger = function(){ root.moveRequested(root.taskId); root.open = false } } }
        Loader { sourceComponent: actionRow; width: parent.width - 20
            onLoaded: { item.actIcon = "plus"; item.actLabel = qsTr("Дублировать"); item.onTrigger = function(){ root.duplicateRequested(root.taskId); root.open = false } } }
        Loader { sourceComponent: actionRow; width: parent.width - 20
            visible: typeof templateViewModel !== "undefined"
            onLoaded: { item.actIcon = "star"; item.actLabel = qsTr("Сохранить как шаблон"); item.onTrigger = function(){ if (typeof templateViewModel !== "undefined") templateViewModel.saveAsTemplate(root.taskText, [root.taskId]); root.open = false } } }
        Loader { sourceComponent: actionRow; width: parent.width - 20
            onLoaded: { item.actIcon = "trash"; item.actColor = ThemeManager.errorColor; item.actTextColor = ThemeManager.errorColor; item.actLabel = qsTr("Удалить"); item.onTrigger = function(){ root.deleteRequested(root.taskId); root.open = false } } }
    }
}
