import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

// Root is a Rectangle (not Item) so the `color` background binding below
// is valid — Qt's strict QML compiler rejects `color:` on plain Item and
// makes the entire type unavailable, which transitively breaks ProjectView.
Rectangle {
    id: root
    anchors.fill: parent

    // ── Design constants ──────────────────────────────────────────────────────
    readonly property color accentRed:    ThemeManager.accentColor
    readonly property bool  isDarkTheme:   ThemeManager.isDark
    readonly property color bgColor:       ThemeManager.backgroundColor
    readonly property color contentColor:  ThemeManager.surfaceColor
    readonly property color borderColor:   ThemeManager.borderColor
    readonly property color textPrimary:   ThemeManager.textColor
    readonly property color textSecondary: ThemeManager.secondaryTextColor

    // ── Public API ────────────────────────────────────────────────────────────
    property string projectId: ""

    signal taskClicked(string taskId)

    color: bgColor

    // ── Section data ──────────────────────────────────────────────────────────
    property var _sections: []

    function _refreshSections() {
        var raw    = dataStore.sectionsForProject(projectId)
        var result = [{ id: "", name: qsTr("(Без раздела)") }]
        for (var i = 0; i < raw.length; i++) {
            result.push({ id: raw[i].id, name: raw[i].name })
        }
        _sections = result
    }

    function _tasksForSection(sectionId) {
        var tasks = []
        for (var i = 0; i < taskViewModel.count; i++) {
            var t = taskViewModel.taskAt(i)
            if (t && t.sectionId === sectionId && !t.isCompleted) {
                tasks.push({
                    id:        t.id,
                    content:   t.content,
                    priority:  t.priority,
                    dueDate:   t.dueDate   || "",
                    dueString: t.dueString || "",
                    labels:    t.labels    || [],
                    sectionId: t.sectionId
                })
            }
        }
        return tasks
    }

    function _priorityColor(p) {
        switch (p) {
            case 1:  return ThemeManager.priorityP1Color
            case 2:  return ThemeManager.priorityP2Color
            case 3:  return ThemeManager.priorityP3Color
            default: return ThemeManager.priorityP4Color
        }
    }

    function _isOverdue(dateStr) {
        if (!dateStr || dateStr === "") return false
        var due   = new Date(dateStr)
        var today = new Date()
        today.setHours(0, 0, 0, 0)
        return due < today
    }

    Component.onCompleted: _refreshSections()
    onProjectIdChanged:    _refreshSections()

    // ── Board scroll area ─────────────────────────────────────────────────────
    ScrollView {
        anchors.fill: parent
        // L-6.49 — removed the extra + 32; padding already included in implicitWidth
        contentWidth: boardRow.implicitWidth
        contentHeight: height

        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
        ScrollBar.vertical:   ScrollBar { policy: ScrollBar.AlwaysOff }

        Row {
            id:      boardRow
            spacing: 12
            padding: Spacing.md
            height:  parent.height

            // ── Columns (one per section) ─────────────────────────────────────
            Repeater {
                id:    columnRepeater
                model: root._sections

                delegate: Rectangle {
                    id: column

                    required property int index
                    required property var modelData   // { id, name }

                    // Q4.01 design parity: column 220 wide, surface bg, radius 10, 1px border
                    width:  220
                    height: boardRow.height - 32
                    radius: 10
                    color:  ThemeManager.surfaceColor
                    border.color: ThemeManager.borderColor
                    border.width: 1

                    // Re-evaluate the task list each time taskViewModel changes
                    property var columnTasks: root._tasksForSection(modelData.id)

                    Connections {
                        target: taskViewModel
                        function onDataChanged()  { column.columnTasks = root._tasksForSection(column.modelData.id) }
                        function onRowsInserted() { column.columnTasks = root._tasksForSection(column.modelData.id) }
                        function onRowsRemoved()  { column.columnTasks = root._tasksForSection(column.modelData.id) }
                        function onModelReset()   { column.columnTasks = root._tasksForSection(column.modelData.id) }
                    }

                    ColumnLayout {
                        anchors.fill:    parent
                        spacing: 0

                        // Column header — Q4.01 design parity: padding 10 12, border-bottom 1px
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: headerRow.implicitHeight + 20  // padding-top 10 + padding-bot 10
                            color: "transparent"

                            // 1px bottom border line
                            Rectangle {
                                anchors.bottom: parent.bottom
                                anchors.left:   parent.left
                                anchors.right:  parent.right
                                height:         1
                                color:          ThemeManager.borderColor
                            }

                            RowLayout {
                                id: headerRow
                                anchors.left:           parent.left
                                anchors.right:          parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin:     12
                                anchors.rightMargin:    12
                                spacing: 6

                                Label {
                                    text:             modelData.name
                                    font.pixelSize:   FontSize.small      // 12 per design
                                    font.weight:      Font.Medium         // weight 500 per design
                                    color:            root.textPrimary
                                    elide:            Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                // Task count badge — fontSize 10, padding 1 7, radius 9, bg t.bg, color subText
                                Rectangle {
                                    visible: column.columnTasks.length > 0
                                    implicitWidth:  countLabel.implicitWidth + 14   // 7 + 7 horizontal padding
                                    implicitHeight: countLabel.implicitHeight + 2   // 1 + 1 vertical padding
                                    radius: 9
                                    color:  ThemeManager.backgroundColor

                                    Text {
                                        id:             countLabel
                                        anchors.centerIn: parent
                                        text:           column.columnTasks.length
                                        font.pixelSize: FontSize.tiny    // 10 per design
                                        color:          root.textSecondary
                                    }
                                }

                                // Column menu — more icon size 12, color subText
                                ToolButton {
                                    id:    columnMenuBtn
                                    onClicked: columnMenu.open()
                                    // Task 17cea65b iter-3: было unicode "⋯".
                                    contentItem: IconSvg {
                                        anchors.centerIn: parent
                                        name: "more-horizontal"
                                        size: 12
                                        color: root.textSecondary
                                    }
                                }

                                Menu {
                                    id: columnMenu
                                    y:  columnMenuBtn.height

                                    MenuItem {
                                        text: qsTr("Переименовать раздел")
                                        onTriggered: {
                                            renameSectionDialog.sectionId   = modelData.id
                                            renameSectionDialog.sectionName = modelData.name
                                            renameSectionDialog.open()
                                        }
                                    }
                                    MenuSeparator {}
                                    MenuItem {
                                        text: qsTr("Удалить раздел")
                                        Material.foreground: root.accentRed
                                        onTriggered: {
                                            // B-6.40 — canonical API is removeSection (not deleteSection)
                                            if (modelData.id !== "")
                                                dataStore.removeSection(modelData.id)
                                            root._refreshSections()
                                        }
                                    }
                                }
                            }   // close RowLayout (headerRow)
                        }       // close header Rectangle wrapper

                        // ── Card list ─────────────────────────────────────────
                        // F-6.45 — drop area covering the whole card list for inter-column moves
                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            ListView {
                                id:               cardList
                                anchors.fill:     parent
                                model:   column.columnTasks
                                clip:    true
                                spacing: Spacing.sm

                                delegate: Rectangle {
                                    id: card

                                    required property int index
                                    required property var modelData   // task snapshot

                                    // Q4.01 design parity: bg t.bg, 1px border, radius 6, padding 8 10
                                    width:  cardList.width
                                    height: cardContent.implicitHeight + 16   // 8 + 8 vertical padding
                                    radius: Radius.md                          // 6
                                    color:  ThemeManager.backgroundColor
                                    border.color: ThemeManager.borderColor
                                    border.width: 1

                                    // Bug 8 fix: switched from Drag.Internal to Drag.Automatic
                                    // so the ghost image is created at the system/scene level
                                    // and can cross between column ListViews without being
                                    // clipped by the source column's contentItem. The MouseArea
                                    // drag.target points at a tiny invisible helper (cardGhost)
                                    // — same pattern as TaskItem.qml — so the visible card
                                    // never gets reparented or pulled out of the ListView,
                                    // while drag.active still toggles correctly to engage the
                                    // Drag attached property.
                                    Drag.active:       cardMouse.drag.active
                                    Drag.hotSpot.x:    width  / 2
                                    Drag.hotSpot.y:    height / 2
                                    Drag.dragType:     Drag.Automatic
                                    Drag.supportedActions: Qt.MoveAction
                                    Drag.mimeData:     ({ "application/x-opentodo-task": modelData.id,
                                                          "application/x-opentodo-section": modelData.sectionId })
                                    Drag.keys:         ["application/x-opentodo-task"]

                                    states: State {
                                        when: cardMouse.drag.active
                                        PropertyChanges { target: card; opacity: 0.6; z: 10 }
                                    }

                                    MouseArea {
                                        id: cardMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                                        // Drag.target is an invisible 1×1 helper outside the
                                        // layout flow. Movement of cardGhost engages drag.active
                                        // but never visually shifts the card — the actual ghost
                                        // image is provided by Drag.Automatic at the system level.
                                        drag.target:     cardGhost
                                        drag.threshold:  6
                                        drag.smoothed:   false
                                        // Click only fires when no drag occurred.
                                        onClicked: if (!drag.active) root.taskClicked(modelData.id)
                                    }

                                    // Invisible drag.target helper — see comment on cardMouse.
                                    Item {
                                        id: cardGhost
                                        width: 1; height: 1
                                        visible: false
                                    }

                                    // Hover colour effect — Q4.01: base is t.bg per design
                                    Behavior on color { ColorAnimation { duration: 80 } }
                                    property bool _cardHovered: cardMouse.containsMouse && !cardMouse.drag.active
                                    // Q4.01 (night iter-8): hover bg → theme-aware
                                    // hoverColor (был "#303030" / "#f5f5f5" hardcoded).
                                    color: _cardHovered
                                           ? ThemeManager.hoverColor
                                           : ThemeManager.backgroundColor

                                    ColumnLayout {
                                        id: cardContent
                                        // Q4.01 design parity: padding 8 10 (vertical 8, horizontal 10)
                                        anchors {
                                            left:   parent.left
                                            right:  parent.right
                                            top:    parent.top
                                            leftMargin:  10
                                            rightMargin: 10
                                            topMargin:   8
                                        }
                                        spacing: 6

                                        // Priority colour bar
                                        Rectangle {
                                            Layout.fillWidth: true
                                            height:  3; radius: 1.5
                                            color:   root._priorityColor(modelData.priority)
                                            visible: modelData.priority < 4
                                        }

                                        // Task title — Q4.01 design parity: fontSize 12, lineHeight 1.45
                                        Text {
                                            text:            modelData.content
                                            font.pixelSize:  FontSize.small        // 12 per design
                                            color:           root.textPrimary
                                            wrapMode:        Text.Wrap
                                            lineHeight:      1.45
                                            lineHeightMode:  Text.ProportionalHeight
                                            Layout.fillWidth: true
                                        }

                                        // Due date
                                        RowLayout {
                                            visible: modelData.dueDate !== ""
                                            spacing: Spacing.xs

                                            // Task 17cea65b iter-3: было "📅".
                                            IconSvg {
                                                name: "calendar"
                                                size: 11
                                                color: ThemeManager.secondaryTextColor
                                            }
                                            Text {
                                                text:           modelData.dueString !== ""
                                                                ? modelData.dueString
                                                                : modelData.dueDate
                                                font.pixelSize: FontSize.caption
                                                // Q4.01 night iter-28: overdue → errorColor
                                                // (was accentRed/accentColor — wrong semantic, also
                                                // accent doesn't mean "warning/error" on non-default
                                                // themes like kale/sunflower).
                                                color:          root._isOverdue(modelData.dueDate)
                                                                ? ThemeManager.errorColor
                                                                : root.textSecondary
                                            }
                                        }

                                        // Label chips
                                        Flow {
                                            Layout.fillWidth: true
                                            spacing: Spacing.xs
                                            visible: modelData.labels && modelData.labels.length > 0

                                            Repeater {
                                                model: modelData.labels || []
                                                delegate: Rectangle {
                                                    required property string modelData
                                                    height: 18
                                                    width:  chipLabel.implicitWidth + 8
                                                    radius: Radius.pill
                                                    // Q4.01 (night iter-8): theme-aware label chip
                                                    color:  ThemeManager.hoverColor

                                                    Text {
                                                        id:             chipLabel
                                                        anchors.centerIn: parent
                                                        text:           modelData
                                                        // Q4.01 night iter-28: was FontSize.xs which
                                                        // doesn't exist in FontSize singleton (only
                                                        // tiny/caption/small/...). The undefined
                                                        // resolved to default font size, breaking
                                                        // the chip's design intent (small 10px).
                                                        font.pixelSize: FontSize.tiny
                                                        color:          root.textSecondary
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    // ITER1 #1/#7: removed the redundant second MouseArea
                                    // that referenced an undefined `dragHandler` and
                                    // shadowed cardMouse's drag handling. cardMouse now
                                    // also handles click — see its onClicked above.
                                }

                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            }

                            // F-6.45 — drop area for receiving cards from other columns
                            DropArea {
                                id: columnDropArea
                                anchors.fill: parent
                                keys: ["application/x-opentodo-task"]

                                // Visual drop indicator — thin accent line at top of column
                                Rectangle {
                                    anchors.top:   parent.top
                                    anchors.left:  parent.left
                                    anchors.right: parent.right
                                    height: 3
                                    radius: 1.5
                                    color:  root.accentRed
                                    visible: parent.containsDrag
                                    opacity: 0.85
                                }

                                onDropped: function(drop) {
                                    var tid = drop.getDataAsString("application/x-opentodo-task")
                                    if (tid && tid.length > 0) {
                                        // Move task to this column's section
                                        taskViewModel.moveTask(tid, root.projectId, column.modelData.id)
                                        drop.accept(Qt.MoveAction)
                                    }
                                }
                            }
                        }

                        // ── Add card button ───────────────────────────────────
                        // Q4.01 night iter-45 design parity: the footer row was a
                        // centered 36px button with body(14) text + plus(16) and NO
                        // separating border. The design canvas (view-others.jsx
                        // BoardView footer) specifies borderTop 1px, left-aligned
                        // content inset 12px, plus + caption(11) text, gap 6 — a subtle
                        // footer mirroring the header's border-bottom treatment.
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: addCardRow.implicitHeight + 16  // padding 8 top + 8 bottom
                            radius: Radius.xs
                            // Q4.01 (night iter-8): theme-aware add-card hover
                            color:  addCardHover.containsMouse
                                    ? ThemeManager.hoverColor
                                    : "transparent"

                            // 1px top border separating the footer from the card list
                            Rectangle {
                                anchors.top:   parent.top
                                anchors.left:  parent.left
                                anchors.right: parent.right
                                height:        1
                                color:         ThemeManager.borderColor
                            }

                            RowLayout {
                                id: addCardRow
                                anchors.left:           parent.left
                                anchors.right:          parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin:     12
                                anchors.rightMargin:    12
                                spacing: 6

                                // Task 17cea65b iter-3: было unicode "+".
                                IconSvg {
                                    name: "plus"
                                    size: 12
                                    color: root.textSecondary
                                }
                                Text {
                                    text:             qsTr("Добавить задачу")
                                    color:            root.textSecondary
                                    font.pixelSize:   FontSize.caption   // 11 per design
                                    Layout.fillWidth: true
                                }
                            }

                            HoverHandler { id: addCardHover }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape:  Qt.PointingHandCursor
                                onClicked: {
                                    addCardDialog.targetSectionId = modelData.id
                                    addCardDialog.open()
                                }
                            }
                        }
                    }
                }
            }

            // ── Add column button ─────────────────────────────────────────────
            Rectangle {
                width:  280
                height: boardRow.height - 32
                radius: Radius.md
                color:  "transparent"
                border.color: root.borderColor
                border.width: 1
                opacity: 0.7

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: Spacing.sm

                    // Task 17cea65b iter-3: было unicode "+".
                    IconSvg {
                        name: "plus"
                        size: 24
                        color: root.textSecondary
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Text {
                        text:           qsTr("Добавить раздел")
                        font.pixelSize: FontSize.body
                        color:          root.textSecondary
                        Layout.alignment: Qt.AlignHCenter
                    }
                }

                HoverHandler { id: addColHover }

                MouseArea {
                    anchors.fill: parent
                    cursorShape:  Qt.PointingHandCursor
                    onClicked:    addColumnDialog.open()
                }
            }
        }
    }

    // ── Add card dialog ───────────────────────────────────────────────────────
    // Q3.26: explicit Dialog width + anchors.fill on the inner ColumnLayout.
    // Without an explicit width on the Dialog itself, Material Dialog computes
    // its width from contentItem.implicitWidth (often the title bar width),
    // and the inner ColumnLayout's `width: 280` does NOT propagate up — so the
    // TextField rendered outside the visible frame ("dialog cropped"). Mirror
    // the working pattern from ProjectView.qml's addSectionDialog.
    Dialog {
        id:    addCardDialog
        title: qsTr("Добавить задачу")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(360, Overlay.overlay.width - Spacing.xl)
        standardButtons: Dialog.Ok | Dialog.Cancel

        property string targetSectionId: ""

        // ITER1 #38: clear text and focus the field when the dialog opens
        onOpened: {
            addCardField.text = ""
            addCardField.forceActiveFocus()
        }
        onAccepted: {
            var t = addCardField.text.trim()
            if (t !== "") {
                // B-6.41 — addTask returns QString taskId; use return value for moveTask
                var taskId = taskViewModel.addTask(t, root.projectId, 4, "", [])
                if (taskId && targetSectionId !== "")
                    taskViewModel.moveTask(taskId, root.projectId, targetSectionId)
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: Spacing.sm

            Label { text: qsTr("Название задачи"); font.pixelSize: FontSize.small; color: root.textSecondary }
            TextField {
                id:              addCardField
                Layout.fillWidth: true
                placeholderText: qsTr("Название задачи")
                color:           root.textPrimary
            }
        }
    }

    // ── Add column dialog ─────────────────────────────────────────────────────
    Dialog {
        id:    addColumnDialog
        title: qsTr("Добавить раздел")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(360, Overlay.overlay.width - Spacing.xl)
        standardButtons: Dialog.Ok | Dialog.Cancel

        // ITER1 #39: clear text and focus the field when the dialog opens
        onOpened: {
            colNameField.text = ""
            colNameField.forceActiveFocus()
        }
        onAccepted: {
            var name = colNameField.text.trim()
            if (name !== "") {
                dataStore.addSection(name, root.projectId)
                root._refreshSections()
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: Spacing.sm

            Label { text: qsTr("Название раздела"); font.pixelSize: FontSize.small; color: root.textSecondary }
            TextField {
                id:              colNameField
                Layout.fillWidth: true
                placeholderText: qsTr("Название раздела")
                color:           root.textPrimary
            }
        }
    }

    // ── Rename section dialog ─────────────────────────────────────────────────
    Dialog {
        id:    renameSectionDialog
        title: qsTr("Переименовать раздел")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(360, Overlay.overlay.width - Spacing.xl)
        standardButtons: Dialog.Ok | Dialog.Cancel

        property string sectionId:   ""
        property string sectionName: ""

        onOpened:   renameField.text = sectionName
        onAccepted: {
            var name = renameField.text.trim()
            if (name !== "" && sectionId !== "") {
                // Bug fix: DataStore exposes renameSection(id, newName) — there is
                // no Q_INVOKABLE updateSection on DataStore, so the previous
                // dataStore.updateSection(id, "name", value) call was a silent no-op
                // (TypeError in the QML console, dialog dismissed without effect).
                dataStore.renameSection(sectionId, name)
                root._refreshSections()
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: Spacing.sm

            Label { text: qsTr("Название раздела"); font.pixelSize: FontSize.small; color: root.textSecondary }
            TextField {
                id:              renameField
                Layout.fillWidth: true
                placeholderText: qsTr("Название раздела")
                color:           root.textPrimary
            }
        }
    }
}
