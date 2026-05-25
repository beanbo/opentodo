import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material
import OpenTodoUI

Item {
    id: root

    property string emoji: "🎉"        // big visual (fallback)
    // Q4.01 (redesign): set this to render an IconSvg from the stroke pack
    // instead of the emoji. Wins over emoji when non-empty.
    property string iconName: ""
    // Q4.01 (redesign): "all done" pattern — render iconName inside a filled
    // circular badge instead of a greyed-out glyph. Used by Inbox/Today/Upcoming
    // when the list is empty.
    property bool successBadge: false
    property color badgeColor: "#3aa84e"
    property string title: ""          // "Задач нет"
    property string message: ""        // "Всё сделано!"
    property string actionLabel: ""    // optional CTA button text
    signal actionClicked()

    ColumnLayout {
        anchors.centerIn: parent
        spacing: Spacing.lg
        width: Math.min(parent.width - Spacing.xl * 2, 400)

        // "Задач нет" success badge — green circle + white check. Drawn as a
        // pre-rendered SVG (resources/icons/ui/success-badge.svg) because Qt
        // 6.11's Rectangle.radius and Shape arc both produce visible flat-edged
        // corners at this scale on high-DPI displays (corners clamp to ~25 px
        // regardless of the requested radius). An SVG with an explicit
        // <circle cx="44" cy="44" r="44"> renders crisp at every zoom level.
        // sourceSize is 2× the display size so the rasteriser has enough
        // resolution for smooth anti-aliased edges on hi-DPI screens.
        Image {
            visible: root.successBadge && root.iconName !== ""
            source: "qrc:/icons/resources/icons/ui/success-badge.svg"
            sourceSize.width: 176
            sourceSize.height: 176
            Layout.preferredWidth: 88
            Layout.preferredHeight: 88
            Layout.maximumWidth: 88
            Layout.maximumHeight: 88
            Layout.fillWidth: false
            Layout.alignment: Qt.AlignHCenter
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: false
        }

        IconSvg {
            visible: !root.successBadge && root.iconName !== ""
            name: root.iconName
            size: 96
            color: ThemeManager.secondaryTextColor
            opacity: 0.6
            Layout.alignment: Qt.AlignHCenter
        }

        Text {
            visible: root.iconName === ""
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
            font.pixelSize: FontSize.bodySmall
            color: ThemeManager.secondaryTextColor
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
            Layout.maximumWidth: 320
            wrapMode: Text.WordWrap
        }

        // Q4.01 (redesign): pill-shaped CTA matching design's borderRadius:20 +
        // padding 8/16. Material's default Button uses radius 4 which doesn't
        // read as a "primary" pill on the empty-state hero.  Custom contentItem
        // + background so we control both the corner radius and the inline
        // icon (PlusIcon before the label).
        Button {
            id: actionButton
            visible: root.actionLabel.length > 0
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Spacing.md
            implicitHeight: 36
            padding: 0
            background: Rectangle {
                radius: 20
                color: actionButton.down
                    ? Qt.darker(ThemeManager.accentColor, 1.1)
                    : (actionButton.hovered
                       ? Qt.lighter(ThemeManager.accentColor, 1.1)
                       : ThemeManager.accentColor)
            }
            contentItem: Row {
                anchors.centerIn: parent
                spacing: 6
                leftPadding: 16
                rightPadding: 16
                IconSvg {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "plus"
                    size: 14
                    color: "white"
                }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.actionLabel
                    color: "white"
                    font.pixelSize: FontSize.bodySmall
                    font.weight: Font.Medium
                }
            }
            onClicked: root.actionClicked()
        }
    }
}
