import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtCore
import OpenTodoUI

// SettingsSectionDefer — пять настраиваемых длительностей «Отложить»,
// используемых пунктом контекстного меню задачи и баннером напоминаний.
// Хранение через Qt.labs.settings (категория deferDurations) — отдельно
// от SettingsViewModel, чтобы не трогать остальные слои.
Rectangle {
    id: root
    radius: 8
    color: ThemeManager.surfaceColor
    border.color: ThemeManager.borderColor
    border.width: 1
    implicitHeight: sectionHeader.height + (expanded ? sectionContent.implicitHeight : 0)
    clip: true

    property bool expanded: false

    Behavior on implicitHeight {
        NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
    }

    // ── Persisted slots (defaults match historical fixed values) ──────────────
    Settings {
        id: deferSettings
        category: "deferDurations"
        property int slot1: 5
        property int slot2: 15
        property int slot3: 30
        property int slot4: 60
        property int slot5: 240
    }

    // ── Header ────────────────────────────────────────────────────────────────
    Rectangle {
        id: sectionHeader
        width: parent.width
        height: 48
        color: "transparent"
        radius: 8

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20

            Text {
                text: qsTr("Длительность отложенного")
                font.pixelSize: FontSize.body
                font.weight: Font.DemiBold
                color: ThemeManager.textColor
                Layout.fillWidth: true
            }

            // Q4.01 (redesign): "▾"/"▸" → stroke chevron IconSvg
            IconSvg {
                name: root.expanded ? "chevron-down" : "chevron-right"
                size: 14
                color: ThemeManager.secondaryTextColor
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            height: 1
            color: ThemeManager.borderColor
            visible: root.expanded
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.expanded = !root.expanded
        }
    }

    // ── Content ───────────────────────────────────────────────────────────────
    ColumnLayout {
        id: sectionContent
        anchors.top: sectionHeader.bottom
        width: parent.width
        spacing: 0

        // Caption row (grey hint about where these values are used)
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: captionText.implicitHeight + 16
            color: "transparent"

            Text {
                id: captionText
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                text: qsTr("При нажатии Snooze в напоминании или меню задачи")
                font.pixelSize: 12
                color: ThemeManager.secondaryTextColor
                wrapMode: Text.WordWrap
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                height: 1
                color: ThemeManager.borderColor
            }
        }

        // ── Slot 1 ────────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 16

                Text {
                    text: qsTr("Отложить #1 (минут)")
                    font.pixelSize: FontSize.bodySmall
                    color: ThemeManager.textColor
                    Layout.fillWidth: true
                }

                SpinBox {
                    id: spin1
                    from: 1
                    to: 10080
                    stepSize: 5
                    editable: true
                    value: deferSettings.slot1
                    implicitWidth: 130
                    Layout.preferredWidth: 130
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    onValueModified: deferSettings.slot1 = value

                    contentItem: TextInput {
                        z: 2
                        text: spin1.displayText
                        font.pixelSize: 13
                        color: ThemeManager.textColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        readOnly: !spin1.editable
                        validator: spin1.validator
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                    }
                    background: Rectangle {
                        radius: 4
                        color: ThemeManager.hoverColor
                        border.color: ThemeManager.borderColor
                        border.width: 1
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                height: 1
                color: ThemeManager.borderColor
            }
        }

        // ── Slot 2 ────────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 16

                Text {
                    text: qsTr("Отложить #2 (минут)")
                    font.pixelSize: FontSize.bodySmall
                    color: ThemeManager.textColor
                    Layout.fillWidth: true
                }

                SpinBox {
                    id: spin2
                    from: 1
                    to: 10080
                    stepSize: 5
                    editable: true
                    value: deferSettings.slot2
                    implicitWidth: 130
                    Layout.preferredWidth: 130
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    onValueModified: deferSettings.slot2 = value

                    contentItem: TextInput {
                        z: 2
                        text: spin2.displayText
                        font.pixelSize: 13
                        color: ThemeManager.textColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        readOnly: !spin2.editable
                        validator: spin2.validator
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                    }
                    background: Rectangle {
                        radius: 4
                        color: ThemeManager.hoverColor
                        border.color: ThemeManager.borderColor
                        border.width: 1
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                height: 1
                color: ThemeManager.borderColor
            }
        }

        // ── Slot 3 ────────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 16

                Text {
                    text: qsTr("Отложить #3 (минут)")
                    font.pixelSize: FontSize.bodySmall
                    color: ThemeManager.textColor
                    Layout.fillWidth: true
                }

                SpinBox {
                    id: spin3
                    from: 1
                    to: 10080
                    stepSize: 5
                    editable: true
                    value: deferSettings.slot3
                    implicitWidth: 130
                    Layout.preferredWidth: 130
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    onValueModified: deferSettings.slot3 = value

                    contentItem: TextInput {
                        z: 2
                        text: spin3.displayText
                        font.pixelSize: 13
                        color: ThemeManager.textColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        readOnly: !spin3.editable
                        validator: spin3.validator
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                    }
                    background: Rectangle {
                        radius: 4
                        color: ThemeManager.hoverColor
                        border.color: ThemeManager.borderColor
                        border.width: 1
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                height: 1
                color: ThemeManager.borderColor
            }
        }

        // ── Slot 4 ────────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 16

                Text {
                    text: qsTr("Отложить #4 (минут)")
                    font.pixelSize: FontSize.bodySmall
                    color: ThemeManager.textColor
                    Layout.fillWidth: true
                }

                SpinBox {
                    id: spin4
                    from: 1
                    to: 10080
                    stepSize: 5
                    editable: true
                    value: deferSettings.slot4
                    implicitWidth: 130
                    Layout.preferredWidth: 130
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    onValueModified: deferSettings.slot4 = value

                    contentItem: TextInput {
                        z: 2
                        text: spin4.displayText
                        font.pixelSize: 13
                        color: ThemeManager.textColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        readOnly: !spin4.editable
                        validator: spin4.validator
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                    }
                    background: Rectangle {
                        radius: 4
                        color: ThemeManager.hoverColor
                        border.color: ThemeManager.borderColor
                        border.width: 1
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                height: 1
                color: ThemeManager.borderColor
            }
        }

        // ── Slot 5 ────────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 16

                Text {
                    text: qsTr("Отложить #5 (минут)")
                    font.pixelSize: FontSize.bodySmall
                    color: ThemeManager.textColor
                    Layout.fillWidth: true
                }

                SpinBox {
                    id: spin5
                    from: 1
                    to: 10080
                    stepSize: 5
                    editable: true
                    value: deferSettings.slot5
                    implicitWidth: 130
                    Layout.preferredWidth: 130
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    onValueModified: deferSettings.slot5 = value

                    contentItem: TextInput {
                        z: 2
                        text: spin5.displayText
                        font.pixelSize: 13
                        color: ThemeManager.textColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        readOnly: !spin5.editable
                        validator: spin5.validator
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                    }
                    background: Rectangle {
                        radius: 4
                        color: ThemeManager.hoverColor
                        border.color: ThemeManager.borderColor
                        border.width: 1
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                height: 1
                color: ThemeManager.borderColor
            }
        }

        // ── Reset row ─────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 16

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("Сбросить значения")
                    implicitHeight: 34
                    implicitWidth: 170

                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font.pixelSize: 12
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 4
                        color: ThemeManager.accentColor
                    }

                    onClicked: {
                        deferSettings.slot1 = 5
                        deferSettings.slot2 = 15
                        deferSettings.slot3 = 30
                        deferSettings.slot4 = 60
                        deferSettings.slot5 = 240
                    }
                }
            }
        }
    }
}
