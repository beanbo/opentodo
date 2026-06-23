import QtQuick
import OpenTodoUI

// MTaskRow — swipeable mobile task row. Ported from m-components.jsx MTaskRow.
//   • swipe right past 92px  → complete (green backdrop, fling-off)
//   • swipe left  past 84px  → opens action drawer at −156 (Отложить + Удалить)
//   • long-press 480ms       → longPressed() (context menu)
//   • selection mode         → tap toggles selection; swipe/open disabled
// Axis is locked on first move (horizontal only when |dx|>|dy| and |dx|>6) so
// vertical list scrolling is preserved.
Item {
    id: root

    // ── Model inputs (bound by the host delegate) ──────────────────────────
    property string taskId: ""
    property string content: ""
    property int    priority: 4
    property bool   isCompleted: false
    property string dueDate: ""
    property string dueString: ""
    property bool   isRecurring: false
    property string deadline: ""
    property var    labels: []
    property string projectId: ""
    property var    dens
    property bool   showProject: false
    property bool   selectionMode: false
    property bool   selected: false

    // ── Signals ────────────────────────────────────────────────────────────
    signal toggleComplete()
    signal openTask()
    signal snooze()
    signal deleteTask()
    signal longPressed()
    signal selectToggle()

    // ── Swipe thresholds (design tokens) ───────────────────────────────────
    readonly property int _swComplete: 92
    readonly property int _swActions: 84
    readonly property int _swOpenAt: 156

    // ── Swipe runtime state ────────────────────────────────────────────────
    property real dragOffset: 0
    property bool _open: false
    property bool _dragging: false
    property string _axis: ""        // "" | "x" | "y"
    property real _px: 0
    property real _py: 0
    property real _base: 0
    property bool _lpFired: false

    // ── Subtask progress ───────────────────────────────────────────────────
    property int _subTotal: 0
    property int _subDone: 0
    function _refreshSubs() {
        if (!taskId || typeof taskViewModel === "undefined") { _subTotal = 0; _subDone = 0; return }
        var subs = taskViewModel.subtasksOf(taskId)
        if (!subs || subs.length === 0) { _subTotal = 0; _subDone = 0; return }
        var d = 0
        for (var i = 0; i < subs.length; i++) if (subs[i] && subs[i].isCompleted) d++
        _subTotal = subs.length; _subDone = d
    }
    Component.onCompleted: _refreshSubs()
    onTaskIdChanged: _refreshSubs()
    Connections {
        target: taskViewModel
        function onTaskChanged(id) { if (id === root.taskId || root._subTotal > 0) Qt.callLater(root._refreshSubs) }
        function onTaskAdded(id)   { Qt.callLater(root._refreshSubs) }
        function onTaskRemoved(id) { if (root._subTotal > 0) Qt.callLater(root._refreshSubs) }
    }

    // ── Due-date colour + label ────────────────────────────────────────────
    readonly property string _dueKind: {
        if (!dueDate || dueDate === "") return (dueString !== "" ? "future" : "")
        var today = ThemeManager.todayStr
        if (dueDate < today)  return "overdue"
        if (dueDate === today) return "today"
        var tomorrow = Qt.formatDate(new Date(Date.now() + 86400000), "yyyy-MM-dd")
        if (dueDate === tomorrow) return "soon"
        return "future"
    }
    readonly property color _dueColor:
        _dueKind === "overdue" ? ThemeManager.errorColor
        : _dueKind === "today" ? ThemeManager.successColor
        : _dueKind === "soon"  ? ThemeManager.priorityP2Color
                               : ThemeManager.secondaryTextColor
    readonly property string _dueLabel: {
        if (dueString && dueString !== "") return dueString
        if (!dueDate || dueDate === "") return ""
        if (_dueKind === "today") return qsTr("Сегодня")
        if (_dueKind === "soon")  return qsTr("Завтра")
        return DateFormatter.format(dueDate)
    }
    readonly property var _proj: {
        var _dep = projectViewModel.count   // re-resolve once the project model loads
        return (showProject && projectId !== "") ? projectViewModel.projectById(projectId) : null
    }

    width: parent ? parent.width : 0
    implicitHeight: Math.max(dens ? dens.rowMinH : 66, col.implicitHeight + (dens ? dens.rowPadV : 15) * 2)
    height: implicitHeight
    clip: true

    Behavior on dragOffset {
        enabled: !root._dragging
        NumberAnimation { duration: 260; easing.type: Easing.OutCubic }
    }

    Timer {
        id: lpTimer
        interval: 480
        onTriggered: { root._lpFired = true; haptics.medium(); root.longPressed() }
    }
    Timer {
        id: completeTimer
        interval: 180
        onTriggered: { root.toggleComplete(); root._open = false; root.dragOffset = 0 }
    }

    function _settle() {
        lpTimer.stop()
        root._dragging = false
        if (root.selectionMode || root._axis !== "x") { root._axis = ""; return }
        root._axis = ""
        if (root.dragOffset > root._swComplete) {
            haptics.medium()
            root.dragOffset = root.width + 40       // fling off
            completeTimer.restart()
            return
        }
        if (root.dragOffset < -root._swActions) { root._open = true; root.dragOffset = -root._swOpenAt; return }
        root._open = false; root.dragOffset = 0
    }
    function _close() { root._open = false; root.dragOffset = 0 }

    // ── Complete backdrop (revealed on right-swipe) ────────────────────────
    Rectangle {
        anchors.fill: parent
        color: ThemeManager.successColor
        opacity: root.dragOffset > 4 ? 1 : 0
        Row {
            anchors.left: parent.left
            anchors.leftMargin: 22
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12
            IconSvg { anchors.verticalCenter: parent.verticalCenter; name: "check-circle"; size: 24; color: "#ffffff" }
            Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Выполнить"); color: "#ffffff"; font.pixelSize: 14; font.weight: Font.DemiBold }
        }
    }

    // ── Action drawer (revealed on left-swipe): Отложить + Удалить ─────────
    Row {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        spacing: 0

        Rectangle {
            width: 78; height: parent.height
            color: ThemeManager.warningColor
            Column {
                anchors.centerIn: parent
                spacing: 3
                IconSvg { anchors.horizontalCenter: parent.horizontalCenter; name: "alarm"; size: 20; color: "#ffffff" }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Отложить"); color: "#ffffff"; font.pixelSize: 11; font.weight: Font.DemiBold }
            }
            MouseArea { anchors.fill: parent; onClicked: { root._close(); root.snooze() } }
        }
        Rectangle {
            width: 78; height: parent.height
            color: ThemeManager.errorColor
            Column {
                anchors.centerIn: parent
                spacing: 3
                IconSvg { anchors.horizontalCenter: parent.horizontalCenter; name: "trash"; size: 20; color: "#ffffff" }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Удалить"); color: "#ffffff"; font.pixelSize: 11; font.weight: Font.DemiBold }
            }
            MouseArea { anchors.fill: parent; onClicked: { haptics.heavy(); root._close(); root.deleteTask() } }
        }
    }

    // ── Foreground row (translates with the swipe) ─────────────────────────
    Item {
        id: fg
        x: root.dragOffset
        width: parent.width
        height: parent.height

        Rectangle {
            anchors.fill: parent
            color: root.selected ? ThemeManager.accentContainer : ThemeManager.backgroundColor
            Rectangle {  // bottom hairline
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: 1; color: ThemeManager.hairlineColor
            }
        }

        // Swipe + tap MouseArea — declared before the content so non-interactive
        // content (text/chips) lets taps fall through here, while MCheck (on top)
        // still handles its own taps.
        MouseArea {
            id: swipe
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onPressed: function(mouse) {
                var pt = mapToItem(root, mouse.x, mouse.y)
                root._px = pt.x; root._py = pt.y
                root._base = root._open ? -root._swOpenAt : 0
                root._axis = ""
                root._lpFired = false
                if (!root.selectionMode) { root._dragging = true; lpTimer.restart() }
            }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                var pt = mapToItem(root, mouse.x, mouse.y)
                var ddx = pt.x - root._px, ddy = pt.y - root._py
                if (Math.abs(ddx) > 6 || Math.abs(ddy) > 6) lpTimer.stop()
                if (root.selectionMode) return
                if (root._axis === "") {
                    if (Math.abs(ddx) < 6 && Math.abs(ddy) < 6) return
                    root._axis = Math.abs(ddx) > Math.abs(ddy) ? "x" : "y"
                }
                if (root._axis !== "x") return
                var nx = root._base + ddx
                root.dragOffset = Math.max(-root._swOpenAt - 24, Math.min(root._swComplete + 40, nx))
            }
            onReleased: root._settle()
            onCanceled: { lpTimer.stop(); root._settle() }
            onClicked: function(mouse) {
                if (root._lpFired) { root._lpFired = false; return }
                if (root.selectionMode) { root.selectToggle(); return }
                if (root._open) { root._close(); return }
                if (Math.abs(root.dragOffset) < 4) root.openTask()
            }
        }

        // ── Checkbox / selection circle ────────────────────────────────────
        Item {
            id: checkBox
            anchors.left: parent.left
            anchors.leftMargin: root.dens ? root.dens.rowPadH : 18
            anchors.top: parent.top
            anchors.topMargin: (root.dens ? root.dens.rowPadV : 15) + 1
            width: root.dens ? root.dens.check : 24
            height: width

            // Selection circle
            Rectangle {
                visible: root.selectionMode
                anchors.fill: parent
                radius: width / 2
                color: root.selected ? ThemeManager.accentColor : "transparent"
                border.width: 2
                border.color: root.selected ? ThemeManager.accentColor : ThemeManager.faintTextColor
                IconSvg {
                    visible: root.selected
                    anchors.centerIn: parent
                    name: "check"; size: root.dens ? root.dens.checkInner : 13
                    color: ThemeManager.accentForeground
                }
                MouseArea { anchors.fill: parent; anchors.margins: -8; onClicked: root.selectToggle() }
            }

            // Priority checkbox
            MCheck {
                visible: !root.selectionMode
                anchors.fill: parent
                dens: root.dens
                done: root.isCompleted
                priority: root.priority
                onToggled: root.toggleComplete()
            }
        }

        // ── Content column ─────────────────────────────────────────────────
        Item {
            anchors.left: checkBox.right
            anchors.leftMargin: root.dens ? root.dens.rowGap : 14
            anchors.right: parent.right
            anchors.rightMargin: root.dens ? root.dens.rowPadH : 18
            anchors.top: parent.top
            anchors.topMargin: root.dens ? root.dens.rowPadV : 15
            height: col.implicitHeight

            Column {
                id: col
                width: parent.width
                spacing: 7

                Text {
                    width: parent.width
                    text: root.content
                    color: root.isCompleted ? ThemeManager.secondaryTextColor : ThemeManager.textColor
                    font.pixelSize: root.dens ? root.dens.title : 16
                    font.strikeout: root.isCompleted
                    lineHeight: root.dens ? root.dens.titleLh : 1.32
                    lineHeightMode: Text.ProportionalHeight
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                Flow {
                    width: parent.width
                    spacing: 10
                    visible: root.isRecurring || root._dueLabel !== "" || root.deadline !== ""
                             || root._subTotal > 0 || (root.labels && root.labels.length > 0)
                             || root._proj !== null

                    MChip {
                        visible: root.isRecurring
                        dens: root.dens
                        iconName: "repeat"; text: qsTr("повтор"); chipColor: ThemeManager.accentColor
                    }
                    MChip {
                        visible: root._dueLabel !== ""
                        dens: root.dens
                        iconName: "calendar"; text: root._dueLabel; chipColor: root._dueColor
                    }
                    MChip {
                        visible: root.deadline !== ""
                        dens: root.dens
                        iconName: "flag"; text: DateFormatter.format(root.deadline); chipColor: ThemeManager.errorColor
                    }
                    MChip {
                        visible: root._subTotal > 0
                        dens: root.dens
                        iconName: root._subDone === root._subTotal ? "check-circle" : "list"
                        text: root._subDone + "/" + root._subTotal
                        chipColor: root._subDone === root._subTotal ? ThemeManager.successColor : ThemeManager.secondaryTextColor
                    }
                    Repeater {
                        model: root.labels ? root.labels : []
                        delegate: MChip {
                            required property var modelData
                            dens: root.dens
                            tinted: true
                            iconName: "tag"
                            text: modelData
                            chipColor: {
                                var l = dataStore.labelByName(modelData)
                                return (l && l.color && l.color !== "charcoal" && l.color !== "")
                                       ? l.color : ThemeManager.secondaryTextColor
                            }
                        }
                    }
                    MChip {
                        visible: root._proj !== null
                        dens: root.dens
                        iconName: "hash"
                        text: root._proj ? root._proj.name : ""
                        chipColor: (root._proj && root._proj.color && root._proj.color !== "")
                                   ? root._proj.color : ThemeManager.secondaryTextColor
                    }
                }

                Rectangle {  // subtask progress bar
                    visible: root._subTotal > 0 && root._subDone < root._subTotal
                    width: parent.width
                    height: 3
                    radius: 2
                    color: ThemeManager.hairlineColor
                    Rectangle {
                        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                        width: parent.width * (root._subTotal > 0 ? root._subDone / root._subTotal : 0)
                        radius: 2
                        color: ThemeManager.accentColor
                        Behavior on width { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }
                    }
                }
            }
        }
    }
}
