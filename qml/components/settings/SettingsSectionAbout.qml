import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
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

    // ── "Что нового" changelog dialog ─────────────────────────────────────────
    WhatsNewDialog {
        id: whatsNewDialog
    }

    // ── Reset confirmation dialog ─────────────────────────────────────────────
    Dialog {
        id: resetDialog
        title: "Сбросить настройки"
        anchors.centerIn: Overlay.overlay
        width: 340
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel

        background: Rectangle {
            color: ThemeManager.surfaceColor
            radius: 8
            border.color: ThemeManager.borderColor
            border.width: 1
        }

        ColumnLayout {
            width: parent.width
            spacing: 8

            Label {
                text: "Сбросить все настройки?"
                font.pixelSize: 15
                font.bold: true
                color: ThemeManager.textColor
                Layout.fillWidth: true
            }

            Label {
                text: "Это вернёт все настройки к значениям по умолчанию. Задачи и проекты затронуты не будут."
                wrapMode: Text.WordWrap
                font.pixelSize: 14
                color: ThemeManager.secondaryTextColor
                Layout.fillWidth: true
            }
        }

        onAccepted: settingsViewModel.resetDefaults()
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
                text: "О программе"
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

        // App info card
        Rectangle {
            Layout.fillWidth: true
            Layout.margins: 20
            height: appInfoLayout.implicitHeight + 24
            radius: 8
            color: ThemeManager.hoverColor

            ColumnLayout {
                id: appInfoLayout
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 16
                spacing: 6

                RowLayout {
                    spacing: 10

                    // App icon placeholder
                    Rectangle {
                        width: 36; height: 36; radius: 8
                        color: ThemeManager.accentColor

                        Text {
                            anchors.centerIn: parent
                            text: "✓"
                            color: "white"
                            font.pixelSize: 20
                            font.bold: true
                        }
                    }

                    ColumnLayout {
                        spacing: 2
                        Text {
                            text: "OpenTodo"
                            font.pixelSize: 18
                            font.bold: true
                            color: ThemeManager.textColor
                        }
                        Text {
                            text: "Версия 1.0.0"
                            font.pixelSize: 12
                            color: ThemeManager.secondaryTextColor
                        }
                    }
                }

                Text {
                    text: "Приватный планировщик задач с открытым исходным кодом."
                    font.pixelSize: 14
                    color: ThemeManager.textColor
                    Layout.topMargin: 4
                }

                Text {
                    text: "Создано с использованием Qt 6 · QML · C++20"
                    font.pixelSize: 12
                    color: ThemeManager.secondaryTextColor
                }

                // ── "Что нового..." link ─────────────────────────────────
                // Hyperlink-styled label: accent color + underline on hover.
                Label {
                    id: whatsNewLink
                    text: "✨ Что нового..."
                    font.pixelSize: 13
                    font.underline: whatsNewMouseArea.containsMouse
                    color: ThemeManager.accentColor
                    Layout.topMargin: 6

                    MouseArea {
                        id: whatsNewMouseArea
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        onClicked: whatsNewDialog.open()
                    }
                }
            }
        }

        // Telemetry toggle — B-7.72: hide when telemetry context property is absent
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            height: telemetryRow.implicitHeight + 24
            radius: 8
            color: ThemeManager.hoverColor
            visible: typeof telemetry !== "undefined" && telemetry !== null

            RowLayout {
                id: telemetryRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 16
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Label {
                        text: "Анонимная статистика"
                        font.pixelSize: 14
                        font.bold: true
                        color: ThemeManager.textColor
                    }
                    Label {
                        text: "Помогает улучшать продукт. Без личных данных. Можно отключить в любой момент."
                        font.pixelSize: 12
                        color: ThemeManager.secondaryTextColor
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                Switch {
                    // B-7.72: guard against undefined telemetry context property
                    checked: (typeof telemetry !== "undefined" && telemetry)
                             ? telemetry.enabled : false
                    onToggled: {
                        if (typeof telemetry !== "undefined" && telemetry)
                            telemetry.setEnabled(checked)
                        // No trackEvent here intentionally: if the user just turned
                        // telemetry OFF this event would fire despite their opt-out,
                        // and if they turned it ON we'd be logging before they've
                        // confirmed consent.  The C++ Telemetry class records the
                        // enabled-state change itself.
                    }
                }
            }
        }

        // ── Notification diagnostics card ─────────────────────────────────────
        Rectangle {
            id: notifCardRoot
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            implicitHeight: notifCardLayout.implicitHeight + 24
            radius: 8
            color: ThemeManager.hoverColor

            readonly property bool _hasNotifSvc: typeof notificationService !== "undefined"
                                                 && notificationService !== null
            readonly property bool _granted: _hasNotifSvc && notificationService.hasPermission()

            ColumnLayout {
                id: notifCardLayout
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 16
                spacing: 8

                // Header row: label + status pill
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Text {
                        text: "Уведомления"
                        font.pixelSize: 14
                        font.bold: true
                        color: ThemeManager.textColor
                    }

                    Item { Layout.fillWidth: true }

                    // Status pill
                    Rectangle {
                        implicitHeight: 22
                        implicitWidth: notifStatusLabel.implicitWidth + 16
                        radius: 11
                        color: !notifCardRoot._hasNotifSvc
                            ? Qt.rgba(0.6, 0.6, 0.6, 0.20)
                            : notifCardRoot._granted
                                ? Qt.rgba(0.18, 0.55, 0.31, 0.20)
                                : Qt.rgba(0.86, 0.30, 0.25, 0.20)

                        Text {
                            id: notifStatusLabel
                            anchors.centerIn: parent
                            text: !notifCardRoot._hasNotifSvc
                                ? "Недоступно"
                                : notifCardRoot._granted
                                    ? "Разрешено"
                                    : "Запрещено"
                            font.pixelSize: 12
                            font.bold: true
                            color: !notifCardRoot._hasNotifSvc
                                ? ThemeManager.secondaryTextColor
                                : notifCardRoot._granted
                                    ? "#2e8b57"
                                    : "#db4c3f"
                        }
                    }
                }

                // Hint shown only when permission is denied
                Text {
                    visible: notifCardRoot._hasNotifSvc && !notifCardRoot._granted
                    text: "Чтобы получать напоминания, разрешите уведомления в настройках операционной системы."
                    font.pixelSize: 12
                    color: ThemeManager.secondaryTextColor
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                // Action buttons
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Button {
                        text: "Послать тестовое"
                        enabled: notifCardRoot._hasNotifSvc
                        implicitHeight: 36

                        contentItem: Text {
                            text: parent.text
                            color: parent.enabled ? ThemeManager.accentColor : ThemeManager.secondaryTextColor
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            radius: 4
                            color: parent.hovered ? ThemeManager.hoverColor : "transparent"
                            border.color: parent.enabled ? ThemeManager.accentColor : ThemeManager.borderColor
                            border.width: 1
                        }

                        onClicked: {
                            notificationService.showNotification(
                                "OpenTodo — тест",
                                "Если ты видишь это уведомление, всё работает!",
                                ""
                            )
                            if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                                telemetry.trackEvent("notification_test_sent", {})
                        }
                    }

                    Button {
                        visible: notifCardRoot._hasNotifSvc && !notifCardRoot._granted
                        text: "Запросить разрешение"
                        implicitHeight: 36

                        contentItem: Text {
                            text: parent.text
                            color: ThemeManager.accentColor
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            radius: 4
                            color: parent.hovered ? ThemeManager.hoverColor : "transparent"
                            border.color: ThemeManager.accentColor
                            border.width: 1
                        }

                        onClicked: notificationService.requestPermission()
                    }

                    Item { Layout.fillWidth: true }
                }
            }
        }

        // ── Журнал ошибок ──────────────────────────────────────────────────────
        Rectangle {
            id: logCardRoot
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            implicitHeight: logCardLayout.implicitHeight + 24
            radius: 8
            color: ThemeManager.hoverColor

            readonly property bool _hasLogger: typeof crashLogger !== "undefined"
                                               && crashLogger !== null

            ColumnLayout {
                id: logCardLayout
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 16
                spacing: 8

                // Header
                Text {
                    text: "Журнал ошибок"
                    font.pixelSize: 14
                    font.bold: true
                    color: ThemeManager.textColor
                }

                Text {
                    text: "Сюда пишутся все qWarning / qFatal сообщения. Полезно при отчёте о баге."
                    font.pixelSize: 12
                    color: ThemeManager.secondaryTextColor
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                // File info row
                RowLayout {
                    visible: logCardRoot._hasLogger
                    Layout.fillWidth: true
                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            text: logCardRoot._hasLogger ? crashLogger.logFilePath() : ""
                            font.family: "Consolas"
                            font.pixelSize: 11
                            color: ThemeManager.textColor
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }

                        Text {
                            text: {
                                if (!logCardRoot._hasLogger) return ""
                                const sz = crashLogger.logFileSize()
                                const iso = crashLogger.logFileModifiedIso()
                                const sizeStr = sz < 1024
                                    ? sz + " Б"
                                    : sz < 1048576
                                        ? Math.round(sz / 1024) + " КБ"
                                        : (sz / 1048576).toFixed(1) + " МБ"
                                const dateStr = iso === ""
                                    ? "пусто"
                                    : iso.replace("T", " ").substring(0, 16)
                                return sizeStr + " · обновлён " + dateStr
                            }
                            font.pixelSize: 11
                            color: ThemeManager.secondaryTextColor
                        }
                    }

                    Button {
                        text: "Открыть папку"
                        enabled: logCardRoot._hasLogger
                        implicitHeight: 36

                        contentItem: Text {
                            text: parent.text
                            color: parent.enabled ? ThemeManager.accentColor : ThemeManager.secondaryTextColor
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            radius: 4
                            color: parent.hovered ? Qt.darker(ThemeManager.hoverColor, 1.05) : "transparent"
                            border.color: parent.enabled ? ThemeManager.accentColor : ThemeManager.borderColor
                            border.width: 1
                        }

                        onClicked: crashLogger.openLogFolderInExplorer()
                    }
                }
            }
        }

        // ── Расположение данных ────────────────────────────────────────────────
        Rectangle {
            id: dataPathRoot
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            implicitHeight: dataPathCol.implicitHeight + 32
            color: ThemeManager.hoverColor
            radius: 8

            readonly property string _dataDir: (typeof fileManager !== "undefined" && fileManager)
                                               ? fileManager.dataPath : ""

            ColumnLayout {
                id: dataPathCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 16
                spacing: 8

                Label {
                    text: "Расположение данных"
                    font.pixelSize: 14
                    font.bold: true
                    color: ThemeManager.textColor
                }

                Label {
                    text: "Здесь хранятся state.json, бэкапы, логи и pending-commits для синхронизации."
                    font.pixelSize: 11
                    color: ThemeManager.secondaryTextColor
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Label {
                    text: dataPathRoot._dataDir !== "" ? dataPathRoot._dataDir : "недоступно"
                    font.family: "Consolas"
                    font.pixelSize: 11
                    color: ThemeManager.textColor
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 8

                    Item { Layout.fillWidth: true }

                    Button {
                        text: "Открыть папку"
                        enabled: dataPathRoot._dataDir !== ""
                        implicitHeight: 36

                        contentItem: Text {
                            text: parent.text
                            color: parent.enabled ? ThemeManager.accentColor : ThemeManager.secondaryTextColor
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            radius: 4
                            color: parent.hovered ? Qt.darker(ThemeManager.hoverColor, 1.05) : "transparent"
                            border.color: parent.enabled ? ThemeManager.accentColor : ThemeManager.borderColor
                            border.width: 1
                        }

                        onClicked: {
                            if (typeof crashLogger !== "undefined" && crashLogger)
                                crashLogger.openLogFolderInExplorer()
                            if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                                telemetry.trackEvent("settings_open_data_folder", {})
                        }
                    }
                }
            }
        }

        // ── Экспорт / импорт настроек ─────────────────────────────────────────────
        Rectangle {
            id: settingsExportImportRoot
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            implicitHeight: settingsIeCol.implicitHeight + 32
            color: ThemeManager.hoverColor
            radius: 8

            ColumnLayout {
                id: settingsIeCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 16
                spacing: 8

                Label {
                    text: "Экспорт / импорт настроек"
                    font.pixelSize: 14
                    font.bold: true
                    color: ThemeManager.textColor
                }

                Label {
                    text: "Сохраните настройки в JSON или восстановите из ранее сохранённого файла."
                    font.pixelSize: 11
                    color: ThemeManager.secondaryTextColor
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 8

                    Button {
                        text: "Экспорт"
                        implicitHeight: 36

                        contentItem: Text {
                            text: parent.text
                            color: ThemeManager.accentColor
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            radius: 4
                            color: parent.hovered ? Qt.darker(ThemeManager.hoverColor, 1.05) : "transparent"
                            border.color: ThemeManager.accentColor
                            border.width: 1
                        }

                        onClicked: {
                            const path = dataStore.exportSettings("")
                            const w = root.Window.window
                            if (w && typeof w.showToast === "function") {
                                if (path && path !== "")
                                    w.showToast("Сохранено: " + path, "success")
                                else
                                    w.showToast("Не удалось экспортировать настройки", "error")
                            }
                        }
                    }

                    Button {
                        text: "Импорт..."
                        implicitHeight: 36

                        contentItem: Text {
                            text: parent.text
                            color: ThemeManager.accentColor
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            radius: 4
                            color: parent.hovered ? Qt.darker(ThemeManager.hoverColor, 1.05) : "transparent"
                            border.color: ThemeManager.accentColor
                            border.width: 1
                        }

                        onClicked: importFileDialog.open()
                    }

                    Item { Layout.fillWidth: true }
                }
            }

            // File picker — shown when "Импорт..." is clicked
            FileDialog {
                id: importFileDialog
                title: "Выберите файл настроек"
                nameFilters: ["JSON (*.json)", "Все файлы (*)"]
                onAccepted: {
                    // Convert QUrl to a local file-system path that C++ can open.
                    // Qt gives us a proper QUrl; toString() yields "file:///C:/..."
                    // on Windows or "file:///home/..." on Linux.
                    const raw = selectedFile.toString()
                    const localPath = raw.startsWith("file:///")
                        ? raw.substring(8).replace(/\//g, "\\")  // Windows
                        : raw.replace(/^file:\/\//, "")          // Unix/WASM
                    importConfirmDialog.targetPath = localPath
                    importConfirmDialog.open()
                }
            }

            // Confirmation dialog — shown before overwriting settings
            Dialog {
                id: importConfirmDialog
                title: "Перезаписать настройки?"
                modal: true
                anchors.centerIn: Overlay.overlay
                width: 360
                standardButtons: Dialog.Ok | Dialog.Cancel

                property string targetPath: ""

                background: Rectangle {
                    color: ThemeManager.surfaceColor
                    radius: 8
                    border.color: ThemeManager.borderColor
                    border.width: 1
                }

                contentItem: Label {
                    text: "Текущие настройки будут заменены содержимым файла. Продолжить?"
                    wrapMode: Text.WordWrap
                    color: ThemeManager.textColor
                    width: 320
                }

                onAccepted: {
                    const ok = dataStore.importSettings(importConfirmDialog.targetPath)
                    const w = root.Window.window
                    if (w && typeof w.showToast === "function") {
                        if (ok)
                            w.showToast("Настройки импортированы", "success")
                        else
                            w.showToast("Не удалось импортировать (проверьте формат файла)", "error")
                    }
                }
            }
        }

        // ── Сброс кармы ───────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.preferredHeight: 60
            color: ThemeManager.hoverColor
            radius: 8

            RowLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Label {
                        text: "Сбросить карму"
                        font.pixelSize: 14
                        font.bold: true
                        color: ThemeManager.textColor
                    }

                    Label {
                        text: "Очистить очки, уровень, серии, достижения и историю кармы."
                        font.pixelSize: 11
                        color: ThemeManager.secondaryTextColor
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                Button {
                    text: "Сбросить"
                    flat: true
                    Material.foreground: "#db4c3f"
                    onClicked: resetKarmaDialog.open()
                }
            }

            Dialog {
                id: resetKarmaDialog
                title: "Сбросить карму?"
                modal: true
                anchors.centerIn: Overlay.overlay
                width: 360
                standardButtons: Dialog.Ok | Dialog.Cancel

                background: Rectangle {
                    color: ThemeManager.surfaceColor
                    radius: 8
                    border.color: ThemeManager.borderColor
                    border.width: 1
                }

                contentItem: Label {
                    text: "Это сбросит очки кармы, уровень, ежедневную и недельную серии, а также все достижения и историю. Действие необратимо."
                    wrapMode: Text.WordWrap
                    color: ThemeManager.textColor
                    width: 320
                }

                onAccepted: {
                    karmaViewModel.resetAllKarma()
                    if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                        telemetry.trackEvent("settings_karma_reset", {})
                }
            }
        }

        // Reset all settings button
        Button {
            text: "Сбросить все настройки"
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 16
            implicitHeight: 40
            implicitWidth: 260

            contentItem: Text {
                text: parent.text
                color: ThemeManager.accentColor
                font.pixelSize: 14
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            background: Rectangle {
                radius: 4
                color: parent.hovered ? ThemeManager.hoverColor : "transparent"
                border.color: ThemeManager.accentColor
                border.width: 1
            }

            onClicked: resetDialog.open()
        }
    }
}
