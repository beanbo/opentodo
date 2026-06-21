import QtQuick
import QtQuick.Controls
import OpenTodoUI

// MSyncScreen — mobile sync configuration (Google Drive / self-hosted PC).
// Restores the per-platform sync setup that the desktop SettingsSectionSync
// provides: on a phone it offers the server-URL + token inputs needed to
// connect to a PC running the OpenTodo sync server; on a PC it shows the token
// + Tailscale-Funnel hint. Wired to settingsViewModel / syncViewModel /
// gdriveClient. Opened from MSettingsScreen via a back-button overlay.
Item {
    id: root
    signal closeRequested()

    // "Phone" = a sync client. True on Android and in the desktop mobile-preview
    // (which simulates the phone). The real desktop uses DesktopLayout, not this.
    readonly property bool _isPhone: Qt.platform.os === "android"
                                     || (typeof forceMobileLayout !== "undefined" && forceMobileLayout)
    readonly property bool _selfHost: settingsViewModel.syncBackend === "selfhost"
    readonly property bool _driveConnected: (typeof gdriveClient !== "undefined") ? gdriveClient.authenticated : false
    // The bearer token is a secret — masked by default, with a reveal toggle so
    // the setup flow (read on PC, type on phone) still works.
    property bool _revealToken: false

    Rectangle { anchors.fill: parent; color: ThemeManager.backgroundColor }

    // ── Header (back + title + status) ─────────────────────────────────────
    Rectangle {
        id: header
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: 56
        color: ThemeManager.backgroundColor
        MIconButton {
            id: backBtn
            anchors.left: parent.left; anchors.leftMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            iconName: "chevron-left"; iconColor: ThemeManager.textColor; iconSize: 24
            onClicked: root.closeRequested()
        }
        Text {
            anchors.left: backBtn.right; anchors.leftMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Синхронизация")
            font.pixelSize: 20; font.weight: Font.Bold; color: ThemeManager.textColor
        }
        Rectangle {
            anchors.right: parent.right; anchors.rightMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            width: 9; height: 9; radius: 5
            color: syncViewModel.isSyncing ? ThemeManager.warningColor
                   : (syncViewModel.lastError !== "" ? ThemeManager.errorColor : ThemeManager.successColor)
        }
    }

    Flickable {
        anchors.top: header.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        contentHeight: col.implicitHeight + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: col
            width: parent.width
            spacing: 0

            // ── Backend selector ───────────────────────────────────────────
            MSettingsSectionLabel { text: qsTr("Способ синхронизации") }
            Item {
                width: parent.width; height: 56
                MSeg {
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.leftMargin: 16; anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    value: settingsViewModel.syncBackend
                    options: [
                        { "value": "drive",    "label": qsTr("Google Drive"), "icon": "sync" },
                        { "value": "selfhost", "label": qsTr("Свой ПК"),       "icon": "folder" }
                    ]
                    onChanged: function(v) { settingsViewModel.syncBackend = v }
                }
            }
            Text {
                width: parent.width - 40; x: 20; bottomPadding: 8
                wrapMode: Text.WordWrap
                text: qsTr("После смены способа синхронизации перезапустите приложение.")
                font.pixelSize: 12; color: ThemeManager.secondaryTextColor
            }

            // ── Self-hosted PC card ────────────────────────────────────────
            Rectangle {
                visible: root._selfHost
                width: parent.width - 24; x: 12
                height: shCol.implicitHeight + 28
                radius: 14
                color: ThemeManager.surfaceColor
                border.width: 1; border.color: ThemeManager.accentColor
                Column {
                    id: shCol
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: 14
                    spacing: 10

                    Text {
                        text: root._isPhone ? qsTr("Подключение к ПК") : qsTr("Этот ПК — сервер синхронизации")
                        font.pixelSize: 15; font.weight: Font.Bold; color: ThemeManager.textColor
                    }

                    // PHONE: enter the PC's URL + token.
                    Column {
                        visible: root._isPhone
                        width: parent.width
                        spacing: 8
                        Text { text: qsTr("Адрес сервера (с ПК)"); font.pixelSize: 12; color: ThemeManager.secondaryTextColor }
                        Rectangle {
                            width: parent.width; height: 46; radius: 12
                            color: ThemeManager.backgroundColor; border.width: 1; border.color: ThemeManager.borderColor
                            TextField {
                                id: urlField
                                anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14
                                verticalAlignment: Text.AlignVCenter
                                text: settingsViewModel.syncServerUrl
                                placeholderText: "https://my-pc.tail0000.ts.net"
                                placeholderTextColor: ThemeManager.faintTextColor
                                font.pixelSize: 14; color: ThemeManager.textColor
                                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                                background: Item {}
                                onEditingFinished: settingsViewModel.syncServerUrl = text.trim()
                            }
                        }
                        Row {
                            width: parent.width
                            Text { text: qsTr("Токен (с ПК)"); font.pixelSize: 12; color: ThemeManager.secondaryTextColor; width: parent.width - revealPhone.implicitWidth; anchors.verticalCenter: parent.verticalCenter }
                            Text {
                                id: revealPhone
                                text: root._revealToken ? qsTr("Скрыть") : qsTr("Показать")
                                font.pixelSize: 12; font.weight: Font.DemiBold; color: ThemeManager.accentColor
                                anchors.verticalCenter: parent.verticalCenter
                                MouseArea { anchors.fill: parent; onClicked: root._revealToken = !root._revealToken }
                            }
                        }
                        Rectangle {
                            width: parent.width; height: 46; radius: 12
                            color: ThemeManager.backgroundColor; border.width: 1; border.color: ThemeManager.borderColor
                            TextField {
                                id: tokenField
                                anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14
                                verticalAlignment: Text.AlignVCenter
                                echoMode: root._revealToken ? TextInput.Normal : TextInput.Password
                                text: settingsViewModel.syncToken
                                placeholderText: qsTr("вставьте токен с ПК")
                                placeholderTextColor: ThemeManager.faintTextColor
                                font.pixelSize: 14; color: ThemeManager.textColor
                                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhSensitiveData
                                background: Item {}
                                onEditingFinished: settingsViewModel.syncToken = text.trim()
                            }
                        }
                        Text {
                            width: parent.width; wrapMode: Text.WordWrap
                            text: qsTr("Сервер на ПК слушает только локально, поэтому нужен Tailscale: на ПК один раз выполните «tailscale funnel 8765» и вставьте сюда выданный адрес (https://…ts.net).")
                            font.pixelSize: 11; color: ThemeManager.faintTextColor
                        }
                    }

                    // PC: show token + generate + Funnel hint.
                    Column {
                        visible: !root._isPhone
                        width: parent.width
                        spacing: 8
                        Row {
                            width: parent.width
                            Text { text: qsTr("Токен (введите его на телефоне)"); font.pixelSize: 12; color: ThemeManager.secondaryTextColor; width: parent.width - revealPc.implicitWidth; anchors.verticalCenter: parent.verticalCenter }
                            Text {
                                id: revealPc
                                text: root._revealToken ? qsTr("Скрыть") : qsTr("Показать")
                                font.pixelSize: 12; font.weight: Font.DemiBold; color: ThemeManager.accentColor
                                anchors.verticalCenter: parent.verticalCenter
                                MouseArea { anchors.fill: parent; onClicked: root._revealToken = !root._revealToken }
                            }
                        }
                        Rectangle {
                            width: parent.width; height: 46; radius: 12
                            color: ThemeManager.backgroundColor; border.width: 1; border.color: ThemeManager.borderColor
                            TextField {
                                anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14
                                verticalAlignment: Text.AlignVCenter
                                readOnly: true; selectByMouse: true
                                echoMode: root._revealToken ? TextInput.Normal : TextInput.Password
                                text: settingsViewModel.syncToken
                                font.pixelSize: 13; color: ThemeManager.textColor; font.family: "monospace"
                                background: Item {}
                            }
                        }
                        Row {
                            spacing: 10
                            Rectangle {
                                width: genTxt.implicitWidth + 28; height: 38; radius: 10
                                color: ThemeManager.accentContainer
                                Text { id: genTxt; anchors.centerIn: parent; text: qsTr("Новый токен"); color: ThemeManager.accentColor; font.pixelSize: 13; font.weight: Font.Bold }
                                MouseArea { anchors.fill: parent; onClicked: settingsViewModel.generateSyncToken() }
                            }
                        }
                        Text {
                            width: parent.width; wrapMode: Text.WordWrap
                            text: qsTr("Один раз выполните в терминале: tailscale funnel 8765")
                            font.pixelSize: 11; color: ThemeManager.faintTextColor
                        }

                        // LAN / Wi-Fi opt-in: bind the server to the local network
                        // so the phone can sync over Wi-Fi without Tailscale.
                        Rectangle { width: parent.width; height: 1; color: ThemeManager.hairlineColor }
                        Row {
                            width: parent.width
                            spacing: 10
                            Column {
                                width: parent.width - lanSwitch.width - 10
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2
                                Text { text: qsTr("Доступ по Wi-Fi (локальная сеть)"); font.pixelSize: 13; font.weight: Font.DemiBold; color: ThemeManager.textColor }
                                Text { width: parent.width; wrapMode: Text.WordWrap; text: qsTr("Без Tailscale — телефон должен быть в той же сети Wi-Fi"); font.pixelSize: 11; color: ThemeManager.faintTextColor }
                            }
                            MSwitch {
                                id: lanSwitch
                                anchors.verticalCenter: parent.verticalCenter
                                checked: settingsViewModel.syncLanEnabled
                                onToggled: settingsViewModel.syncLanEnabled = !settingsViewModel.syncLanEnabled
                            }
                        }
                        // LAN address to enter on the phone (only when enabled).
                        Column {
                            visible: settingsViewModel.syncLanEnabled
                            width: parent.width
                            spacing: 6
                            Text { text: qsTr("Адрес в локальной сети (введите на телефоне)"); font.pixelSize: 12; color: ThemeManager.secondaryTextColor }
                            Rectangle {
                                width: parent.width; height: 46; radius: 12
                                color: ThemeManager.backgroundColor; border.width: 1; border.color: ThemeManager.borderColor
                                TextField {
                                    anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14
                                    verticalAlignment: Text.AlignVCenter
                                    readOnly: true; selectByMouse: true
                                    text: settingsViewModel.localSyncUrl()
                                    placeholderText: qsTr("адрес не найден")
                                    placeholderTextColor: ThemeManager.faintTextColor
                                    font.pixelSize: 13; color: ThemeManager.textColor; font.family: "monospace"
                                    background: Item {}
                                }
                            }
                            Text {
                                width: parent.width; wrapMode: Text.WordWrap
                                text: qsTr("Включите, затем перезапустите приложение на ПК. На телефоне выберите «Свой ПК» и впишите этот адрес и токен.")
                                font.pixelSize: 11; color: ThemeManager.faintTextColor
                            }
                        }
                    }
                }
            }

            // ── Google Drive card ──────────────────────────────────────────
            Rectangle {
                visible: !root._selfHost
                width: parent.width - 24; x: 12
                height: 76; radius: 14
                color: ThemeManager.surfaceColor
                border.width: 1; border.color: root._driveConnected ? ThemeManager.accentColor : ThemeManager.borderColor
                Rectangle {
                    id: gIcon
                    anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter
                    width: 40; height: 40; radius: 10; color: "#4285f4"
                    Text { anchors.centerIn: parent; text: "G"; color: "white"; font.pixelSize: 20; font.bold: true }
                }
                Column {
                    anchors.left: gIcon.right; anchors.leftMargin: 14; anchors.right: gBtn.left; anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter; spacing: 2
                    Text { text: root._driveConnected ? qsTr("Подключено к Google Drive") : qsTr("Google Drive"); font.pixelSize: 14; font.weight: root._driveConnected ? Font.Bold : Font.Medium; color: ThemeManager.textColor }
                    Text { width: parent.width; text: root._driveConnected ? ((typeof gdriveClient !== "undefined") ? gdriveClient.accountEmail : "") : qsTr("Синхронизация между устройствами"); font.pixelSize: 12; color: ThemeManager.secondaryTextColor; elide: Text.ElideRight }
                }
                Rectangle {
                    id: gBtn
                    anchors.right: parent.right; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter
                    width: 110; height: 34; radius: 8
                    color: root._driveConnected ? ThemeManager.accentColor : "#4285f4"
                    Text { anchors.centerIn: parent; text: root._driveConnected ? qsTr("Отключить") : qsTr("Подключить"); color: root._driveConnected ? ThemeManager.accentForeground : "white"; font.pixelSize: 13; font.weight: Font.Bold }
                    MouseArea { anchors.fill: parent; enabled: typeof gdriveClient !== "undefined"; onClicked: { if (root._driveConnected) gdriveClient.signOut(); else gdriveClient.authenticate() } }
                }
            }

            // ── Sync mode ──────────────────────────────────────────────────
            MSettingsSectionLabel { text: qsTr("Когда синхронизировать") }
            Item {
                width: parent.width; height: 56
                MSeg {
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.leftMargin: 16; anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    value: settingsViewModel.syncMode
                    options: [
                        { "value": "after_action", "label": qsTr("Авто") },
                        { "value": "interval",     "label": qsTr("По времени") },
                        { "value": "manual",        "label": qsTr("Вручную") }
                    ]
                    onChanged: function(v) { settingsViewModel.syncMode = v }
                }
            }
            // Interval chips (interval mode only).
            Flow {
                visible: settingsViewModel.syncMode === "interval"
                width: parent.width - 32; x: 16; bottomPadding: 8; spacing: 9
                Repeater {
                    model: [ {l: qsTr("5 мин"), s: 300}, {l: qsTr("15 мин"), s: 900}, {l: qsTr("30 мин"), s: 1800}, {l: qsTr("60 мин"), s: 3600} ]
                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool on: settingsViewModel.syncInterval === modelData.s
                        height: 36; radius: 18; width: ivTxt.implicitWidth + 26
                        color: on ? ThemeManager.accentContainer : "transparent"
                        border.width: on ? 0 : 1; border.color: ThemeManager.borderColor
                        Text { id: ivTxt; anchors.centerIn: parent; text: modelData.l; font.pixelSize: 13; font.weight: Font.DemiBold; color: on ? ThemeManager.accentColor : ThemeManager.secondaryTextColor }
                        MouseArea { anchors.fill: parent; onClicked: settingsViewModel.syncInterval = modelData.s }
                    }
                }
            }

            // ── Status + actions ───────────────────────────────────────────
            Item { width: parent.width; height: 12 }
            Text {
                width: parent.width - 40; x: 20
                text: qsTr("Последняя синхронизация: %1").arg(syncViewModel.formatLastSyncTime())
                font.pixelSize: 12; color: ThemeManager.secondaryTextColor; elide: Text.ElideRight
            }
            Item {
                width: parent.width; height: 70
                Row {
                    anchors.centerIn: parent
                    spacing: 12
                    Rectangle {
                        width: 150; height: 46; radius: 14
                        color: "transparent"; border.width: 1; border.color: ThemeManager.borderColor
                        Text { anchors.centerIn: parent; text: qsTr("Скачать"); color: ThemeManager.textColor; font.pixelSize: 14; font.weight: Font.DemiBold }
                        MouseArea { anchors.fill: parent; enabled: !syncViewModel.isSyncing; onClicked: syncViewModel.pull() }
                    }
                    Rectangle {
                        width: 150; height: 46; radius: 14
                        color: syncViewModel.isSyncing ? ThemeManager.hoverColor : ThemeManager.accentColor
                        Text { anchors.centerIn: parent; text: syncViewModel.isSyncing ? qsTr("Синхронизация…") : qsTr("Синхронизировать"); color: ThemeManager.accentForeground; font.pixelSize: 14; font.weight: Font.Bold }
                        MouseArea { anchors.fill: parent; enabled: !syncViewModel.isSyncing; onClicked: syncViewModel.sync() }
                    }
                }
            }

            // Pending / error banners.
            Rectangle {
                width: parent.width - 24; x: 12; radius: 10
                height: syncViewModel.pendingCommits > 0 ? 40 : 0
                clip: true
                color: ThemeManager.isDark ? "#2d2800" : "#fffce6"
                Behavior on height { NumberAnimation { duration: 160 } }
                Text { anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter; text: qsTr("Несохранённых изменений: %1").arg(syncViewModel.pendingCommits); font.pixelSize: 12; color: ThemeManager.warningColor }
            }
            Item { width: 1; height: 6 }
            Rectangle {
                width: parent.width - 24; x: 12; radius: 10
                height: syncViewModel.lastError !== "" ? errTxt.implicitHeight + 20 : 0
                clip: true
                color: ThemeManager.isDark ? "#2d1010" : "#fff0ef"
                Behavior on height { NumberAnimation { duration: 160 } }
                Text { id: errTxt; anchors.left: parent.left; anchors.leftMargin: 14; anchors.right: parent.right; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter; text: syncViewModel.lastError; wrapMode: Text.WordWrap; font.pixelSize: 12; color: ThemeManager.errorColor }
            }
        }
    }
}
