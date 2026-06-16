import QtQuick
import QtQuick.Controls
import OpenTodoUI

// MSearchScreen — full-screen search. Ported from m-screens2.jsx MSearchScreen:
// back + autofocus field, "Недавнее" recent searches when empty, results as
// task rows when querying. Drives searchViewModel.
Item {
    id: root
    signal closeRequested()
    signal taskClicked(string id)

    MDensity { id: mdens }
    Rectangle { anchors.fill: parent; color: ThemeManager.backgroundColor }

    function focusInput() { field.forceActiveFocus() }
    readonly property string q: searchViewModel.searchQuery

    // ── Search bar ─────────────────────────────────────────────────────────
    Item {
        id: bar
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: 60

        MIconButton {
            id: backBtn
            anchors.left: parent.left; anchors.leftMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            iconName: "chevron-left"; iconColor: ThemeManager.textColor; iconSize: 22
            onClicked: root.closeRequested()
        }
        Rectangle {
            anchors.left: backBtn.right; anchors.leftMargin: 4
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            height: 46; radius: 23
            color: ThemeManager.surfaceColor
            border.width: 1; border.color: ThemeManager.borderColor

            IconSvg { id: si; anchors.left: parent.left; anchors.leftMargin: 16; anchors.verticalCenter: parent.verticalCenter; name: "search"; size: 19; color: ThemeManager.secondaryTextColor }
            TextField {
                id: field
                anchors.left: si.right; anchors.leftMargin: 10
                anchors.right: clearBtn.left; anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                placeholderText: qsTr("Поиск задач, проектов, меток")
                placeholderTextColor: ThemeManager.secondaryTextColor
                font.pixelSize: 15; color: ThemeManager.textColor
                background: Item {}
                text: searchViewModel.searchQuery
                onTextChanged: if (text !== searchViewModel.searchQuery) searchViewModel.search(text)
            }
            Rectangle {
                id: clearBtn
                visible: root.q.length > 0
                anchors.right: parent.right; anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                width: 20; height: 20; radius: 10
                color: ThemeManager.borderColor
                Text { anchors.centerIn: parent; text: "×"; color: ThemeManager.secondaryTextColor; font.pixelSize: 14 }
                MouseArea { anchors.fill: parent; onClicked: { field.text = ""; searchViewModel.clearSearch() } }
            }
        }
    }

    // ── Body ───────────────────────────────────────────────────────────────
    Flickable {
        anchors.top: bar.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        contentHeight: body.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: body
            width: parent.width

            // Recent searches (empty query)
            Column {
                width: parent.width
                visible: root.q.length === 0
                Text {
                    leftPadding: 16; topPadding: 12; bottomPadding: 4
                    text: qsTr("Недавнее")
                    font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 0.3
                    font.capitalization: Font.AllUppercase
                    color: ThemeManager.secondaryTextColor
                }
                Repeater {
                    model: searchViewModel.recentSearches
                    delegate: Rectangle {
                        required property var modelData
                        width: body.width; height: 48
                        color: rma.pressed ? ThemeManager.hoverColor : "transparent"
                        IconSvg { id: rci; anchors.left: parent.left; anchors.leftMargin: 16; anchors.verticalCenter: parent.verticalCenter; name: "clock"; size: 19; color: ThemeManager.secondaryTextColor }
                        Text { anchors.left: rci.right; anchors.leftMargin: 14; anchors.right: parent.right; anchors.rightMargin: 16; anchors.verticalCenter: parent.verticalCenter; text: modelData; font.pixelSize: 15; color: ThemeManager.textColor; elide: Text.ElideRight }
                        MouseArea { id: rma; anchors.fill: parent; onClicked: { field.text = modelData; searchViewModel.search(modelData) } }
                    }
                }
                Text {
                    visible: searchViewModel.recentSearches.length === 0
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    topPadding: 40
                    text: qsTr("Начните вводить запрос")
                    font.pixelSize: 14; color: ThemeManager.secondaryTextColor
                }
            }

            // Results (non-empty query)
            Column {
                width: parent.width
                visible: root.q.length > 0

                Text {
                    visible: searchViewModel.resultCount === 0
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    topPadding: 60
                    text: qsTr("Ничего не найдено по «%1»").arg(root.q)
                    font.pixelSize: 15; color: ThemeManager.secondaryTextColor
                    wrapMode: Text.WordWrap
                }

                Repeater {
                    model: searchViewModel.results
                    delegate: Loader {
                        required property var modelData
                        width: body.width
                        sourceComponent: (modelData.type === "task") ? taskResult : otherResult

                        Component {
                            id: taskResult
                            MTaskRow {
                                readonly property var _tk: dataStore.taskById(modelData.id)
                                width: body.width
                                dens: mdens
                                showProject: true
                                taskId: modelData.id
                                content: _tk ? _tk.content : (modelData.title || "")
                                priority: _tk ? _tk.priority : 4
                                isCompleted: _tk ? _tk.isCompleted : false
                                dueDate: _tk ? _tk.dueDate : ""
                                dueString: _tk ? _tk.dueString : ""
                                isRecurring: _tk ? _tk.dueIsRecurring : false
                                deadline: _tk ? _tk.deadline : ""
                                labels: _tk ? _tk.labels : []
                                projectId: _tk ? _tk.projectId : ""
                                onOpenTask: root.taskClicked(modelData.id)
                                onToggleComplete: { if (_tk) { if (_tk.isCompleted) taskViewModel.uncompleteTask(modelData.id); else taskViewModel.completeTask(modelData.id) } }
                            }
                        }
                        Component {
                            id: otherResult
                            Rectangle {
                                width: body.width; height: 56
                                color: oma.pressed ? ThemeManager.hoverColor : "transparent"
                                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: ThemeManager.hairlineColor }
                                IconSvg {
                                    id: oi
                                    anchors.left: parent.left; anchors.leftMargin: 18; anchors.verticalCenter: parent.verticalCenter
                                    name: modelData.type === "project" ? "hash" : modelData.type === "label" ? "tag" : modelData.type === "filter" ? "filter" : "note"
                                    size: 18; color: ThemeManager.secondaryTextColor
                                }
                                Column {
                                    anchors.left: oi.right; anchors.leftMargin: 14; anchors.right: parent.right; anchors.rightMargin: 16
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 2
                                    Text { width: parent.width; text: modelData.title || ""; font.pixelSize: 15; font.weight: Font.Medium; color: ThemeManager.textColor; elide: Text.ElideRight }
                                    Text { visible: !!modelData.subtitle; width: parent.width; text: modelData.subtitle || ""; font.pixelSize: 12; color: ThemeManager.secondaryTextColor; elide: Text.ElideRight }
                                }
                                MouseArea {
                                    id: oma
                                    anchors.fill: parent
                                    onClicked: {
                                        if (modelData.type === "project" && modelData.id) sidebarViewModel.navigateToProject(modelData.id)
                                        else if (modelData.type === "label" && modelData.title) sidebarViewModel.navigateToLabel(modelData.title)
                                        else if (modelData.type === "filter" && modelData.id) sidebarViewModel.navigateToFilter(modelData.id)
                                        root.closeRequested()
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
