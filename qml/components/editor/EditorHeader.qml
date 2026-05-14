import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

// Close-button row at the top of the editor panel, plus a "Reading mode"
// trigger that opens a full-screen distraction-free view of the description.
RowLayout {
    id: root

    signal closeClicked()
    signal readingModeClicked()

    Layout.leftMargin:   Spacing.md
    Layout.rightMargin:  Spacing.sm
    Layout.topMargin:    12
    Layout.bottomMargin: Spacing.xs
    spacing: Spacing.sm

    // ── Close ✕ ───────────────────────────────────────────────────────────
    Rectangle {
        width:  28
        height: 28
        radius: Radius.sm
        color:  closeHover.containsMouse ? ThemeManager.hoverColor : "transparent"

        Text {
            anchors.centerIn: parent
            text:  "✕"
            color: ThemeManager.secondaryTextColor
            font.pixelSize: FontSize.body
        }

        MouseArea {
            id: closeHover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape:  Qt.PointingHandCursor
            onClicked:    root.closeClicked()
        }
    }

    // ── Pusher — keeps action buttons grouped on the right edge ──────────
    Item { Layout.fillWidth: true }

    // ── Reading-mode 📖 ───────────────────────────────────────────────────
    Rectangle {
        width:  28
        height: 28
        radius: Radius.sm
        color:  readingHover.containsMouse ? ThemeManager.hoverColor : "transparent"

        ToolTip.visible: readingHover.containsMouse
        ToolTip.text:    "Режим чтения (Ctrl+Shift+M)"
        ToolTip.delay:   400

        Text {
            anchors.centerIn: parent
            text:  "📖"
            font.pixelSize: FontSize.body
        }

        MouseArea {
            id: readingHover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape:  Qt.PointingHandCursor
            onClicked:    root.readingModeClicked()
        }
    }
}
