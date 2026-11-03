import QtQuick
import QtQuick.Controls
import OpenTodoUI

// MEditorScreen — full-screen mobile task editor. Ported from m-screens2.jsx
// MEditorScreen. Toolbar + title/description + property meta-rows (each opens a
// picker sheet) + subtasks + comments + history + bottom quick-action bar.
// Operates on taskViewModel.selectedTask (an existing task).
Rectangle {
    id: root
    color: ThemeManager.surfaceColor

    signal closeRequested()

    // Flush in-progress title/description edits. Hosts call this before
    // tearing the editor down (e.g. the Android back gesture), since
    // destroying a focused TextArea doesn't reliably commit it.
    function commitEdits() {
        titleField.commit()
        descField.commit()
    }

    readonly property var    task: taskViewModel.selectedTask
    readonly property string tid: task ? task.id : ""
    readonly property color  _prio: {
        if (!task) return ThemeManager.priorityP4Color
        switch (task.priority) {
        case 1: return ThemeManager.priorityP1Color
        case 2: return ThemeManager.priorityP2Color
        case 3: return ThemeManager.priorityP3Color
        default: return ThemeManager.priorityP4Color
        }
    }

    // ── Subtasks ───────────────────────────────────────────────────────────
    property var _subs: []
    function _refreshSubs() { _subs = (tid !== "") ? taskViewModel.getSubtasks(tid) : [] }
    readonly property int _subDone: { var n = 0; for (var i = 0; i < _subs.length; i++) if (_subs[i].isCompleted) n++; return n }

    // ── Comments ───────────────────────────────────────────────────────────
    property var _comments: []
    function _refreshComments() { _comments = (tid !== "") ? dataStore.commentsForTask(tid) : [] }

    // Q-audit #17: do NOT imperatively assign titleField.text/descField.text here
    // — that breaks the declarative `text: root.task.content/description` bindings
    // below, so an inbound sync or AI edit of the open task would never refresh the
    // fields. The bindings already re-seed when root.task changes (and a user edit
    // naturally takes over the field until the editor is reopened).
    onTidChanged: { _refreshSubs(); _refreshComments() }
    Component.onCompleted: { _refreshSubs(); _refreshComments() }
    Connections {
        target: taskViewModel
        function onTaskChanged(id) { if (id === root.tid) root._refreshSubs() }
        function onTaskAdded() { root._refreshSubs() }
        function onTaskRemoved() { root._refreshSubs() }
    }
    Connections { target: dataStore; ignoreUnknownSignals: true
        function onCommentsChanged() { root._refreshComments() }
        // Re-resolve author labels when a collaborator profile is edited.
        function onCollaboratorsChanged() { root._refreshComments() }
    }

    function _dueLabel() {
        if (!task) return qsTr("Без срока")
        if (task.dueString && task.dueString.length) return task.dueString
        if (task.dueDate && task.dueDate.length) return DateFormatter.format(task.dueDate)
        return qsTr("Без срока")
    }

    // ── Toolbar ────────────────────────────────────────────────────────────
    Rectangle {
        id: toolbar
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: 58
        color: ThemeManager.surfaceColor
        MIconButton {
            id: closeBtn
            anchors.left: parent.left; anchors.leftMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            iconName: "chevron-down"; iconColor: ThemeManager.textColor; iconSize: 24
            onClicked: { root.commitEdits(); root.closeRequested() }
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            MIconButton { iconName: "flag"; iconColor: root._prio; iconSize: 20; onClicked: prioSheet.open = true }
            MIconButton {
                iconName: "pin"
                iconColor: (root.task && root.task.isPinned) ? ThemeManager.accentColor : ThemeManager.secondaryTextColor
                iconSize: 19
                onClicked: if (root.task) taskViewModel.updateTask(root.tid, "isPinned", !root.task.isPinned)
            }
        }
    }

    Flickable {
        id: scroll
        anchors.top: toolbar.bottom; anchors.left: parent.left; anchors.right: parent.right
        anchors.bottom: actionBar.top
        contentHeight: body.implicitHeight + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: body
            width: parent.width
            spacing: 0

            // Title + checkbox + description
            Item {
                width: parent.width
                height: titleBlock.implicitHeight + 22
                Rectangle {
                    id: bigCheck
                    anchors.left: parent.left; anchors.leftMargin: 18
                    anchors.top: parent.top; anchors.topMargin: 9
                    width: 26; height: 26; radius: 13
                    color: (root.task && root.task.isCompleted) ? ThemeManager.successColor : "transparent"
                    border.width: 2
                    border.color: (root.task && root.task.isCompleted) ? ThemeManager.successColor : root._prio
                    IconSvg { visible: root.task && root.task.isCompleted; anchors.centerIn: parent; name: "check"; size: 14; color: ThemeManager.accentForeground }
                    MouseArea {
                        anchors.fill: parent; anchors.margins: -6
                        onClicked: {
                            if (!root.task) return
                            if (root.task.isCompleted) taskViewModel.uncompleteTask(root.tid)
                            else taskViewModel.completeTask(root.tid)
                        }
                    }
                }
                Column {
                    id: titleBlock
                    anchors.left: bigCheck.right; anchors.leftMargin: 14
                    anchors.right: parent.right; anchors.rightMargin: 18
                    anchors.top: parent.top; anchors.topMargin: 6
                    spacing: 8
                    TextArea {
                        id: titleField
                        width: parent.width
                        padding: 0
                        wrapMode: Text.WordWrap
                        text: root.task ? root.task.content : ""
                        font.pixelSize: 21; font.weight: Font.Bold
                        font.strikeout: root.task && root.task.isCompleted
                        color: (root.task && root.task.isCompleted) ? ThemeManager.secondaryTextColor : ThemeManager.textColor
                        background: Item {}
                        function commit() {
                            if (!root.task) return
                            var t = text.trim()
                            if (t !== "" && t !== root.task.content) taskViewModel.updateTask(root.tid, "content", t)
                        }
                        onActiveFocusChanged: if (!activeFocus) commit()
                    }
                    TextArea {
                        id: descField
                        width: parent.width
                        padding: 0
                        wrapMode: Text.WordWrap
                        text: root.task ? root.task.description : ""
                        placeholderText: qsTr("Описание…")
                        font.pixelSize: 14
                        color: ThemeManager.secondaryTextColor
                        placeholderTextColor: ThemeManager.faintTextColor
                        background: Item {}
                        function commit() {
                            if (!root.task) return
                            if (text !== root.task.description) taskViewModel.updateTask(root.tid, "description", text)
                        }
                        onActiveFocusChanged: if (!activeFocus) commit()
                    }
                }
            }

            // ── Property meta-rows ─────────────────────────────────────────
            Rectangle {
                width: parent.width
                height: metaCol.implicitHeight
                color: ThemeManager.backgroundColor
                Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: ThemeManager.hairlineColor }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: ThemeManager.hairlineColor }
                Column {
                    id: metaCol
                    width: parent.width
                    MEditorMetaRow { width: parent.width; iconName: "calendar"; iconColor: ThemeManager.accentColor; label: qsTr("Срок"); value: root._dueLabel(); valueColor: ThemeManager.accentColor; onClicked: dateSheet.open = true }
                    MEditorMetaRow { width: parent.width; iconName: "flag"; iconColor: root._prio; label: qsTr("Приоритет"); value: root.task ? ("P" + root.task.priority) : "P4"; valueColor: root._prio; onClicked: prioSheet.open = true }
                    MEditorMetaRow { width: parent.width; iconName: "hash"; label: qsTr("Проект"); value: root._projName(); onClicked: { projSheet.options = root._projectOptions(); projSheet.open = true } }
                    MEditorMetaRow { width: parent.width; iconName: "tag"; label: qsTr("Метки"); value: (root.task && root.task.labels && root.task.labels.length) ? root.task.labels.join(", ") : qsTr("Добавить"); valueColor: (root.task && root.task.labels && root.task.labels.length) ? ThemeManager.textColor : ThemeManager.secondaryTextColor; onClicked: { labelsSheet.options = root._labelOptions(); labelsSheet.selectedIds = (root.task ? root.task.labels.slice() : []); labelsSheet.open = true } }
                    MEditorMetaRow { width: parent.width; iconName: "repeat"; label: qsTr("Повтор"); value: (root.task && root.task.dueIsRecurring) ? qsTr("Повтор") : qsTr("Не повторять"); valueColor: (root.task && root.task.dueIsRecurring) ? ThemeManager.accentColor : ThemeManager.secondaryTextColor; onClicked: repeatSheet.open = true }
                    MEditorMetaRow { width: parent.width; iconName: "alarm"; label: qsTr("Напоминание"); value: root._reminderLabel; valueColor: root._reminderId === "Без напоминания" ? ThemeManager.secondaryTextColor : ThemeManager.textColor; last: true; onClicked: reminderSheet.open = true }
                }
            }

            // ── Subtasks ───────────────────────────────────────────────────
            Item {
                width: parent.width
                height: subCol.implicitHeight + 28
                Column {
                    id: subCol
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.leftMargin: 18; anchors.rightMargin: 18
                    anchors.top: parent.top; anchors.topMargin: 16
                    spacing: 0

                    Row {
                        width: parent.width
                        height: 24
                        spacing: 10
                        Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Подзадачи"); font.pixelSize: 14; font.weight: Font.Bold; color: ThemeManager.textColor }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: root._subDone + "/" + root._subs.length; font.pixelSize: 12; font.weight: Font.DemiBold; color: ThemeManager.secondaryTextColor }
                        Item {
                            width: parent.width - 200; height: 4
                            anchors.verticalCenter: parent.verticalCenter
                            Rectangle { anchors.fill: parent; radius: 2; color: ThemeManager.hairlineColor
                                Rectangle { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; radius: 2; color: ThemeManager.accentColor; width: parent.width * (root._subs.length > 0 ? root._subDone / root._subs.length : 0) } }
                        }
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: aiRow.width + 16; height: 24; radius: 8
                            color: ThemeManager.accentContainer
                            Row { id: aiRow; anchors.centerIn: parent; spacing: 4
                                IconSvg { anchors.verticalCenter: parent.verticalCenter; name: "sparkles"; size: 13; color: ThemeManager.accentColor }
                                Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("ИИ"); font.pixelSize: 12; font.weight: Font.Bold; color: ThemeManager.accentColor } }
                            MouseArea { anchors.fill: parent; onClicked: { subtaskGen.taskId = root.tid; subtaskGen.taskContent = root.task ? root.task.content : ""; subtaskGen.open() } }
                        }
                    }

                    Repeater {
                        model: root._subs
                        delegate: Item {
                            required property var modelData
                            width: subCol.width
                            height: 44
                            Rectangle {
                                id: sc
                                anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                                width: 21; height: 21; radius: 11
                                color: modelData.isCompleted ? ThemeManager.successColor : "transparent"
                                border.width: 2; border.color: modelData.isCompleted ? ThemeManager.successColor : ThemeManager.secondaryTextColor
                                IconSvg { visible: modelData.isCompleted; anchors.centerIn: parent; name: "check"; size: 11; color: ThemeManager.accentForeground }
                                MouseArea { anchors.fill: parent; anchors.margins: -6; onClicked: { if (modelData.isCompleted) taskViewModel.uncompleteTask(modelData.id); else taskViewModel.completeTask(modelData.id) } }
                            }
                            Text {
                                anchors.left: sc.right; anchors.leftMargin: 12; anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.content
                                font.pixelSize: 14
                                color: modelData.isCompleted ? ThemeManager.secondaryTextColor : ThemeManager.textColor
                                font.strikeout: modelData.isCompleted
                                elide: Text.ElideRight
                            }
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: ThemeManager.hairlineColor }
                        }
                    }

                    // Add subtask
                    Row {
                        width: parent.width
                        height: 44
                        spacing: 9
                        IconSvg { anchors.verticalCenter: parent.verticalCenter; name: "plus"; size: 16; color: ThemeManager.secondaryTextColor }
                        TextField {
                            id: subInput
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 30
                            placeholderText: qsTr("Добавить подзадачу")
                            font.pixelSize: 14; color: ThemeManager.textColor
                            placeholderTextColor: ThemeManager.secondaryTextColor
                            background: Item {}
                            onAccepted: { var t = text.trim(); if (t !== "" && root.tid !== "") { taskViewModel.addSubtask(root.tid, t); text = ""; root._refreshSubs() } }
                        }
                    }
                }
            }

            // ── Comments ───────────────────────────────────────────────────
            Item {
                width: parent.width
                height: cmtCol.implicitHeight + 16
                Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: ThemeManager.hairlineColor }
                Column {
                    id: cmtCol
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.leftMargin: 18; anchors.rightMargin: 18
                    anchors.top: parent.top; anchors.topMargin: 14
                    spacing: 14
                    Text { text: qsTr("Комментарии"); font.pixelSize: 14; font.weight: Font.Bold; color: ThemeManager.textColor }

                    Repeater {
                        model: root._comments
                        delegate: Item {
                            id: cmtDelegate
                            required property var modelData
                            // Author id resolves to a first-class collaborator
                            // profile (name/colour/icon), configurable in Settings.
                            // Empty author ⇒ the built-in "me".
                            readonly property string authorId:      modelData.author
                            readonly property bool   isAssistant:   dataStore.collaboratorIsAssistant(authorId)
                            readonly property string overrideColor: dataStore.collaboratorColor(authorId)
                            readonly property string avatarIcon:    dataStore.collaboratorIcon(authorId)
                            readonly property string displayName:   dataStore.collaboratorName(authorId)
                            width: cmtCol.width
                            height: cmtBody.implicitHeight + 26
                            Rectangle {
                                id: avatar
                                anchors.left: parent.left; anchors.top: parent.top
                                width: 32; height: 32; radius: 16
                                color: cmtDelegate.overrideColor !== "" ? cmtDelegate.overrideColor
                                       : (cmtDelegate.isAssistant ? ThemeManager.accentColor
                                                                  : ThemeManager.borderColor)
                                IconSvg { anchors.centerIn: parent
                                    visible: cmtDelegate.avatarIcon !== ""
                                    name: cmtDelegate.avatarIcon
                                    size: 16
                                    color: cmtDelegate.isAssistant ? ThemeManager.accentForeground : ThemeManager.textColor }
                                Text { anchors.centerIn: parent
                                    visible: cmtDelegate.avatarIcon === ""
                                    text: cmtDelegate.displayName.length ? cmtDelegate.displayName.charAt(0).toUpperCase() : "?"
                                    color: cmtDelegate.isAssistant ? ThemeManager.accentForeground : ThemeManager.textColor
                                    font.pixelSize: 13; font.weight: Font.Bold }
                            }
                            Column {
                                id: cmtBody
                                anchors.left: avatar.right; anchors.leftMargin: 12; anchors.right: parent.right
                                anchors.top: parent.top
                                spacing: 3
                                Row { spacing: 8
                                    Text { text: cmtDelegate.displayName; font.pixelSize: 13; font.weight: Font.Bold; color: ThemeManager.textColor }
                                    Text { text: modelData.postedAt ? Qt.formatDateTime(modelData.postedAt, "dd.MM HH:mm") : ""; font.pixelSize: 11; color: ThemeManager.faintTextColor; anchors.bottom: parent.bottom } }
                                Text { width: parent.width; text: modelData.content || ""; wrapMode: Text.WordWrap; font.pixelSize: 14; color: ThemeManager.textColor; lineHeight: 1.45; lineHeightMode: Text.ProportionalHeight }
                            }
                        }
                    }

                    // Add comment
                    Row {
                        width: parent.width
                        spacing: 10
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 52; height: 42; radius: 21
                            color: ThemeManager.backgroundColor
                            border.width: 1; border.color: ThemeManager.borderColor
                            TextField {
                                id: cmtInput
                                anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 12
                                verticalAlignment: Text.AlignVCenter
                                placeholderText: qsTr("Добавить комментарий…")
                                font.pixelSize: 14; color: ThemeManager.textColor
                                placeholderTextColor: ThemeManager.secondaryTextColor
                                background: Item {}
                                onAccepted: root._addComment()
                            }
                        }
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 42; height: 42; radius: 21
                            color: cmtInput.text.trim() !== "" ? ThemeManager.accentColor : ThemeManager.hoverColor
                            IconSvg { anchors.centerIn: parent; name: "send"; size: 17; color: cmtInput.text.trim() !== "" ? ThemeManager.accentForeground : ThemeManager.faintTextColor }
                            MouseArea { anchors.fill: parent; onClicked: root._addComment() }
                        }
                    }
                }
            }

            // ── History (collapsible) ──────────────────────────────────────
            Item {
                width: parent.width
                height: histCol.implicitHeight + 12
                Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: ThemeManager.hairlineColor }
                Column {
                    id: histCol
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.leftMargin: 18; anchors.rightMargin: 18
                    anchors.top: parent.top; anchors.topMargin: 6
                    spacing: 0
                    property bool expanded: false
                    Row {
                        width: parent.width; height: 44; spacing: 8
                        IconSvg { anchors.verticalCenter: parent.verticalCenter; name: "chevron-down"; size: 15; color: ThemeManager.secondaryTextColor; rotation: histCol.expanded ? 0 : -90 }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("История изменений"); font.pixelSize: 14; font.weight: Font.Bold; color: ThemeManager.textColor }
                        MouseArea { anchors.fill: parent; onClicked: histCol.expanded = !histCol.expanded }
                    }
                    Column {
                        visible: histCol.expanded
                        width: parent.width
                        spacing: 0
                        MEditorHistoryRow { width: parent.width; label: qsTr("Изменена"); when: root.task ? Qt.formatDateTime(root.task.updatedAt, "dd.MM.yyyy HH:mm") : "" }
                        MEditorHistoryRow { width: parent.width; label: qsTr("Задача создана"); when: root.task ? Qt.formatDateTime(root.task.createdAt, "dd.MM.yyyy HH:mm") : "" }
                    }
                }
            }
        }
    }

    // ── Bottom quick-action bar ────────────────────────────────────────────
    Rectangle {
        id: actionBar
        anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
        height: 64
        color: ThemeManager.surfaceColor
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: ThemeManager.borderColor }
        Row {
            anchors.centerIn: parent
            spacing: (parent.width - 5 * 48) / 6
            MIconButton { iconName: "calendar"; iconColor: ThemeManager.secondaryTextColor; iconSize: 22; diameter: 48; onClicked: dateSheet.open = true }
            MIconButton { iconName: "flag"; iconColor: ThemeManager.secondaryTextColor; iconSize: 22; diameter: 48; onClicked: prioSheet.open = true }
            MIconButton { iconName: "tag"; iconColor: ThemeManager.secondaryTextColor; iconSize: 22; diameter: 48; onClicked: { labelsSheet.options = root._labelOptions(); labelsSheet.selectedIds = (root.task ? root.task.labels.slice() : []); labelsSheet.open = true } }
            MIconButton { iconName: "alarm"; iconColor: ThemeManager.secondaryTextColor; iconSize: 22; diameter: 48; onClicked: reminderSheet.open = true }
            MIconButton { iconName: "repeat"; iconColor: ThemeManager.secondaryTextColor; iconSize: 22; diameter: 48; onClicked: repeatSheet.open = true }
        }
    }

    // ── Helpers for option lists ───────────────────────────────────────────
    function _projName() {
        if (!root.task) return ""
        var p = projectViewModel.projectById(root.task.projectId)
        return p ? p.name : qsTr("Входящие")
    }
    function _projectOptions() {
        var arr = []
        for (var i = 0; i < projectViewModel.count; i++) {
            var p = projectViewModel.projectAt(i)
            if (!p || p.isArchived) continue
            arr.push({ "id": p.id, "label": p.name, "sub": projectViewModel.taskCountForProject(p.id) + " задач",
                       "icon": (p.icon && p.icon.length > 0) ? p.icon : "hash",
                       "color": (p.color && p.color.length > 0) ? p.color : "" })
        }
        return arr
    }
    function _labelOptions() {
        var arr = []
        var labs = (typeof sidebarViewModel !== "undefined" && sidebarViewModel.labels) ? sidebarViewModel.labels : []
        for (var i = 0; i < labs.length; i++)
            arr.push({ "id": labs[i].name, "label": labs[i].name, "icon": "tag", "color": labs[i].color || ThemeManager.secondaryTextColor })
        return arr
    }
    function _addComment() {
        var t = cmtInput.text.trim()
        if (t === "" || root.tid === "") return
        dataStore.addComment(t, root.tid, root.task ? root.task.projectId : "", "")
        cmtInput.text = ""
        root._refreshComments()
    }

    // ── Reminder ───────────────────────────────────────────────────────────
    // Backed by real Reminder entities (type "relative" = minuteOffset before
    // the task's due) created / updated / removed through dataStore, so they
    // persist, sync and are scheduled (and re-planned when the due moves) by
    // ReminderScheduler — like the desktop EditorRemindersSection. This used
    // to fire a bare one-shot "reminder-<tid>" notification: no entity, the
    // label reset on reopen, "Без напоминания" cancelled nothing, never synced.
    readonly property var _reminderOffsets: ({ "В момент срока": 0, "За 10 минут": 10,
                                               "За 1 час": 60, "За день": 1440 })
    property int _reminderToken: 0            // bumped on dataStore.remindersChanged
    function _taskReminders() {
        var out = []
        if (root.tid === "") return out
        var list = dataStore.remindersForTask(root.tid)
        if (list) for (var i = 0; i < list.length; i++) if (list[i]) out.push(list[i])
        return out
    }
    function _relativeReminder(rems) {
        for (var i = 0; i < rems.length; i++) if (rems[i].type === "relative") return rems[i]
        return null
    }
    // Option id of the current state ("" when it matches no preset).
    readonly property string _reminderId: {
        var _dep = root._reminderToken
        var rems = root._taskReminders()
        if (rems.length === 0) return "Без напоминания"
        var rel = root._relativeReminder(rems)
        if (!rel) return ""
        for (var k in root._reminderOffsets)
            if (root._reminderOffsets[k] === rel.minuteOffset) return k
        return ""
    }
    readonly property string _reminderLabel: {
        var _dep = root._reminderToken
        switch (root._reminderId) {
        case "Без напоминания": return qsTr("Без напоминания")
        case "В момент срока":  return qsTr("В момент срока")
        case "За 10 минут":     return qsTr("За 10 минут")
        case "За 1 час":        return qsTr("За 1 час")
        case "За день":         return qsTr("За день")
        }
        var rems = root._taskReminders()
        var rel = root._relativeReminder(rems)
        if (rel) return qsTr("За %1 мин").arg(rel.minuteOffset)
        return qsTr("Напоминаний: %1").arg(rems.length)
    }
    Connections { target: dataStore; ignoreUnknownSignals: true
        function onRemindersChanged() { root._reminderToken++ }
    }
    function _applyReminderChoice(choice) {
        if (root.tid === "") return
        var rems = root._taskReminders()
        if (choice === "Без напоминания") {
            for (var i = 0; i < rems.length; i++) dataStore.removeReminder(rems[i].id)
            return
        }
        var off = root._reminderOffsets[choice]
        if (off === undefined) return
        var rel = root._relativeReminder(rems)
        if (!rel) rel = dataStore.addReminder(root.tid, "relative")
        if (!rel) return
        // minuteOffset is a Q_PROPERTY (its setter is not invokable); stamp the
        // field so the change wins on sync — this also emits remindersChanged,
        // which makes ReminderScheduler (re)arm it.
        rel.minuteOffset = off
        dataStore.markReminderFieldsModified(rel.id, ["minute_offset"])
    }

    // ── Picker sheets ──────────────────────────────────────────────────────
    MEditorDateSheet {
        id: dateSheet
        anchors.fill: parent; z: 60
        onCloseRequested: open = false
        onPicked: function(dueString) { if (root.tid !== "") taskViewModel.setTaskDue(root.tid, dueString); open = false }
    }
    MOptionSheet {
        id: prioSheet
        anchors.fill: parent; z: 60
        title: qsTr("Приоритет")
        currentId: root.task ? String(root.task.priority) : "4"
        options: [
            { "id": "1", "label": qsTr("Приоритет 1"), "sub": qsTr("Срочно и важно"), "icon": "flag", "color": ThemeManager.priorityP1Color },
            { "id": "2", "label": qsTr("Приоритет 2"), "icon": "flag", "color": ThemeManager.priorityP2Color },
            { "id": "3", "label": qsTr("Приоритет 3"), "icon": "flag", "color": ThemeManager.priorityP3Color },
            { "id": "4", "label": qsTr("Приоритет 4"), "sub": qsTr("Без приоритета"), "icon": "circle", "color": ThemeManager.priorityP4Color }
        ]
        onSelected: function(id) { if (root.tid !== "") taskViewModel.setTaskPriority(root.tid, parseInt(id)); open = false }
        onCloseRequested: open = false
    }
    MOptionSheet {
        id: projSheet
        anchors.fill: parent; z: 60
        title: qsTr("Проект")
        currentId: root.task ? root.task.projectId : ""
        onSelected: function(id) { if (root.tid !== "") taskViewModel.moveTask(root.tid, id, ""); open = false }
        onCloseRequested: open = false
    }
    MOptionSheet {
        id: labelsSheet
        anchors.fill: parent; z: 60
        title: qsTr("Метки"); subtitle: qsTr("Можно выбрать несколько")
        multi: true; showDone: true
        onSelected: function(id) {
            var s = selectedIds.slice()
            var i = s.indexOf(id)
            if (i >= 0) s.splice(i, 1); else s.push(id)
            selectedIds = s
            if (root.tid !== "") taskViewModel.setTaskLabels(root.tid, s)
        }
        onCloseRequested: open = false
    }
    MOptionSheet {
        id: repeatSheet
        anchors.fill: parent; z: 60
        title: qsTr("Повтор")
        options: [
            { "id": "Не повторять", "label": qsTr("Не повторять"), "icon": "repeat", "color": ThemeManager.secondaryTextColor },
            { "id": "каждый день", "label": qsTr("Каждый день"), "icon": "repeat", "color": ThemeManager.accentColor },
            { "id": "каждую неделю", "label": qsTr("Каждую неделю"), "icon": "repeat", "color": ThemeManager.accentColor },
            { "id": "каждые 2 недели", "label": qsTr("Каждые 2 недели"), "icon": "repeat", "color": ThemeManager.accentColor },
            { "id": "каждый месяц", "label": qsTr("Каждый месяц"), "icon": "repeat", "color": ThemeManager.accentColor },
            { "id": "каждый год", "label": qsTr("Каждый год"), "icon": "repeat", "color": ThemeManager.accentColor }
        ]
        onSelected: function(id) {
            if (root.tid !== "") {
                if (id === "Не повторять") taskViewModel.setTaskDue(root.tid, root.task && root.task.dueDate ? root.task.dueDate : "")
                else taskViewModel.setTaskDue(root.tid, id)
            }
            open = false
        }
        onCloseRequested: open = false
    }
    MOptionSheet {
        id: reminderSheet
        anchors.fill: parent; z: 60
        title: qsTr("Напоминание")
        options: [
            { "id": "Без напоминания", "label": qsTr("Без напоминания"), "icon": "alarm", "color": ThemeManager.secondaryTextColor },
            { "id": "В момент срока", "label": qsTr("В момент срока"), "icon": "alarm", "color": ThemeManager.priorityP2Color },
            { "id": "За 10 минут", "label": qsTr("За 10 минут"), "icon": "alarm", "color": ThemeManager.priorityP2Color },
            { "id": "За 1 час", "label": qsTr("За 1 час"), "icon": "alarm", "color": ThemeManager.priorityP2Color },
            { "id": "За день", "label": qsTr("За день"), "icon": "alarm", "color": ThemeManager.priorityP2Color }
        ]
        currentId: root._reminderId
        onSelected: function(id) { root._applyReminderChoice(id); open = false }
        onCloseRequested: open = false
    }

    // AI subtask generator (reuses the existing dialog).
    SubtaskGeneratorDialog {
        id: subtaskGen
        onSubtasksAdded: root._refreshSubs()
    }
}
