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
                version: qsTr("Версия 1.8 — Май 2026"),
                entries: [
                    qsTr("Корзина задач — восстановление случайно удалённых и удаление навсегда"),
                    qsTr("Дублирование задач одним кликом"),
                    qsTr("Перемещение задачи в начало или конец списка"),
                    qsTr("Перемещение задачи в другой проект (Ctrl+M)"),
                    qsTr("Подсветка задач цветом проекта в общем списке"),
                    qsTr("Прогресс выполнения по подзадачам"),
                    qsTr("Закреплённые задачи в боковой панели для быстрого доступа"),
                    qsTr("Счётчик просроченных задач"),
                    qsTr("Режим чтения для длинных описаний"),
                    qsTr("Конфигурируемое время ежедневного обзора")
                ]
            },
            {
                version: qsTr("Версия 1.7 — Апрель 2026"),
                entries: [
                    qsTr("Тема «Высокая контрастность» для людей со слабым зрением"),
                    qsTr("Свой цвет акцента — настройка под себя"),
                    qsTr("Pomodoro со статистикой и графиками по дням"),
                    qsTr("Прогресс выполнения по проектам"),
                    qsTr("Vim-навигация: g+i, g+t, g+u и другие сочетания"),
                    qsTr("Горячие клавиши приоритета 1–4 в редакторе задачи"),
                    qsTr("Напоминания «До утра» и «До выходных»"),
                    qsTr("Авто-применение меток проекта при создании задачи"),
                    qsTr("AI-подсказки для приоритета и описания задачи"),
                    qsTr("Экспорт данных в CSV, JSON и Markdown")
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
                    text: qsTr("✨ Что нового в OpenTodo")
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

                    // Task 17cea65b iter-3: было unicode "✕".
                    contentItem: IconSvg {
                        name: "close"
                        size: 16
                        color: ThemeManager.secondaryTextColor
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
