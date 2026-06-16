import QtQuick
import QtQuick.Controls
import OpenTodoUI

// MPinSheet — "Закреплённое в нижней панели" management sheet. Ported from
// m-app.jsx MPinSheet: two groups (Умные списки / Проекты) of pin toggles,
// limit 3 (the 4th evicts the oldest), №1–3 numbering. Built on MSheet.
MSheet {
    id: root

    property var pins: []
    signal togglePin(string id)

    readonly property bool full: pins.length >= 3

    // ── Title ────────────────────────────────────────────────────────────
    Item {
        width: parent.width
        height: titleCol.implicitHeight + 12

        Column {
            id: titleCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.top: parent.top
            anchors.topMargin: 4
            spacing: 4

            Text {
                text: qsTr("Закреплённое в нижней панели")
                font.pixelSize: 18
                font.weight: Font.Bold
                color: ThemeManager.textColor
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Слоты для умных списков и проектов · %1/3").arg(root.pins.length)
                      + (root.full ? qsTr(" · новый заменит самый старый") : "")
                font.pixelSize: 13
                color: ThemeManager.secondaryTextColor
            }
        }
    }

    // ── Scrollable list (capped at 400) ──────────────────────────────────
    Item {
        width: parent.width
        height: Math.min(listCol.implicitHeight, 400)

        Flickable {
            anchors.fill: parent
            contentHeight: listCol.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            Column {
                id: listCol
                width: parent.width
                leftPadding: 10
                rightPadding: 10

                Text {
                    leftPadding: 6; topPadding: 10; bottomPadding: 4
                    text: qsTr("Умные списки")
                    font.pixelSize: 11
                    font.weight: Font.Bold
                    font.letterSpacing: 0.4
                    font.capitalization: Font.AllUppercase
                    color: ThemeManager.secondaryTextColor
                }

                Repeater {
                    model: [
                        { id: "sv:today",    name: qsTr("Сегодня"),     icon: "today",    color: ThemeManager.accentColor },
                        { id: "sv:upcoming", name: qsTr("Предстоящие"), icon: "upcoming", color: ThemeManager.priorityP3Color }
                    ]
                    delegate: MPinRow {
                        required property var modelData
                        rowId: modelData.id
                        rowName: modelData.name
                        rowIconName: modelData.icon
                        rowColor: modelData.color
                        pinned: root.pins.indexOf(modelData.id) >= 0
                        pinIndex: root.pins.indexOf(modelData.id)
                        onToggled: root.togglePin(modelData.id)
                    }
                }

                Text {
                    leftPadding: 6; topPadding: 12; bottomPadding: 4
                    text: qsTr("Проекты")
                    font.pixelSize: 11
                    font.weight: Font.Bold
                    font.letterSpacing: 0.4
                    font.capitalization: Font.AllUppercase
                    color: ThemeManager.secondaryTextColor
                }

                Repeater {
                    model: projectViewModel
                    delegate: MPinRow {
                        required property var projectObject
                        required property string id
                        visible: projectObject && !projectObject.isInbox && !projectObject.isArchived
                        height: visible ? 54 : 0
                        rowId: id
                        rowName: projectObject ? projectObject.name : ""
                        rowIconName: (projectObject && projectObject.icon && projectObject.icon.length > 0)
                                     ? projectObject.icon : "hash"
                        rowColor: (projectObject && projectObject.color && projectObject.color.length > 0)
                                  ? projectObject.color : ThemeManager.accentColor
                        pinned: root.pins.indexOf(id) >= 0
                        pinIndex: root.pins.indexOf(id)
                        onToggled: root.togglePin(id)
                    }
                }
            }
        }
    }

    Item { width: parent.width; height: 14 }
}
