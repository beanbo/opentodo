import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

// Full reminders section for the task editor.
// Supports time-based ("absolute") and location-based ("location") reminders.
ColumnLayout {
    id: root

    property string taskId: ""

    Layout.fillWidth:    true
    Layout.leftMargin:   Spacing.md
    Layout.rightMargin:  Spacing.md
    Layout.bottomMargin: Spacing.xs
    spacing: Spacing.xs

    // ── JS helpers ─────────────────────────────────────────────────────────
    function _reminderLabel(reminder) {
        if (!reminder) return ""
        const t = reminder.type || "absolute"

        if (t === "location") {
            const loc = reminder.location
            if (loc) {
                const r = loc.radius > 0 ? loc.radius : 200
                const nm = loc.name && loc.name.length > 0 ? loc.name : null
                const isLeave = (loc.trigger === "on_leave")
                const zoneDir = isLeave
                    ? qsTr("🚪 при выходе из")
                    : qsTr("🚶 при входе в")
                const baseLabel = isLeave ? qsTr("При выходе") : qsTr("В радиусе")
                if (nm)
                    return qsTr("%1 зону «%2» (%3 м)").arg(zoneDir).arg(nm).arg(r)
                return (isLeave ? qsTr("🚪 При выходе из зоны %1 м").arg(r)
                                : qsTr("🚶 При входе в зону %1 м").arg(r))
            }
            return qsTr("Геолокационное напоминание")
        }

        // time-based
        const due = reminder.due
        let baseStr = ""
        if (due) {
            if (due.datetime && due.datetime.length > 0) {
                const dt = new Date(due.datetime)
                if (!isNaN(dt.getTime())) {
                    const now = new Date()
                    const tomorrow = new Date(now)
                    tomorrow.setDate(tomorrow.getDate() + 1)
                    const isToday    = dt.toDateString() === now.toDateString()
                    const isTomorrow = dt.toDateString() === tomorrow.toDateString()
                    const timeStr = Qt.formatTime(dt, "HH:mm")
                    if (isToday)    baseStr = qsTr("Сегодня в %1").arg(timeStr)
                    else if (isTomorrow) baseStr = qsTr("Завтра в %1").arg(timeStr)
                    else baseStr = qsTr("%1 в %2").arg(Qt.formatDate(dt, "dd.MM.yyyy")).arg(timeStr)
                }
            }
            if (baseStr === "" && due.date && due.date.length > 0) {
                // BUG-FIX (C-3): JS parses bare "yyyy-MM-dd" as UTC midnight,
                // shifting the rendered date back one day for UTC- users.
                // Append "T00:00:00" so the local-midnight interpretation
                // matches the convention used elsewhere (DesktopLayout.qml:843).
                baseStr = Qt.formatDate(new Date(due.date + "T00:00:00"), "dd.MM.yyyy")
            }
        }

        if (t === "relative") {
            const off = reminder.minuteOffset || 0
            if (off === 0)   return qsTr("В момент наступления")
            if (off < 60)    return qsTr("За %1 мин").arg(off)
            if (off < 1440)  return qsTr("За %1 ч").arg(Math.floor(off / 60))
            return qsTr("За %1 д").arg(Math.floor(off / 1440))
        }

        // absolute — append offset chip when non-zero
        if (baseStr !== "") {
            const off = reminder.minuteOffset || 0
            if (off > 0) {
                let chip = ""
                if (off < 60)        chip = qsTr("за %1 мин").arg(off)
                else if (off < 1440) chip = qsTr("за %1 ч").arg(Math.floor(off / 60))
                else                 chip = qsTr("за %1 д").arg(Math.floor(off / 1440))
                return baseStr + " (" + chip + ")"
            }
            return baseStr
        }

        return qsTr("Напоминание")
    }

    function _refreshModel() {
        if (root.taskId !== "")
            reminderRepeater.model = dataStore.remindersForTask(root.taskId)
        else
            reminderRepeater.model = []
    }

    // ── Auto-refresh ───────────────────────────────────────────────────────
    Component.onCompleted: _refreshModel()
    onTaskIdChanged:       _refreshModel()

    Connections {
        target: dataStore
        function onRemindersChanged() { root._refreshModel() }
    }

    // ── Section header row ─────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Spacing.sm

        Text {
            text:          qsTr("Напоминания")
            font.pixelSize: 13
            font.weight:   Font.Medium
            color:         ThemeManager.textColor
        }

        Item { Layout.fillWidth: true }

        // "+" button
        Rectangle {
            width:  22
            height: 22
            radius: 11
            color:  addReminderHover.containsMouse
                    ? ThemeManager.hoverColor : "transparent"
            border.color: ThemeManager.borderColor
            border.width: 1

            // Task 17cea65b iter-3: было unicode "+".
            IconSvg {
                anchors.centerIn: parent
                name: "plus"
                size: 14
                color: ThemeManager.textColor
            }

            MouseArea {
                id:           addReminderHover
                anchors.fill: parent
                hoverEnabled: true
                cursorShape:  Qt.PointingHandCursor
                onClicked:    addReminderDialog.open()
            }
        }
    }

    // ── Reminder list ──────────────────────────────────────────────────────
    Repeater {
        id: reminderRepeater

        delegate: Rectangle {
            required property var modelData
            required property int index

            id:     reminderRow
            width:  parent ? parent.width : 320
            height: reminderRowLayout.implicitHeight + Spacing.xs * 2
            radius: Radius.sm
            color:  reminderRowHover.containsMouse
                    ? ThemeManager.hoverColor : "transparent"

            RowLayout {
                id:      reminderRowLayout
                anchors {
                    left:    parent.left
                    right:   parent.right
                    top:     parent.top
                    margins: Spacing.xs
                }
                spacing: Spacing.sm

                // Type icon
                Text {
                    font.pixelSize: 14
                    text:           (modelData.type === "location")
                                    ? "📍"   // 📍 pin
                                    : "⏰"          // ⏰ clock
                    color:          ThemeManager.secondaryTextColor
                }

                Text {
                    Layout.fillWidth: true
                    text:             root._reminderLabel(modelData)
                    font.pixelSize:   13
                    color:            ThemeManager.textColor
                    elide:            Text.ElideRight
                }

                // Trash icon (visible on hover)
                Rectangle {
                    visible:  reminderRowHover.containsMouse
                    width:    20
                    height:   20
                    radius:   Radius.xs
                    color:    reminderTrashHover.containsMouse
                              ? Qt.rgba(ThemeManager.accentColor.r,
                                        ThemeManager.accentColor.g,
                                        ThemeManager.accentColor.b, 0.15)
                              : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text:          "\u{1F5D1}"
                        font.pixelSize: 12
                    }

                    MouseArea {
                        id:           reminderTrashHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape:  Qt.PointingHandCursor
                        onClicked: {
                            dataStore.removeReminder(modelData.id)
                            root._refreshModel()
                        }
                    }
                }
            }

            MouseArea {
                id:           reminderRowHover
                anchors.fill: parent
                hoverEnabled: true
            }
        }
    }

    // ── Empty state ────────────────────────────────────────────────────────
    Text {
        visible:             reminderRepeater.count === 0
        Layout.fillWidth:    true
        horizontalAlignment: Text.AlignHCenter
        text:                qsTr("Пока нет напоминаний")
        font.pixelSize:      12
        font.italic:         true
        color:               ThemeManager.secondaryTextColor
        opacity:             0.7
        topPadding:          2
        bottomPadding:       2
    }

    // ── Add Reminder Dialog ────────────────────────────────────────────────
    Dialog {
        id:     addReminderDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        title:  qsTr("Добавить напоминание")
        modal:  true
        width:  360
        standardButtons: Dialog.NoButton

        background: Rectangle {
            color:        ThemeManager.surfaceColor
            radius:       Radius.md
            border.color: ThemeManager.borderColor
            border.width: 1
        }

        header: Rectangle {
            height: 48
            color:  "transparent"
            Text {
                anchors {
                    left:           parent.left
                    leftMargin:     Spacing.md
                    verticalCenter: parent.verticalCenter
                }
                text:          qsTr("Добавить напоминание")
                font.pixelSize: 15
                font.weight:   Font.Medium
                color:         ThemeManager.textColor
            }
        }

        // ── Dialog internals ───────────────────────────────────────────────
        property int  activeTab:   0          // 0=По времени, 1=По месту

        // Time tab state
        property string pickedDate:        ""       // "yyyy-MM-dd"
        property string pickedTime:        "09:00"  // "HH:mm"
        property bool   repeat_:           false
        property string repeatRule:        ""
        property int    minuteOffsetIndex: 0        // index into offsetOptions

        // Location tab state
        property string locLat:        ""
        property string locLon:        ""
        property string locRadius:     "200"
        property string locName:       ""
        property bool   locTriggerEnter: true       // true→on_enter, false→on_leave

        // Reset on open
        onOpened: {
            activeTab         = 0
            pickedDate        = ""
            pickedTime        = "09:00"
            repeat_           = false
            repeatRule        = ""
            minuteOffsetIndex = 0
            locLat            = ""
            locLon            = ""
            locRadius         = "200"
            locName           = ""
            locTriggerEnter   = true
        }

        ColumnLayout {
            width:   parent.width
            spacing: Spacing.sm

            // ── Tab selector ───────────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                spacing: 0

                Repeater {
                    model: [qsTr("По времени"), qsTr("По месту")]

                    delegate: Rectangle {
                        required property string modelData
                        required property int    index

                        Layout.fillWidth: true
                        height:  36
                        color:   addReminderDialog.activeTab === index
                                 ? ThemeManager.backgroundColor : "transparent"
                        border.color: ThemeManager.borderColor
                        border.width: 1
                        radius:  0

                        Text {
                            anchors.centerIn: parent
                            text:          modelData
                            font.pixelSize: 13
                            color:         addReminderDialog.activeTab === index
                                           ? ThemeManager.textColor
                                           : ThemeManager.secondaryTextColor
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape:  Qt.PointingHandCursor
                            onClicked:    addReminderDialog.activeTab = index
                        }
                    }
                }
            }

            // ── Offset options (shared property, used by By-Time tab) ──────
            readonly property var offsetOptions: [
                { label: qsTr("В точное время"), value: 0    },
                { label: qsTr("За 5 минут"),     value: 5    },
                { label: qsTr("За 10 минут"),    value: 10   },
                { label: qsTr("За 15 минут"),    value: 15   },
                { label: qsTr("За 30 минут"),    value: 30   },
                { label: qsTr("За 1 час"),       value: 60   },
                { label: qsTr("За 2 часа"),      value: 120  },
                { label: qsTr("За 1 день"),      value: 1440 }
            ]

            // ── By Time tab ────────────────────────────────────────────────
            ColumnLayout {
                visible:          addReminderDialog.activeTab === 0
                Layout.fillWidth: true
                spacing:          Spacing.sm

                // Date picker row
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Spacing.sm

                    Text {
                        text:          qsTr("Дата:")
                        font.pixelSize: 13
                        color:         ThemeManager.secondaryTextColor
                        Layout.preferredWidth: 60
                    }

                    Rectangle {
                        height: 30
                        width:  datePickBtn.implicitWidth + 24
                        radius: Radius.sm
                        color:  datePickBtnHover.containsMouse
                                ? ThemeManager.hoverColor : ThemeManager.backgroundColor
                        border.color: ThemeManager.borderColor
                        border.width: 1

                        Text {
                            id:            datePickBtn
                            anchors.centerIn: parent
                            text:          addReminderDialog.pickedDate !== ""
                                           ? addReminderDialog.pickedDate
                                           : qsTr("Выбрать дату")
                            font.pixelSize: 13
                            color:         addReminderDialog.pickedDate !== ""
                                           ? ThemeManager.textColor
                                           : ThemeManager.secondaryTextColor
                        }

                        MouseArea {
                            id:           datePickBtnHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape:  Qt.PointingHandCursor
                            onClicked:    reminderDatePicker.open()
                        }
                    }

                    DatePicker {
                        id:           reminderDatePicker
                        selectedDate: addReminderDialog.pickedDate
                        onDateSelected: function(ds) {
                            addReminderDialog.pickedDate = ds
                        }
                    }
                }

                // Time row
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Spacing.sm

                    Text {
                        text:          qsTr("Время:")
                        font.pixelSize: 13
                        color:         ThemeManager.secondaryTextColor
                        Layout.preferredWidth: 60
                    }

                    TextField {
                        id:               timeField
                        text:             addReminderDialog.pickedTime
                        font.pixelSize:   13
                        color:            ThemeManager.textColor
                        inputMask:        "99:99"
                        placeholderText:  qsTr("ЧЧ:ММ")
                        Layout.preferredWidth: 80
                        background: Rectangle {
                            color:        ThemeManager.backgroundColor
                            border.color: ThemeManager.borderColor
                            border.width: 1
                            radius:       Radius.sm
                        }
                        onTextChanged: addReminderDialog.pickedTime = text
                    }
                }

                // Repeat checkbox
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Spacing.sm

                    CheckBox {
                        id:      repeatCheck
                        checked: addReminderDialog.repeat_
                        onCheckedChanged: addReminderDialog.repeat_ = checked

                        contentItem: Text {
                            leftPadding:   repeatCheck.indicator.width + 4
                            text:          qsTr("Повторять")
                            font.pixelSize: 13
                            color:         ThemeManager.textColor
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                TextField {
                    visible:          addReminderDialog.repeat_
                    Layout.fillWidth: true
                    // B-7.65: bind text so dialog reset (onOpened sets repeatRule="")
                    // clears this field rather than leaving stale text.
                    text:             addReminderDialog.repeatRule
                    placeholderText:  qsTr("Правило повтора (напр. FREQ=DAILY)")
                    font.pixelSize:   12
                    color:            ThemeManager.textColor
                    onTextChanged:    addReminderDialog.repeatRule = text
                    background: Rectangle {
                        color:        ThemeManager.backgroundColor
                        border.color: ThemeManager.borderColor
                        border.width: 1
                        radius:       Radius.sm
                    }
                }

                // ── Minute-offset selector ─────────────────────────────────
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Spacing.sm

                    Text {
                        text:                  qsTr("Напомнить:")
                        font.pixelSize:        13
                        color:                 ThemeManager.secondaryTextColor
                        Layout.preferredWidth: 80
                    }

                    ComboBox {
                        id:               minuteOffsetCombo
                        Layout.fillWidth: true
                        textRole:         "label"
                        model:            addReminderDialog.offsetOptions
                        currentIndex:     addReminderDialog.minuteOffsetIndex
                        font.pixelSize:   13

                        onCurrentIndexChanged:
                            addReminderDialog.minuteOffsetIndex = currentIndex

                        contentItem: Text {
                            leftPadding:        8
                            text:               minuteOffsetCombo.displayText
                            font:               minuteOffsetCombo.font
                            color:              ThemeManager.textColor
                            verticalAlignment:  Text.AlignVCenter
                            elide:              Text.ElideRight
                        }

                        background: Rectangle {
                            color:        ThemeManager.backgroundColor
                            border.color: ThemeManager.borderColor
                            border.width: 1
                            radius:       Radius.sm
                        }

                        delegate: ItemDelegate {
                            required property var  modelData
                            required property int  index

                            width:  minuteOffsetCombo.width
                            highlighted: minuteOffsetCombo.highlightedIndex === index

                            contentItem: Text {
                                text:              modelData.label
                                font.pixelSize:    13
                                color:             ThemeManager.textColor
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                color: highlighted
                                       ? ThemeManager.hoverColor
                                       : ThemeManager.surfaceColor
                            }
                        }

                        popup: Popup {
                            y:      minuteOffsetCombo.height
                            width:  minuteOffsetCombo.width
                            padding: 0

                            background: Rectangle {
                                color:        ThemeManager.surfaceColor
                                border.color: ThemeManager.borderColor
                                border.width: 1
                                radius:       Radius.sm
                            }

                            contentItem: ListView {
                                implicitHeight: contentHeight
                                model:          minuteOffsetCombo.delegateModel
                                clip:           true
                            }
                        }
                    }
                }
            }

            // ── By Place tab ───────────────────────────────────────────────
            ColumnLayout {
                visible:          addReminderDialog.activeTab === 1
                Layout.fillWidth: true
                spacing:          Spacing.sm

                // Helper label
                Text {
                    Layout.fillWidth: true
                    text:          qsTr("Введите координаты и радиус зоны.")
                    font.pixelSize: 12
                    color:         ThemeManager.secondaryTextColor
                    wrapMode:      Text.Wrap
                }

                // Use current location button (only shown if locationService exists)
                Rectangle {
                    id: useLocBtn
                    visible:  typeof locationService !== "undefined"
                    height:   30
                    width:    useLocText.implicitWidth + 24
                    radius:   Radius.sm
                    color:    useLocHover.containsMouse
                              ? ThemeManager.hoverColor : ThemeManager.backgroundColor
                    border.color: ThemeManager.borderColor
                    border.width: 1

                    Text {
                        id:            useLocText
                        anchors.centerIn: parent
                        text:          qsTr("Использовать текущее местоположение")
                        font.pixelSize: 12
                        color:         ThemeManager.textColor
                    }

                    MouseArea {
                        id:           useLocHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape:  Qt.PointingHandCursor
                        onClicked: {
                            if (typeof locationService === "undefined") return
                            // B-7.38: use lastKnownPosition() if already available;
                            // otherwise start() and wait for the first valid position
                            // via the positionUpdated signal (filled through a Connections).
                            if (locationService.hasLastKnownPosition()) {
                                const coord = locationService.lastKnownPosition()
                                addReminderDialog.locLat = coord.latitude.toString()
                                addReminderDialog.locLon = coord.longitude.toString()
                            } else {
                                locationService.start()
                            }
                        }
                    }

                    // B-7.38 + Wave 3 follow-up: react to LocationService.positionUpdated
                    // signal directly (no polling). The handler is gated by `armed`
                    // so it only fires when the user explicitly clicked the button
                    // and a fix wasn't already available.
                    property bool _locArmed: false

                    Timer {
                        id: locPollTimer  // legacy id retained for compat; just a guard timeout now
                        interval: 30000
                        repeat: false
                        running: false
                        onTriggered: useLocBtn._locArmed = false
                    }

                    Connections {
                        target: typeof locationService !== "undefined" ? locationService : null
                        function onPositionUpdated(coord) {
                            if (!useLocBtn._locArmed) return
                            addReminderDialog.locLat = coord.latitude.toString()
                            addReminderDialog.locLon = coord.longitude.toString()
                            useLocBtn._locArmed = false
                            locPollTimer.stop()
                        }
                    }

                    // enabler: arm the positionUpdated handler when the user
                    // clicks "use current location" without an existing fix.
                    Connections {
                        target: useLocHover
                        function onClicked() {
                            if (typeof locationService !== "undefined"
                                    && !locationService.hasLastKnownPosition()) {
                                useLocBtn._locArmed = true
                                locPollTimer.start()  // 30s timeout
                            }
                        }
                    }
                }

                // Latitude
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Spacing.sm

                    Text {
                        text:          qsTr("Широта:")
                        font.pixelSize: 13
                        color:         ThemeManager.secondaryTextColor
                        Layout.preferredWidth: 70
                    }

                    TextField {
                        Layout.fillWidth: true
                        placeholderText: qsTr("напр. 55.751244")
                        font.pixelSize:  13
                        color:           ThemeManager.textColor
                        validator:       DoubleValidator { bottom: -90; top: 90 }
                        onTextChanged:   addReminderDialog.locLat = text
                        background: Rectangle {
                            color:        ThemeManager.backgroundColor
                            border.color: ThemeManager.borderColor
                            border.width: 1
                            radius:       Radius.sm
                        }
                    }
                }

                // Longitude
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Spacing.sm

                    Text {
                        text:          qsTr("Долгота:")
                        font.pixelSize: 13
                        color:         ThemeManager.secondaryTextColor
                        Layout.preferredWidth: 70
                    }

                    TextField {
                        Layout.fillWidth: true
                        placeholderText: qsTr("напр. 37.618423")
                        font.pixelSize:  13
                        color:           ThemeManager.textColor
                        validator:       DoubleValidator { bottom: -180; top: 180 }
                        onTextChanged:   addReminderDialog.locLon = text
                        background: Rectangle {
                            color:        ThemeManager.backgroundColor
                            border.color: ThemeManager.borderColor
                            border.width: 1
                            radius:       Radius.sm
                        }
                    }
                }

                // Radius
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Spacing.sm

                    Text {
                        text:          qsTr("Радиус, м:")
                        font.pixelSize: 13
                        color:         ThemeManager.secondaryTextColor
                        Layout.preferredWidth: 70
                    }

                    TextField {
                        Layout.preferredWidth: 90
                        text:            addReminderDialog.locRadius
                        font.pixelSize:  13
                        color:           ThemeManager.textColor
                        validator:       IntValidator { bottom: 1; top: 100000 }
                        onTextChanged:   addReminderDialog.locRadius = text
                        background: Rectangle {
                            color:        ThemeManager.backgroundColor
                            border.color: ThemeManager.borderColor
                            border.width: 1
                            radius:       Radius.sm
                        }
                    }
                }

                // Name / description (optional)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Spacing.sm

                    Text {
                        text:          qsTr("Название:")
                        font.pixelSize: 13
                        color:         ThemeManager.secondaryTextColor
                        Layout.preferredWidth: 70
                    }

                    TextField {
                        Layout.fillWidth: true
                        placeholderText: qsTr("необязательно")
                        font.pixelSize:  13
                        color:           ThemeManager.textColor
                        onTextChanged:   addReminderDialog.locName = text
                        background: Rectangle {
                            color:        ThemeManager.backgroundColor
                            border.color: ThemeManager.borderColor
                            border.width: 1
                            radius:       Radius.sm
                        }
                    }
                }

                // ── Trigger selector ───────────────────────────────────────
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Spacing.sm

                    Text {
                        text:                  qsTr("Сработать:")
                        font.pixelSize:        13
                        color:                 ThemeManager.secondaryTextColor
                        Layout.preferredWidth: 70
                    }

                    RadioButton {
                        id:      triggerEnterBtn
                        text:    qsTr("При входе")
                        checked: addReminderDialog.locTriggerEnter
                        onCheckedChanged: {
                            if (checked) addReminderDialog.locTriggerEnter = true
                        }

                        contentItem: Text {
                            leftPadding:       triggerEnterBtn.indicator.width + 4
                            text:              triggerEnterBtn.text
                            font.pixelSize:    13
                            color:             ThemeManager.textColor
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    RadioButton {
                        id:      triggerLeaveBtn
                        text:    qsTr("При выходе")
                        checked: !addReminderDialog.locTriggerEnter
                        onCheckedChanged: {
                            if (checked) addReminderDialog.locTriggerEnter = false
                        }

                        contentItem: Text {
                            leftPadding:       triggerLeaveBtn.indicator.width + 4
                            text:              triggerLeaveBtn.text
                            font.pixelSize:    13
                            color:             ThemeManager.textColor
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }

            // ── Action buttons ─────────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                spacing:          Spacing.sm

                Item { Layout.fillWidth: true }

                // Cancel
                Rectangle {
                    height: 30
                    width:  cancelReminderText.implicitWidth + 20
                    radius: Radius.sm
                    color:  cancelReminderHover.containsMouse
                            ? ThemeManager.hoverColor : "transparent"
                    border.color: ThemeManager.borderColor
                    border.width: 1

                    Text {
                        id:            cancelReminderText
                        anchors.centerIn: parent
                        text:          qsTr("Отмена")
                        font.pixelSize: 13
                        color:         ThemeManager.textColor
                    }

                    MouseArea {
                        id:           cancelReminderHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    addReminderDialog.close()
                    }
                }

                // Save
                Rectangle {
                    id:     saveReminderBtn
                    height: 30
                    width:  saveReminderText.implicitWidth + 20
                    radius: Radius.sm

                    readonly property bool canSave: {
                        if (addReminderDialog.activeTab === 0)
                            return addReminderDialog.pickedDate.length > 0
                        // location tab — need lat and lon
                        return addReminderDialog.locLat.length > 0
                               && addReminderDialog.locLon.length > 0
                    }

                    color: {
                        if (!canSave) return ThemeManager.borderColor
                        if (saveReminderHover.containsMouse)
                            return Qt.darker(ThemeManager.accentColor, 1.1)
                        return ThemeManager.accentColor
                    }
                    opacity: canSave ? 1.0 : 0.5

                    Text {
                        id:            saveReminderText
                        anchors.centerIn: parent
                        text:          qsTr("Сохранить")
                        font.pixelSize: 13
                        color:         saveReminderBtn.canSave ? "white"
                                       : ThemeManager.secondaryTextColor
                    }

                    MouseArea {
                        id:           saveReminderHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape:  saveReminderBtn.canSave
                                      ? Qt.PointingHandCursor : Qt.ArrowCursor
                        enabled:      saveReminderBtn.canSave
                        onClicked: {
                            if (!saveReminderBtn.canSave || root.taskId === "") return

                            const dlg = addReminderDialog
                            if (dlg.activeTab === 0) {
                                // ── B-7.37: Time-based reminder ────────────
                                const rem = dataStore.addReminder(root.taskId, "absolute")
                                if (rem) {
                                    // setDueDatetime(iso) is Q_INVOKABLE on Reminder
                                    const dtStr = dlg.pickedDate + "T" + dlg.pickedTime + ":00"
                                    rem.setDueDatetime(dtStr)
                                    // setRepeatRule(rule) is Q_INVOKABLE on Reminder
                                    if (dlg.repeat_ && dlg.repeatRule.length > 0)
                                        rem.setRepeatRule(dlg.repeatRule)
                                    // setMinuteOffset(minutes) — 0 means exact time (default)
                                    const offVal = dlg.offsetOptions[dlg.minuteOffsetIndex].value
                                    if (offVal > 0)
                                        rem.setMinuteOffset(offVal)
                                    dataStore.markDataChanged()
                                }
                            } else {
                                // ── B-7.37: Location-based reminder ────────
                                const remLoc = dataStore.addReminder(root.taskId, "location")
                                if (remLoc) {
                                    const lat = parseFloat(dlg.locLat)
                                    const lon = parseFloat(dlg.locLon)
                                    const rad = parseInt(dlg.locRadius) || 200
                                    const trig = dlg.locTriggerEnter ? "on_enter" : "on_leave"
                                    // setLocationCoords(lat, lon, radiusMeters, name, trigger)
                                    remLoc.setLocationCoords(lat, lon, rad, dlg.locName, trig)
                                    if (typeof locationService !== "undefined") {
                                        locationService.addGeofence(remLoc.id, lat, lon, rad)
                                    }
                                    dataStore.markDataChanged()
                                }
                            }

                            root._refreshModel()
                            addReminderDialog.close()
                        }
                    }
                }
            }
        }
    }
}
