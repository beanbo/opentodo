import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
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

    // ── gdriveClient convenience aliases (guarded for test/older builds) ───────
    readonly property bool driveConnected:
        (typeof gdriveClient !== "undefined") ? gdriveClient.authenticated : false
    readonly property string driveEmail:
        (typeof gdriveClient !== "undefined") ? gdriveClient.accountEmail : ""

    Behavior on implicitHeight {
        NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
    }

    // ── Disconnect confirmation dialog ────────────────────────────────────────
    Dialog {
        id: disconnectDriveDialog
        title: qsTr("Отключить Google Drive")
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

        Label {
            width: parent.width
            text: qsTr("Вы уверены, что хотите отключить Google Drive? Локальные данные останутся без изменений.")
            wrapMode: Text.WordWrap
            font.pixelSize: 14
            color: ThemeManager.textColor
        }

        onAccepted: {
            if (typeof gdriveClient !== "undefined")
                gdriveClient.signOut()
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
                text: qsTr("Синхронизация")
                font.pixelSize: FontSize.body
                font.weight: Font.DemiBold
                color: ThemeManager.textColor
                Layout.fillWidth: true
            }

            // Sync status indicator dot. Q4.01 night iter-25: hex
            // colors → semantic tokens so dot colors track theme.
            Rectangle {
                width: 8
                height: 8
                radius: 4
                color: {
                    if (syncViewModel.isSyncing) return ThemeManager.warningColor
                    if (syncViewModel.lastError !== "") return ThemeManager.errorColor
                    return ThemeManager.successColor
                }

                SequentialAnimation on opacity {
                    running: syncViewModel.isSyncing
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.3; duration: 600 }
                    NumberAnimation { to: 1.0; duration: 600 }
                }
            }

            // Q4.01 (redesign): chevron unicode → stroke IconSvg
            IconSvg {
                name: root.expanded ? "chevron-down" : "chevron-right"
                size: 14
                color: ThemeManager.secondaryTextColor
                Layout.leftMargin: Spacing.sm
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

        // ── Backend selector: Google Drive vs self-hosted PC ──────────────────
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
                        text: qsTr("Способ синхронизации")
                        font.pixelSize: FontSize.bodySmall
                        color: ThemeManager.textColor
                    }
                    Text {
                        text: qsTr("Google Drive или свой ПК (через Tailscale Funnel)")
                        font.pixelSize: FontSize.caption
                        color: ThemeManager.secondaryTextColor
                    }
                }

                DesignSelect {
                    id: backendCombo
                    implicitWidth: 200
                    Layout.preferredWidth: 200
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

                    model: [ qsTr("Google Drive"), qsTr("Свой ПК") ]
                    readonly property var _vals: ["drive", "selfhost"]
                    function _idx(b) { return b === "selfhost" ? 1 : 0 }

                    Component.onCompleted: currentIndex = _idx(settingsViewModel.syncBackend)
                    Connections {
                        target: settingsViewModel
                        function onSyncBackendChanged() {
                            backendCombo.currentIndex = backendCombo._idx(settingsViewModel.syncBackend)
                        }
                    }
                    onActivated: settingsViewModel.syncBackend = _vals[currentIndex]
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

        // Restart reminder (the remote is wired once at app start).
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 8
            text: qsTr("После смены способа синхронизации перезапустите приложение.")
            wrapMode: Text.WordWrap
            font.pixelSize: FontSize.caption
            color: ThemeManager.secondaryTextColor
        }

        // ── Self-hosted PC card (visible only for the self-hosted backend) ────
        Rectangle {
            id: selfHostCard
            Layout.fillWidth: true
            Layout.margins: 16
            visible: settingsViewModel.syncBackend === "selfhost"
            radius: 8
            color: ThemeManager.hoverColor
            border.color: ThemeManager.accentColor
            border.width: 1
            implicitHeight: selfHostCol.implicitHeight + 28

            readonly property bool isPhone: Qt.platform.os === "android"

            ColumnLayout {
                id: selfHostCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 14
                spacing: 8

                Text {
                    text: selfHostCard.isPhone
                          ? qsTr("Подключение к ПК")
                          : qsTr("Этот ПК — сервер синхронизации")
                    font.pixelSize: 14
                    font.bold: true
                    color: ThemeManager.textColor
                }

                // ── PHONE / client: enter the PC's URL + token ────────────────
                ColumnLayout {
                    visible: selfHostCard.isPhone
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        text: qsTr("Адрес сервера (с ПК):")
                        font.pixelSize: 12
                        color: ThemeManager.secondaryTextColor
                    }
                    TextField {
                        Layout.fillWidth: true
                        text: settingsViewModel.syncServerUrl
                        placeholderText: "https://my-pc.tail0000.ts.net"
                        color: ThemeManager.textColor
                        onEditingFinished: settingsViewModel.syncServerUrl = text
                    }
                    Text {
                        text: qsTr("Токен (с ПК):")
                        font.pixelSize: 12
                        color: ThemeManager.secondaryTextColor
                    }
                    TextField {
                        Layout.fillWidth: true
                        text: settingsViewModel.syncToken
                        placeholderText: qsTr("вставьте токен с ПК")
                        color: ThemeManager.textColor
                        onEditingFinished: settingsViewModel.syncToken = text
                    }
                }

                // ── PC / server: show token + Funnel instructions ─────────────
                ColumnLayout {
                    visible: !selfHostCard.isPhone
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Адрес для телефона: https://my-pc.tail0000.ts.net")
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                        color: ThemeManager.textColor
                    }
                    Text {
                        text: qsTr("Токен (введите его на телефоне):")
                        font.pixelSize: 12
                        color: ThemeManager.secondaryTextColor
                    }
                    TextField {
                        Layout.fillWidth: true
                        readOnly: true
                        selectByMouse: true
                        text: settingsViewModel.syncToken
                        color: ThemeManager.textColor
                        font.family: "monospace"
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Button {
                            text: qsTr("Новый токен")
                            onClicked: settingsViewModel.generateSyncToken()
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Один раз выполните в терминале: tailscale funnel 8765")
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                            color: ThemeManager.secondaryTextColor
                        }
                    }
                }
            }
        }

        // OAuth setup notice
        Rectangle {
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.bottomMargin: 0
            visible: settingsViewModel.syncBackend === "drive"
            radius: 8
            color: ThemeManager.surfaceColor
            border.color: ThemeManager.accentColor
            border.width: 1
            implicitHeight: oauthNoticeRow.implicitHeight + 24

            RowLayout {
                id: oauthNoticeRow
                anchors.fill: parent
                anchors.margins: 12
                spacing: 12

                Text {
                    text: "⚠"
                    font.pixelSize: 20
                    color: ThemeManager.accentColor
                    Layout.alignment: Qt.AlignTop
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("При нажатии «Подключить» откроется браузер с экраном входа Google. После согласия токены сохраняются локально и используются для последующей синхронизации.")
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    color: ThemeManager.textColor
                }
            }
        }

        // Google Drive card
        Rectangle {
            Layout.fillWidth: true
            Layout.margins: 16
            height: 72
            visible: settingsViewModel.syncBackend === "drive"
            radius: 8
            color: ThemeManager.hoverColor
            border.color: root.driveConnected ? "#4285f4" : ThemeManager.borderColor
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 14

                // Google Drive icon
                Rectangle {
                    width: 40; height: 40; radius: 8
                    color: "#4285f4"

                    Text {
                        anchors.centerIn: parent
                        text: "G"
                        color: "white"
                        font.pixelSize: 20
                        font.bold: true
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: root.driveConnected
                              ? qsTr("Подключено к Google Drive")
                              : qsTr("Google Drive")
                        font.pixelSize: 14
                        font.bold: root.driveConnected
                        color: ThemeManager.textColor
                    }

                    Text {
                        text: root.driveConnected
                              ? root.driveEmail
                              : qsTr("Синхронизация ваших данных между устройствами")
                        font.pixelSize: 12
                        color: ThemeManager.secondaryTextColor
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                Button {
                    text: root.driveConnected ? qsTr("Отключить") : qsTr("Подключить")
                    enabled: typeof gdriveClient !== "undefined"
                    implicitHeight: 34
                    implicitWidth: 110

                    contentItem: Text {
                        text: parent.text
                        // Connected → accent bg (theme-aware fg); Disconnected → Google blue bg (always white).
                        color: root.driveConnected ? ThemeManager.accentForeground : "white"
                        font.pixelSize: 13
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 4
                        color: root.driveConnected
                               ? ThemeManager.accentColor
                               : "#4285f4"
                    }

                    onClicked: {
                        if (root.driveConnected) {
                            disconnectDriveDialog.open()
                        } else {
                            gdriveClient.authenticate()
                        }
                    }
                }
            }
        }

        // Unavailable hint placeholder (kept for layout compatibility)
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: -8
            Layout.bottomMargin: Spacing.sm
            text: ""
            font.pixelSize: FontSize.caption
            color: ThemeManager.secondaryTextColor
        }

        // ── Sync mode picker ─────────────────────────────────────────────────────
        // "after_action" | "interval" | "manual"
        Rectangle {
            id: syncModeContainer
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
                        text: qsTr("Когда выполнять синхронизацию")
                        font.pixelSize: FontSize.bodySmall
                        color: ThemeManager.textColor
                    }
                    Text {
                        text: qsTr("Режим автоматической синхронизации")
                        font.pixelSize: FontSize.caption
                        color: ThemeManager.secondaryTextColor
                    }
                }

                DesignSelect {
                    id: syncModeCombo
                    implicitWidth: 200
                    Layout.preferredWidth: 200
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

                    // Order must match _modeValues below
                    model: [
                        qsTr("После каждого действия"),
                        qsTr("Раз в N минут"),
                        qsTr("Только вручную")
                    ]

                    readonly property var _modeValues: ["after_action", "interval", "manual"]

                    function _indexForMode(m) {
                        for (var i = 0; i < _modeValues.length; ++i)
                            if (_modeValues[i] === m) return i
                        return 0  // default: after_action
                    }

                    Component.onCompleted: {
                        currentIndex = _indexForMode(settingsViewModel.syncMode)
                    }

                    Connections {
                        target: settingsViewModel
                        function onSyncModeChanged() {
                            syncModeCombo.currentIndex =
                                syncModeCombo._indexForMode(settingsViewModel.syncMode)
                        }
                    }

                    onActivated: {
                        settingsViewModel.syncMode = _modeValues[currentIndex]
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

        // ITER1 #11: Sync interval selector — named options (never / 5 / 15 / 30 / 60 min)
        // ITER3 #8: give this Rectangle an id so the Connections block below can reference
        // _indexForSeconds() directly instead of using the fragile parent.parent chain.
        // Visible only when syncMode == "interval".
        Rectangle {
            id: syncIntervalContainer
            Layout.fillWidth: true
            Layout.preferredHeight: syncIntervalContainer.visible ? 52 : 0
            visible: settingsViewModel.syncMode === "interval"
            color: "transparent"

            // Map index → seconds: 0=never, 1=5 min, 2=15 min, 3=30 min, 4=60 min
            readonly property var _intervalSeconds: [0, 300, 900, 1800, 3600]

            // Resolve the stored seconds value back to an index (fallback: index 2 = 15 min)
            function _indexForSeconds(s) {
                for (var i = 0; i < _intervalSeconds.length; ++i)
                    if (_intervalSeconds[i] === s) return i
                return 2   // default: 15 minutes
            }

            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: 160 }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 16

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: qsTr("Интервал синхронизации")
                        font.pixelSize: FontSize.bodySmall
                        color: ThemeManager.textColor
                    }
                    Text {
                        text: qsTr("Как часто выполнять автоматическую синхронизацию")
                        font.pixelSize: FontSize.caption
                        color: ThemeManager.secondaryTextColor
                    }
                }

                DesignSelect {
                    id: syncIntervalCombo
                    implicitWidth: 150
                    Layout.preferredWidth: 150
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

                    model: [qsTr("Никогда"), qsTr("5 минут"), qsTr("15 минут"), qsTr("30 минут"), qsTr("60 минут")]

                    // ITER2 #11: replace the fragile `currentIndex: parent.parent._indexForSeconds(...)`
                    // binding (which fights with onActivated's imperative set) with a
                    // one-time Component.onCompleted assignment + a Connections block
                    // that syncs when settingsViewModel.syncInterval changes externally.
                    // ITER3 #8: use syncIntervalContainer id instead of parent.parent
                    Component.onCompleted: {
                        currentIndex = syncIntervalContainer._indexForSeconds(settingsViewModel.syncInterval)
                    }

                    // ITER3 #8: reference syncIntervalContainer by id instead of
                    // the fragile parent.parent traversal that breaks on layout refactors.
                    Connections {
                        target: settingsViewModel
                        function onSyncIntervalChanged() {
                            syncIntervalCombo.currentIndex =
                                syncIntervalContainer._indexForSeconds(
                                    settingsViewModel.syncInterval)
                        }
                    }

                    onActivated: {
                        // ITER3 #8: use syncIntervalContainer id instead of parent.parent
                        const secs = syncIntervalContainer._intervalSeconds[currentIndex]
                        settingsViewModel.syncInterval = secs
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
                visible: syncIntervalContainer.visible
            }
        }

        // Last sync row
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 8

                Text {
                    text: qsTr("Последняя синхронизация: %1").arg(syncViewModel.formatLastSyncTime())
                    font.pixelSize: 12
                    color: ThemeManager.secondaryTextColor
                    Layout.fillWidth: true
                }

                // «Скачать с Drive» — explicit pull without pushing local changes
                Button {
                    text: qsTr("Скачать с Drive")
                    enabled: !syncViewModel.isSyncing
                    implicitHeight: 30
                    implicitWidth: 130
                    flat: true

                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("Перезагрузить состояние с Google Drive (без отправки локальных изменений)")

                    contentItem: Text {
                        text: parent.text
                        color: parent.enabled ? ThemeManager.textColor : ThemeManager.secondaryTextColor
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 4
                        color: parent.hovered && parent.enabled
                               ? ThemeManager.hoverColor
                               : "transparent"
                        border.color: parent.enabled ? ThemeManager.borderColor : ThemeManager.borderColor
                        border.width: 1
                    }

                    onClicked: syncViewModel.pull()
                }

                Button {
                    text: syncViewModel.isSyncing ? qsTr("Синхронизация…") : qsTr("Синхронизировать")
                    enabled: !syncViewModel.isSyncing
                    implicitHeight: 30
                    implicitWidth: 140
                    flat: true

                    contentItem: Text {
                        text: parent.text
                        color: parent.enabled ? ThemeManager.accentColor : ThemeManager.secondaryTextColor
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 4
                        color: parent.hovered && parent.enabled
                               ? ThemeManager.hoverColor
                               : "transparent"
                        border.color: parent.enabled ? ThemeManager.accentColor : ThemeManager.borderColor
                        border.width: 1
                    }

                    onClicked: syncViewModel.sync()
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

        // Unavailable hint under sync button (kept for layout compatibility)
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: Spacing.xs
            Layout.bottomMargin: Spacing.xs
            text: ""
            font.pixelSize: FontSize.caption
            color: ThemeManager.secondaryTextColor
        }

        // Pending changes banner
        Rectangle {
            Layout.fillWidth: true
            // B-7.71: animate `height` not `implicitHeight` — this Rectangle's
            // visible size is controlled via `height` (explicit binding), so the
            // Behavior must target the same property. Animating implicitHeight
            // has no effect when height is set explicitly.
            height: syncViewModel.pendingCommits > 0 ? 36 : 0
            color: ThemeManager.isDark ? "#2d2800" : "#fffce6"
            clip: true

            Behavior on height {
                NumberAnimation { duration: 160 }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 8

                Rectangle {
                    width: 8; height: 8; radius: 4
                    color: ThemeManager.warningColor
                }

                Text {
                    text: qsTr("%1 несохранённых изменений").arg(syncViewModel.pendingCommits)
                    font.pixelSize: 12
                    color: ThemeManager.isDark ? "#f0c040" : "#7a5800"
                    Layout.fillWidth: true
                }

                ToolButton {
                    text: qsTr("Отправить")
                    enabled: true
                    font.pixelSize: 12
                    contentItem: Text {
                        text: parent.text
                        font: parent.font
                        color: parent.enabled ? "#f49c18" : ThemeManager.secondaryTextColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle { color: "transparent" }
                    onClicked: syncViewModel.push()
                }
            }
        }

        // Last error banner
        Rectangle {
            Layout.fillWidth: true
            height: syncViewModel.lastError !== "" ? 36 : 0
            color: ThemeManager.isDark ? "#2d1010" : "#fff0ef"
            clip: true

            // ITER3 #18: animate `height` (the property actually being changed by the
            // binding above), not `implicitHeight` which is never written to here.
            Behavior on height {
                NumberAnimation { duration: 160 }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 8

                Text {
                    text: "⚠"
                    font.pixelSize: 14
                    color: ThemeManager.accentColor
                }

                Text {
                    text: syncViewModel.lastError
                    font.pixelSize: 12
                    color: ThemeManager.isDark ? "#f08080" : "#b00000"
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }
    }
}
