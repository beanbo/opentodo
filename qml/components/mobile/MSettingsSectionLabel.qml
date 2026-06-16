import QtQuick
import OpenTodoUI

// MSettingsSectionLabel — uppercase section header in the settings list.
Text {
    width: parent ? parent.width : 0
    leftPadding: 20; topPadding: 16; bottomPadding: 8
    font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 0.3
    font.capitalization: Font.AllUppercase
    color: ThemeManager.secondaryTextColor
}
