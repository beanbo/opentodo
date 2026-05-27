import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Controls   // Dialog, ScrollView, Label
import OpenTodoUI

Rectangle {
    id: root

    // ─── Public API ────────────────────────────────────────────────────────────
    property int    selectedPriority:  4
    property string selectedDueString: ""
    property var    selectedLabels:    []
    // ITER1 #30: selected section for the task (empty = project root)
    property string selectedSectionId: ""

    // Resolved project: falls back to taskViewModel.currentProjectId
    property string selectedProjectId: (typeof taskViewModel !== "undefined")
                                        ? taskViewModel.currentProjectId
                                        : ""

    // ─── Internals ─────────────────────────────────────────────────────────────
    readonly property bool isDark: ThemeManager.isDark

    readonly property color accentRed:    ThemeManager.accentColor
    readonly property color bgColor:    ThemeManager.backgroundColor
    readonly property color borderCol:  ThemeManager.borderColor
    readonly property color textCol:    ThemeManager.textColor
    readonly property color subTextCol: ThemeManager.secondaryTextColor

    // Q4.01 (night iter-14): theme-aware priority colors (was hardcoded
    // hex, не подстраивались под kale/blueberry/etc.).
    readonly property color p1Color: ThemeManager.priorityP1Color
    readonly property color p2Color: ThemeManager.priorityP2Color
    readonly property color p3Color: ThemeManager.priorityP3Color
    readonly property color p4Color: ThemeManager.priorityP4Color

    // ─── Sizing / animation target ─────────────────────────────────────────────
    // ITER2 #3: remove hardcoded expandedH=216 which clipped the submit row when
    // section + preview + label-input rows were simultaneously visible. Drive the
    // expanded height from the ColumnLayout's implicitHeight instead.
    property bool expanded: false
    readonly property int collapsedH: 56

    // ITER4 #2: single source of truth for the inline label-input row's
    // visibility. Driving labelInputRow.preferredHeight off labelInputField.visible
    // (which was set imperatively) caused a height snap on dismiss because the
    // outer height binding re-evaluated synchronously, before the Behavior could
    // animate. Now we drive both the row's height and the field's visibility
    // off this property, so the Behavior owns the transition end-to-end.
    property bool _labelInputOpen: false

    // I22-D follow-up: per-project default label names that the user has
    // explicitly dismissed for THIS draft (clicking the × on a preview chip).
    // Reset when the project changes or after a task is submitted. Compared
    // case-insensitively at merge time.
    property var _dismissedDefaultLabels: []

    // AI priority suggestion in flight (used by the ✨ button next to the
    // priority picker). Toggled when the user clicks the suggest button and
    // cleared by aiAssistant.prioritySuggested / errorReceived.
    property bool _aiBusy: false

    // True when the AI assistant is available and configured — gates the
    // visibility of the ✨ "suggest priority" button.
    readonly property bool _aiAvailable: typeof aiAssistant !== "undefined"
                                          && aiAssistant !== null
                                          && aiAssistant.isConfigured

    color:  bgColor
    radius: Radius.lg

    // Top separator line
    Rectangle {
        anchors.top:   parent.top
        anchors.left:  parent.left
        anchors.right: parent.right
        height: 1
        color:  root.borderCol
    }

    // ITER2 #3: use expandedColumn.implicitHeight so all rows (toolbar, section,
    // preview, label-input, submit) are included without a magic constant.
    // collapsedH accounts for the always-visible bar; topMargin 4 + leftMargin 12
    // are the column's own anchors so no extra padding needed.
    height: expanded ? (collapsedH + expandedColumn.implicitHeight + 8) : collapsedH
    clip:   true

    Behavior on height {
        NumberAnimation { duration: Duration.normal; easing.type: Easing.InOutQuad }
    }

    // Always-on collapsed-row hit area: clicking ANYWHERE on the strip
    // (icon, placeholder text, blank space) opens the panel and focuses
    // the input. Sits below the inner TextField so typing still routes
    // to the field once focused.
    MouseArea {
        id: barHitArea
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: root.collapsedH
        cursorShape: Qt.IBeamCursor
        onClicked: {
            if (!root.expanded) root.expand()
            taskField.forceActiveFocus()
        }
    }

    // ─── Collapsed bar: "+" icon + placeholder/input ──────────────────────────
    // Rendered above the MouseArea, anchored so vertical centering is
    // unambiguous (no Layout dependency).
    // Task 17cea65b iter-3: было unicode "+".
    // Q4.01 pixel-perfect: дизайн quick-add — plus 14px accent, gap до текста 10.
    IconSvg {
        id: plusIcon
        name: "plus"
        size: 14
        color: root.accentRed
        anchors.left:           parent.left
        anchors.leftMargin:     16
        anchors.verticalCenter: barHitArea.verticalCenter
        width: 14
    }

    // Plain TextInput (no Material chrome / floating label) — keeps the text
    // baseline pinned to the bar's vertical center regardless of focus state.
    // Q4.01 pixel-perfect: gap plus→text 10, fontSize 13 (дизайн placeholder).
    TextInput {
        id: taskField
        anchors.left:           plusIcon.right
        anchors.leftMargin:     10
        anchors.right:          parent.right
        anchors.rightMargin:    16
        anchors.verticalCenter: barHitArea.verticalCenter
        height: root.collapsedH - 8

        verticalAlignment: TextInput.AlignVCenter
        clip: true
        selectByMouse: true
        color: root.textCol
        selectionColor: root.accentRed
        // Q4.01 night iter-27: selectedTextColor was hardcoded "white" —
        // invisible on sunflower's yellow accent selection. Use
        // accentForeground for theme-aware contrast.
        selectedTextColor: ThemeManager.accentForeground
        font.pixelSize: FontSize.bodySmall

        // Manual placeholder — visible only when empty, never floats.
        Text {
            id: placeholderLabel
            text: qsTr("Добавить задачу")
            color: root.subTextCol
            font: taskField.font
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            visible: taskField.text.length === 0
        }

        Keys.onEscapePressed: { root.collapse(); focus = false }
        Keys.onReturnPressed: (event) => {
            if (event.modifiers & Qt.ShiftModifier) event.accepted = false
            else root.submitTask()
        }
        onActiveFocusChanged: { if (activeFocus && !root.expanded) root.expand() }

        // ── Checklist-paste detection ──────────────────────────────────────────
        // Fires on every change (keypress OR paste). We only act when the text
        // contains newlines AND at least two lines match the markdown checklist
        // pattern "- [ ]" / "- [x]". A single checklist line is ignored so the
        // user can still type manually without triggering the dialog.
        onTextChanged: {
            var t = text
            // Fast-path: no newlines → nothing to detect.
            if (t.indexOf("\n") < 0) return

            var lines = t.split("\n")
            // Regex: optional leading whitespace, "-" or "*", optional space,
            // "[" + one of: space / x / X + "]", one or more spaces, capture content.
            var re = /^\s*[-*]\s*\[(\s|x|X)\]\s*(.+)$/
            var checklist = []
            for (var i = 0; i < lines.length; i++) {
                var m = re.exec(lines[i])
                if (m) {
                    checklist.push({
                        content: m[2].trim(),
                        done: m[1].toLowerCase() === "x"
                    })
                }
            }

            // Only trigger when 2 or more checklist items are found.
            if (checklist.length >= 2) {
                checklistImportDialog.checklist = checklist
                // Clear the raw pasted text so the dialog is the only path forward.
                text = ""
                checklistImportDialog.open()
            }
            // Mixed or single-item pastes are left as-is; user decides.
        }
    }

    // ─── Toolbar/buttons (visible only when expanded) ─────────────────────────
    ColumnLayout {
        id: expandedColumn   // ITER2 #3: referenced by height binding above
        anchors {
            left:    parent.left
            right:   parent.right
            top:     barHitArea.bottom
            topMargin: 4
            leftMargin:  12
            rightMargin: 12
        }
        spacing: 0
        visible: root.expanded

        // Row 2 – action toolbar (date / priority / labels / project)
        // ITER3 #3: use Layout.preferredHeight + Behavior so ColumnLayout interpolates
        // the height change frame-by-frame instead of snapping from 0 to 36 in one frame.
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: root.expanded ? 36 : 0
            clip: true
            visible: Layout.preferredHeight > 0
            spacing: 4

            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: Duration.normal; easing.type: Easing.InOutQuad }
            }

            // Date picker button
            ToolButton {
                id: dateBtn
                implicitHeight: 28
                implicitWidth:  28
                padding: 0

                // ITER2 #23: icon-only button — provide accessible role + name.
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Установить срок")

                // Q4.01 (redesign): 📅 emoji replaced with IconSvg "calendar".
                contentItem: IconSvg {
                    anchors.centerIn: parent
                    name: "calendar"
                    size: 16
                    color: root.selectedDueString !== "" ? root.accentRed : root.subTextCol
                }

                background: Rectangle {
                    radius: Radius.sm
                    color:  dateBtn.hovered ? (root.isDark ? "#3a3a3a" : "#eeeeee") : "transparent"
                }

                ToolTip.visible: hovered
                ToolTip.text:    root.selectedDueString !== "" ? root.selectedDueString : qsTr("Установить срок")

                onClicked: datePicker.open()

                DatePicker {
                    id: datePicker
                    onDateSelected: (dateString) => {
                        root.selectedDueString = dateString
                    }
                }
            }

            // Recurrence preset chip
            ToolButton {
                id: recurChip
                implicitHeight: 28
                implicitWidth:  28
                padding: 0

                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Повторение")

                // Q4.01 (redesign): 🔁 emoji replaced with IconSvg "repeat".
                contentItem: IconSvg {
                    anchors.centerIn: parent
                    name: "repeat"
                    size: 16
                    color: {
                        var re = /\b(каждый|каждую|каждые)\b/i
                        return re.test(taskField.text) ? root.accentRed : root.subTextCol
                    }
                }

                background: Rectangle {
                    radius: Radius.sm
                    color:  recurChip.hovered ? (root.isDark ? "#3a3a3a" : "#eeeeee") : "transparent"
                }

                ToolTip.visible: hovered
                ToolTip.text:    qsTr("Повторение")

                onClicked: recurMenu.open()

                Menu {
                    id: recurMenu
                    y: recurChip.height

                    Repeater {
                        model: [
                            { label: qsTr("Ежедневно"),          phrase: "каждый день"         },
                            { label: qsTr("По будням"),          phrase: "каждый будний день"   },
                            { label: qsTr("По выходным"),        phrase: "каждые выходные"      },
                            { label: qsTr("Еженедельно"),        phrase: "каждую неделю"        },
                            { label: qsTr("Каждый понедельник"), phrase: "каждый понедельник"   },
                            { label: qsTr("Каждый вторник"),     phrase: "каждый вторник"       },
                            { label: qsTr("Каждую среду"),       phrase: "каждую среду"         },
                            { label: qsTr("Каждый четверг"),     phrase: "каждый четверг"       },
                            { label: qsTr("Каждую пятницу"),     phrase: "каждую пятницу"       },
                            { label: qsTr("Ежемесячно"),         phrase: "каждый месяц"         },
                            { label: qsTr("Ежегодно"),           phrase: "каждый год"           },
                            // ── Annual / birthday presets ──────────────────────────────
                            // "Каждый год <today>" and "Каждый год <tomorrow>" are
                            // evaluated once when the menu model is constructed (on first
                            // open).  The label shows a short human date (dd MMM); the
                            // phrase uses dd.MM so the Russian date parser can resolve it.
                            {
                                label:  qsTr("Каждый год %1").arg(Qt.formatDate(new Date(), "dd MMM")),
                                phrase: "каждый год " + Qt.formatDate(new Date(), "dd.MM")
                            },
                            {
                                label:  qsTr("Каждый год %1").arg(Qt.formatDate(new Date(Date.now() + 86400000), "dd MMM")),
                                phrase: "каждый год " + Qt.formatDate(new Date(Date.now() + 86400000), "dd.MM")
                            },
                            // Sentinel that opens the birthday date-picker popup instead
                            // of inserting a phrase directly.
                            { label: qsTr("День рождения…"),     phrase: "__pick__"             }
                        ]

                        delegate: MenuItem {
                            text: modelData.label
                            onTriggered: {
                                if (modelData.phrase === "__pick__") {
                                    annualDatePicker.open()
                                    return
                                }
                                var phrase = modelData.phrase
                                var s = taskField.text
                                var re = /\b(каждый|каждую|каждые)\b[^,]*/i
                                if (re.test(s)) {
                                    taskField.text = s.replace(re, phrase)
                                } else {
                                    taskField.text = (s.length > 0 ? s + " " : "") + phrase
                                }
                                taskField.cursorPosition = taskField.text.length
                                taskField.forceActiveFocus()
                                if (typeof telemetry !== "undefined" && telemetry.enabled)
                                    telemetry.trackEvent("quickadd_annual_preset", { "phrase": phrase })
                            }
                        }
                    }

                    // ── Birthday date-picker popup ─────────────────────────────────
                    // Reuses the existing DatePicker component (Popup).  The user picks
                    // any calendar day; month+day are extracted and inserted as
                    // "каждый год DD.MM" so the Russian date parser can handle it.
                    // Year is intentionally ignored — annual recurrence is year-agnostic.
                    DatePicker {
                        id: annualDatePicker
                        // dateSelected delivers a "YYYY-MM-DD" string from DatePicker.formatDate()
                        onDateSelected: function(dateString) {
                            // Extract DD and MM from "YYYY-MM-DD"
                            var parts = dateString.split("-")
                            if (parts.length !== 3) return
                            var ddmm = parts[2] + "." + parts[1]   // e.g. "15.05"
                            var phrase = "каждый год " + ddmm
                            var s = taskField.text
                            var re = /\b(каждый|каждую|каждые)\b[^,]*/i
                            if (re.test(s)) {
                                taskField.text = s.replace(re, phrase)
                            } else {
                                taskField.text = (s.length > 0 ? s + " " : "") + phrase
                            }
                            taskField.cursorPosition = taskField.text.length
                            taskField.forceActiveFocus()
                            if (typeof telemetry !== "undefined" && telemetry.enabled)
                                telemetry.trackEvent("quickadd_annual_preset", { "phrase": phrase })
                        }
                    }
                }
            }

            // Priority picker button
            ToolButton {
                id: prioBtn
                implicitHeight: 28
                implicitWidth:  28
                padding: 0

                // ITER2 #23: icon-only button — provide accessible role + name.
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Приоритет")

                // Q4.01 (redesign): unicode flag → stroke IconSvg "flag"
                contentItem: IconSvg {
                    name: "flag"
                    size: 14
                    color: root.priorityColor(root.selectedPriority)
                    anchors.centerIn: parent
                }

                background: Rectangle {
                    radius: Radius.sm
                    color:  prioBtn.hovered ? (root.isDark ? "#3a3a3a" : "#eeeeee") : "transparent"
                }

                ToolTip.visible: hovered
                ToolTip.text:    qsTr("Приоритет %1").arg(root.selectedPriority)

                onClicked: priorityPicker.open()

                PriorityPicker {
                    id: priorityPicker
                    selectedPriority: root.selectedPriority
                    onPrioritySelected: (p) => {
                        root.selectedPriority = p
                    }
                }
            }

            // AI priority suggestion button — visible only when AI is configured.
            // Sends the current title to the LLM and applies the returned 1-4
            // value to root.selectedPriority. Disabled while text < 4 chars or
            // while a request is in flight.
            ToolButton {
                id: aiPriorityBtn
                visible: root._aiAvailable
                implicitHeight: 28
                implicitWidth:  28
                padding: 0

                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Подсказать приоритет от ИИ")

                enabled: !root._aiBusy && taskField.text.length >= 4

                contentItem: Item {
                    anchors.fill: parent

                    // Q4.01 (redesign): ✨ emoji → IconSvg "sparkles".
                    IconSvg {
                        anchors.centerIn: parent
                        visible: !root._aiBusy
                        name: "sparkles"
                        size: 14
                        color: aiPriorityBtn.enabled
                                  ? ThemeManager.accentColor
                                  : root.subTextCol
                    }

                    // Spinner overlay shown while the HTTP round-trip is in flight.
                    BusyIndicator {
                        anchors.centerIn: parent
                        visible: root._aiBusy
                        running: root._aiBusy
                        width:   20
                        height:  20
                    }
                }

                background: Rectangle {
                    radius: Radius.sm
                    color:  aiPriorityBtn.hovered && aiPriorityBtn.enabled
                              ? (root.isDark ? "#3a3a3a" : "#eeeeee")
                              : "transparent"
                }

                ToolTip.visible: hovered
                ToolTip.text:    qsTr("Подсказать приоритет от ИИ")

                onClicked: {
                    if (taskField.text.length < 4 || root._aiBusy) return
                    root._aiBusy = true
                    aiAssistant.suggestPriority(taskField.text)
                }
            }

            // Labels add button
            ToolButton {
                id: labelBtn
                implicitHeight: 28
                implicitWidth:  28
                padding: 0

                // ITER2 #23: icon-only button — provide accessible role + name.
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Добавить метку")

                // Q4.01 (redesign): 🏷 emoji → IconSvg "label".
                contentItem: IconSvg {
                    anchors.centerIn: parent
                    name: "label"
                    size: 14
                    color: root.selectedLabels.length > 0 ? root.accentRed : root.subTextCol
                }

                background: Rectangle {
                    radius: Radius.sm
                    color:  labelBtn.hovered ? (root.isDark ? "#3a3a3a" : "#eeeeee") : "transparent"
                }

                ToolTip.visible: hovered
                ToolTip.text:    root.selectedLabels.length > 0
                                  ? root.selectedLabels.join(", ")
                                  : qsTr("Добавить метку")

                // F-7.12: was a no-op; now shows inline label input row
                onClicked: root.openLabelInput()
            }

            // Project selector
            ComboBox {
                id: projectCombo
                Layout.fillWidth: true
                implicitHeight: 28
                font.pixelSize: FontSize.bodySmall

                model: (typeof projectViewModel !== "undefined") ? projectViewModel : null
                textRole:  "name"
                valueRole: "id"

                // B-7.13: use textRole/valueRole so ComboBox resolves role names
                // via the model's roleNames() hash rather than a hardcoded UserRole
                // integer. ProjectViewModel exposes "name" and "id" as named roles.
                Component.onCompleted: {
                    if (model) {
                        for (let i = 0; i < model.rowCount(); ++i) {
                            if (model.index(i, 0).data(Qt.UserRole + 1) === root.selectedProjectId) {
                                currentIndex = i
                                break
                            }
                        }
                    }
                }

                onActivated: {
                    root.selectedProjectId = currentValue
                    // ITER1 #30: reset section when project changes
                    root.selectedSectionId = ""
                    // I22-D follow-up: dismissed default-label names are
                    // tied to the previous project; clear them so the new
                    // project's defaults all show in the preview.
                    root._dismissedDefaultLabels = []
                }

                // Chip label: stroke-icon (Q4.01) or emoji (legacy) + project name.
                // Q4.01 iter-7: добавлен auto-detect префикса "projects/" в
                // Project.icon. Новый формат рендерится через IconSvg рядом
                // с именем; legacy emoji-проекты и пустой icon работают как
                // раньше (Text-фолбэк, "📁" для пустого).
                contentItem: Row {
                    leftPadding: Spacing.xs
                    spacing: 6

                    readonly property var _proj: root.selectedProjectId
                            && typeof projectViewModel !== "undefined"
                            ? projectViewModel.projectById(root.selectedProjectId)
                            : null
                    readonly property string _icon: _proj ? (_proj.icon || "") : ""
                    readonly property bool   _isStroke: _icon.indexOf("projects/") === 0
                    readonly property string _name: _proj ? _proj.name
                                                          : (root.selectedProjectId
                                                             ? projectCombo.displayText
                                                             : qsTr("Проект"))

                    IconSvg {
                        visible: parent._isStroke
                        anchors.verticalCenter: parent.verticalCenter
                        name: parent._icon
                        size: 14
                        color: root.textCol
                    }
                    // Q4.01 (redesign): empty/legacy fallback was "📁" emoji.
                    // Show a stroke folder IconSvg when project.icon is empty so
                    // the chip stays visually consistent with the rest of the
                    // chrome; legacy emoji-projects (icon stored as a single
                    // unicode glyph, not "projects/...") still render as Text.
                    IconSvg {
                        visible: !parent._isStroke && parent._icon === ""
                        anchors.verticalCenter: parent.verticalCenter
                        name: "folder"
                        size: 14
                        color: root.textCol
                    }
                    Text {
                        visible: !parent._isStroke && parent._icon !== ""
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent._icon
                        color: root.textCol
                        font: projectCombo.font
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent._name
                        color: root.textCol
                        font:  projectCombo.font
                        elide: Text.ElideRight
                    }
                }

                // Dropdown delegate: emoji icon if set, otherwise color dot
                delegate: ItemDelegate {
                    width: projectCombo.width
                    height: Spacing.xl

                    // Pull the Project* object via the "projectObject" model role
                    // so we can read the icon Q_PROPERTY (not exposed as a plain role).
                    readonly property var _proj: model.projectObject

                    contentItem: RowLayout {
                        spacing: 8
                        anchors.leftMargin: 8

                        // Project icon — Q4.01 iter-7: три ветки
                        //   1. иконка с префиксом "projects/" → IconSvg
                        //   2. user-typed эмодзи              → Text
                        //   3. без иконки                      → цветная точка
                        Item {
                            width: 18
                            height: 18

                            readonly property string _icon: (_proj && _proj.icon) ? _proj.icon : ""
                            readonly property bool   _isStroke: _icon.indexOf("projects/") === 0

                            IconSvg {
                                anchors.centerIn: parent
                                visible: parent._isStroke
                                name: parent._icon
                                size: 14
                                color: ThemeManager.textColor
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: !parent._isStroke && parent._icon !== ""
                                text:    parent._icon
                                font.pixelSize: FontSize.body
                            }
                            Rectangle {
                                anchors.centerIn: parent
                                visible: parent._icon === ""
                                width:  10
                                height: 10
                                radius: 5
                                color:  model.color || "#808080"
                            }
                        }

                        Label {
                            text:  model.name
                            color: ThemeManager.textColor
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }

                        // Inbox marker — Q4.01 (redesign): "📥" emoji заменён
                        // на IconSvg "inbox" в Item с visible-детектом.
                        Item {
                            visible: model.isInbox === true
                            implicitWidth: 14
                            implicitHeight: 14
                            IconSvg {
                                anchors.centerIn: parent
                                name: "inbox"
                                size: 12
                                color: ThemeManager.secondaryTextColor
                            }
                        }
                    }

                    highlighted: projectCombo.highlightedIndex === index

                    onClicked: {
                        projectCombo.currentIndex = index
                        projectCombo.activated(index)
                        projectCombo.popup.close()
                    }
                }

                background: Rectangle {
                    radius: Radius.sm
                    color:  projectCombo.hovered ? (root.isDark ? "#3a3a3a" : "#eeeeee") : "transparent"
                    border.color: root.borderCol
                    border.width: 1
                }
            }
        }

        // ITER1 #30: Row 2a – section selector (shown only when selected project has sections)
        RowLayout {
            id: sectionRow
            Layout.fillWidth: true
            spacing: 4

            // Compute sections for the current project
            property var _sections: {
                if (root.selectedProjectId === "" || typeof dataStore === "undefined")
                    return []
                var secs = dataStore.sectionsForProject(root.selectedProjectId)
                return secs ? secs : []
            }

            // ITER3 #3: Layout.preferredHeight + Behavior for smooth animation
            Layout.preferredHeight: (root.expanded && _sections.length > 0) ? 36 : 0
            clip: true
            visible: Layout.preferredHeight > 0

            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: Duration.normal; easing.type: Easing.InOutQuad }
            }

            Text {
                text: qsTr("Раздел:")
                font.pixelSize: FontSize.small
                color: root.subTextCol
                Layout.alignment: Qt.AlignVCenter
                leftPadding: Spacing.xs
            }

            ComboBox {
                id: sectionCombo
                Layout.fillWidth: true
                implicitHeight: 28
                font.pixelSize: FontSize.bodySmall

                // Build model: "(без раздела)" + actual sections
                model: {
                    var items = [{ id: "", name: qsTr("(без раздела)") }]
                    var secs = sectionRow._sections
                    for (var i = 0; i < secs.length; ++i)
                        items.push({ id: secs[i].id || secs[i].id, name: secs[i].name })
                    return items
                }
                textRole:  "name"
                valueRole: "id"

                currentIndex: 0

                onActivated: root.selectedSectionId = currentValue || ""

                // Reset when project changes
                Connections {
                    target: root
                    function onSelectedProjectIdChanged() { sectionCombo.currentIndex = 0 }
                }

                contentItem: Text {
                    leftPadding: Spacing.xs
                    text:  sectionCombo.displayText
                    color: root.textCol
                    font:  sectionCombo.font
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }

                background: Rectangle {
                    radius: Radius.sm
                    color:  sectionCombo.hovered ? (root.isDark ? "#3a3a3a" : "#eeeeee") : "transparent"
                    border.color: root.borderCol
                    border.width: 1
                }
            }
        }

        // Row 2b – NLP preview (shown when text is non-empty and focused)
        // ITER3 #3: Layout.preferredHeight + Behavior for smooth animation
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: root.expanded && taskField.text.trim().length > 0 ? 22 : 0
            clip: true
            visible: Layout.preferredHeight > 0
            spacing: 6

            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: Duration.normal; easing.type: Easing.InOutQuad }
            }

            // Compute preview lazily via previewQuickAdd
            property var parsed: (typeof taskViewModel !== "undefined" && taskField.text.trim().length > 0)
                                  ? taskViewModel.previewQuickAdd(taskField.text.trim())
                                  : null

            // Q4.01 (redesign): 🏷 + project-name (text)
            //   → IconSvg "label" + Text. Note: legacy code used the tag glyph
            //   for project preview — keeping the same icon to avoid changing
            //   the meaning, even though "project" would arguably warrant "folder".
            Row {
                visible: parent.parsed && parent.parsed.projectName !== ""
                spacing: 3
                IconSvg {
                    name: "label"
                    size: 11
                    color: root.accentRed
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: parent.parent.parsed ? parent.parent.parsed.projectName : ""
                    color: root.accentRed
                    font.pixelSize: FontSize.caption
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            // Q4.01 (redesign): ⏰ + due-string → IconSvg "alarm" + Text.
            Row {
                visible: parent.parsed && parent.parsed.dueString !== ""
                spacing: 3
                IconSvg {
                    name: "alarm"
                    size: 11
                    color: root.textCol
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: parent.parent.parsed ? parent.parent.parsed.dueString : ""
                    color: root.textCol
                    font.pixelSize: FontSize.caption
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            // Q4.01 (redesign): 🎯 + priority → IconSvg "flag" + "pN".
            Row {
                visible: parent.parsed && parent.parsed.priority < 4
                spacing: 3
                IconSvg {
                    name: "flag"
                    size: 11
                    color: root.priorityColor(parent.parent.parsed ? parent.parent.parsed.priority : 4)
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "p" + (parent.parent.parsed ? parent.parent.parsed.priority : "")
                    color: root.priorityColor(parent.parent.parsed ? parent.parent.parsed.priority : 4)
                    font.pixelSize: FontSize.caption
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            Repeater {
                model: parent.parsed ? parent.parsed.labels : []
                Text {
                    required property string modelData
                    text: "@" + modelData
                    color: root.subTextCol
                    font.pixelSize: FontSize.caption
                }
            }
            Item { Layout.fillWidth: true }
        }

        // Row 2c – F-7.12: inline label input (shown after clicking the label button)
        // ITER3 #3: Layout.preferredHeight + Behavior for smooth animation
        RowLayout {
            id: labelInputRow
            Layout.fillWidth: true
            // ITER4 #2: gate by root._labelInputOpen so the height transition
            // is owned by Behavior instead of snapping when labelInputField
            // becomes invisible synchronously.
            Layout.preferredHeight: (root.expanded && root._labelInputOpen) ? 30 : 0
            clip: true
            visible: Layout.preferredHeight > 0
            spacing: 6

            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: Duration.normal; easing.type: Easing.InOutQuad }
            }

            Text {
                text: "@"
                font.pixelSize: FontSize.body
                color: root.subTextCol
                Layout.alignment: Qt.AlignVCenter
            }

            TextField {
                id: labelInputField
                Layout.fillWidth: true
                // ITER4 #2: driven by root._labelInputOpen (single source of truth);
                // no more imperative `labelInputField.visible = true/false`.
                visible: root._labelInputOpen
                placeholderText: qsTr("Метки через запятую (напр. работа, дом)")
                font.pixelSize: FontSize.body
                color: root.textCol
                background: Rectangle {
                    radius: Radius.sm
                    color: "transparent"
                    border.color: root.borderCol
                    border.width: 1
                }
                leftPadding: Spacing.xs
                rightPadding: Spacing.xs

                Keys.onReturnPressed: root._commitLabelInput()
                Keys.onEnterPressed:  root._commitLabelInput()
                Keys.onEscapePressed: {
                    // ITER4 #2: drive via the gating property, not imperatively.
                    root._labelInputOpen = false
                    labelInputField.text = ""
                }
            }
        }

        // I22-D follow-up: Row 2d – preview chips for per-project default labels.
        // Visible only when the selected project has resolved default labels and
        // the user hasn't dismissed all of them. Light/grey styling distinguishes
        // them from manually picked labels; clicking the × on a chip dismisses
        // that default for the current draft, so it won't be merged into the new
        // task. The dismissal resets after the task is added (or the project
        // changes).
        RowLayout {
            id: defaultLabelsPreviewRow
            Layout.fillWidth: true
            spacing: 6

            // Cached resolved default labels — names, post-dismissal-filter.
            // Recomputed when the project changes, when the user dismisses a
            // chip, or when QSettings is updated. _dismissedDefaultLabels and
            // selectedProjectId are the gating dependencies.
            property var _resolved: {
                // Touch the dependencies so QML re-evaluates on change.
                let _t1 = root.selectedProjectId
                let _t2 = root._dismissedDefaultLabels
                return root._resolveDefaultLabelNames(root.selectedProjectId)
            }

            Layout.preferredHeight: (root.expanded && _resolved.length > 0) ? 28 : 0
            clip: true
            visible: Layout.preferredHeight > 0

            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: Duration.normal; easing.type: Easing.InOutQuad }
            }

            Text {
                text: qsTr("По умолчанию:")
                font.pixelSize: FontSize.caption
                color: root.subTextCol
                Layout.alignment: Qt.AlignVCenter
                leftPadding: Spacing.xs
            }

            Repeater {
                model: defaultLabelsPreviewRow._resolved

                delegate: Rectangle {
                    id: previewChip
                    required property string modelData

                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredHeight: 22
                    Layout.preferredWidth: previewChipRow.implicitWidth + 12
                    implicitWidth: previewChipRow.implicitWidth + 12
                    implicitHeight: 22
                    radius: 11
                    // Light/grey styling: secondaryTextColor with low alpha
                    // gives the preview a "this is a default" cue without
                    // competing visually with manually picked labels.
                    color: Qt.rgba(ThemeManager.secondaryTextColor.r,
                                   ThemeManager.secondaryTextColor.g,
                                   ThemeManager.secondaryTextColor.b, 0.12)
                    border.color: ThemeManager.borderColor
                    border.width: 1

                    RowLayout {
                        id: previewChipRow
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 6
                        spacing: 4

                        Text {
                            text: "@" + previewChip.modelData
                            font.pixelSize: FontSize.caption
                            color: ThemeManager.secondaryTextColor
                            Layout.alignment: Qt.AlignVCenter
                        }

                        // Dismiss × — clicking removes this default from the
                        // current draft only. The label itself is unchanged
                        // in QSettings and will reappear next time.
                        Text {
                            text: "×"
                            font.pixelSize: FontSize.bodySmall
                            color: ThemeManager.secondaryTextColor
                            Layout.alignment: Qt.AlignVCenter
                            Layout.rightMargin: 2

                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -3   // fatter hit-area
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    let copy = root._dismissedDefaultLabels.slice()
                                    if (copy.indexOf(previewChip.modelData) === -1)
                                        copy.push(previewChip.modelData)
                                    root._dismissedDefaultLabels = copy
                                }
                            }
                        }
                    }
                }
            }

            Item { Layout.fillWidth: true }   // spacer
        }

        // Row 3 – submit / cancel
        // ITER3 #3: Layout.preferredHeight + Behavior for smooth animation
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: root.expanded ? 40 : 0
            clip: true
            visible: Layout.preferredHeight > 0
            spacing: 8

            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: Duration.normal; easing.type: Easing.InOutQuad }
            }

            Item { Layout.fillWidth: true }   // spacer

            // Cancel
            Button {
                id: cancelBtn
                text: qsTr("Отмена")
                flat: true
                font.pixelSize: FontSize.bodySmall

                contentItem: Text {
                    text:  cancelBtn.text
                    color: root.subTextCol
                    font:  cancelBtn.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment:   Text.AlignVCenter
                }

                background: Rectangle {
                    radius: Radius.sm
                    color:  cancelBtn.hovered ? (root.isDark ? "#3a3a3a" : "#eeeeee") : "transparent"
                }

                onClicked: root.collapse()
            }

            // Add task
            Button {
                id: addBtn
                text: qsTr("Добавить задачу")
                font.pixelSize: FontSize.bodySmall
                enabled: taskField.text.trim().length > 0

                contentItem: Text {
                    text:  addBtn.text
                    color: addBtn.enabled ? "white" : "#c0c0c0"
                    font:  addBtn.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment:   Text.AlignVCenter
                }

                // Q4.01 (night iter-14): theme-aware accent for primary
                // submit button. Раньше hardcoded "#db4c3f" — default accent.
                background: Rectangle {
                    radius: Radius.sm
                    color:  addBtn.enabled
                             ? (addBtn.hovered
                                ? Qt.darker(ThemeManager.accentColor, 1.1)
                                : ThemeManager.accentColor)
                             : ThemeManager.borderColor
                }

                onClicked: root.submitTask()
            }
        }
    }

    // ─── Checklist import dialog ───────────────────────────────────────────────
    // Shown when the user pastes text that contains ≥ 2 markdown checklist lines.
    // Offers to create one task per line; already-ticked items are immediately
    // completed after creation.
    Dialog {
        id: checklistImportDialog

        // Populated by taskField.onTextChanged before open() is called.
        property var checklist: []

        title: qsTr("Импорт чек-листа")
        modal: true
        // Center over the Overlay (covers the full window, not just this item).
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel

        // Explicit width breaks the implicitWidth ↔ contentItem.Layout.fillWidth
        // binding loop that QML detects at line 981. Without this, Dialog tries
        // to derive its implicitWidth from the ColumnLayout below, but the
        // ColumnLayout's children use Layout.fillWidth: true (which expands to
        // the dialog's width) producing a cycle. A fixed width — clamped to the
        // overlay so we never overflow on small screens — bypasses derivation.
        // (Window.window is unavailable here because Dialog is a Popup, not an
        // Item, so the attached Window property is invalid; Overlay.overlay is
        // the natural parent and exposes a width.)
        width: Math.min(420, (Overlay.overlay ? Overlay.overlay.width : 480) - 32)

        // ── Content ─────────────────────────────────────────────────────────
        contentItem: ColumnLayout {
            spacing: 8

            // "Создать N задач?" headline
            Label {
                Layout.fillWidth: true
                text: qsTr("Создать %1 %2?")
                        .arg(checklistImportDialog.checklist.length)
                        .arg(root._pluralTasks(checklistImportDialog.checklist.length))
                color: ThemeManager.textColor
                font.pixelSize: FontSize.body
                wrapMode: Text.WordWrap
            }

            // Destination project info
            Label {
                Layout.fillWidth: true
                text: qsTr("В проект: %1").arg((function() {
                    if (typeof taskViewModel === "undefined") return qsTr("Входящие")
                    var pid = root.selectedProjectId
                    if (pid === "") return qsTr("Входящие")
                    if (typeof projectViewModel !== "undefined") {
                        var p = projectViewModel.projectById(pid)
                        return (p && p.name) ? p.name : qsTr("Входящие")
                    }
                    return qsTr("Входящие")
                })())
                color: ThemeManager.secondaryTextColor
                font.pixelSize: FontSize.small
                wrapMode: Text.WordWrap
            }

            // Scrollable preview of lines to be created
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredWidth: 360
                Layout.preferredHeight: Math.min(200,
                    checklistImportDialog.checklist.length * 22 + 16)
                clip: true

                Column {
                    width: parent.width
                    spacing: 2

                    Repeater {
                        model: checklistImportDialog.checklist
                        delegate: Text {
                            required property var modelData
                            width: parent.width
                            text: (modelData.done ? "✓ " : "○ ") + modelData.content
                            color: modelData.done
                                   ? ThemeManager.secondaryTextColor
                                   : ThemeManager.textColor
                            font.pixelSize: FontSize.small
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        // ── Actions ──────────────────────────────────────────────────────────
        onAccepted: {
            var pid = root.selectedProjectId
            var sid = root.selectedSectionId
            var list = checklist   // local copy; property resets on close

            for (var i = 0; i < list.length; i++) {
                var item = list[i]
                var newId = (typeof taskViewModel !== "undefined")
                    ? taskViewModel.addTask(item.content, pid, 4, "", [])
                    : null
                if (newId && item.done && typeof taskViewModel !== "undefined")
                    taskViewModel.completeTask(newId)
                if (newId && sid !== "" && typeof taskViewModel !== "undefined")
                    taskViewModel.moveTask(newId, pid, sid)
            }

            // Optional telemetry (guard against absence).
            if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                telemetry.trackEvent("quickadd_checklist_paste", { "count": list.length })

            taskField.text = ""
        }

        onRejected: {
            // Leave taskField empty — the raw multi-line text was already cleared
            // before the dialog opened (prevents pasted garbage lingering in the
            // single-line input).  User can retype manually if desired.
            taskField.text = ""
        }
    }

    // ─── AI priority suggestion — signal wiring ──────────────────────────────
    // Listens for prioritySuggested / errorReceived from the shared AIAssistant
    // service. The target is null-guarded so QuickAdd still works in contexts
    // where aiAssistant isn't exposed (tests, WASM without a key).
    Connections {
        target: (typeof aiAssistant !== "undefined" && aiAssistant !== null)
                  ? aiAssistant : null

        function onPrioritySuggested(p) {
            root._aiBusy = false
            // Clamp defensively in case future changes widen the contract.
            if (p >= 1 && p <= 4)
                root.selectedPriority = p
            // Telemetry: ai_priority_suggested {priority: N}
            if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled) {
                try {
                    telemetry.trackEvent("ai_priority_suggested", { priority: p })
                } catch (e) { /* never crash the UI */ }
            }
        }

        function onErrorReceived(error) {
            // Only react if WE were the ones who triggered an AI call. Other
            // callers (chat view) also use errorReceived; we can't reliably
            // tell them apart, so we just clear our busy flag if it's set.
            if (!root._aiBusy) return
            root._aiBusy = false
            const w = Window.window
            if (w && typeof w.showToast === "function")
                w.showToast("AI: " + error, "error")
            // Telemetry: ai_priority_failed (parsing or transport failure)
            if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled) {
                try {
                    telemetry.trackEvent("ai_priority_failed", { reason: error })
                } catch (e) { /* never crash the UI */ }
            }
        }
    }

    // ─── Helper functions ──────────────────────────────────────────────────────
    function priorityColor(p) {
        switch (p) {
            case 1:  return p1Color
            case 2:  return p2Color
            case 3:  return p3Color
            default: return p4Color
        }
    }

    function submitTask() {
        let rawText = taskField.text.trim()
        if (rawText.length === 0) return

        if (typeof taskViewModel !== "undefined") {
            // I22-D follow-up: resolve per-project default labels for the
            // currently selected project (skipped for Inbox / no project).
            // Returns label NAMES (not IDs) because Task.labels stores names
            // and the rest of the QuickAdd flow (manual chip input, inline
            // @labelname NLP) uses names. _resolveDefaultLabelNames handles
            // the ID→name lookup via dataStore.labelById.
            let projectDefaultLabels = root._resolveDefaultLabelNames(root.selectedProjectId)

            // If the user explicitly picked a priority / due / labels via the
            // popup buttons, those overrides take precedence and we call the
            // structured addTask. Otherwise the freeform NLP parser handles
            // the text — except when a per-project default priority should
            // be applied: in that case we still go through the structured
            // path but seed it with the NLP-parsed components so dates and
            // labels typed inline still work.
            let userOverrode = root.selectedPriority !== 4
                            || root.selectedDueString !== ""
                            || (root.selectedLabels && root.selectedLabels.length > 0)

            if (userOverrode) {
                // Merge project default labels into the manually picked set,
                // dedup by name (case-insensitive). The merge happens even
                // when selectedLabels is non-empty — the user explicitly
                // adding labels does NOT mean they want to opt out of the
                // project's defaults, only that they want both.
                let mergedLabels = root._mergeLabels(root.selectedLabels, projectDefaultLabels)
                let appliedDefaultsCount = mergedLabels.length - (root.selectedLabels ? root.selectedLabels.length : 0)

                var newId = taskViewModel.addTask(rawText,
                                                   root.selectedProjectId,
                                                   root.selectedPriority,
                                                   root.selectedDueString,
                                                   mergedLabels)
                // ITER1 #30: move to section if one was selected
                if (newId && root.selectedSectionId !== "")
                    taskViewModel.moveTask(newId, root.selectedProjectId, root.selectedSectionId)

                if (appliedDefaultsCount > 0)
                    root._trackDefaultLabelsApplied(appliedDefaultsCount)
            } else {
                // Check whether a per-project default priority would change
                // the resulting priority. We only apply it when:
                //   - a real, non-Inbox project is selected,
                //   - the user did NOT type an inline `pN` token (parser
                //     would otherwise return priority < 4),
                //   - the user did NOT type a `#projectname` redirect (so
                //     the project default actually matches the destination),
                //   - the project's default is something other than P4.
                let projectDefault = 4
                let canApplyDefaultPriority = false
                if (root.selectedProjectId !== ""
                    && typeof projectViewModel !== "undefined") {
                    let proj = projectViewModel.projectById(root.selectedProjectId)
                    if (proj && !proj.isInbox) {
                        let def = projectViewModel.defaultPriorityForProject(root.selectedProjectId)
                        if (def >= 1 && def <= 4 && def !== 4) {
                            projectDefault = def
                            canApplyDefaultPriority = true
                        }
                    }
                }

                // We need to route through the structured addTask path
                // whenever default-priority OR default-labels would change
                // the outcome — both cases need to merge the parsed-from-NLP
                // values with the per-project defaults. Otherwise we fall
                // through to the legacy addTaskFromQuickAdd freeform path.
                let canApplyDefaultLabels = projectDefaultLabels.length > 0
                let needsStructuredPath = canApplyDefaultPriority || canApplyDefaultLabels

                if (needsStructuredPath) {
                    let parsed = taskViewModel.previewQuickAdd(rawText)
                    let parserPriority = (parsed && parsed.priority) ? parsed.priority : 4
                    let parserProjectName = (parsed && parsed.projectName) ? parsed.projectName : ""

                    // Only route through structured path if the parser did
                    // NOT pick up an inline #projectname (in which case the
                    // task is destined for a different project and our
                    // defaults shouldn't apply) AND, if we'd be applying
                    // a default priority, the parser didn't already pick
                    // up an inline pN. Default LABELS, by contrast, are
                    // additive and merge with whatever @labelname tokens
                    // the parser found — so they don't impose a
                    // parserPriority constraint.
                    let priorityOk = !canApplyDefaultPriority || parserPriority === 4
                    let projectOk  = parserProjectName === ""

                    if (priorityOk && projectOk) {
                        let cleanContent = (parsed && parsed.content) ? parsed.content : rawText
                        let parsedDue    = (parsed && parsed.dueString) ? parsed.dueString : ""
                        let parsedLabels = (parsed && parsed.labels) ? parsed.labels : []
                        let mergedLabels = root._mergeLabels(parsedLabels, projectDefaultLabels)
                        let appliedDefaultsCount = mergedLabels.length - parsedLabels.length
                        let effectivePriority = canApplyDefaultPriority
                                                ? projectDefault
                                                : (parserPriority || 4)

                        var newId2 = taskViewModel.addTask(cleanContent,
                                                            root.selectedProjectId,
                                                            effectivePriority,
                                                            parsedDue,
                                                            mergedLabels)
                        if (newId2 && root.selectedSectionId !== "")
                            taskViewModel.moveTask(newId2, root.selectedProjectId, root.selectedSectionId)

                        if (appliedDefaultsCount > 0)
                            root._trackDefaultLabelsApplied(appliedDefaultsCount)
                    } else {
                        taskViewModel.addTaskFromQuickAdd(rawText)
                    }
                } else {
                    taskViewModel.addTaskFromQuickAdd(rawText)
                }
            }
        }

        clearFields()
        collapse()
    }

    function clearFields() {
        taskField.text         = ""
        root.selectedPriority  = 4
        root.selectedDueString = ""
        root.selectedLabels    = []
        // ITER1 #30: reset section selection
        root.selectedSectionId = ""
        // F-7.12: also reset the inline label input.
        // ITER4 #2: drive via _labelInputOpen.
        root._labelInputOpen = false
        labelInputField.text = ""
        // I22-D follow-up: reset per-draft dismissed default labels so the
        // next draft starts fresh.
        root._dismissedDefaultLabels = []
    }

    function collapse() {
        // Cancel-style collapse: discard typed text and reset selection state.
        // Without this, re-opening the panel shows the previous draft.
        clearFields()
        // ITER2 #18: only set expanded = false; the height binding handles the
        // animation. Setting root.height imperatively here would break the binding
        // and cause the Behavior to fire twice, producing a visible glitch.
        root.expanded = false
        taskField.focus = false
    }

    function expand() {
        // ITER2 #18: only set expanded = true; the binding drives the height.
        root.expanded = true
    }

    // Public: focus the input so callers (EmptyState button, hotkeys, command
    // palette) can open the panel programmatically. forceActiveFocus on the
    // QuickAdd root is a no-op because Rectangle isn't focusable; this routes
    // focus to the inner TextField, whose onActiveFocusChanged then expands
    // the panel.
    function focusInput() {
        taskField.forceActiveFocus()
    }

    // Russian plural for "задача": 1 → задачу, 2–4 → задачи, 5+ → задач.
    // Also handles the teens (11–14 → задач).
    function _pluralTasks(n) {
        var m100 = n % 100
        var m10  = n % 10
        if (m100 >= 11 && m100 <= 14) return qsTr("задач")
        if (m10 === 1)                 return qsTr("задачу")
        if (m10 >= 2 && m10 <= 4)     return qsTr("задачи")
        return qsTr("задач")
    }

    // F-7.12: show the inline label text field
    // ITER4 #2: drive via _labelInputOpen so the row's Behavior owns the slide-in.
    function openLabelInput() {
        root._labelInputOpen = true
        labelInputField.forceActiveFocus()
    }

    // I22-D follow-up: resolve per-project default labels (stored as IDs in
    // QSettings) to label NAMES. Returns [] for empty/Inbox/no-project, and
    // skips IDs whose label was deleted from DataStore. Names dismissed by
    // the user via the preview-chip × are filtered out (case-insensitive).
    function _resolveDefaultLabelNames(projectId) {
        if (projectId === ""
            || typeof projectViewModel === "undefined"
            || typeof dataStore === "undefined")
            return []
        let proj = projectViewModel.projectById(projectId)
        if (!proj || proj.isInbox) return []
        let ids = projectViewModel.defaultLabelsForProject(projectId)
        if (!ids || ids.length === 0) return []
        // Build a lower-cased dismissed-set for O(1) lookups.
        let dismissed = {}
        for (let d = 0; d < root._dismissedDefaultLabels.length; d++)
            dismissed[root._dismissedDefaultLabels[d].toLowerCase()] = true
        let out = []
        for (let i = 0; i < ids.length; i++) {
            let lbl = dataStore.labelById(ids[i])
            if (lbl && lbl.name && lbl.name.length > 0
                && !dismissed[lbl.name.toLowerCase()])
                out.push(lbl.name)
        }
        return out
    }

    // I22-D follow-up: merge two label-name arrays, preserving order of
    // `primary` first, appending unique entries from `defaults`. Comparison
    // is case-insensitive so "Работа" and "работа" don't both end up on
    // the task.
    function _mergeLabels(primary, defaults) {
        let merged = []
        let seen = {}   // lowercased name → true
        let push = function(name) {
            if (!name || name.length === 0) return
            let key = name.toLowerCase()
            if (seen[key]) return
            seen[key] = true
            merged.push(name)
        }
        if (primary)  for (let i = 0; i < primary.length;  i++) push(primary[i])
        if (defaults) for (let j = 0; j < defaults.length; j++) push(defaults[j])
        return merged
    }

    // I22-D follow-up: telemetry helper. Guarded for environments where
    // `telemetry` isn't exposed (tests, certain WASM paths).
    function _trackDefaultLabelsApplied(count) {
        if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled) {
            try {
                telemetry.trackEvent("project_default_labels_applied", { count: count })
            } catch (e) { /* never crash the UI */ }
        }
    }

    // F-7.12: parse comma-separated label names and store them
    function _commitLabelInput() {
        var raw = labelInputField.text.trim()
        if (raw.length > 0) {
            var parts = raw.split(",")
            var newLabels = []
            for (var i = 0; i < parts.length; i++) {
                var t = parts[i].trim()
                if (t.length > 0) newLabels.push(t)
            }
            // ITER2 #9: merge with existing selectedLabels instead of replacing,
            // so re-opening the label input appends rather than overwrites.
            root.selectedLabels = root.selectedLabels.concat(
                newLabels.filter(function(l) {
                    return root.selectedLabels.indexOf(l) === -1
                })
            )
        }
        // ITER4 #2: drive via _labelInputOpen.
        root._labelInputOpen = false
        labelInputField.text = ""
    }
}
