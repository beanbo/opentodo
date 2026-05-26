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

    property bool expanded: true

    Behavior on implicitHeight {
        NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
    }

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
                text: qsTr("Общие")
                font.pixelSize: FontSize.body
                font.weight: Font.DemiBold
                color: ThemeManager.textColor
                Layout.fillWidth: true
            }

            // Q4.01 (redesign): chevron unicode → stroke IconSvg
            IconSvg {
                name: root.expanded ? "chevron-down" : "chevron-right"
                size: 14
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

        // Language picker was here. Removed (Q3.07-followup): it duplicated
        // the dedicated "Язык интерфейса" section below — two surfaces that
        // wrote to the same settings/language key and visually competed. The
        // dedicated section also owns the "Перезапустить" button next to the
        // restart-required hint, which is the primary affordance for applying
        // a language change.

        // Date format — persisted via Qt.labs.settings (category "general",
        // key "dateFormat"). DateFormatter.qml singleton reads from the same
        // store, so changing the choice here updates every binding that uses
        // DateFormatter.format(...) elsewhere in the app.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 68
            color: "transparent"

            Settings {
                id: gen
                category: "general"
                property string dateFormat: "dd.MM.yyyy"
            }

            // Mirror the persisted choice onto the DateFormatter singleton at
            // startup. The singleton itself can't import Qt.labs.settings (Qt
            // resolves singleton imports too early — the optional module isn't
            // mounted yet, which produced "module Qt.labs.settings is not
            // installed" failures cascading through every consumer).
            Component.onCompleted: DateFormatter.setFormat(gen.dateFormat)

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 16

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: qsTr("Формат даты")
                        font.pixelSize: FontSize.bodySmall
                        color: ThemeManager.textColor
                        Layout.fillWidth: true
                    }

                    Text {
                        text: qsTr("Используется во всех частях приложения")
                        font.pixelSize: FontSize.caption
                        color: ThemeManager.secondaryTextColor
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                ComboBox {
                    id: dateFormatCombo
                    implicitWidth: 240
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

                    textRole: "label"
                    valueRole: "id"

                    model: [
                        { label: qsTr("ДД.ММ.ГГГГ (31.12.2026)"), id: "dd.MM.yyyy" },
                        { label: qsTr("ГГГГ-ММ-ДД (2026-12-31)"), id: "yyyy-MM-dd" },
                        { label: qsTr("ММ/ДД/ГГГГ (12/31/2026)"), id: "MM/dd/yyyy" }
                    ]

                    currentIndex: {
                        var cur = gen.dateFormat
                        for (var i = 0; i < model.length; i++)
                            if (model[i].id === cur) return i
                        return 0
                    }

                    onActivated: {
                        gen.dateFormat = model[currentIndex].id
                        DateFormatter.setFormat(model[currentIndex].id)
                        if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                            telemetry.trackEvent("settings_date_format_changed", { "format": model[currentIndex].id })
                    }

                    contentItem: Text {
                        leftPadding: 10
                        text: dateFormatCombo.displayText
                        font.pixelSize: 13
                        color: ThemeManager.textColor
                        verticalAlignment: Text.AlignVCenter
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

        // Time format
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 16

                Text {
                    text: qsTr("Формат времени")
                    font.pixelSize: FontSize.bodySmall
                    color: ThemeManager.textColor
                    Layout.fillWidth: true
                }

                RowLayout {
                    spacing: 16

                    RadioButton {
                        text: qsTr("12-часовой")
                        checked: settingsViewModel.timeFormat === 12
                        onClicked: settingsViewModel.updateSetting("timeFormat", 12)
                        contentItem: Text {
                            leftPadding: parent.indicator.width + 6
                            text: parent.text
                            font.pixelSize: 13
                            color: ThemeManager.textColor
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    RadioButton {
                        text: qsTr("24-часовой")
                        checked: settingsViewModel.timeFormat === 24
                        onClicked: settingsViewModel.updateSetting("timeFormat", 24)
                        contentItem: Text {
                            leftPadding: parent.indicator.width + 6
                            text: parent.text
                            font.pixelSize: 13
                            color: ThemeManager.textColor
                            verticalAlignment: Text.AlignVCenter
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

        // Start of week
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 16

                Text {
                    text: qsTr("Начало недели")
                    font.pixelSize: FontSize.bodySmall
                    color: ThemeManager.textColor
                    Layout.fillWidth: true
                }

                ComboBox {
                    id: weekStartCombo
                    model: [qsTr("Понедельник"), qsTr("Вторник"), qsTr("Среда"), qsTr("Четверг"), qsTr("Пятница"), qsTr("Суббота"), qsTr("Воскресенье")]
                    implicitWidth: 160
                    currentIndex: settingsViewModel.weekStart
                    onActivated: settingsViewModel.updateSetting("weekStart", currentIndex)

                    contentItem: Text {
                        leftPadding: 10
                        text: weekStartCombo.displayText
                        font.pixelSize: 13
                        color: ThemeManager.textColor
                        verticalAlignment: Text.AlignVCenter
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

        // Start page
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 16

                Text {
                    text: qsTr("Стартовая страница")
                    font.pixelSize: FontSize.bodySmall
                    color: ThemeManager.textColor
                    Layout.fillWidth: true
                }

                ComboBox {
                    id: startPageCombo
                    property var pageKeys: ["Inbox", "Today", "Upcoming"]
                    model: [qsTr("Входящие"), qsTr("Сегодня"), qsTr("Предстоящие")]
                    implicitWidth: 160
                    currentIndex: {
                        for (var i = 0; i < pageKeys.length; i++) {
                            if (pageKeys[i] === settingsViewModel.startPage) return i
                        }
                        return 0
                    }
                    onActivated: {
                        settingsViewModel.updateSetting("startPage", pageKeys[currentIndex])
                        if (typeof telemetry !== "undefined" && telemetry.enabled)
                            telemetry.trackEvent("settings_start_page_changed", { "page": pageKeys[currentIndex] })
                    }

                    contentItem: Text {
                        leftPadding: 10
                        text: startPageCombo.displayText
                        font.pixelSize: 13
                        color: ThemeManager.textColor
                        verticalAlignment: Text.AlignVCenter
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

        // Next-week day
        // nextWeekDay stores 1–7 (Qt dayOfWeek: 1=Monday … 7=Sunday),
        // so ComboBox index = nextWeekDay - 1, and we save index + 1.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 16

                Text {
                    text: qsTr("День, считающийся следующей неделей")
                    font.pixelSize: FontSize.bodySmall
                    color: ThemeManager.textColor
                    Layout.fillWidth: true
                }

                ComboBox {
                    id: nextWeekDayCombo
                    model: [qsTr("Понедельник"), qsTr("Вторник"), qsTr("Среда"), qsTr("Четверг"), qsTr("Пятница"), qsTr("Суббота"), qsTr("Воскресенье")]
                    implicitWidth: 160
                    // nextWeekDay is 1-based (1=Mon, 7=Sun); convert to 0-based index
                    currentIndex: Math.max(0, settingsViewModel.nextWeekDay - 1)
                    onActivated: {
                        settingsViewModel.updateSetting("nextWeekDay", currentIndex + 1)
                        if (typeof telemetry !== "undefined" && telemetry.enabled)
                            telemetry.trackEvent("settings_next_week_day_changed", { "day": currentIndex + 1 })
                    }

                    contentItem: Text {
                        leftPadding: 10
                        text: nextWeekDayCombo.displayText
                        font.pixelSize: 13
                        color: ThemeManager.textColor
                        verticalAlignment: Text.AlignVCenter
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

        // Auto-archive old projects
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 68
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
                        text: qsTr("Авто-архивирование старых проектов")
                        font.pixelSize: FontSize.bodySmall
                        color: ThemeManager.textColor
                        Layout.fillWidth: true
                    }

                    Text {
                        text: qsTr("Архивировать проекты, в которых все задачи выполнены и нет активности более N дней. 0 = выключено.")
                        font.pixelSize: FontSize.caption
                        color: ThemeManager.secondaryTextColor
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                SpinBox {
                    id: autoArchiveSpin
                    from: 0
                    to: 365
                    stepSize: 1
                    value: settingsViewModel.autoArchiveDays
                    editable: true
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    Layout.preferredWidth: 110
                    onValueModified: {
                        if (settingsViewModel.autoArchiveDays !== value)
                            settingsViewModel.autoArchiveDays = value
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

        // Ежедневный итог в 18:00
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 68
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
                        text: qsTr("Ежедневный итог в 18:00")
                        font.pixelSize: FontSize.bodySmall
                        color: ThemeManager.textColor
                        Layout.fillWidth: true
                    }

                    Text {
                        text: qsTr("Уведомление в 18:00 о незавершённых задачах текущего дня.")
                        font.pixelSize: FontSize.caption
                        color: ThemeManager.secondaryTextColor
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                DesignSwitch {
                    id: dailyReviewSwitch
                    checked: settingsViewModel.dailyReviewEnabled
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    onToggled: {
                        settingsViewModel.dailyReviewEnabled = checked
                        if (typeof telemetry !== "undefined" && telemetry.enabled)
                            telemetry.trackEvent("settings_daily_review_toggled", { "enabled": checked })
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

        // Сортировка задач по умолчанию
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 68
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
                        text: qsTr("Сортировка задач по умолчанию")
                        font.pixelSize: FontSize.bodySmall
                        color: ThemeManager.textColor
                        Layout.fillWidth: true
                    }

                    Text {
                        text: qsTr("Применяется ко всем спискам, кроме календаря и доски.")
                        font.pixelSize: FontSize.caption
                        color: ThemeManager.secondaryTextColor
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                ComboBox {
                    id: defaultSortCombo
                    implicitWidth: 210
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

                    textRole: "label"
                    valueRole: "id"

                    model: [
                        { label: qsTr("Порядок (по умолчанию)"),    id: "order"         },
                        { label: qsTr("По дате — старые сверху"),   id: "createdAt"     },
                        { label: qsTr("По дате — новые сверху"),    id: "createdAtDesc" },
                        { label: qsTr("По приоритету"),              id: "priority"      },
                        { label: qsTr("По сроку"),                   id: "date"          }
                    ]

                    currentIndex: {
                        const cur = taskViewModel.sortBy
                        for (let i = 0; i < model.length; i++)
                            if (model[i].id === cur) return i
                        return 0
                    }

                    onActivated: {
                        const id = model[currentIndex].id
                        taskViewModel.sortBy = id
                        // TODO settingsViewModel: persist taskViewModel.sortBy across sessions via
                        //      a "default_sort" key in DataStore settings.
                        if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                            telemetry.trackEvent("settings_default_sort_changed", { "sort": id })
                    }

                    contentItem: Text {
                        leftPadding: 10
                        text: defaultSortCombo.displayText
                        font.pixelSize: 13
                        color: ThemeManager.textColor
                        verticalAlignment: Text.AlignVCenter
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

        // Обратный порядок сортировки
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 68
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
                        text: qsTr("Обратный порядок сортировки")
                        font.pixelSize: FontSize.bodySmall
                        color: ThemeManager.textColor
                        Layout.fillWidth: true
                    }

                    Text {
                        text: qsTr("Переворачивает текущий порядок задач")
                        font.pixelSize: FontSize.caption
                        color: ThemeManager.secondaryTextColor
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                DesignSwitch {
                    id: sortReversedSwitch
                    checked: taskViewModel.sortReversed
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    onToggled: taskViewModel.sortReversed = checked
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

        // Плотность задач
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 68
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
                        text: qsTr("Плотность задач")
                        font.pixelSize: FontSize.bodySmall
                        color: ThemeManager.textColor
                        Layout.fillWidth: true
                    }

                    Text {
                        text: qsTr("Компактный режим уменьшает высоту строк.")
                        font.pixelSize: FontSize.caption
                        color: ThemeManager.secondaryTextColor
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                RowLayout {
                    spacing: 2
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

                    ToolButton {
                        text: qsTr("Удобный")
                        checked: !settingsViewModel.compactDensity
                        onClicked: {
                            settingsViewModel.compactDensity = false
                            if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                                telemetry.trackEvent("settings_density_changed", { "value": "comfortable" })
                        }
                        Material.foreground: checked ? ThemeManager.accentColor : ThemeManager.secondaryTextColor
                    }

                    ToolButton {
                        text: qsTr("Компактный")
                        checked: settingsViewModel.compactDensity
                        onClicked: {
                            settingsViewModel.compactDensity = true
                            if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                                telemetry.trackEvent("settings_density_changed", { "value": "compact" })
                        }
                        Material.foreground: checked ? ThemeManager.accentColor : ThemeManager.secondaryTextColor
                    }
                }
            }
        }
    }
}
