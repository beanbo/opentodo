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
            spacing: 16

            // Q4.01 pixel-perfect: дизайн SettingsView header — IconSvg "settings" 22 + title.
            RowLayout {
                Layout.topMargin: 28
                Layout.leftMargin: 24
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

            SettingsSectionGeneral      { Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16 }
            SettingsSectionLanguage     { Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16 }
            SettingsSectionAppearance   { Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16 }
            SettingsSectionSync         { Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16 }
            SettingsSectionAI           { Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16 }
            SettingsSectionProductivity { Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16 }
            SettingsSectionReminders    { Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16 }
            SettingsSectionDefer        { Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16 }
            SettingsSectionSidebar      { Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16 }
            SettingsSectionBackup       { Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16 }
            SettingsSectionAbout        { Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16 }
            SettingsSectionSupport      { Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16 }

            Item { Layout.preferredHeight: 40 }
        }
    }
}
