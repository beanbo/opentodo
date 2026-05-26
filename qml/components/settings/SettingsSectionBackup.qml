import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
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

    // ── File dialogs ──────────────────────────────────────────────────────────
    FileDialog {
        id: exportFileDialog
        title: qsTr("Сохранить резервную копию")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("JSON-файлы (*.json)"), qsTr("Все файлы (*)")]
        defaultSuffix: "json"
        // Default to the user's Documents folder. The bare filename
        // "opentodo-backup.json" used to be in `currentFile`, which Qt 6's
        // FileDialog interpreted as a path relative to nothing and rejected
        // with "Cannot set as a selected file because it doesn't exist" on
        // every reopen. Splitting into folder+name avoids that warning.
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        selectedFile: currentFolder + "/opentodo-backup.json"

        onAccepted: {
            // B-7.68: use the FileManager.exportTo Q_INVOKABLE which writes
            // atomically via QSaveFile and accepts file:// URLs natively.
            // On WASM the path is meaningless (no fs) so we keep the data-URL
            // fallback there.
            if (Qt.platform.os === "wasm") {
                var encoded = encodeURIComponent(fileManager.exportData())
                Qt.openUrlExternally("data:application/json;charset=utf-8,"
                                     + encoded)
            } else if (!fileManager.exportTo(selectedFile.toString())) {
                console.warn("Export failed:", selectedFile)
            }
        }
    }

    // ── iCalendar (.ics) Save dialog ─────────────────────────────────────────
    // Saves a VCALENDAR/VTODO file users can import or subscribe to from
    // Apple Calendar, Google Calendar, Outlook, etc.
    FileDialog {
        id: icsExportFileDialog
        title: qsTr("Сохранить как iCalendar")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("iCalendar (*.ics)"), qsTr("Все файлы (*)")]
        defaultSuffix: "ics"
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        selectedFile: currentFolder + "/opentodo-tasks.ics"

        onAccepted: {
            const path = icsExporter.exportToIcs(selectedFile.toString())
            if (path && path !== "" && root.Window.window
                    && typeof root.Window.window.showToast === "function") {
                root.Window.window.showToast(
                    qsTr("Экспортировано в %1").arg(path), "success")
            }
        }
    }

    FileDialog {
        id: importFileDialog
        title: qsTr("Загрузить резервную копию")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("JSON-файлы (*.json)"), qsTr("Все файлы (*)")]

        onAccepted: {
            var xhr = new XMLHttpRequest()
            xhr.open("GET", selectedFile, false)
            xhr.send()
            if (xhr.status === 200 || xhr.status === 0) {
                fileManager.importData(xhr.responseText)
            }
        }
    }

    // ── Todoist ZIP file dialog ───────────────────────────────────────────────
    FileDialog {
        id: todoistZipDialog
        title: qsTr("Выбрать файл резервной копии Todoist (.zip)")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("ZIP-архивы (*.zip)"), qsTr("Все файлы (*)")]

        // ITER1 #35: do NOT start import immediately — open confirmation first.
        onAccepted: zipImportConfirmDialog.open()
    }

    // ITER1 #35: confirmation before destructive import operation
    // Explicit width avoids implicitWidth binding loop with wrapping Label.
    Dialog {
        id: zipImportConfirmDialog
        title: qsTr("Подтвердить импорт")
        modal: true
        anchors.centerIn: parent
        width: 380
        standardButtons: Dialog.Ok | Dialog.Cancel

        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label {
                text: qsTr("Импортировать из этого файла? Существующие данные будут объединены.")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                color: ThemeManager.textColor
                font.pixelSize: 13
            }
        }

        onAccepted: {
            todoistImporter.importFromBackupZip(todoistZipDialog.selectedFile,
                                                importCompletedCheck.checked)
        }
    }

    // ── Todoist API token dialog ──────────────────────────────────────────────
    Dialog {
        id: todoistTokenDialog
        title: qsTr("Импорт через Todoist API")
        modal: true
        width: 420
        anchors.centerIn: parent

        contentItem: ColumnLayout {
            spacing: 12
            anchors.margins: Spacing.md

            Text {
                text: qsTr("Введите API-токен из Todoist:\nНастройки → Интеграции → Разработчикам → API-токен")
                font.pixelSize: 13
                color: ThemeManager.textColor
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            TextField {
                id: tokenField
                placeholderText: qsTr("API-токен Todoist")
                echoMode: TextInput.Password
                Layout.fillWidth: true
                font.pixelSize: 13
                color: ThemeManager.textColor
                background: Rectangle {
                    color: ThemeManager.surfaceColor
                    radius: 4
                    border.color: tokenField.activeFocus
                                  ? ThemeManager.accentColor
                                  : ThemeManager.borderColor
                    border.width: 1
                }
            }

            CheckBox {
                id: importCompletedApiCheck
                text: qsTr("Импортировать завершённые задачи")
                font.pixelSize: 13
                checked: false
                contentItem: Text {
                    text: parent.text
                    font: parent.font
                    color: ThemeManager.textColor
                    leftPadding: parent.indicator.width + parent.spacing
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("Отмена")
                    implicitHeight: 34
                    implicitWidth: 90
                    contentItem: Text {
                        text: parent.text
                        color: ThemeManager.textColor
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 4
                        color: "transparent"
                        border.color: ThemeManager.borderColor
                        border.width: 1
                    }
                    onClicked: todoistTokenDialog.close()
                }

                Button {
                    text: qsTr("Импортировать")
                    implicitHeight: 34
                    implicitWidth: 130
                    enabled: tokenField.text.trim().length > 0
                    contentItem: Text {
                        text: parent.text
                        color: "#ffffff"
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 4
                        color: parent.enabled ? ThemeManager.accentColor : ThemeManager.borderColor
                    }
                    onClicked: {
                        todoistTokenDialog.close()
                        todoistImporter.importFromApiToken(tokenField.text.trim(),
                                                           importCompletedApiCheck.checked)
                        tokenField.clear()
                    }
                }
            }
        }
    }

    // ── Import result dialog ──────────────────────────────────────────────────
    Dialog {
        id: importResultDialog
        title: importResultIsError ? qsTr("Ошибка импорта") : qsTr("Импорт завершён")
        modal: true
        width: 380
        anchors.centerIn: parent

        property bool   importResultIsError: false
        property string importResultText: ""

        contentItem: ColumnLayout {
            spacing: 12
            anchors.margins: Spacing.md

            Text {
                text: importResultDialog.importResultText
                font.pixelSize: 13
                color: importResultDialog.importResultIsError
                       ? "#e53935"
                       : ThemeManager.textColor
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Button {
                text: qsTr("Закрыть")
                Layout.alignment: Qt.AlignRight
                implicitHeight: 34
                implicitWidth: 90
                contentItem: Text {
                    text: parent.text
                    color: ThemeManager.textColor
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 4
                    color: "transparent"
                    border.color: ThemeManager.borderColor
                    border.width: 1
                }
                onClicked: importResultDialog.close()
            }
        }
    }

    // ── Connections for todoistImporter signals ───────────────────────────────
    Connections {
        target: typeof todoistImporter !== "undefined" ? todoistImporter : null

        function onImportProgress(done, total, phase) {
            importProgressBar.value = done / Math.max(1, total)
            importPhaseText.text    = phase
        }

        function onImportFinished(taskCount, projectCount, error) {
            importProgressBar.value = 0
            importPhaseText.text    = ""
            if (error.length > 0) {
                importResultDialog.importResultIsError = true
                importResultDialog.importResultText    = error
            } else {
                importResultDialog.importResultIsError = false
                importResultDialog.importResultText    =
                    qsTr("Импортировано: %1 задач из %2 проектов.").arg(taskCount).arg(projectCount)
            }
            importResultDialog.open()
        }
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
                text: qsTr("Резервная копия")
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

        // ── OpenTodo JSON backup row ──────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 68
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: qsTr("Экспорт / Импорт")
                        font.pixelSize: FontSize.bodySmall
                        color: ThemeManager.textColor
                    }
                    Text {
                        text: qsTr("Сохранить или восстановить все задачи и проекты в формате JSON")
                        font.pixelSize: FontSize.caption
                        color: ThemeManager.secondaryTextColor
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                RowLayout {
                    spacing: 8

                    Button {
                        text: qsTr("Экспорт")
                        implicitHeight: 34
                        implicitWidth: 90

                        contentItem: Text {
                            text: parent.text
                            color: ThemeManager.textColor
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            radius: 4
                            color: "transparent"
                            border.color: ThemeManager.borderColor
                            border.width: 1
                        }

                        onClicked: exportFileDialog.open()
                    }

                    Button {
                        text: qsTr("Импорт")
                        implicitHeight: 34
                        implicitWidth: 90

                        contentItem: Text {
                            text: parent.text
                            color: ThemeManager.textColor
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            radius: 4
                            color: "transparent"
                            border.color: ThemeManager.borderColor
                            border.width: 1
                        }

                        onClicked: importFileDialog.open()
                    }
                }
            }
        }

        // ── Divider ───────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            height: 1
            color: ThemeManager.borderColor
        }

        // ── Todoist import section header ─────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20

                Text {
                    text: qsTr("Импорт из Todoist")
                    font.pixelSize: FontSize.body
                    font.weight: Font.DemiBold
                    color: ThemeManager.textColor
                    Layout.fillWidth: true
                }
            }
        }

        // ── Import completed tasks checkbox ───────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: "transparent"

            CheckBox {
                id: importCompletedCheck
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 20
                text: qsTr("Импортировать завершённые задачи")
                font.pixelSize: 13
                checked: false
                contentItem: Text {
                    text: parent.text
                    font: parent.font
                    color: ThemeManager.secondaryTextColor
                    leftPadding: parent.indicator.width + parent.spacing
                }
            }
        }

        // ── ZIP / API buttons ─────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: qsTr("Из файла бэкапа (.zip)")
                        font.pixelSize: FontSize.bodySmall
                        color: ThemeManager.textColor
                    }
                    Text {
                        text: qsTr("Todoist → Настройки → Резервные копии → Скачать")
                        font.pixelSize: FontSize.caption
                        color: ThemeManager.secondaryTextColor
                    }
                }

                Button {
                    text: qsTr("Выбрать файл")
                    implicitHeight: 34
                    implicitWidth: 120
                    enabled: typeof todoistImporter !== "undefined"
                             && !todoistImporter.busy

                    contentItem: Text {
                        text: parent.text
                        color: parent.enabled ? ThemeManager.textColor
                                              : ThemeManager.secondaryTextColor
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 4
                        color: "transparent"
                        border.color: ThemeManager.borderColor
                        border.width: 1
                    }
                    onClicked: todoistZipDialog.open()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: qsTr("Через API-токен")
                        font.pixelSize: FontSize.bodySmall
                        color: ThemeManager.textColor
                    }
                    Text {
                        text: qsTr("Todoist → Настройки → Интеграции → Разработчикам")
                        font.pixelSize: FontSize.caption
                        color: ThemeManager.secondaryTextColor
                    }
                }

                Button {
                    text: qsTr("Ввести токен")
                    implicitHeight: 34
                    implicitWidth: 120
                    enabled: typeof todoistImporter !== "undefined"
                             && !todoistImporter.busy

                    contentItem: Text {
                        text: parent.text
                        color: parent.enabled ? "#ffffff"
                                              : ThemeManager.secondaryTextColor
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 4
                        color: parent.enabled ? ThemeManager.accentColor
                                              : ThemeManager.borderColor
                    }
                    onClicked: {
                        tokenField.clear()
                        todoistTokenDialog.open()
                    }
                }
            }
        }

        // ── Progress bar (visible during import) ──────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: importProgressBar.visible ? 48 : 0
            color: "transparent"
            visible: typeof todoistImporter !== "undefined" && todoistImporter.busy

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                anchors.topMargin: Spacing.sm
                spacing: 4

                ProgressBar {
                    id: importProgressBar
                    Layout.fillWidth: true
                    from: 0; to: 1; value: 0

                    background: Rectangle {
                        implicitHeight: 6
                        radius: 3
                        color: ThemeManager.borderColor
                    }
                    contentItem: Item {
                        implicitHeight: 6
                        Rectangle {
                            width: importProgressBar.visualPosition * parent.width
                            height: parent.height
                            radius: 3
                            color: ThemeManager.accentColor
                        }
                    }
                }

                Text {
                    id: importPhaseText
                    font.pixelSize: FontSize.caption
                    color: ThemeManager.secondaryTextColor
                }
            }
        }

        // ── Divider ───────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            height: 1
            color: ThemeManager.borderColor
        }

        // ── CSV / Markdown export row ─────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 68
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: qsTr("Экспорт задач")
                        font.pixelSize: FontSize.bodySmall
                        color: ThemeManager.textColor
                    }
                    Text {
                        text: qsTr("Сохранить все задачи в формате CSV или Markdown")
                        font.pixelSize: FontSize.caption
                        color: ThemeManager.secondaryTextColor
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                RowLayout {
                    spacing: 8

                    Button {
                        text: "CSV"
                        implicitHeight: 34
                        implicitWidth: 72

                        contentItem: Text {
                            text: parent.text
                            color: ThemeManager.textColor
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 4
                            color: "transparent"
                            border.color: ThemeManager.borderColor
                            border.width: 1
                        }
                        onClicked: {
                            const path = taskExporter.exportToCsv("")
                            if (path && path !== "")
                                root.Window.window.showToast(
                                    qsTr("Экспортировано в %1").arg(path), "success")
                        }
                    }

                    Button {
                        text: "Markdown"
                        implicitHeight: 34
                        implicitWidth: 90

                        contentItem: Text {
                            text: parent.text
                            color: ThemeManager.textColor
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 4
                            color: "transparent"
                            border.color: ThemeManager.borderColor
                            border.width: 1
                        }
                        onClicked: {
                            const path = taskExporter.exportToMarkdown("")
                            if (path && path !== "")
                                root.Window.window.showToast(
                                    qsTr("Экспортировано в %1").arg(path), "success")
                        }
                    }

                    // iCalendar export — opens a Save-As dialog so users can
                    // pick where the .ics goes (calendar apps usually want
                    // ~/Calendars/ or a Drive/iCloud location to subscribe).
                    Button {
                        text: "iCalendar"
                        implicitHeight: 34
                        implicitWidth: 96

                        contentItem: Text {
                            text: parent.text
                            color: "#ffffff"
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 4
                            color: ThemeManager.accentColor
                        }
                        onClicked: icsExportFileDialog.open()
                    }
                }
            }
        }

        // ── Divider ───────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            height: 1
            color: ThemeManager.borderColor
        }

        // ── История бэкапов ───────────────────────────────────────────────
        ColumnLayout {
            id: backupHistoryCol
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 12
            Layout.bottomMargin: Spacing.xs
            spacing: 0

            // Loaded on expand, refreshed after each restore.
            property var backups: []

            Component.onCompleted: backups = fileManager.listBackups()
            function refresh() { backups = fileManager.listBackups() }

            // Section label
            Text {
                text: qsTr("История бэкапов")
                font.pixelSize: FontSize.body
                font.weight: Font.DemiBold
                color: ThemeManager.textColor
                bottomPadding: 2
            }
            Text {
                text: qsTr("Локальные копии state.json. Сохраняются перед каждым записью данных.")
                font.pixelSize: FontSize.caption
                color: ThemeManager.secondaryTextColor
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                bottomPadding: Spacing.sm
            }

            // Empty state
            Text {
                visible: backupHistoryCol.backups.length === 0
                text: qsTr("Бэкапов пока нет. Они появятся после первого сохранения.")
                font.pixelSize: FontSize.caption
                color: ThemeManager.secondaryTextColor
                Layout.fillWidth: true
                bottomPadding: Spacing.xs
            }

            // One row per backup file
            Repeater {
                model: backupHistoryCol.backups

                delegate: Rectangle {
                    Layout.fillWidth: true
                    height: 52
                    color: "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.topMargin: Spacing.xs
                        anchors.bottomMargin: Spacing.xs
                        spacing: 10

                        // Icon column — Q4.01 (redesign): "📦" заменён на
                        // IconSvg "archive" из stroke-пака.
                        IconSvg {
                            name: "archive"
                            size: 14
                            color: ThemeManager.secondaryTextColor
                            Layout.alignment: Qt.AlignVCenter
                        }

                        // Name + meta column
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            Text {
                                text: modelData.name
                                font.pixelSize: FontSize.bodySmall
                                font.family: "Consolas, monospace"
                                color: ThemeManager.textColor
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                // QML does not climb the parent chain to
                                // resolve free function names from inside a
                                // Repeater delegate. Without the explicit
                                // `backupHistoryCol.` qualifier we got
                                //   "ReferenceError: _formatSize is not defined"
                                // every time the user opened the Backup tab,
                                // and every backup row showed `undefined · ...`.
                                text: backupHistoryCol._formatSize(modelData.size) + " · "
                                      + backupHistoryCol._formatMtime(modelData.mtime)
                                font.pixelSize: FontSize.caption
                                color: ThemeManager.secondaryTextColor
                            }
                        }

                        // Restore button
                        Button {
                            text: qsTr("Восстановить")
                            implicitHeight: 30
                            implicitWidth: 120
                            flat: true

                            contentItem: Text {
                                text: parent.text
                                color: ThemeManager.accentColor
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                radius: 4
                                color: "transparent"
                                border.color: ThemeManager.accentColor
                                border.width: 1
                            }

                            onClicked: {
                                restoreConfirmDialog.targetPath = modelData.path
                                restoreConfirmDialog.targetName = modelData.name
                                restoreConfirmDialog.open()
                            }
                        }
                    }

                    // Row separator (skip after the last item)
                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 1
                        color: ThemeManager.borderColor
                        visible: index < backupHistoryCol.backups.length - 1
                    }
                }
            }

            // Helper: format byte count as B / KB / MB
            function _formatSize(bytes) {
                if (bytes < 1024)        return bytes + " B"
                if (bytes < 1048576)     return Math.round(bytes / 1024) + " KB"
                return (bytes / 1048576).toFixed(1) + " MB"
            }

            // Helper: format QDateTime (arrives as JS Date in QML) as dd.MM.yyyy HH:mm
            function _formatMtime(dt) {
                if (!dt) return ""
                return Qt.formatDateTime(dt, "dd.MM.yyyy HH:mm")
            }
        }

        // ── Restore confirmation dialog ────────────────────────────────────
        Dialog {
            id: restoreConfirmDialog
            title: qsTr("Восстановить бэкап")
            modal: true
            anchors.centerIn: parent
            width: 360
            standardButtons: Dialog.Ok | Dialog.Cancel

            property string targetPath: ""
            property string targetName: ""

            contentItem: ColumnLayout {
                spacing: 8
                anchors.margins: Spacing.xs

                Text {
                    text: qsTr("Восстановить из «%1»?\n\nТекущая state.json будет сохранена в ротацию .bak перед заменой — последующее восстановление позволит откатить это действие.").arg(restoreConfirmDialog.targetName)
                    wrapMode: Text.WordWrap
                    color: ThemeManager.textColor
                    font.pixelSize: 13
                    Layout.fillWidth: true
                    Layout.preferredWidth: 320
                }
            }

            onAccepted: {
                const ok = fileManager.restoreBackup(restoreConfirmDialog.targetPath)
                backupHistoryCol.refresh()
                const w = root.Window.window
                if (w && typeof w.showToast === "function") {
                    if (ok)
                        w.showToast(qsTr("Восстановлено из %1").arg(restoreConfirmDialog.targetName), "success")
                    else
                        w.showToast(qsTr("Не удалось восстановить бэкап"), "error")
                }
            }
        }

        // ── Bottom padding ────────────────────────────────────────────────
        Item { Layout.fillWidth: true; height: 8 }
    }
}
