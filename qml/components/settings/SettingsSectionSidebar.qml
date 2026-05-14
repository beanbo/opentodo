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

    property bool expanded: false

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
                text: "Боковая панель"
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

        // Show Inbox count
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
                    text: "Показывать счётчик Входящих"
                    font.pixelSize: 14
                    color: ThemeManager.textColor
                    Layout.fillWidth: true
                }

                Switch {
                    checked: settingsViewModel.showInboxCount
                    onToggled: {
                        settingsViewModel.updateSetting("showInboxCount", checked)
                        if (typeof telemetry !== "undefined" && telemetry.enabled)
                            telemetry.trackEvent("settings_show_inbox_count_toggled", { "enabled": checked })
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

        // Show Today count
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
                    text: "Показывать счётчик Сегодня"
                    font.pixelSize: 14
                    color: ThemeManager.textColor
                    Layout.fillWidth: true
                }

                Switch {
                    checked: settingsViewModel.showTodayCount
                    onToggled: {
                        settingsViewModel.updateSetting("showTodayCount", checked)
                        if (typeof telemetry !== "undefined" && telemetry.enabled)
                            telemetry.trackEvent("settings_show_today_count_toggled", { "enabled": checked })
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

        // Show Upcoming count
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
                    text: "Показывать счётчик Предстоящих"
                    font.pixelSize: 14
                    color: ThemeManager.textColor
                    Layout.fillWidth: true
                }

                Switch {
                    checked: settingsViewModel.showUpcomingCount
                    onToggled: {
                        settingsViewModel.updateSetting("showUpcomingCount", checked)
                        if (typeof telemetry !== "undefined" && telemetry.enabled)
                            telemetry.trackEvent("settings_show_upcoming_count_toggled", { "enabled": checked })
                    }
                }
            }
        }
    }
}
