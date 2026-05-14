// TODO CMakeLists.txt: register qml/components/WhatsNewDialog.qml in QML_FILES
import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

Popup {
    id: root

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    width: parent ? parent.width * 0.7 : 600
    height: parent ? parent.height * 0.8 : 500
    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: ThemeManager.surfaceColor
        border.color: ThemeManager.borderColor
        border.width: 1
        radius: Radius.lg
    }

    // Static changelog data — populated below in Component.onCompleted to keep
    // the long Russian string list out of the property declarations.
    property var sections: []

    Component.onCompleted: {
        sections = [
            {
                version: "Версия 1.8 — Май 2026",
                entries: [
                    "Корзина задач — восстановление случайно удалённых и удаление навсегда",
                    "Дублирование задач одним кликом",
                    "Перемещение задачи в начало или конец списка",
                    "Перемещение задачи в другой проект (Ctrl+M)",
                    "Подсветка задач цветом проекта в общем списке",
                    "Прогресс выполнения по подзадачам",
                    "Закреплённые задачи в боковой панели для быстрого доступа",
                    "Счётчик просроченных задач",
                    "Режим чтения для длинных описаний",
                    "Конфигурируемое время ежедневного обзора"
                ]
            },
            {
                version: "Версия 1.7 — Апрель 2026",
                entries: [
                    "Тема «Высокая контрастность» для людей со слабым зрением",
                    "Свой цвет акцента — настройка под себя",
                    "Pomodoro со статистикой и графиками по дням",
                    "Прогресс выполнения по проектам",
                    "Vim-навигация: g+i, g+t, g+u и другие сочетания",
                    "Горячие клавиши приоритета 1–4 в редакторе задачи",
                    "Напоминания «До утра» и «До выходных»",
                    "Авто-применение меток проекта при создании задачи",
                    "AI-подсказки для приоритета и описания задачи",
                    "Экспорт данных в CSV, JSON и Markdown"
                ]
            }
        ]
    }

    contentItem: ColumnLayout {
        spacing: 0
        anchors.fill: parent

        // ── Header ────────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Spacing.lg
                anchors.rightMargin: Spacing.sm
                spacing: Spacing.sm

                Label {
                    text: "✨ Что нового в OpenTodo"
                    font.pixelSize: FontSize.title
                    font.bold: true
                    color: ThemeManager.textColor
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                Button {
                    flat: true
                    implicitWidth: 36
                    implicitHeight: 36

                    contentItem: Text {
                        text: "✕"
                        font.pixelSize: FontSize.subtitle
                        color: ThemeManager.secondaryTextColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: Radius.sm
                        color: parent.hovered ? ThemeManager.hoverColor : "transparent"
                    }

                    onClicked: root.close()
                }
            }

            // Underline
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: ThemeManager.borderColor
            }
        }

        // ── Body: scrollable list of version sections ─────────────────────────
        ListView {
            id: sectionsList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.sections
            spacing: Spacing.lg
            topMargin: Spacing.lg
            bottomMargin: Spacing.lg
            leftMargin: Spacing.lg
            rightMargin: Spacing.lg
            boundsBehavior: Flickable.StopAtBounds

            delegate: ColumnLayout {
                width: sectionsList.width - (sectionsList.leftMargin + sectionsList.rightMargin)
                spacing: Spacing.sm

                // Version heading
                Label {
                    text: modelData.version
                    font.pixelSize: FontSize.subtitle
                    font.bold: true
                    color: ThemeManager.accentColor
                    Layout.fillWidth: true
                }

                // Divider under the version heading
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: ThemeManager.borderColor
                }

                // Bullet list of entries
                Repeater {
                    model: modelData.entries

                    delegate: RowLayout {
                        Layout.fillWidth: true
                        spacing: Spacing.sm

                        Label {
                            text: "•"
                            font.pixelSize: FontSize.body
                            color: ThemeManager.accentColor
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 2
                        }

                        Label {
                            text: modelData
                            font.pixelSize: FontSize.body
                            color: ThemeManager.textColor
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }
    }
}
