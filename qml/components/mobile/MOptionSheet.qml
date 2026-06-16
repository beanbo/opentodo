import QtQuick
import QtQuick.Controls
import OpenTodoUI

// MOptionSheet — generic selection sheet. Ported from m-pickers.jsx
// MOptionSheet. `options` is an array of { id, label, sub, icon, color }.
// Single-select (currentId) or multi-select (selectedIds + multi). Emits
// selected(id) on tap. Optional «Готово» footer for multi-select.
MSheet {
    id: root

    property string title: ""
    property string subtitle: ""
    property var    options: []
    property string currentId: ""
    property var    selectedIds: []
    property bool   multi: false
    property bool   showDone: false
    signal selected(string id)

    function _isOn(id) {
        return root.multi ? (root.selectedIds.indexOf(id) >= 0) : (root.currentId === id)
    }

    // Header
    Item {
        width: parent.width
        height: (root.title !== "" || root.subtitle !== "") ? hdr.implicitHeight + 14 : 0
        visible: root.title !== "" || root.subtitle !== ""
        Column {
            id: hdr
            anchors.left: parent.left; anchors.right: parent.right
            anchors.leftMargin: 20; anchors.rightMargin: 20
            anchors.top: parent.top; anchors.topMargin: 4
            spacing: 4
            Text {
                visible: root.title !== ""
                text: root.title
                font.pixelSize: 18; font.weight: Font.Bold
                color: ThemeManager.textColor
            }
            Text {
                visible: root.subtitle !== ""
                width: parent.width; wrapMode: Text.WordWrap
                text: root.subtitle
                font.pixelSize: 13; color: ThemeManager.secondaryTextColor
            }
        }
    }

    // Options (scrollable, capped at 380)
    Item {
        width: parent.width
        height: Math.min(optCol.implicitHeight, 380)
        Flickable {
            anchors.fill: parent
            contentHeight: optCol.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            Column {
                id: optCol
                width: parent.width
                leftPadding: 10; rightPadding: 10; topPadding: 2; bottomPadding: 8

                Repeater {
                    model: root.options
                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool on: root._isOn(modelData.id)
                        width: parent.width - 20
                        height: Math.max(54, lbl.implicitHeight + 16)
                        radius: 14
                        color: on ? ThemeManager.accentContainer
                               : (rowMa.pressed ? ThemeManager.hoverColor : "transparent")
                        Behavior on color { ColorAnimation { duration: Duration.fast } }

                        Rectangle {  // icon tile
                            id: tile
                            visible: !!modelData.icon
                            anchors.left: parent.left; anchors.leftMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            width: 34; height: 34; radius: 10
                            color: {
                                var c = modelData.color || ThemeManager.secondaryTextColor
                                return Qt.rgba(Qt.color(c).r, Qt.color(c).g, Qt.color(c).b, 0.12)
                            }
                            IconSvg {
                                anchors.centerIn: parent
                                name: modelData.icon || ""
                                size: 18
                                color: modelData.color || ThemeManager.secondaryTextColor
                            }
                        }

                        Column {
                            anchors.left: tile.visible ? tile.right : parent.left
                            anchors.leftMargin: tile.visible ? 14 : 16
                            anchors.right: checkIc.left; anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Text {
                                id: lbl
                                width: parent.width
                                text: modelData.label || ""
                                elide: Text.ElideRight
                                font.pixelSize: 15
                                font.weight: parent.parent.on ? Font.Bold : Font.Medium
                                color: parent.parent.on ? ThemeManager.accentColor : ThemeManager.textColor
                            }
                            Text {
                                visible: !!modelData.sub
                                width: parent.width
                                text: modelData.sub || ""
                                elide: Text.ElideRight
                                font.pixelSize: 12
                                color: ThemeManager.secondaryTextColor
                            }
                        }

                        IconSvg {
                            id: checkIc
                            visible: parent.on
                            anchors.right: parent.right; anchors.rightMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            name: "check"; size: 18; color: ThemeManager.accentColor
                        }

                        MouseArea {
                            id: rowMa
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.selected(modelData.id)
                        }
                    }
                }
            }
        }
    }

    // Optional Готово button (multi-select)
    Item {
        width: parent.width
        height: root.showDone ? 66 : 12
        Rectangle {
            visible: root.showDone
            anchors.left: parent.left; anchors.right: parent.right
            anchors.leftMargin: 16; anchors.rightMargin: 16
            anchors.top: parent.top; anchors.topMargin: 4
            height: 50; radius: 14
            color: ThemeManager.accentColor
            Text {
                anchors.centerIn: parent
                text: qsTr("Готово")
                color: ThemeManager.accentForeground
                font.pixelSize: 15; font.weight: Font.Bold
            }
            MouseArea { anchors.fill: parent; onClicked: root.closeRequested() }
        }
    }
}
