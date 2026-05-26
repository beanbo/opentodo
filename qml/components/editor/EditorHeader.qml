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

    // Q4.01 (redesign): header sits flush at the top of the editor pane.
    // Padding follows view-others.jsx Card pattern (16px horizontal, 12px
    // vertical at the top of a sheet).
    Layout.leftMargin:   12
    Layout.rightMargin:  12
    Layout.topMargin:    12
    Layout.bottomMargin: 0
    spacing: Spacing.sm

    // ── Close ✕ — 28×28 iconBtn box from app-shell.jsx ────────────────────
    Rectangle {
        width:  28
        height: 28
        radius: Radius.md
        color:  closeHover.containsMouse ? ThemeManager.hoverColor : "transparent"

        // Task 17cea65b iter-3: было unicode "✕".
        IconSvg {
            anchors.centerIn: parent
            name: "close"
            size: 14
            color: ThemeManager.secondaryTextColor
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

    // ── Reading-mode → "more-horizontal" 28×28 iconBtn ────────────────────
    // Q4.01 (redesign): дизайн показывает справа от title компактный
    // ⋯-button (см. iconBtn 28×28 / more-horizontal size 14 из
    // view-project.jsx:97). Само reading-mode action сохранилось, просто
    // теперь это primary action этой кнопки.
    Rectangle {
        width:  28
        height: 28
        radius: Radius.md
        color:  readingHover.containsMouse ? ThemeManager.hoverColor : "transparent"

        ToolTip.visible: readingHover.containsMouse
        ToolTip.text:    qsTr("Режим чтения (Ctrl+Shift+M)")
        ToolTip.delay:   400

        IconSvg {
            anchors.centerIn: parent
            name: "more-horizontal"
            size: 14
            color: ThemeManager.secondaryTextColor
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
