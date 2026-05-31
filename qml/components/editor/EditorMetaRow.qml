import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

// Vertical column with Project, Section, Due date, Priority and Labels rows.
// Q4.01 (redesign): TaskEditor wraps this component in a Card-like
// surface (border + radius 10). Inner rows share a unified label width
// (100px) and label style (fontSize 13 / subText) so the card reads as
// one unit matching the Settings Card from view-others.jsx.
ColumnLayout {
    id: root

    property string taskId: ""
    property var    task:   null

    // Q4.01: design label slot — keeps all 9 meta rows perfectly aligned.
    readonly property int _labelSlot: 100

    // Q4.01: TaskEditor wraps us in a Card with 14px internal padding, so
    // we no longer need our own outer margins — driving padding from one
    // place keeps every Card consistent.
    Layout.fillWidth:   true
    spacing: 0

    // ── Priority helpers ──────────────────────────────────────────────────
    // Q4.01 (night iter-3): theme-aware токены вместо hardcoded hex. Раньше
    // priority цвета здесь были зафиксированы для classic_light/dark и не
    // переключались при выборе kale/blueberry/lavender etc. — P-чипы в
    // editor оставались red/orange/blue/gray, а priority circle в TaskItem
    // (через root._priorityColor байндинг ThemeManager.priorityP*Color)
    // менялся под тему. Несогласованность между TaskItem и editor под
    // не-default темами.
    function _priorityColor(p) {
        switch (p) {
            case 1:  return ThemeManager.priorityP1Color
            case 2:  return ThemeManager.priorityP2Color
            case 3:  return ThemeManager.priorityP3Color
            default: return ThemeManager.priorityP4Color
        }
    }
    function _priorityLabel(p) {
        switch (p) {
            case 1:  return "P1"
            case 2:  return "P2"
            case 3:  return "P3"
            default: return "P4"
        }
    }

    // ── Derived state from task ───────────────────────────────────────────
    property int    _priority:  task ? (task.priority  !== undefined ? task.priority : 4) : 4
    property var    _labels:    task ? (task.labels    || []) : []
    property string _dueString: task ? (task.dueString || "") : ""

    // ── Project row ───────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        height: 36
        spacing: Spacing.sm

        Text {
            text:                 qsTr("Проект")
            font.pixelSize:       13
            color:                ThemeManager.secondaryTextColor
            Layout.preferredWidth: root._labelSlot
        }

        // Q4.01 (redesign): match the `Select` pill in view-others.jsx —
        // bg, 1px border, radius 6, fontSize 12, ChevronDown 11/subText.
        ComboBox {
            id: projectCombo
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            font.pixelSize:   FontSize.small

            model:     projectViewModel
            textRole:  "name"
            valueRole: "id"

            Component.onCompleted: {
                if (root.task && root.task.projectId)
                    projectCombo.currentIndex =
                        projectCombo.indexOfValue(root.task.projectId)
            }
            Connections {
                target: root
                function onTaskChanged() {
                    if (root.task)
                        projectCombo.currentIndex =
                            projectCombo.indexOfValue(root.task.projectId)
                }
            }

            background: Rectangle {
                color:        projectComboArea.containsMouse
                              ? ThemeManager.hoverColor
                              : ThemeManager.backgroundColor
                border.color: ThemeManager.borderColor
                border.width: 1
                radius:       Radius.md
            }

            indicator: IconSvg {
                name: "chevron-down"
                size: 11
                color: ThemeManager.secondaryTextColor
                anchors.right: parent.right
                anchors.rightMargin: Spacing.sm
                anchors.verticalCenter: parent.verticalCenter
            }

            contentItem: Text {
                leftPadding:       Spacing.sm
                rightPadding:      Spacing.lg
                text:              projectCombo.displayText
                font:              projectCombo.font
                color:             ThemeManager.textColor
                verticalAlignment: Text.AlignVCenter
                elide:             Text.ElideRight
            }

            MouseArea {
                id: projectComboArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked:    projectCombo.popup.open()
            }

            onActivated: {
                if (root.taskId !== "")
                    taskViewModel.updateTask(root.taskId, "projectId",
                                            projectCombo.currentValue)
                // ITER1 #22: when the project changes, reset the section combo so it
                // shows sections for the newly-selected project (task.projectId hasn't
                // changed yet in QML, so sectionsForCurrentProject would still return
                // the old project's sections; force a reset to index 0 / no section).
                sectionCombo.currentIndex = 0
                // ITER3 #6: TODO — coalesce project+section undo entries into one
                // "Изменено в редакторе" entry. Requires UndoStack::setNextDescription()
                // or a batch-mode API that doesn't exist yet. Until then, changing both
                // project and section in the editor creates two separate undo entries.
            }
        }
    }

    // ── Section row ───────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        height: 36
        spacing: Spacing.sm
        visible: sectionCombo.count > 0

        Text {
            text:                 qsTr("Раздел")
            font.pixelSize:       13
            color:                ThemeManager.secondaryTextColor
            Layout.preferredWidth: root._labelSlot
        }

        ComboBox {
            id: sectionCombo
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            font.pixelSize:   FontSize.small

            // ITER1 #22: drive off projectCombo.currentValue so the list refreshes
            // immediately when the user picks a new project, without waiting for
            // the C++ Task object's projectId property to propagate back to QML.
            property var sectionsForCurrentProject:
                projectCombo.currentValue
                    ? dataStore.sectionsForProject(projectCombo.currentValue)
                    : (root.task && root.task.projectId)
                      ? dataStore.sectionsForProject(root.task.projectId)
                      : []

            model: {
                const items = [{ id: "", name: qsTr("(без раздела)") }]
                for (let i = 0; i < sectionsForCurrentProject.length; ++i) {
                    items.push({
                        id:   sectionsForCurrentProject[i].id,
                        name: sectionsForCurrentProject[i].name
                    })
                }
                return items
            }
            textRole:  "name"
            valueRole: "id"

            // Q4.01 (redesign): match the `Select` pill style — same as
            // projectCombo above.
            background: Rectangle {
                color:        ThemeManager.backgroundColor
                border.color: ThemeManager.borderColor
                border.width: 1
                radius:       Radius.md
            }

            indicator: IconSvg {
                name: "chevron-down"
                size: 11
                color: ThemeManager.secondaryTextColor
                anchors.right: parent.right
                anchors.rightMargin: Spacing.sm
                anchors.verticalCenter: parent.verticalCenter
            }

            contentItem: Text {
                leftPadding:       Spacing.sm
                rightPadding:      Spacing.lg
                text:              sectionCombo.displayText
                font:              sectionCombo.font
                color:             ThemeManager.textColor
                verticalAlignment: Text.AlignVCenter
                elide:             Text.ElideRight
            }

            // ITER2 #16: clamp to 0 so a stale/mismatched sectionId never leaves
            // the ComboBox displaying blank text (currentIndex = -1).
            // night iter-128: the section model derives from projectCombo's
            // currentValue, which settles AFTER this combo's Component.onCompleted.
            // A direct indexOfValue at onCompleted therefore ran against an
            // unsettled model (and an unbuilt internal value-index) and clamped to
            // 0 — the editor showed "(без раздела)" for a task that IS in a section
            // (state.json confirmed task.section_id matched its section, yet the
            // combo displayed no-section; the project combo above is fine because
            // its model is ready synchronously). Run the match immediately AND defer
            // a second pass via Qt.callLater so it re-resolves after the current
            // event-loop cycle, once projectCombo.currentValue + the section model +
            // the ComboBox's value index have all settled.
            function _matchTaskSection() {
                if (root.task)
                    sectionCombo.currentIndex = Math.max(0,
                        sectionCombo.indexOfValue(root.task.sectionId || ""))
            }
            Component.onCompleted: { sectionCombo._matchTaskSection();
                                     Qt.callLater(sectionCombo._matchTaskSection) }
            Connections {
                target: root
                function onTaskChanged() { sectionCombo._matchTaskSection();
                                           Qt.callLater(sectionCombo._matchTaskSection) }
            }

            onActivated: {
                if (root.taskId !== "")
                    taskViewModel.updateTask(root.taskId, "sectionId",
                                             sectionCombo.currentValue)
            }
        }
    }

    // ── Due date row ──────────────────────────────────────────────────────
    // Q4.01 (redesign): value is a chip with inline calendar icon (size 14)
    // — matches the TaskChip pattern from view-project.jsx.
    RowLayout {
        Layout.fillWidth: true
        height: 36
        spacing: Spacing.sm

        Text {
            text:                 qsTr("Срок")
            font.pixelSize:       13
            color:                ThemeManager.secondaryTextColor
            Layout.preferredWidth: root._labelSlot
        }

        Rectangle {
            height: 28
            width:  dueDateRow.implicitWidth + 16
            radius: Radius.md
            color:  dueDateHover.containsMouse
                    ? ThemeManager.hoverColor : "transparent"
            border.color: ThemeManager.borderColor
            border.width: 1

            Row {
                id: dueDateRow
                anchors {
                    verticalCenter: parent.verticalCenter
                    left:           parent.left
                    leftMargin:     8
                }
                spacing: 6

                IconSvg {
                    name: "calendar"
                    size: 14
                    color: root._dueString !== ""
                           ? ThemeManager.textColor
                           : ThemeManager.secondaryTextColor
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    id: dueDateText
                    text:          root._dueString !== ""
                                   ? root._dueString : qsTr("Установить срок")
                    font.pixelSize: 13
                    color:         root._dueString !== ""
                                   ? ThemeManager.textColor
                                   : ThemeManager.secondaryTextColor
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            MouseArea {
                id: dueDateHover
                anchors.fill: parent
                hoverEnabled: true
                cursorShape:  Qt.PointingHandCursor
                onClicked:    editorDatePicker.open()
            }
        }

        // Optional: a small clear-button when a due date is set.
        Rectangle {
            visible: root._dueString !== ""
            height: 22
            width:  22
            radius: 11
            color:  clearDueArea.containsMouse ? ThemeManager.hoverColor : "transparent"

            // ITER2 #23: icon-only ✕ button — provide accessible role + name.
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Очистить дату")

            // Task 17cea65b iter-3: было unicode "✕".
            IconSvg {
                anchors.centerIn: parent
                name: "close"
                size: 12
                color: ThemeManager.secondaryTextColor
            }
            MouseArea {
                id: clearDueArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    // ITER3 #13: don't break the `_dueString` binding by
                    // assigning to it imperatively. setTaskDue propagates
                    // back through Task.dueString → the binding refreshes.
                    if (root.taskId !== "")
                        taskViewModel.setTaskDue(root.taskId, "")
                }
            }
        }

        // ── Calendar date picker (reuses the QuickAdd's DatePicker popup) ─
        DatePicker {
            id: editorDatePicker
            selectedDate: root._dueString
            onDateSelected: function(dateString) {
                // ITER3 #13: same — don't shadow the binding.
                if (root.taskId !== "")
                    taskViewModel.setTaskDue(root.taskId, dateString)
            }
        }

    }

    // ── Deadline row ──────────────────────────────────────────────────────
    // ITER1 #42: expose deadline editing via a DatePicker, same pattern as due date.
    RowLayout {
        id: deadlineRow
        Layout.fillWidth: true
        height: 36
        spacing: Spacing.sm

        // ITER2 #7: direct property binding so in-place task mutations (sync, undo)
        // are reflected immediately without waiting for the task object reference
        // to change. The Connections onTaskChanged block is no longer needed here.
        property string _deadlineStr: root.task ? (root.task.deadline || "") : ""

        Text {
            text:                 qsTr("Дедлайн")
            font.pixelSize:       13
            color:                ThemeManager.secondaryTextColor
            Layout.preferredWidth: root._labelSlot
        }

        // Q4.01 (redesign): chip with inline alarm icon — color picks up
        // errorColor when deadline is set (matches TaskChip deadline pattern
        // in view-project.jsx).
        Rectangle {
            height: 28
            width:  deadlineDateRow.implicitWidth + 16
            radius: Radius.md
            color:  deadlineDateHover.containsMouse
                    ? ThemeManager.hoverColor : "transparent"
            border.color: ThemeManager.borderColor
            border.width: 1

            Row {
                id: deadlineDateRow
                anchors {
                    verticalCenter: parent.verticalCenter
                    left:           parent.left
                    leftMargin:     8
                }
                spacing: 6

                IconSvg {
                    name: "alarm"
                    size: 14
                    color: deadlineRow._deadlineStr !== ""
                           ? ThemeManager.errorColor
                           : ThemeManager.secondaryTextColor
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    id: deadlineDateText
                    text:          deadlineRow._deadlineStr !== ""
                                   ? deadlineRow._deadlineStr : qsTr("Установить дедлайн")
                    font.pixelSize: 13
                    color:         deadlineRow._deadlineStr !== ""
                                   ? ThemeManager.textColor
                                   : ThemeManager.secondaryTextColor
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            MouseArea {
                id: deadlineDateHover
                anchors.fill: parent
                hoverEnabled: true
                cursorShape:  Qt.PointingHandCursor
                onClicked:    deadlinePicker.open()
            }
        }

        // Clear deadline button
        Rectangle {
            visible: deadlineRow._deadlineStr !== ""
            height: 22
            width:  22
            radius: 11
            color:  clearDeadlineArea.containsMouse ? ThemeManager.hoverColor : "transparent"

            // ITER2 #23: icon-only ✕ button — provide accessible role + name.
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Очистить дедлайн")

            // Task 17cea65b iter-3: было unicode "✕".
            IconSvg {
                anchors.centerIn: parent
                name: "close"
                size: 12
                color: ThemeManager.secondaryTextColor
            }
            MouseArea {
                id: clearDeadlineArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    deadlineRow._deadlineStr = ""
                    if (root.taskId !== "")
                        taskViewModel.updateTask(root.taskId, "deadline", "")
                }
            }
        }

        DatePicker {
            id: deadlinePicker
            selectedDate: deadlineRow._deadlineStr
            onDateSelected: function(dateString) {
                deadlineRow._deadlineStr = dateString
                if (root.taskId !== "")
                    taskViewModel.updateTask(root.taskId, "deadline", dateString)
            }
        }
    }

    // ── Priority row ──────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        height: 36
        spacing: Spacing.sm

        Text {
            text:                 qsTr("Приоритет")
            font.pixelSize:       13
            color:                ThemeManager.secondaryTextColor
            Layout.preferredWidth: root._labelSlot
        }

        RowLayout {
            spacing: Spacing.xs

            Repeater {
                model: [1, 2, 3, 4]

                delegate: Rectangle {
                    required property int modelData
                    readonly property int pVal: modelData

                    // Q4.01 (redesign): unified chip height 28, radius 6
                    // to match the rest of the meta row chips.
                    width:  36
                    height: 28
                    radius: Radius.md
                    color:  root._priority === pVal
                            ? root._priorityColor(pVal) + "33"
                            : (flagHover.containsMouse ? ThemeManager.hoverColor : "transparent")
                    border.color: root._priority === pVal
                                  ? root._priorityColor(pVal) : ThemeManager.borderColor
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text:          root._priorityLabel(pVal)
                        font.pixelSize: FontSize.small
                        font.bold:      root._priority === pVal
                        color:          root._priorityColor(pVal)
                    }

                    MouseArea {
                        id: flagHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape:  Qt.PointingHandCursor
                        onClicked: {
                            root._priority = pVal
                            if (root.taskId !== "")
                                taskViewModel.setTaskPriority(root.taskId, pVal)
                        }
                    }
                }
            }
        }
    }

    // ── Duration row ──────────────────────────────────────────────────────
    // ITER1 #43: SpinBox for amount + ComboBox for unit (minute/hour/day).
    RowLayout {
        id: durationRow
        Layout.fillWidth: true
        height: 36
        spacing: Spacing.sm

        // ITER2 #7: direct property bindings so sync/undo mutations are reactive
        // without needing the task object reference to change.
        property int    _amount: root.task ? (root.task.durationAmount || 0) : 0
        property string _unit:   root.task ? (root.task.durationUnit   || "minute") : "minute"

        Text {
            text:                 qsTr("Длительность")
            font.pixelSize:       13
            color:                ThemeManager.secondaryTextColor
            Layout.preferredWidth: root._labelSlot
        }

        SpinBox {
            id: durationSpinBox
            from:  0
            to:    9999
            value: durationRow._amount
            font.pixelSize: 13
            editable: true
            // Q4.04 night iter-77: was implicitWidth 90 — too narrow for the
            // Material SpinBox's square up/down indicators, which squeezed the
            // value area to a sliver (the "0" rendered as a clipped ")"). Widen
            // to 120 (mirrors the working auto-archive SpinBox in Settings) so
            // the 1–4 digit value renders, and make it editable for keyboard entry.
            implicitWidth: 120

            onValueModified: {
                durationRow._amount = value
                if (root.taskId !== "")
                    taskViewModel.updateTask(root.taskId, "durationAmount", value)
            }
        }

        ComboBox {
            id: durationUnitCombo
            font.pixelSize: 13
            implicitWidth: 100

            // Internal keys map to the C++ durationUnit field values.
            model: [
                { text: qsTr("минут"), key: "minute" },
                { text: qsTr("часов"), key: "hour"   },
                { text: qsTr("дней"),  key: "day"    }
            ]
            textRole: "text"

            // Sync currentIndex to _unit whenever _unit changes (task load/change).
            // Use a simple binding helper: currentIndex tracks _unit via a function.
            function syncUnitIndex() {
                for (var i = 0; i < model.length; i++) {
                    if (model[i].key === durationRow._unit) {
                        currentIndex = i
                        return
                    }
                }
                currentIndex = 0
            }

            Component.onCompleted: syncUnitIndex()

            Connections {
                target: root
                function onTaskChanged() { durationUnitCombo.syncUnitIndex() }
            }

            onActivated: {
                const key = model[currentIndex].key
                durationRow._unit = key
                if (root.taskId !== "")
                    taskViewModel.updateTask(root.taskId, "durationUnit", key)
            }
        }
    }

    // ── Оценка трудозатрат ────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        height: 36
        spacing: Spacing.sm

        Text {
            text:                 qsTr("Оценка")
            font.pixelSize:       13
            color:                ThemeManager.secondaryTextColor
            Layout.preferredWidth: root._labelSlot
        }

        // Q4.01 (redesign): chip-style ToolButton — bordered surface, height 28,
        // radius 6 — matches the Inbox filter pill from view-others.jsx.
        ToolButton {
            id: estimateChip
            flat: true
            padding: 0
            implicitHeight: 28

            // Q4.01 (redesign): "⏱" emoji заменён на IconSvg "clock" из
            // stroke-пака. Тинт берёт цвет соседнего текста (theme-aware).
            contentItem: Row {
                spacing: 6
                leftPadding: 10
                rightPadding: 10
                IconSvg {
                    name: "clock"
                    size: 14
                    color: root.task && root.task.estimateMinutes > 0
                           ? ThemeManager.textColor
                           : ThemeManager.secondaryTextColor
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text:          root.task && root.task.estimateMinutes > 0
                                   ? estimateChip._formatEstimate(root.task.estimateMinutes)
                                   : qsTr("Не задана")
                    color:         root.task && root.task.estimateMinutes > 0
                                   ? ThemeManager.textColor
                                   : ThemeManager.secondaryTextColor
                    font.pixelSize: 13
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            background: Rectangle {
                color:        estimateChip.hovered
                              ? ThemeManager.hoverColor : "transparent"
                border.color: ThemeManager.borderColor
                border.width: 1
                radius:       Radius.md
            }

            onClicked: estimatePopup.open()

            function _formatEstimate(min) {
                if (min < 60)        return qsTr("%1 мин").arg(min)
                if (min % 60 === 0)  return qsTr("%1 ч").arg(min / 60)
                return qsTr("%1 ч %2 мин").arg(Math.floor(min / 60)).arg(min % 60)
            }

            Popup {
                id: estimatePopup
                y: estimateChip.height
                modal: false
                padding: Spacing.sm

                background: Rectangle {
                    color:        ThemeManager.surfaceColor
                    border.color: ThemeManager.borderColor
                    border.width: 1
                    radius:       Radius.md
                }

                contentItem: ColumnLayout {
                    spacing: 2

                    Repeater {
                        model: [
                            { label: qsTr("Без оценки"),    minutes: 0   },
                            { label: qsTr("10 минут"),      minutes: 10  },
                            { label: qsTr("30 минут"),      minutes: 30  },
                            { label: qsTr("1 час"),         minutes: 60  },
                            { label: qsTr("2 часа"),        minutes: 120 },
                            { label: qsTr("4 часа"),        minutes: 240 },
                            { label: qsTr("8 часов (день)"), minutes: 480 }
                        ]

                        delegate: ItemDelegate {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredHeight: Spacing.xl
                            Layout.preferredWidth:  180
                            text:        modelData.label
                            highlighted: root.task && root.task.estimateMinutes === modelData.minutes
                            onClicked: {
                                if (root.taskId !== "")
                                    taskViewModel.updateTask(root.taskId, "estimateMinutes", modelData.minutes)
                                estimatePopup.close()
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Labels row ────────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        height: 36
        spacing: Spacing.sm

        Text {
            text:                 qsTr("Метки")
            font.pixelSize:       13
            color:                ThemeManager.secondaryTextColor
            Layout.preferredWidth: root._labelSlot
        }

        Flow {
            Layout.fillWidth: true
            spacing: Spacing.xs

            Repeater {
                model: root._labels

                delegate: LabelChip {
                    required property string modelData
                    labelName: modelData
                    labelColor: {
                        const lbl = dataStore.labelByName(modelData)
                        return (lbl && lbl.color && lbl.color !== "charcoal")
                               ? lbl.color : "#808080"
                    }
                    removable: true
                    onRemoved: {
                        const newLabels = root._labels.filter(
                            function(l) { return l !== modelData })
                        root._labels = newLabels
                        if (root.taskId !== "")
                            taskViewModel.setTaskLabels(root.taskId, newLabels)
                    }
                }
            }

            // Add-label button — Q4.01: unified chip style.
            Rectangle {
                height: 26
                width:  addLabelText.implicitWidth + 20
                radius: Radius.md
                color:  addLabelHover.containsMouse
                        ? ThemeManager.hoverColor : "transparent"
                border.color: ThemeManager.borderColor
                border.width: 1

                Text {
                    id: addLabelText
                    anchors.centerIn: parent
                    text:          qsTr("+ Добавить метку")
                    font.pixelSize: FontSize.small
                    color:         ThemeManager.secondaryTextColor
                }

                MouseArea {
                    id: addLabelHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape:  Qt.PointingHandCursor
                    onClicked:    labelInputRow.visible = !labelInputRow.visible
                }
            }
        }
    }

    // ── Color row ─────────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        height: 36
        spacing: Spacing.sm

        Text {
            text:                 qsTr("Цвет")
            font.pixelSize:       13
            color:                ThemeManager.secondaryTextColor
            Layout.preferredWidth: root._labelSlot
        }

        // Q4.01 (redesign): same chip style as estimate.
        ToolButton {
            id: colorChip
            flat: true
            padding: 0
            implicitHeight: 28

            background: Rectangle {
                color:        colorChip.hovered
                              ? ThemeManager.hoverColor : "transparent"
                border.color: ThemeManager.borderColor
                border.width: 1
                radius:       Radius.md
            }

            contentItem: Row {
                spacing: 6
                leftPadding: 10
                rightPadding: 10
                Rectangle {
                    width: 12; height: 12; radius: 6
                    color: root.task && root.task.color !== "" ? root.task.color : "transparent"
                    border.color: ThemeManager.borderColor
                    border.width: 1
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text:          root.task && root.task.color !== "" ? root.task.color : qsTr("Не задан")
                    color:         ThemeManager.textColor
                    font.pixelSize: 13
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            onClicked: colorPickerPopup.open()

            Popup {
                id: colorPickerPopup
                y: colorChip.height
                modal: false
                padding: Spacing.sm

                background: Rectangle {
                    color:        ThemeManager.surfaceColor
                    border.color: ThemeManager.borderColor
                    border.width: 1
                    radius:       Radius.md
                }

                contentItem: GridLayout {
                    columns: 4
                    rowSpacing: 4
                    columnSpacing: 4

                    Repeater {
                        model: [
                            "",         // "нет" — очистить цвет
                            "#db4c3f",  // красный
                            "#f49c18",  // оранжевый
                            "#fad000",  // жёлтый
                            "#7ecc49",  // светло-зелёный
                            "#299438",  // тёмно-зелёный
                            "#6accbc",  // бирюзовый
                            "#158fad",  // синий
                            "#14aaf5",  // голубой
                            "#96c3eb",  // небесный
                            "#4073ff",  // индиго
                            "#884dff",  // фиолетовый
                        ]

                        delegate: Rectangle {
                            required property string modelData
                            width: 24; height: 24; radius: 12
                            color:        modelData === "" ? "transparent" : modelData
                            border.color: modelData === "" ? ThemeManager.borderColor
                                          : (root.task && root.task.color === modelData
                                             ? ThemeManager.accentColor : "transparent")
                            border.width: modelData === "" ? 1 : (root.task && root.task.color === modelData ? 2 : 0)

                            // Task 17cea65b iter-3: было unicode "✕".
                            IconSvg {
                                anchors.centerIn: parent
                                visible: modelData === ""
                                name: "close"
                                size: 11
                                color: ThemeManager.secondaryTextColor
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape:  Qt.PointingHandCursor
                                onClicked: {
                                    if (root.taskId !== "")
                                        taskViewModel.updateTask(root.taskId, "color", modelData)
                                    colorPickerPopup.close()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Inline label input (hidden by default)
    RowLayout {
        id: labelInputRow
        visible:          false
        Layout.fillWidth: true
        spacing: 6

        Item { Layout.preferredWidth: root._labelSlot }

        TextField {
            id: labelInput
            Layout.fillWidth: true
            placeholderText:  qsTr("Название метки")
            font.pixelSize:   13
            color:            ThemeManager.textColor
            background: Rectangle {
                color:        ThemeManager.backgroundColor
                border.color: ThemeManager.borderColor
                border.width: 1
                radius:       Radius.sm
            }

            onAccepted: {
                if (text.trim() !== "") {
                    const newLabels = root._labels.concat([text.trim()])
                    root._labels = newLabels
                    if (root.taskId !== "")
                        taskViewModel.setTaskLabels(root.taskId, newLabels)
                    text = ""
                }
                labelInputRow.visible = false
            }

            Keys.onEscapePressed: {
                text = ""
                labelInputRow.visible = false
            }
        }
    }
}
