import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

// Editor for the first-class comment author identities ("me" + "Claude").
// Each identity's display name and avatar colour are user-configurable and
// persist in the synced settings blob (see DataStore's --- Collaborators ---).
Rectangle {
    id: root
    radius: 8
    color: ThemeManager.surfaceColor
    border.color: ThemeManager.borderColor
    border.width: 1
    implicitHeight: sectionHeader.height + (expanded ? sectionContent.implicitHeight : 0)
    clip: true

    property bool expanded: true

    // Resolved [{id,name,color,icon,assistant}] rows; rebuilt on any edit.
    property var _rows: dataStore.collaborators()
    Connections {
        target: dataStore
        ignoreUnknownSignals: true
        function onCollaboratorsChanged() { root._rows = dataStore.collaborators() }
    }

    // Avatar-colour palette offered per identity. "" = follow the theme accent.
    readonly property var _swatches: [
        "", "#e44332", "#f59e0b", "#16a34a", "#2563eb", "#7c3aed", "#db2777", "#0891b2"
    ]

    function _previewColor(row) {
        if (row.color && row.color !== "")
            return row.color
        return row.assistant ? Qt.darker(ThemeManager.accentColor, 1.35)
                             : ThemeManager.accentColor
    }

    Behavior on implicitHeight {
        NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
    }

    // ── Header ──────────────────────────────────────────────────────────────
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

            IconSvg {
                name: "sparkles"
                size: 16
                color: ThemeManager.accentColor
                Layout.alignment: Qt.AlignVCenter
            }
            Text {
                text: qsTr("Профиль и подпись комментариев")
                font.pixelSize: FontSize.body
                font.weight: Font.DemiBold
                color: ThemeManager.textColor
                Layout.fillWidth: true
            }
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

    // ── Content ─────────────────────────────────────────────────────────────
    ColumnLayout {
        id: sectionContent
        anchors.top: sectionHeader.bottom
        width: parent.width
        spacing: 0

        Repeater {
            model: root._rows
            delegate: Rectangle {
                id: rowItem
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredHeight: rowContent.implicitHeight + 24
                color: "transparent"

                ColumnLayout {
                    id: rowContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    anchors.topMargin: 12
                    spacing: 10

                    // Avatar preview + name field
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Rectangle {
                            width: 36; height: 36; radius: 18
                            color: root._previewColor(rowItem.modelData)
                            Layout.alignment: Qt.AlignVCenter

                            IconSvg {
                                anchors.centerIn: parent
                                visible: rowItem.modelData.icon !== ""
                                name: rowItem.modelData.icon
                                size: 18
                                color: ThemeManager.accentForeground
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: rowItem.modelData.icon === ""
                                text: nameField.text.length ? nameField.text.charAt(0).toUpperCase() : "?"
                                font.pixelSize: 15
                                font.bold: true
                                color: ThemeManager.accentForeground
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            TextField {
                                id: nameField
                                Layout.fillWidth: true
                                text: rowItem.modelData.name
                                font.pixelSize: FontSize.bodySmall
                                color: ThemeManager.textColor
                                selectByMouse: true
                                // Commit on focus loss / Enter; empty ⇒ reverts
                                // to the built-in default name.
                                onEditingFinished: {
                                    if (text !== rowItem.modelData.name)
                                        dataStore.setCollaboratorProfile(
                                            rowItem.modelData.id, text,
                                            rowItem.modelData.color, rowItem.modelData.icon)
                                }
                            }
                            Text {
                                text: rowItem.modelData.assistant
                                      ? qsTr("Комментарии от ассистента (Claude, CLI-мост, автозаметки)")
                                      : qsTr("Ваши комментарии")
                                font.pixelSize: FontSize.caption
                                color: ThemeManager.secondaryTextColor
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }
                    }

                    // Avatar colour swatches
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8

                        Repeater {
                            model: root._swatches
                            delegate: Rectangle {
                                required property string modelData
                                required property int index
                                readonly property bool isDefault: modelData === ""
                                readonly property bool isSelected:
                                    (rowItem.modelData.color || "") === modelData
                                width: 26; height: 26; radius: 13
                                color: isDefault
                                       ? (rowItem.modelData.assistant
                                          ? Qt.darker(ThemeManager.accentColor, 1.35)
                                          : ThemeManager.accentColor)
                                       : modelData
                                border.width: isSelected ? 2 : (isDefault ? 1 : 0)
                                border.color: isSelected ? ThemeManager.textColor
                                                         : ThemeManager.borderColor

                                // "T" marks the theme-default (accent) swatch;
                                // a check marks the currently-selected one.
                                Text {
                                    anchors.centerIn: parent
                                    visible: parent.isDefault && !parent.isSelected
                                    text: qsTr("Т")
                                    font.pixelSize: 11
                                    font.bold: true
                                    color: ThemeManager.accentForeground
                                }
                                IconSvg {
                                    anchors.centerIn: parent
                                    visible: parent.isSelected
                                    name: "check"
                                    size: 14
                                    color: ThemeManager.accentForeground
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: dataStore.setCollaboratorProfile(
                                        rowItem.modelData.id, nameField.text,
                                        parent.modelData, rowItem.modelData.icon)
                                }
                            }
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
                    visible: rowItem.modelData.id !== root._rows[root._rows.length - 1].id
                }
            }
        }

        // Footer explainer
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 10
            Layout.bottomMargin: 14
            text: qsTr("Claude подписывает комментарии, созданные через ассистента или CLI-мост. Ваши комментарии остаются под вашим именем. Имя и цвет синхронизируются между устройствами.")
            font.pixelSize: FontSize.caption
            color: ThemeManager.secondaryTextColor
            wrapMode: Text.WordWrap
        }
    }
}
