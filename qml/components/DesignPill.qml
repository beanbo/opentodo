import QtQuick
import QtQuick.Controls.Material
import OpenTodoUI

// Дизайнерский Pill (.design-temp/opentodo/project/view-others.jsx:253-260)
// Используется как сегмент в группе выбора (например 12/24-hour).
AbstractButton {
    id: control
    implicitHeight: 24
    leftPadding: 10
    rightPadding: 10
    topPadding: 4
    bottomPadding: 4

    // night iter-123: was `control.checked || control.highlighted`, but the
    // root is an AbstractButton which has NO `highlighted` property (that's a
    // Button/ItemDelegate addition). So `control.highlighted` evaluated to
    // undefined and `false || undefined` → undefined, spamming
    // "Unable to assign [undefined] to bool" on every unchecked pill. Every
    // DesignPill caller drives state via `checkable`+`checked` (see
    // SettingsSectionGeneral time-format / density toggles); none sets
    // highlighted. Bind to `checked` alone — same result (undefined was falsy),
    // minus the warning.
    property bool active: control.checked

    background: Rectangle {
        radius: Radius.md
        color: control.active ? ThemeManager.accentColor : "transparent"
        border.color: control.active ? "transparent" : ThemeManager.borderColor
        border.width: 1

        Behavior on color { ColorAnimation { duration: 150 } }
        Behavior on border.color { ColorAnimation { duration: 150 } }
    }

    contentItem: Text {
        text: control.text
        font.pixelSize: FontSize.small
        // Q4.01 night iter-58: active text sits on the accent background, so it
        // must use accentForeground (dark on yellow themes — sunflower/
        // highcontrast — white elsewhere). Hardcoded "#ffffff" was unreadable
        // white-on-yellow on those palettes. Same accentForeground class as
        // iters 16/17/22.
        color: control.active ? ThemeManager.accentForeground : ThemeManager.secondaryTextColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
