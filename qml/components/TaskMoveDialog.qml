import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

Popup {
    id: root

    width: Math.min(560, parent ? parent.width - 80 : 560)
    height: Math.min(440, parent ? parent.height - 80 : 440)
    anchors.centerIn: Overlay.overlay
    parent: Overlay.overlay
    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    background: Rectangle {
        color: ThemeManager.surfaceColor
        border.color: ThemeManager.borderColor
        border.width: 1
        radius: Radius.md
    }

    // Snapshot taken on open() — list does not change while user is searching
    property var _allProjects: []
    property var _results: []
    property int _selectedIndex: 0

    onOpened: {
        // Snapshot active (non-deleted, non-archived) projects once
        const snapshot = []
        if (typeof dataStore !== "undefined"
                && dataStore
                && typeof dataStore.allProjects === "function") {
            const all = dataStore.allProjects()
            for (let i = 0; i < all.length; i++) {
                const p = all[i]
                if (!p) continue
                if (p.isDeleted || p.isArchived) continue
                snapshot.push(p)
            }
        }
        _allProjects = snapshot
        searchInput.text = ""
        _refresh()
        Qt.callLater(function() {
            searchInput.forceActiveFocus()
            searchInput.selectAll()
        })
    }

    onClosed: {
        _allProjects = []
        _results = []
        _selectedIndex = 0
    }

    function _refresh() {
        const q = searchInput.text.toLowerCase().trim()
        const out = []
        for (let i = 0; i < _allProjects.length; i++) {
            const p = _allProjects[i]
            if (!p) continue
            const name = (p.name || "")
            if (q === "" || name.toLowerCase().indexOf(q) !== -1)
                out.push(p)
        }
        _results = out
        _selectedIndex = out.length > 0 ? 0 : -1
    }

    function _commit() {
        if (_selectedIndex < 0 || _selectedIndex >= _results.length) return
        const target = _results[_selectedIndex]
        if (!target || !target.id) return
        const projectId = target.id

        // Selection-mode: bulk move all selected tasks.
        // Single-open-editor mode: move just the open task.
        let count = 0
        if (typeof taskViewModel !== "undefined" && taskViewModel) {
            if (taskViewModel.selectionCount > 0) {
                if (typeof taskViewModel.bulkMoveToProject === "function") {
                    count = taskViewModel.selectionCount
                    taskViewModel.bulkMoveToProject(projectId)
                } else {
                    const ids = taskViewModel.selectedIds || []
                    for (let i = 0; i < ids.length; i++) {
                        if (typeof taskViewModel.moveTask === "function")
                            taskViewModel.moveTask(ids[i], projectId, "")
                        else if (typeof taskViewModel.updateTask === "function")
                            taskViewModel.updateTask(ids[i], "project_id", projectId)
                        count++
                    }
                }
            } else if (taskViewModel.selectedTask) {
                const t = taskViewModel.selectedTask
                const tid = t.id
                if (tid) {
                    if (typeof taskViewModel.moveTask === "function")
                        taskViewModel.moveTask(tid, projectId, "")
                    else if (typeof taskViewModel.updateTask === "function")
                        taskViewModel.updateTask(tid, "project_id", projectId)
                    count = 1
                }
            }
        }

        if (count > 0
                && typeof telemetry !== "undefined"
                && telemetry
                && typeof telemetry.trackEvent === "function") {
            telemetry.trackEvent("task_quick_move", { "count": count, "project_id": projectId })
        }

        root.close()
    }

    contentItem: ColumnLayout {
        spacing: 0

        // Search input
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Spacing.md
                anchors.rightMargin: Spacing.md
                spacing: Spacing.sm

                // Task 17cea65b iter-3: было unicode "📁".
                IconSvg {
                    name: "folder"
                    size: 18
                    color: ThemeManager.secondaryTextColor
                }

                TextField {
                    id: searchInput
                    Layout.fillWidth: true
                    placeholderText: qsTr("Поиск проекта…")
                    background: Item {}
                    color: ThemeManager.textColor
                    font.pixelSize: 16

                    onTextChanged: root._refresh()

                    Keys.onUpPressed: {
                        if (root._results.length === 0) return
                        if (root._selectedIndex > 0) root._selectedIndex--
                        else root._selectedIndex = root._results.length - 1
                    }
                    Keys.onDownPressed: {
                        if (root._results.length === 0) return
                        if (root._selectedIndex < root._results.length - 1)
                            root._selectedIndex++
                        else
                            root._selectedIndex = 0
                    }
                    Keys.onReturnPressed: root._commit()
                    Keys.onEnterPressed:  root._commit()
                    Keys.onEscapePressed: root.close()
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: ThemeManager.borderColor
            }
        }

        // Header line: how many tasks will move
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 26
            color: Qt.rgba(ThemeManager.textColor.r,
                           ThemeManager.textColor.g,
                           ThemeManager.textColor.b, 0.04)
            visible: _moveCountText.text.length > 0

            Text {
                id: _moveCountText
                anchors.left: parent.left
                anchors.leftMargin: Spacing.md
                anchors.verticalCenter: parent.verticalCenter
                color: ThemeManager.secondaryTextColor
                font.pixelSize: FontSize.caption
                text: {
                    if (typeof taskViewModel === "undefined" || !taskViewModel) return ""
                    if (taskViewModel.selectionCount > 0)
                        return qsTr("Переместить выделенных задач: %1").arg(taskViewModel.selectionCount)
                    if (taskViewModel.selectedTask)
                        return qsTr("Переместить открытую задачу")
                    return ""
                }
            }
        }

        // Results list
        ListView {
            id: resultsList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root._results
            currentIndex: root._selectedIndex
            highlightMoveDuration: 0
            keyNavigationEnabled: false  // arrow keys handled by TextField

            delegate: Rectangle {
                required property var modelData
                required property int index
                width: ListView.view.width
                height: 40
                color: index === root._selectedIndex
                    ? Qt.rgba(ThemeManager.accentColor.r,
                              ThemeManager.accentColor.g,
                              ThemeManager.accentColor.b, 0.20)
                    : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Spacing.md
                    anchors.rightMargin: Spacing.md
                    spacing: Spacing.sm

                    // Project icon emoji (or coloured dot fallback)
                    Item {
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        Layout.alignment: Qt.AlignVCenter

                        Text {
                            anchors.centerIn: parent
                            visible: !!modelData.icon && modelData.icon.length > 0
                            text: modelData.icon || ""
                            font.pixelSize: FontSize.body
                        }
                        Rectangle {
                            anchors.centerIn: parent
                            visible: !modelData.icon || modelData.icon.length === 0
                            width: 10
                            height: 10
                            radius: 5
                            color: modelData.color || ThemeManager.accentColor
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: modelData.name || ""
                        color: ThemeManager.textColor
                        font.pixelSize: FontSize.body
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }

                    Text {
                        text: {
                            if (typeof projectViewModel !== "undefined"
                                    && projectViewModel
                                    && typeof projectViewModel.taskCountForProject === "function") {
                                return projectViewModel.taskCountForProject(modelData.id)
                            }
                            return ""
                        }
                        color: ThemeManager.secondaryTextColor
                        font.pixelSize: FontSize.caption
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true
                    onEntered: root._selectedIndex = index
                    onClicked: root._commit()
                }
            }

            // Empty state
            Text {
                anchors.centerIn: parent
                visible: root._results.length === 0
                text: searchInput.text.trim().length === 0
                      ? qsTr("Нет доступных проектов")
                      : qsTr("Проекты не найдены")
                color: ThemeManager.secondaryTextColor
                font.pixelSize: FontSize.body
            }
        }

        // Footer hint
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            color: Qt.rgba(ThemeManager.textColor.r,
                           ThemeManager.textColor.g,
                           ThemeManager.textColor.b, 0.04)

            Text {
                anchors.centerIn: parent
                text: "↑ ↓ — навигация · Enter — переместить · Esc — отмена"
                font.pixelSize: FontSize.caption
                color: ThemeManager.secondaryTextColor
            }
        }
    }
}
