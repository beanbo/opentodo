import QtQuick
import QtQuick.Controls.Material
import OpenTodoUI

// Дизайнерский Toggle (.design-temp/opentodo/project/view-others.jsx:271-281)
// Заменяет Material Switch на pill 36×20 + 16×16 knob.
Switch {
    id: control
    implicitWidth: 36
    implicitHeight: 20
    padding: 0

    indicator: Rectangle {
        implicitWidth: 36
        implicitHeight: 20
        x: 0
        y: 0
        radius: 10
        color: control.checked ? ThemeManager.accentColor : ThemeManager.borderColor

        Behavior on color { ColorAnimation { duration: 150 } }

        Rectangle {
            x: control.checked ? 18 : 2
            y: 2
            width: 16
            height: 16
            radius: 8
            color: "#ffffff"

            Behavior on x { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
        }
    }

    contentItem: Item {}  // отключить дефолтный text — текст должен идти отдельно (Label рядом)
}
