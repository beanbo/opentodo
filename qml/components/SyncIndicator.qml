import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

Rectangle {
    id: root

    property bool isDarkTheme: ThemeManager.isDark

    // ── Notification permission request — runs once per install ──────────────
    // Persistent gate via SettingsViewModel.notificationPermissionAsked Q_PROPERTY
    // so we don't prompt again across restarts.
    //
    // Q-Android-Audit #2.1: previously the flag was set synchronously right
    // after `requestPermission()`. Because Android's POST_NOTIFICATIONS dialog
    // is async, the persistent flag flipped BEFORE the user had answered;
    // re-prompting after a denial was impossible without a manual reset.
    // Fix:
    //   * If no permission is needed (hasPermission() already true) just flip
    //     the flag — there's nothing pending. Covers Windows / WASM / Linux
    //     stub and Android < 13.
    //   * If a permission IS needed, only call requestPermission() and wait
    //     for the permissionRequestCompleted signal handler below to flip the
    //     flag once the OS dialog has resolved.
    Component.onCompleted: {
        if (typeof settingsViewModel === "undefined" || !settingsViewModel)
            return
        if (settingsViewModel.notificationPermissionAsked)
            return
        if (typeof notificationService === "undefined" || !notificationService) {
            // No service at all (some embedded WASM builds) — treat as asked
            // so we don't loop on next launch.
            settingsViewModel.notificationPermissionAsked = true
            return
        }
        if (notificationService.hasPermission()) {
            // Already granted (or platform doesn't require a runtime grant) —
            // safe to mark as asked immediately.
            settingsViewModel.notificationPermissionAsked = true
            return
        }
        // Permission is needed — fire the OS dialog and wait for the callback
        // below to flip the flag.
        notificationService.requestPermission()
    }

    // Q-Android-Audit #2.1: fired from the C++ side once the OS has actually
    // returned from the runtime permission dialog (or immediately on
    // platforms where requestPermission is a no-op fallback). This is the
    // ONE place that flips notificationPermissionAsked so we don't mark the
    // user as "asked" before they had a chance to answer.
    Connections {
        target: typeof notificationService !== "undefined" ? notificationService : null
        function onPermissionRequestCompleted(granted) {
            if (typeof settingsViewModel === "undefined" || !settingsViewModel)
                return
            settingsViewModel.notificationPermissionAsked = true
        }
    }

    implicitHeight: Spacing.xl
    implicitWidth: row.implicitWidth + Spacing.md
    radius: Radius.md
    color: "transparent"

    // Colors
    readonly property color colorSynced:  "#4caf50"
    readonly property color colorError:   "#db4c3f"
    readonly property color colorOffline: "#9e9e9e"
    readonly property color colorSyncing: "#db4c3f"

    // Hover detection
    HoverHandler {
        id: hoverHandler
    }

    // Subtle hover background
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: isDarkTheme ? "#ffffff" : "#000000"
        opacity: hoverHandler.hovered ? 0.06 : 0
        Behavior on opacity { NumberAnimation { duration: Duration.fast } }
    }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 6

        // Status icon / animation container
        Item {
            id: iconArea
            width: Spacing.md
            height: Spacing.md
            Layout.alignment: Qt.AlignVCenter

            // Synced: green filled circle
            Rectangle {
                id: syncedDot
                anchors.centerIn: parent
                width: 10
                height: 10
                radius: 5
                color: colorSynced
                visible: syncViewModel.syncStatus === "synced" && syncViewModel.pendingCommits === 0
            }

            // Syncing: rotating arc indicator
            Rectangle {
                id: syncingRing
                anchors.centerIn: parent
                width: 13
                height: 13
                radius: 6.5
                color: "transparent"
                border.color: colorSyncing
                border.width: 2
                visible: syncViewModel.syncStatus === "syncing" || syncViewModel.isSyncing

                // Cut most of the ring to look like a spinner arc
                Rectangle {
                    width: 7
                    height: 7
                    color: isDarkTheme ? root.color : "transparent"
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                }

                RotationAnimation on rotation {
                    running: syncingRing.visible
                    from: 0
                    to: 360
                    duration: 900
                    loops: Animation.Infinite
                }
            }

            // Error: red circle with exclamation
            Rectangle {
                id: errorDot
                anchors.centerIn: parent
                width: 14
                height: 14
                radius: 7
                color: colorError
                visible: syncViewModel.syncStatus === "error"

                Text {
                    anchors.centerIn: parent
                    text: "!"
                    color: "#ffffff"
                    font.pixelSize: 10
                    font.bold: true
                }
            }

            // Offline: grey circle
            Rectangle {
                id: offlineDot
                anchors.centerIn: parent
                width: 10
                height: 10
                radius: 5
                color: colorOffline
                visible: syncViewModel.syncStatus === "offline"
            }

            // Pending commits badge (shown when synced but have pending commits)
            Rectangle {
                id: pendingBadge
                visible: syncViewModel.pendingCommits > 0
                         && syncViewModel.syncStatus === "synced"
                         && !syncViewModel.isSyncing
                width: Math.max(14, badgeText.implicitWidth + 6)
                height: 14
                radius: 7
                color: ThemeManager.accentColor
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: -3
                anchors.rightMargin: -3

                Text {
                    id: badgeText
                    anchors.centerIn: parent
                    text: syncViewModel.pendingCommits > 99 ? "99+" : syncViewModel.pendingCommits.toString()
                    color: "#ffffff"
                    font.pixelSize: 8
                    font.bold: true
                }
            }
        }

        // Status label text
        Text {
            id: statusLabel
            Layout.alignment: Qt.AlignVCenter
            font.pixelSize: 13
            color: {
                switch (syncViewModel.syncStatus) {
                case "synced":   return isDarkTheme ? "#cccccc" : "#555555"
                case "syncing":  return colorSyncing
                case "error":    return colorError
                case "offline":  return colorOffline
                default:         return isDarkTheme ? "#cccccc" : "#555555"
                }
            }

            text: {
                switch (syncViewModel.syncStatus) {
                case "synced":  return qsTr("Синхронизировано")
                case "syncing": return qsTr("Синхронизация…")
                case "error":   return qsTr("Ошибка")
                case "offline": return qsTr("Офлайн")
                default:        return ""
                }
            }

            // Pulse opacity when syncing
            SequentialAnimation on opacity {
                running: syncViewModel.syncStatus === "syncing"
                loops: Animation.Infinite
                NumberAnimation { to: 0.4; duration: 600; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1.0; duration: 600; easing.type: Easing.InOutSine }
            }
            // F-7.10: removed dead onVisibleChanged handler — it broke the
            // opacity binding by setting it imperatively; the SequentialAnimation
            // already resets opacity when it stops (running = false).
        }
    }

    // Click: trigger sync
    TapHandler {
        onTapped: syncViewModel.sync()
    }

    // Tooltip
    ToolTip {
        id: tooltip
        visible: hoverHandler.hovered
        delay: 500
        timeout: 4000

        contentItem: Column {
            spacing: Spacing.xs
            padding: Spacing.sm

            Text {
                text: syncViewModel.formatLastSyncTime()
                font.pixelSize: 13
                color: isDarkTheme ? "#e0e0e0" : "#333333"
            }

            Text {
                visible: syncViewModel.pendingCommits > 0
                text: qsTr("%n несохранённых изменений", "", syncViewModel.pendingCommits)
                font.pixelSize: 13
                color: isDarkTheme ? "#e0e0e0" : "#333333"
            }

            Text {
                visible: syncViewModel.syncStatus === "error" && syncViewModel.lastError !== ""
                text: qsTr("Ошибка: %1").arg(syncViewModel.lastError)
                font.pixelSize: 13
                color: colorError
                wrapMode: Text.WordWrap
                width: 220
            }
        }

        background: Rectangle {
            color: isDarkTheme ? "#3a3a3a" : "#ffffff"
            border.color: isDarkTheme ? "#4a4a4a" : "#e0e0e0"
            border.width: 1
            radius: Radius.md
        }
    }
}
