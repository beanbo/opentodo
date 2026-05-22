import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtCore
import OpenTodoUI

Rectangle {
    id: root
    radius: 8
    color: ThemeManager.surfaceColor
    border.color: ThemeManager.borderColor
    border.width: 1
    implicitHeight: sectionHeader.height + (expanded ? sectionContent.implicitHeight : 0)
    clip: true

    property bool expanded: false

    Behavior on implicitHeight {
        NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
    }

    // ── Daily review firing time (persisted via Qt.labs.settings) ─────────────
    // Read by DailyReviewService at construction and after rescheduleFromSettings().
    Settings {
        id: dailyReviewSettings
        category: "productivity"
        property int dailyReviewHour: 18
        property int dailyReviewMinute: 0
    }

    // ── Pomodoro durations (I28-A) ────────────────────────────────────────────
    // Persisted via Qt.labs.settings under category "pomodoro". The
    // PomodoroService reads the same keys at startup and via
    // reloadDurationsFromSettings() so changes apply on the next cycle.
    Settings {
        id: pomoSettings
        category: "pomodoro"
        property int workMinutes: 25
        property int shortBreakMinutes: 5
        property int longBreakMinutes: 15
        property int cyclesBeforeLongBreak: 4
    }

    // Helper: ask the C++ service to re-read settings and reschedule. The
    // service is not exposed as a context property in every build, so the
    // call is guarded by typeof — when absent, the new time will simply be
    // picked up at the next app start (the service reads QSettings in ctor).
    function applyDailyReviewTime() {
        if (typeof dailyReviewService !== "undefined" && dailyReviewService)
            dailyReviewService.rescheduleFromSettings()
    }

    function reloadPomo() {
        if (typeof pomodoroService !== "undefined" && pomodoroService
                && typeof pomodoroService.reloadDurationsFromSettings === "function")
            pomodoroService.reloadDurationsFromSettings()
    }

    function pad2(n) { return n < 10 ? "0" + n : "" + n }

    // ── Header ────────────────────────────────────────────────────────────────
    Rectangle {
        id: sectionHeader
        width: parent.width
        height: 48
        color: "transparent"
        radius: 8

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20

            Text {
                text: qsTr("Продуктивность")
                font.pixelSize: 16
                font.bold: true
                color: ThemeManager.textColor
                Layout.fillWidth: true
            }

            Text {
                text: root.expanded ? "▾" : "▸"
                font.pixelSize: 14
                color: ThemeManager.secondaryTextColor
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            height: 1
            color: ThemeManager.borderColor
            visible: root.expanded
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.expanded = !root.expanded
        }
    }

    // ── Content ───────────────────────────────────────────────────────────────
    ColumnLayout {
        id: sectionContent
        anchors.top: sectionHeader.bottom
        width: parent.width
        spacing: 0

        // Daily goal
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 16

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: qsTr("Дневная цель")
                        font.pixelSize: 14
                        color: ThemeManager.textColor
                    }
                    Text {
                        text: qsTr("Сколько задач выполнять в день")
                        font.pixelSize: 12
                        color: ThemeManager.secondaryTextColor
                    }
                }

                SpinBox {
                    from: 1
                    to: 50
                    value: settingsViewModel.dailyGoal
                    implicitWidth: 110

                    Layout.preferredWidth: 110

                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    onValueModified: {
                        settingsViewModel.updateSetting("dailyGoal", value)
                        if (typeof telemetry !== "undefined" && telemetry.enabled)
                            telemetry.trackEvent("settings_daily_goal_changed", { "value": value })
                    }

                    contentItem: TextInput {
                        z: 2
                        text: parent.displayText
                        font.pixelSize: 13
                        color: ThemeManager.textColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        readOnly: !parent.editable
                        validator: parent.validator
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                    }
                    background: Rectangle {
                        radius: 4
                        color: ThemeManager.hoverColor
                        border.color: ThemeManager.borderColor
                        border.width: 1
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                height: 1
                color: ThemeManager.borderColor
            }
        }

        // Weekly goal
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 16

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: qsTr("Недельная цель")
                        font.pixelSize: 14
                        color: ThemeManager.textColor
                    }
                    Text {
                        text: qsTr("Сколько задач выполнять в неделю")
                        font.pixelSize: 12
                        color: ThemeManager.secondaryTextColor
                    }
                }

                SpinBox {
                    from: 1
                    to: 200
                    value: settingsViewModel.weeklyGoal
                    implicitWidth: 110

                    Layout.preferredWidth: 110

                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    onValueModified: {
                        settingsViewModel.updateSetting("weeklyGoal", value)
                        if (typeof telemetry !== "undefined" && telemetry.enabled)
                            telemetry.trackEvent("settings_weekly_goal_changed", { "value": value })
                    }

                    contentItem: TextInput {
                        z: 2
                        text: parent.displayText
                        font.pixelSize: 13
                        color: ThemeManager.textColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        readOnly: !parent.editable
                        validator: parent.validator
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                    }
                    background: Rectangle {
                        radius: 4
                        color: ThemeManager.hoverColor
                        border.color: ThemeManager.borderColor
                        border.width: 1
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                height: 1
                color: ThemeManager.borderColor
            }
        }

        // Vacation mode
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 16

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: qsTr("Режим отпуска")
                        font.pixelSize: 14
                        color: ThemeManager.textColor
                    }

                    Text {
                        text: qsTr("Приостановить отслеживание очков во время отпуска")
                        font.pixelSize: 12
                        color: ThemeManager.secondaryTextColor
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                Switch {
                    checked: settingsViewModel.vacationMode
                    onToggled: {
                        settingsViewModel.updateSetting("vacationMode", checked)
                        if (typeof telemetry !== "undefined" && telemetry.enabled)
                            telemetry.trackEvent("settings_vacation_toggled", { "enabled": checked })
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                height: 1
                color: ThemeManager.borderColor
            }
        }

        // Daily review notification time — only shown when the daily review
        // toggle (in SettingsSectionGeneral) is enabled. The toggle's source
        // of truth is settingsViewModel.dailyReviewEnabled.
        Rectangle {
            id: dailyReviewTimeRow
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 78 : 0
            color: "transparent"
            visible: settingsViewModel.dailyReviewEnabled
            clip: true

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                anchors.topMargin: 8
                anchors.bottomMargin: 8
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: qsTr("Время уведомления о ежедневном обзоре")
                            font.pixelSize: 14
                            color: ThemeManager.textColor
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        Text {
                            text: qsTr("Обзор сработает в %1:%2")
                                  .arg(root.pad2(dailyReviewSettings.dailyReviewHour))
                                  .arg(root.pad2(dailyReviewSettings.dailyReviewMinute))
                            font.pixelSize: 12
                            color: ThemeManager.secondaryTextColor
                        }
                    }

                    SpinBox {
                        id: hourSpin
                        from: 0
                        to: 23
                        value: dailyReviewSettings.dailyReviewHour
                        editable: true
                        implicitWidth: 80
                        Layout.preferredWidth: 80
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        onValueModified: {
                            dailyReviewSettings.dailyReviewHour = value
                            root.applyDailyReviewTime()
                            if (typeof telemetry !== "undefined" && telemetry.enabled)
                                telemetry.trackEvent("settings_daily_review_time_changed",
                                                     { "hour": dailyReviewSettings.dailyReviewHour,
                                                       "minute": dailyReviewSettings.dailyReviewMinute })
                        }
                        textFromValue: function(v) { return root.pad2(v) }
                        valueFromText: function(t) { return parseInt(t, 10) }

                        contentItem: TextInput {
                            z: 2
                            text: parent.displayText
                            font.pixelSize: 13
                            color: ThemeManager.textColor
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            readOnly: !parent.editable
                            validator: parent.validator
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                        }
                        background: Rectangle {
                            radius: 4
                            color: ThemeManager.hoverColor
                            border.color: ThemeManager.borderColor
                            border.width: 1
                        }
                    }

                    Text {
                        text: ":"
                        font.pixelSize: 14
                        font.bold: true
                        color: ThemeManager.textColor
                        Layout.alignment: Qt.AlignVCenter
                    }

                    SpinBox {
                        id: minuteSpin
                        from: 0
                        to: 59
                        value: dailyReviewSettings.dailyReviewMinute
                        editable: true
                        implicitWidth: 80
                        Layout.preferredWidth: 80
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        onValueModified: {
                            dailyReviewSettings.dailyReviewMinute = value
                            root.applyDailyReviewTime()
                            if (typeof telemetry !== "undefined" && telemetry.enabled)
                                telemetry.trackEvent("settings_daily_review_time_changed",
                                                     { "hour": dailyReviewSettings.dailyReviewHour,
                                                       "minute": dailyReviewSettings.dailyReviewMinute })
                        }
                        textFromValue: function(v) { return root.pad2(v) }
                        valueFromText: function(t) { return parseInt(t, 10) }

                        contentItem: TextInput {
                            z: 2
                            text: parent.displayText
                            font.pixelSize: 13
                            color: ThemeManager.textColor
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            readOnly: !parent.editable
                            validator: parent.validator
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                        }
                        background: Rectangle {
                            radius: 4
                            color: ThemeManager.hoverColor
                            border.color: ThemeManager.borderColor
                            border.width: 1
                        }
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                height: 1
                color: ThemeManager.borderColor
            }
        }

        // ── Pomodoro durations (I28-A) ────────────────────────────────────────
        // Sibling row inside sectionContent so the labels and SpinBoxes have
        // the full section width to themselves (previously this block was
        // nested inside the fixed-height dailyReviewTimeRow Rectangle which
        // clipped its contents).
        Rectangle {
            id: pomoRow
            Layout.fillWidth: true
            Layout.preferredHeight: pomoCol.implicitHeight + 24
            color: "transparent"

            ColumnLayout {
                id: pomoCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                anchors.topMargin: 12
                spacing: 10

                // Section title
                Text {
                    text: "🍅 Pomodoro"
                    font.pixelSize: 14
                    font.bold: true
                    color: ThemeManager.textColor
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }

                Text {
                    text: qsTr("Применяется со следующего цикла, если идёт сессия")
                    font.pixelSize: 11
                    color: ThemeManager.secondaryTextColor
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                // 4 SpinBox rows in a grid
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8

                    Text {
                        text: qsTr("Длительность работы (минут)")
                        font.pixelSize: 13
                        color: ThemeManager.textColor
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        elide: Text.ElideRight
                    }
                    SpinBox {
                        id: workSpin
                        from: 1; to: 120
                        value: pomoSettings.workMinutes
                        editable: true
                        Layout.preferredWidth: 110
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        onValueModified: {
                            pomoSettings.workMinutes = value
                            root.reloadPomo()
                        }
                    }

                    Text {
                        text: qsTr("Короткий перерыв (минут)")
                        font.pixelSize: 13
                        color: ThemeManager.textColor
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        elide: Text.ElideRight
                    }
                    SpinBox {
                        id: shortBreakSpin
                        from: 1; to: 60
                        value: pomoSettings.shortBreakMinutes
                        editable: true
                        Layout.preferredWidth: 110
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        onValueModified: {
                            pomoSettings.shortBreakMinutes = value
                            root.reloadPomo()
                        }
                    }

                    Text {
                        text: qsTr("Длинный перерыв (минут)")
                        font.pixelSize: 13
                        color: ThemeManager.textColor
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        elide: Text.ElideRight
                    }
                    SpinBox {
                        id: longBreakSpin
                        from: 1; to: 120
                        value: pomoSettings.longBreakMinutes
                        editable: true
                        Layout.preferredWidth: 110
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        onValueModified: {
                            pomoSettings.longBreakMinutes = value
                            root.reloadPomo()
                        }
                    }

                    Text {
                        text: qsTr("Циклов до длинного перерыва")
                        font.pixelSize: 13
                        color: ThemeManager.textColor
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        elide: Text.ElideRight
                    }
                    SpinBox {
                        id: cyclesSpin
                        from: 2; to: 12
                        value: pomoSettings.cyclesBeforeLongBreak
                        editable: true
                        Layout.preferredWidth: 110
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        onValueModified: {
                            pomoSettings.cyclesBeforeLongBreak = value
                            root.reloadPomo()
                        }
                    }
                }
            }
        }
    }
}
