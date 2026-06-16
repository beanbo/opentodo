import QtQuick
import QtQuick.Controls
import OpenTodoUI

// MBoardScreen — mobile Kanban board. Ported from m-screens.jsx MBoardScreen.
// Columns = the project's sections (+ a leading "Задачи" column for unsectioned
// tasks); cards = the project's tasks grouped by sectionId. Horizontal scroll.
Item {
    id: root
    signal menuRequested()
    signal taskClicked(string id)
    signal addRequested()
    signal viewModeChanged(string mode)   // "list" | "board" | "cal"

    Rectangle { anchors.fill: parent; color: ThemeManager.backgroundColor }

    MDensity { id: mdens }

    readonly property var _proj: taskViewModel.currentProjectId !== ""
                                 ? projectViewModel.projectById(taskViewModel.currentProjectId) : null

    // ── Column + card model ────────────────────────────────────────────────
    property var _columns: []
    function _refresh() {
        var pid = taskViewModel.currentProjectId
        var cols = []
        // Group the project's currently-loaded tasks by sectionId.
        var bySection = ({})
        var hasNoSection = false
        for (var i = 0; i < taskViewModel.count; i++) {
            var t = taskViewModel.taskAt(i)
            if (!t) continue
            if (!taskViewModel.showCompleted && t.isCompleted) continue
            var sid = t.sectionId || ""
            if (sid === "") hasNoSection = true
            if (!bySection[sid]) bySection[sid] = []
            bySection[sid].push({ "id": t.id, "content": t.content, "priority": t.priority,
                                  "isCompleted": t.isCompleted, "dueDate": t.dueDate, "dueString": t.dueString,
                                  "labels": t.labels, "isRecurring": t.dueIsRecurring })
        }
        if (hasNoSection)
            cols.push({ "id": "", "name": qsTr("Задачи"), "tasks": bySection[""] || [] })
        var secs = dataStore.sectionsForProject(pid) || []
        var ordered = secs.slice().sort(function(a, b) { return (a.order || 0) - (b.order || 0) })
        for (var s = 0; s < ordered.length; s++) {
            var sec = ordered[s]
            cols.push({ "id": sec.id, "name": sec.name, "tasks": bySection[sec.id] || [] })
        }
        _columns = cols
    }
    Component.onCompleted: _refresh()
    Connections {
        target: taskViewModel
        function onCountChanged() { Qt.callLater(root._refresh) }
        function onTaskChanged() { Qt.callLater(root._refresh) }
        function onCurrentProjectIdChanged() { Qt.callLater(root._refresh) }
        function onShowCompletedChanged() { Qt.callLater(root._refresh) }
    }

    function _prioColor(p) {
        switch (p) {
        case 1: return ThemeManager.priorityP1Color
        case 2: return ThemeManager.priorityP2Color
        case 3: return ThemeManager.priorityP3Color
        default: return ThemeManager.priorityP4Color
        }
    }

    MTopBar {
        id: topBar
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        title: root._proj ? root._proj.name : qsTr("Доска")
        titleIconName: (root._proj && root._proj.icon && root._proj.icon.length > 0) ? root._proj.icon : "board"
        titleColor: (root._proj && root._proj.color && root._proj.color.length > 0) ? root._proj.color : ThemeManager.accentColor
        onMenuClicked: root.menuRequested()
        MIconButton { iconName: "search"; iconColor: ThemeManager.textColor; onClicked: searchOverlay.open() }
    }

    // View-mode segment (Список / Доска / Календарь)
    MSeg {
        id: seg
        anchors.top: topBar.bottom; anchors.topMargin: 4
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: 16; anchors.rightMargin: 16
        value: "board"
        options: [
            { "value": "list",  "label": qsTr("Список"),    "icon": "list" },
            { "value": "board", "label": qsTr("Доска"),     "icon": "board" },
            { "value": "cal",   "label": qsTr("Календарь"),  "icon": "calendar" }
        ]
        onChanged: function(m) { root.viewModeChanged(m) }
    }

    Flickable {
        anchors.top: seg.bottom; anchors.topMargin: 8
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        contentWidth: colRow.width
        clip: true
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds

        Row {
            id: colRow
            height: parent.height
            leftPadding: 16; rightPadding: 16; spacing: 14

            Repeater {
                model: root._columns
                delegate: Item {
                    required property var modelData
                    width: 280
                    height: colRow.height - 16

                    // Column header
                    Row {
                        id: colHeader
                        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                        height: 30
                        spacing: 8
                        Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 9; height: 9; radius: 5; color: ThemeManager.faintTextColor }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: modelData.name; font.pixelSize: 15; font.weight: Font.Bold; color: ThemeManager.textColor }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: modelData.tasks.length; font.pixelSize: 13; font.weight: Font.DemiBold; color: ThemeManager.secondaryTextColor }
                    }
                    MIconButton {
                        anchors.top: parent.top; anchors.right: parent.right
                        diameter: 30; iconName: "plus"; iconSize: 17; iconColor: ThemeManager.secondaryTextColor
                        onClicked: root.addRequested()
                    }

                    Flickable {
                        anchors.top: colHeader.bottom; anchors.topMargin: 10
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                        contentHeight: cardCol.implicitHeight
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds

                        Column {
                            id: cardCol
                            width: parent.width
                            spacing: 11

                            Repeater {
                                model: modelData.tasks
                                delegate: Rectangle {
                                    required property var modelData
                                    width: cardCol.width
                                    height: cardInner.implicitHeight + 26
                                    radius: 14
                                    color: ThemeManager.cardColor
                                    border.width: 1; border.color: ThemeManager.borderColor

                                    Column {
                                        id: cardInner
                                        anchors.left: parent.left; anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.margins: 13
                                        spacing: 9

                                        Row {
                                            width: parent.width
                                            spacing: 10
                                            Rectangle {
                                                id: cc
                                                anchors.top: parent.top; anchors.topMargin: 1
                                                width: 18; height: 18; radius: 9
                                                color: modelData.isCompleted ? ThemeManager.successColor : "transparent"
                                                border.width: 2
                                                border.color: modelData.isCompleted ? ThemeManager.successColor : root._prioColor(modelData.priority)
                                                IconSvg { visible: modelData.isCompleted; anchors.centerIn: parent; name: "check"; size: 10; color: "#ffffff" }
                                                MouseArea { anchors.fill: parent; anchors.margins: -6; onClicked: { if (modelData.isCompleted) taskViewModel.uncompleteTask(modelData.id); else taskViewModel.completeTask(modelData.id) } }
                                            }
                                            Text {
                                                width: parent.width - 28
                                                text: modelData.content
                                                font.pixelSize: 14
                                                color: modelData.isCompleted ? ThemeManager.secondaryTextColor : ThemeManager.textColor
                                                font.strikeout: modelData.isCompleted
                                                wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight
                                                lineHeight: 1.35; lineHeightMode: Text.ProportionalHeight
                                            }
                                        }

                                        Flow {
                                            width: parent.width
                                            spacing: 8
                                            leftPadding: 28
                                            visible: modelData.isRecurring || (modelData.dueString && modelData.dueString.length)
                                                     || (modelData.dueDate && modelData.dueDate.length)
                                                     || (modelData.labels && modelData.labels.length > 0)
                                            MChip { visible: modelData.isRecurring; dens: mdens; iconName: "repeat"; text: qsTr("повтор"); chipColor: ThemeManager.accentColor }
                                            MChip {
                                                visible: (modelData.dueString && modelData.dueString.length) || (modelData.dueDate && modelData.dueDate.length)
                                                dens: mdens; iconName: "calendar"
                                                text: (modelData.dueString && modelData.dueString.length) ? modelData.dueString : DateFormatter.format(modelData.dueDate)
                                                chipColor: (modelData.dueDate && modelData.dueDate.length && modelData.dueDate < ThemeManager.todayStr) ? ThemeManager.errorColor : ThemeManager.secondaryTextColor
                                            }
                                            Repeater {
                                                model: modelData.labels ? modelData.labels : []
                                                delegate: MChip {
                                                    required property var modelData
                                                    dens: mdens; tinted: true; iconName: "tag"; text: modelData
                                                    chipColor: { var l = dataStore.labelByName(modelData); return (l && l.color && l.color !== "charcoal" && l.color !== "") ? l.color : ThemeManager.secondaryTextColor }
                                                }
                                            }
                                        }
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.taskClicked(modelData.id)
                                        z: -1
                                    }
                                }
                            }

                            // + Добавить
                            Rectangle {
                                width: cardCol.width; height: 42; radius: 12
                                color: "transparent"
                                border.width: 1; border.color: ThemeManager.borderColor
                                Row {
                                    anchors.centerIn: parent; spacing: 8
                                    IconSvg { anchors.verticalCenter: parent.verticalCenter; name: "plus"; size: 15; color: ThemeManager.secondaryTextColor }
                                    Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Добавить"); font.pixelSize: 13; font.weight: Font.DemiBold; color: ThemeManager.secondaryTextColor }
                                }
                                MouseArea { anchors.fill: parent; onClicked: root.addRequested() }
                            }
                        }
                    }
                }
            }
        }
    }
}
