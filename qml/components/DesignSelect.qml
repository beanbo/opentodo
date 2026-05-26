import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

// Дизайнерский Select (.design-temp/opentodo/project/view-others.jsx:261-269)
// Стилизованный ComboBox: padding 5/10, radius 6, fontSize 12, border 1px,
// background bg, chevron-down 11. Минимальная ширина 180.
ComboBox {
    id: control
    implicitHeight: 28
    implicitWidth: 180          // honour design minWidth even outside a Layout
    Layout.minimumWidth: 180

    leftPadding: 10
    rightPadding: 30            // место под chevron справа
    topPadding: 5
    bottomPadding: 5

    background: Rectangle {
        radius: Radius.md
        color: ThemeManager.backgroundColor
        border.color: control.activeFocus
                      ? ThemeManager.accentColor
                      : ThemeManager.borderColor
        border.width: 1

        Behavior on border.color { ColorAnimation { duration: 150 } }
    }

    contentItem: Text {
        text: control.displayText
        font.pixelSize: FontSize.small
        color: ThemeManager.textColor
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignLeft
        elide: Text.ElideRight
    }

    indicator: IconSvg {
        name: "chevron-down"
        size: 11
        color: ThemeManager.secondaryTextColor
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
    }

    delegate: ItemDelegate {
        width: control.popup.width
        contentItem: Text {
            text: modelData !== undefined
                  ? (control.textRole && modelData[control.textRole] !== undefined
                     ? modelData[control.textRole]
                     : modelData)
                  : ""
            font.pixelSize: FontSize.small
            color: ThemeManager.textColor
            verticalAlignment: Text.AlignVCenter
        }
        highlighted: control.highlightedIndex === index
        background: Rectangle {
            color: highlighted ? ThemeManager.hoverColor : "transparent"
        }
    }

    popup: Popup {
        y: control.height + 2
        width: control.width
        implicitHeight: contentItem.implicitHeight + 8
        padding: 4

        background: Rectangle {
            color: ThemeManager.backgroundColor
            radius: Radius.md
            border.color: ThemeManager.borderColor
            border.width: 1
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }
}
