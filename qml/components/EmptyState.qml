import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material
import OpenTodoUI

Item {
    id: root

    property string emoji: "🎉"        // big visual
    property string title: ""          // "Всё сделано!"
    property string message: ""        // "Самое время отдохнуть"
    property string actionLabel: ""    // optional CTA button text
    signal actionClicked()

    ColumnLayout {
        anchors.centerIn: parent
        spacing: Spacing.lg
        width: Math.min(parent.width - Spacing.xl * 2, 400)

        Text {
            text: root.emoji
            font.pixelSize: 96
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignHCenter
            opacity: 0.85
        }

        Label {
            text: root.title
            font.pixelSize: FontSize.h2
            font.weight: Font.DemiBold
            color: ThemeManager.textColor
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }

        Label {
            text: root.message
            font.pixelSize: FontSize.body
            color: ThemeManager.secondaryTextColor
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }

        Button {
            text: root.actionLabel
            visible: root.actionLabel.length > 0
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Spacing.md
            Material.background: ThemeManager.accentColor
            Material.foreground: "white"
            onClicked: root.actionClicked()
        }
    }
}
