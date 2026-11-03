import QtQuick
import QtQuick.Controls
import OpenTodoUI

// MQuickAdd — compact quick-add sheet above the keyboard. Ported from
// m-screens2.jsx MQuickAdd: priority circle + real text input (autofocus) +
// send button, and a chip row that cycles Срок / Приоритет / Проект / Метка.
// Creates a task via taskViewModel.addTask and emits created() for the snackbar.
Item {
    id: root

    property bool open: false
    signal closeRequested()
    signal created(string message)

    // Cycling state
    property string dueState: "Сегодня"      // "Сегодня" | "Завтра" | "" (none)
    property int    prio: 4
    // Index into _projects, or -1 = no explicit pick (TaskViewModel's default:
    // the current project, else the Inbox).
    property int    projIdx: -1
    property bool   hasLabel: false
    property var    _projects: []
    property string _labelName: ""

    function _refreshLists() {
        var ps = []
        for (var i = 0; i < projectViewModel.count; i++) {
            var p = projectViewModel.projectAt(i)
            if (p && !p.isArchived) ps.push({ "id": p.id, "name": p.name, "isInbox": p.isInbox,
                                              "icon": (p.icon && p.icon.length > 0) ? p.icon : "hash",
                                              "color": (p.color && p.color.length > 0) ? p.color : ThemeManager.accentColor })
        }
        root._projects = ps
        var labs = (typeof sidebarViewModel !== "undefined" && sidebarViewModel.labels) ? sidebarViewModel.labels : []
        root._labelName = labs.length > 0 ? (labs[0].name || "") : ""
    }

    // Preselect the project being viewed (project list / board / calendar);
    // elsewhere leave it to the VM default instead of forcing _projects[0]
    // (which filed tasks from Today/Upcoming/labels into an arbitrary project).
    function _defaultProjIdx() {
        var fm = taskViewModel.filterMode
        var cur = taskViewModel.currentProjectId
        if (!cur || !(fm === TaskViewModel.Project || fm === TaskViewModel.Board
                      || fm === TaskViewModel.Calendar))
            return -1
        for (var i = 0; i < _projects.length; i++)
            if (_projects[i].id === cur) return i
        return -1
    }
    // What the "no explicit pick" state resolves to (for display, and for the
    // template path whose C++ side does not apply the VM default).
    function _defaultProject() {
        var cur = taskViewModel.currentProjectId
        var inbox = null
        for (var i = 0; i < _projects.length; i++) {
            if (cur && _projects[i].id === cur) return _projects[i]
            if (_projects[i].isInbox && !inbox) inbox = _projects[i]
        }
        return inbox
    }

    function openAdd() {
        _refreshLists()
        dueState = "Сегодня"; prio = 4; projIdx = _defaultProjIdx(); hasLabel = false
        input.text = ""
        root.open = true
        Qt.callLater(function() { input.forceActiveFocus() })
    }

    readonly property color _prioColor: {
        switch (prio) {
        case 1: return ThemeManager.priorityP1Color
        case 2: return ThemeManager.priorityP2Color
        case 3: return ThemeManager.priorityP3Color
        default: return ThemeManager.priorityP4Color
        }
    }
    readonly property var _curProj: (projIdx >= 0 && projIdx < _projects.length) ? _projects[projIdx] : null
    // Shown on the chip: the explicit pick, else what the default resolves to.
    readonly property var _shownProj: _curProj ? _curProj : (_projects.length > 0 ? _defaultProject() : null)

    function _send() {
        var txt = input.text.trim()
        if (txt === "") return
        var due = dueState === "Сегодня" ? "сегодня" : dueState === "Завтра" ? "завтра" : ""
        var labels = (hasLabel && _labelName !== "") ? [_labelName] : []
        var pid = _curProj ? _curProj.id : ""
        taskViewModel.addTask(txt, pid, prio, due, labels)
        root.open = false
        root.created(qsTr("Задача добавлена"))
    }

    anchors.fill: parent
    visible: open || panel.y < height
    z: 105

    Rectangle {
        id: scrim
        anchors.fill: parent
        color: ThemeManager.scrimColor
        opacity: root.open ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        MouseArea { anchors.fill: parent; enabled: root.open; onClicked: root.closeRequested() }
    }

    Rectangle {
        id: panel
        anchors.left: parent.left
        anchors.right: parent.right
        height: inputRow.height + chipScroll.height + 38
        // Lift above the on-screen keyboard when visible (Android).
        readonly property real _kb: Qt.inputMethod.visible
                                    ? Qt.inputMethod.keyboardRectangle.height / Screen.devicePixelRatio : 0
        y: root.open ? (root.height - height - _kb) : root.height
        Behavior on y { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
        color: ThemeManager.surfaceColor
        radius: 18
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: parent.radius; color: parent.color }

        // Input row
        Item {
            id: inputRow
            anchors.top: parent.top
            anchors.topMargin: 14
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            height: 38

            Rectangle {
                id: prioCircle
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.topMargin: 5
                width: 22; height: 22; radius: 11
                color: "transparent"
                border.width: 2; border.color: root._prioColor
            }
            TextField {
                id: input
                anchors.left: prioCircle.right
                anchors.leftMargin: 12
                anchors.right: sendBtn.left
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                placeholderText: qsTr("Новая задача…")
                font.pixelSize: 17
                font.weight: Font.Medium
                color: ThemeManager.textColor
                placeholderTextColor: ThemeManager.secondaryTextColor
                background: Item {}
                onAccepted: root._send()
            }
            Rectangle {
                id: sendBtn
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 38; height: 38; radius: 19
                color: input.text.trim() !== "" ? ThemeManager.accentColor : ThemeManager.hoverColor
                IconSvg {
                    anchors.centerIn: parent
                    name: "send"; size: 18
                    color: input.text.trim() !== "" ? ThemeManager.accentForeground : ThemeManager.faintTextColor
                }
                MouseArea { anchors.fill: parent; onClicked: root._send() }
            }
        }

        // Chip row
        Flickable {
            id: chipScroll
            anchors.top: inputRow.bottom
            anchors.topMargin: 14
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            height: 38
            contentWidth: chipRow.width
            clip: true
            flickableDirection: Flickable.HorizontalFlick

            Row {
                id: chipRow
                spacing: 9
                height: parent.height

                // Срок
                MQuickAddChip {
                    active: root.dueState !== ""
                    iconName: "calendar"
                    label: root.dueState !== "" ? root.dueState : qsTr("Срок")
                    chipColor: ThemeManager.accentColor
                    onClicked: root.dueState = root.dueState === "Сегодня" ? "Завтра"
                                              : root.dueState === "Завтра" ? "" : "Сегодня"
                }
                // Приоритет
                MQuickAddChip {
                    active: root.prio < 4
                    iconName: "flag"
                    label: "P" + root.prio
                    chipColor: root._prioColor
                    onClicked: root.prio = root.prio === 1 ? 4 : root.prio - 1
                }
                // Проект
                MQuickAddChip {
                    visible: root._projects.length > 0
                    active: true
                    iconName: root._shownProj ? root._shownProj.icon : "inbox"
                    label: root._shownProj ? root._shownProj.name : qsTr("Входящие")
                    chipColor: root._shownProj ? root._shownProj.color : ThemeManager.accentColor
                    // -1 (default) → 0 → 1 … → wraps back to 0
                    onClicked: if (root._projects.length > 0) root.projIdx = (root.projIdx + 1) % root._projects.length
                }
                // Метка
                MQuickAddChip {
                    visible: root._labelName !== ""
                    active: root.hasLabel
                    iconName: "tag"
                    label: root._labelName
                    chipColor: ThemeManager.priorityP2Color
                    onClicked: root.hasLabel = !root.hasLabel
                }
                // Шаблон — insert a saved template into the current project.
                MQuickAddChip {
                    visible: typeof templateViewModel !== "undefined" && templateViewModel.count > 0
                    active: false
                    iconName: "note"
                    label: qsTr("Шаблон")
                    chipColor: ThemeManager.accentColor
                    onClicked: tplPopup.open()
                }
            }
        }

        // Template picker — tap a template to materialise it into the current
        // project and close quick-add.
        Popup {
            id: tplPopup
            x: 16
            y: -height - 8
            width: panel.width - 32
            padding: 6
            background: Rectangle {
                color: ThemeManager.surfaceColor; radius: 14
                border.width: 1; border.color: ThemeManager.borderColor
            }
            contentItem: ListView {
                implicitHeight: Math.min(contentHeight, 260)
                clip: true
                model: (typeof templateViewModel !== "undefined") ? templateViewModel : null
                delegate: Rectangle {
                    required property var model
                    width: ListView.view ? ListView.view.width : 0
                    height: 48
                    radius: 8
                    color: tplMa.pressed ? ThemeManager.hoverColor : "transparent"
                    Row {
                        anchors.fill: parent; anchors.leftMargin: 12; spacing: 10
                        IconSvg { anchors.verticalCenter: parent.verticalCenter; name: "note"; size: 18; color: ThemeManager.accentColor }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter; spacing: 1
                            Text { text: model.name; font.pixelSize: 14; font.weight: Font.DemiBold; color: ThemeManager.textColor }
                            Text { text: qsTr("Задач: %1").arg(model.itemCount); font.pixelSize: 11; color: ThemeManager.secondaryTextColor }
                        }
                    }
                    MouseArea {
                        id: tplMa
                        anchors.fill: parent
                        onClicked: {
                            // insertTemplate has no project fallback — an empty id
                            // would create project-less (invisible) tasks.
                            var proj = root._shownProj
                            var pid = proj ? proj.id : ""
                            templateViewModel.insertTemplate(model.tplId, pid, "")
                            if (typeof haptics !== "undefined") haptics.medium()
                            tplPopup.close()
                            root.open = false
                            root.created(qsTr("Шаблон добавлен"))
                        }
                    }
                }
            }
        }
    }
}
