import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

Item {
    id: root
    anchors.fill: parent

    Rectangle {
        anchors.fill: parent
        color: ThemeManager.backgroundColor
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        // F-6.96 — allow keyboard focus so Tab navigates through child sections
        focus: true
        activeFocusOnTab: true
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        ColumnLayout {
            id: contentColumn
            width: Math.min(parent.width, 680)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Spacing.md

            // Q4.01 pixel-perfect: дизайн SettingsView header — IconSvg "settings" 22 + title.
            RowLayout {
                Layout.topMargin: 28
                Layout.leftMargin: Spacing.lg
                Layout.fillWidth: true
                spacing: 12
                IconSvg {
                    name: "settings"
                    size: 22
                    color: ThemeManager.textColor
                    Layout.alignment: Qt.AlignVCenter
                }
                Label {
                    text: qsTr("Настройки")
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                    color: ThemeManager.textColor
                }
                Item { Layout.fillWidth: true }
            }

            SettingsSectionGeneral      { Layout.fillWidth: true; Layout.leftMargin: Spacing.md; Layout.rightMargin: Spacing.md}
            SettingsSectionLanguage     { Layout.fillWidth: true; Layout.leftMargin: Spacing.md; Layout.rightMargin: Spacing.md}
            SettingsSectionAppearance   { Layout.fillWidth: true; Layout.leftMargin: Spacing.md; Layout.rightMargin: Spacing.md}
            SettingsSectionSync         { Layout.fillWidth: true; Layout.leftMargin: Spacing.md; Layout.rightMargin: Spacing.md}
            SettingsSectionAI           { Layout.fillWidth: true; Layout.leftMargin: Spacing.md; Layout.rightMargin: Spacing.md}
            SettingsSectionCollaborators{ Layout.fillWidth: true; Layout.leftMargin: Spacing.md; Layout.rightMargin: Spacing.md}
            SettingsSectionProductivity { Layout.fillWidth: true; Layout.leftMargin: Spacing.md; Layout.rightMargin: Spacing.md}
            SettingsSectionReminders    { Layout.fillWidth: true; Layout.leftMargin: Spacing.md; Layout.rightMargin: Spacing.md}
            SettingsSectionDefer        { Layout.fillWidth: true; Layout.leftMargin: Spacing.md; Layout.rightMargin: Spacing.md}
            SettingsSectionSidebar      { Layout.fillWidth: true; Layout.leftMargin: Spacing.md; Layout.rightMargin: Spacing.md}
            SettingsSectionBackup       { Layout.fillWidth: true; Layout.leftMargin: Spacing.md; Layout.rightMargin: Spacing.md}
            SettingsSectionAbout        { Layout.fillWidth: true; Layout.leftMargin: Spacing.md; Layout.rightMargin: Spacing.md}
            SettingsSectionSupport      { Layout.fillWidth: true; Layout.leftMargin: Spacing.md; Layout.rightMargin: Spacing.md}

            Item { Layout.preferredHeight: 40 }
        }
    }
}
