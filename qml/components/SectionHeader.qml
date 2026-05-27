import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

Rectangle {
    id: root

    required property string title
    property int taskCount: 0
    property bool collapsed: false
    property bool isDarkTheme: ThemeManager.isDark

    signal toggleCollapsed()
    signal addTaskRequested()
    signal moreOptionsRequested()

    width: parent ? parent.width : 0
    height: 40
    color: "transparent"

    // Bottom border line
    // Q4.01 (night iter-7): theme-aware border — был "#363636" / "#f0f0f0",
    // только для dark/classic_light. kale/blueberry/raspberry получали
    // тёмно-серую полоску на своём accent-tинтованном фоне.
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: ThemeManager.borderColor
    }

    HoverHandler {
        id: headerHover
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 6

        // Collapse arrow
        // Q4.01 (redesign): "\u25B8" (\u25B8) unicode \u2192 stroke chevron-right IconSvg.
        // Rotation transform stays \u2014 it animates 0\u00B0 \u2192 90\u00B0 to convert
        // chevron-right into chevron-down without swapping the icon name.
        IconSvg {
            id: arrowText
            name: "chevron-right"
            size: 12
            color: ThemeManager.textColor

            transform: Rotation {
                id: arrowRotation
                origin.x: arrowText.width / 2
                origin.y: arrowText.height / 2
                angle: root.collapsed ? 0 : 90

                Behavior on angle {
                    NumberAnimation { duration: 150; easing.type: Easing.OutQuad }
                }
            }
        }

        // Section title
        Text {
            id: titleText
            text: root.title
            font.pixelSize: FontSize.small
            font.weight: Font.Medium
            color: ThemeManager.textColor
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        // Task count
        // Q4.01 pixel-perfect: дизайн SectionHeader count — pill с background surface,
        // radius 9, padding 1 7, fontSize 10 / Medium.
        Rectangle {
            id: countBadge
            visible: root.taskCount > 0
            Layout.alignment: Qt.AlignVCenter
            implicitWidth:  countLabel.implicitWidth + 14   // 7+7 padding
            implicitHeight: countLabel.implicitHeight + 2   // 1+1 padding
            radius: 9
            color: ThemeManager.surfaceColor

            Text {
                id: countLabel
                anchors.centerIn: parent
                text: root.taskCount.toString()
                font.pixelSize: FontSize.tiny
                font.weight: Font.Medium
                // Iter-night-31: was hardcoded "#999999" — readable on the dark
                // theme (whose secondaryTextColor is the same shade) but too
                // light against light/kale/lavender/etc. sidebars. Bind to the
                // active palette like the sidebar's section labels do.
                color: ThemeManager.secondaryTextColor
            }
        }

        // Spacer
        Item {
            Layout.fillWidth: true
        }

        // Add task button — visible on hover
        // ITER1 #28: z:2 so this item beats the collapse MouseArea (z:0).
        // Task 17cea65b iter-3: было unicode "+".
        IconSvg {
            id: addButton
            name: "plus"
            size: 14
            color: ThemeManager.iconColor
            visible: headerHover.hovered
            opacity: addHover.hovered ? 1.0 : 0.7
            z: 2

            HoverHandler {
                id: addHover
            }

            MouseArea {
                anchors.fill: parent
                anchors.margins: -4
                cursorShape: Qt.PointingHandCursor
                z: 2
                onClicked: (mouse) => {
                    mouse.accepted = true
                    root.addTaskRequested()
                }
            }
        }

        // More options button — visible on hover
        // Q4.01 (redesign): unicode "⋯" → stroke more (3-dot) IconSvg
        IconSvg {
            id: moreButton
            name: "more"
            size: 14
            color: ThemeManager.iconColor
            visible: headerHover.hovered
            opacity: moreHover.hovered ? 1.0 : 0.7
            z: 2

            HoverHandler {
                id: moreHover
            }

            MouseArea {
                anchors.fill: parent
                anchors.margins: -4
                cursorShape: Qt.PointingHandCursor
                z: 2
                onClicked: (mouse) => {
                    mouse.accepted = true
                    root.moreOptionsRequested()
                }
            }
        }
    }

    // ITER1 #28: collapse MouseArea at z:0 (default). Button MouseAreas (z:2)
    // win the hit-test over this one, so clicking a button no longer also
    // fires toggleCollapsed. Dynamic rightMargin calculation removed — it had
    // a one-frame race where the collapse area was full-width when hover first
    // became true (before the buttons became visible).
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        z: 0
        onClicked: root.toggleCollapsed()
    }
}
