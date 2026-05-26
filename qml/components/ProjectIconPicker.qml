import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

/*
 * Q4.01 (redesign): picker для выбора иконки проекта из единого stroke-пака.
 *
 * Используется в:
 *   • addProjectDialog (SidebarContent.qml) — выбрать иконку при создании проекта
 *   • контекстное меню проекта «Изменить иконку…» — менять для существующего
 *
 * Возвращает строку формата "projects/<glyph>" (TaskItem / SidebarItem /
 * QuickAdd рендерят её через IconSvg благодаря subdir-резолверу в IconSvg.qml
 * + auto-detect префикса в SidebarItem).
 *
 * Источник списка — resources/icons/projects/. Когда туда добавляется новая
 * иконка, нужно дописать её сюда же в ALL_ICONS — это единственная точка
 * регистрации для UI. (Регистрация в QRC уже в CMakeLists.txt.)
 *
 * Public API:
 *   property string selectedIcon  — текущий выбор (полная строка "projects/X")
 *   signal accepted(string icon)  — фаер при OK; icon может быть "" если
 *                                    пользователь нажал «Без иконки»
 *   function openWith(initial)    — открывает Dialog с указанным выбором
 */
Dialog {
    id: root

    title: qsTr("Выберите иконку")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(520, Overlay.overlay.width - Spacing.xl)
    standardButtons: Dialog.Ok | Dialog.Cancel

    property string selectedIcon: ""
    signal iconAccepted(string icon)

    function openWith(initial) {
        root.selectedIcon = initial || ""
        searchField.text = ""
        root.open()
    }

    onAccepted: root.iconAccepted(root.selectedIcon)

    // ── Категории + плоский список ────────────────────────────────────────
    // Категории отражают то, как пак сгруппирован в дизайне (chat1.md, см.
    // .design-temp/opentodo/chats/). Плоский список собран в порядке
    // категорий — пользователю не нужно прыгать между секциями.
    readonly property var _categories: [
        { title: qsTr("Работа"),
          icons: ["briefcase", "building", "monitor", "chart", "coffee", "target"] },
        { title: qsTr("Личное"),
          icons: ["user", "smile", "heart", "dumbbell", "music", "book"] },
        { title: qsTr("Идеи"),
          icons: ["lightbulb", "brain", "puzzle", "pencil", "flag"] },
        { title: qsTr("3D-печать · Мастерская"),
          icons: ["printer", "cube", "layers", "hammer", "wrench", "screwdriver"] },
        { title: qsTr("Покупки"),
          icons: ["cart", "shopping-bag", "basket", "gift", "fork-knife"] },
        { title: qsTr("Путешествия"),
          icons: ["plane", "map-pin", "compass", "mountain", "camera"] },
        { title: qsTr("Дом · Дом. дела"),
          icons: ["house", "key", "bed", "sofa", "broom", "shirt", "plant",
                  "leaf", "sun", "moon"] },
        { title: qsTr("Бонусом"),
          icons: ["car", "bike", "paw", "gamepad", "graduation-cap",
                  "palette", "rocket", "runner"] },
    ]

    // Плоский поисковый индекс (все 51 имя).
    readonly property var _allIcons: {
        const out = []
        for (let c = 0; c < _categories.length; c++)
            for (let i = 0; i < _categories[c].icons.length; i++)
                out.push(_categories[c].icons[i])
        return out
    }

    // Фильтр по подстроке имени иконки (английское имя — устойчивее
    // к локали, чем переводы категорий).
    function _filtered(category) {
        const q = searchField.text.trim().toLowerCase()
        if (q === "") return category.icons
        return category.icons.filter(function(n) { return n.indexOf(q) >= 0 })
    }

    contentItem: ColumnLayout {
        spacing: Spacing.md

        // ── Поиск ──────────────────────────────────────────────────────────
        TextField {
            id: searchField
            Layout.fillWidth: true
            placeholderText: qsTr("Поиск (briefcase, house, …)")
            // Иконка-лупа слева — единый стиль с глобальной строкой поиска.
            leftPadding: 32
            Item {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 8
                width: 16
                height: 16
                IconSvg {
                    anchors.centerIn: parent
                    name: "search"
                    size: 14
                    color: ThemeManager.secondaryTextColor
                }
            }
        }

        // ── Кнопка «Без иконки» ───────────────────────────────────────────
        // Явный способ снять иконку — отдельная строка над сеткой, чтобы
        // пользователь не искал её среди категорий.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            radius: Radius.md
            color: noneArea.containsMouse
                   ? ThemeManager.hoverColor
                   : (root.selectedIcon === "" ? Qt.rgba(
                            ThemeManager.accentColor.r,
                            ThemeManager.accentColor.g,
                            ThemeManager.accentColor.b, 0.15)
                                              : "transparent")
            border.color: root.selectedIcon === ""
                          ? ThemeManager.accentColor : "transparent"
            border.width: root.selectedIcon === "" ? 1 : 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Spacing.sm
                spacing: Spacing.sm

                IconSvg {
                    name: "close"
                    size: 14
                    color: ThemeManager.secondaryTextColor
                    Layout.alignment: Qt.AlignVCenter
                }
                Text {
                    text: qsTr("Без иконки (цветная точка)")
                    color: ThemeManager.textColor
                    font.pixelSize: FontSize.bodySmall
                    Layout.alignment: Qt.AlignVCenter
                    Layout.fillWidth: true
                }
            }

            MouseArea {
                id: noneArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.selectedIcon = ""
            }
        }

        // ── Категории + сетка ──────────────────────────────────────────────
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 360
            clip: true

            ColumnLayout {
                width: 480
                spacing: Spacing.sm

                Repeater {
                    model: root._categories

                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 4
                        visible: root._filtered(modelData).length > 0

                        Text {
                            text: modelData.title
                            font.pixelSize: FontSize.caption
                            font.weight: Font.DemiBold
                            color: ThemeManager.secondaryTextColor
                            Layout.topMargin: Spacing.xs
                        }

                        // GridLayout с фиксированной шириной ячейки.
                        Flow {
                            Layout.fillWidth: true
                            spacing: 4

                            Repeater {
                                model: root._filtered(parent.parent.modelData)

                                delegate: Rectangle {
                                    required property string modelData
                                    readonly property string _full: "projects/" + modelData
                                    readonly property bool _selected: root.selectedIcon === _full

                                    width: 40
                                    height: 40
                                    radius: Radius.sm
                                    color: _selected
                                           ? Qt.rgba(ThemeManager.accentColor.r,
                                                     ThemeManager.accentColor.g,
                                                     ThemeManager.accentColor.b, 0.18)
                                           : iconCellMouse.containsMouse
                                             ? ThemeManager.hoverColor
                                             : "transparent"
                                    border.color: _selected
                                                  ? ThemeManager.accentColor
                                                  : "transparent"
                                    border.width: _selected ? 1 : 0

                                    Behavior on color { ColorAnimation { duration: 100 } }

                                    IconSvg {
                                        anchors.centerIn: parent
                                        name: parent._full
                                        size: 22
                                        color: parent._selected
                                               ? ThemeManager.accentColor
                                               : ThemeManager.textColor
                                    }

                                    MouseArea {
                                        id: iconCellMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.selectedIcon = parent._full

                                        ToolTip.visible: containsMouse
                                        ToolTip.delay: 600
                                        ToolTip.text: parent.modelData
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── Текущий выбор (footer-подсказка) ──────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Spacing.xs
            spacing: Spacing.sm

            Text {
                text: qsTr("Выбрано:")
                color: ThemeManager.secondaryTextColor
                font.pixelSize: FontSize.caption
            }
            IconSvg {
                visible: root.selectedIcon !== ""
                name: root.selectedIcon
                size: 18
                color: ThemeManager.accentColor
            }
            Text {
                text: root.selectedIcon !== ""
                      ? root.selectedIcon.replace("projects/", "")
                      : qsTr("без иконки")
                color: ThemeManager.textColor
                font.pixelSize: FontSize.caption
                font.family: "Consolas, ui-monospace, Menlo, monospace"
            }
            Item { Layout.fillWidth: true }
        }
    }
}
