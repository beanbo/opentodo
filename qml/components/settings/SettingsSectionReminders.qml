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

    // Persisted user preference for notification sound. Read by
    // WindowsNotificationService::showNotification via QSettings under the
    // "notifications/enabled" key (category + property name). Default ON
    // for backwards compatibility.
    Settings {
        id: soundSettings
        category: "notifications"
        property bool enabled: true
    }

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
                text: "Напоминания"
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

        // Notification sound toggle
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
                        text: "Звук уведомлений"
                        font.pixelSize: 14
                        color: ThemeManager.textColor
                    }
                    Text {
                        text: "Воспроизводить системный звук при показе уведомлений"
                        font.pixelSize: 12
                        color: ThemeManager.secondaryTextColor
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                Switch {
                    id: soundSwitch
                    checked: soundSettings.enabled
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    onToggled: {
                        soundSettings.enabled = checked
                        if (typeof telemetry !== "undefined")
                            telemetry.trackEvent("notification_sound_toggle", { enabled: checked })
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

        // Default reminder time
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
                    text: "Время напоминания по умолчанию"
                    font.pixelSize: 14
                    color: ThemeManager.textColor
                    Layout.fillWidth: true
                }

                TextField {
                    id: reminderTimeField
                    text: settingsViewModel.defaultReminderTime
                    placeholderText: "09:00"
                    font.pixelSize: 13
                    color: ThemeManager.textColor
                    implicitWidth: 100
                    horizontalAlignment: Text.AlignHCenter
                    onEditingFinished: settingsViewModel.updateSetting("defaultReminderTime", text)

                    background: Rectangle {
                        radius: 4
                        color: ThemeManager.hoverColor
                        border.color: reminderTimeField.activeFocus
                                      ? ThemeManager.accentColor
                                      : ThemeManager.borderColor
                        border.width: reminderTimeField.activeFocus ? 2 : 1
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

        // Auto-reminder offset
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
                        text: "Авто-напоминание (за сколько до срока)"
                        font.pixelSize: 14
                        color: ThemeManager.textColor
                    }
                    Text {
                        text: "Минут до срока выполнения"
                        font.pixelSize: 12
                        color: ThemeManager.secondaryTextColor
                    }
                }

                SpinBox {
                    from: 0
                    to: 120
                    value: settingsViewModel.autoReminderOffset
                    stepSize: 5
                    implicitWidth: 110

                    Layout.preferredWidth: 110

                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    onValueModified: settingsViewModel.updateSetting("autoReminderOffset", value)

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

        // ITER1 #10: Request notification permission button.
        // Hidden on Windows — toasts via QSystemTrayIcon do not require an
        // OS-level permission prompt (unlike Android/Web/macOS), so this row
        // would be misleading. Height collapses to 0 when hidden so the
        // surrounding ColumnLayout doesn't leave an empty band.
        Rectangle {
            visible: Qt.platform.os !== "windows"
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 56 : 0
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
                        text: "Разрешение на уведомления"
                        font.pixelSize: 14
                        color: ThemeManager.textColor
                    }
                    Text {
                        text: "Запросить у системы разрешение на показ уведомлений"
                        font.pixelSize: 12
                        color: ThemeManager.secondaryTextColor
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                Button {
                    text: "Запросить разрешение"
                    implicitHeight: 34
                    implicitWidth: 170

                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font.pixelSize: 12
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 4
                        color: ThemeManager.accentColor
                    }

                    onClicked: {
                        if (typeof notificationService !== "undefined")
                            notificationService.requestPermission()
                        settingsViewModel.notificationPermissionAsked = false
                    }
                }
            }
        }
    }
}
