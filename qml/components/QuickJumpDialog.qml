// TODO CMakeLists.txt: register qml/components/QuickJumpDialog.qml in QML_FILES
import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI
import QtCore

Popup {
    id: root

    width: Math.min(560, parent.width - 80)
    height: Math.min(440, parent.height - 80)
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

    property var _results: []
    property int _selectedIndex: 0

    Settings {
        id: jumpSettings
        category: "QuickJumpDialog"
        property string lastQuery: ""
    }

    onOpened: {
        searchInput.text = jumpSettings.lastQuery
        _refresh()
        Qt.callLater(function() {
            searchInput.forceActiveFocus()
            searchInput.selectAll()
        })
    }

    function _refresh() {
        const q = searchInput.text.trim()
        if (q.length === 0) {
            _results = []
            _selectedIndex = 0
            return
        }
        // Use existing DataStore search (returns Task* list)
        const tasks = dataStore.searchTasks(q)
        const out = []
        for (let i = 0; i < tasks.length && i < 50; i++) {
            const t = tasks[i]
            if (!t || t.isDeleted) continue
            out.push({
                id: t.id,
                content: t.content || "",
                projectId: t.projectId || "",
                isCompleted: t.isCompleted
            })
        }
        _results = out
        _selectedIndex = 0
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

                Text {
                    text: "🔎"
                    font.pixelSize: 18
                }

                TextField {
                    id: searchInput
                    Layout.fillWidth: true
                    placeholderText: qsTr("Перейти к задаче…")
                    background: Item {}
                    color: ThemeManager.textColor
                    font.pixelSize: 16

                    onTextChanged: {
                        root._refresh()
                        // Debounce persistence — only save when ≥3 chars or empty
                        if (text.length === 0 || text.length >= 3)
                            jumpSettings.lastQuery = text
                    }

                    Keys.onUpPressed: {
                        if (root._selectedIndex > 0) root._selectedIndex--
                    }
                    Keys.onDownPressed: {
                        if (root._selectedIndex < root._results.length - 1)
                            root._selectedIndex++
                    }
                    Keys.onReturnPressed: root._activateSelected()
                    Keys.onEnterPressed:  root._activateSelected()
                    Keys.onEscapePressed: {
                        if (text === "")
                            jumpSettings.lastQuery = ""
                        root.close()
                    }
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

        // Results list
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root._results
            currentIndex: root._selectedIndex
            highlightMoveDuration: 0

            delegate: Rectangle {
                width: ListView.view.width
                height: 36
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

                    Text {
                        text: modelData.isCompleted ? "✓" : "○"
                        color: modelData.isCompleted
                            ? "#2e8b57"
                            : ThemeManager.secondaryTextColor
                        font.pixelSize: FontSize.body
                    }
                    Text {
                        Layout.fillWidth: true
                        text: modelData.content
                        color: ThemeManager.textColor
                        font.strikeout: modelData.isCompleted
                        elide: Text.ElideRight
                        font.pixelSize: FontSize.body
                    }
                    Text {
                        visible: {
                            const p = projectViewModel.projectById(modelData.projectId)
                            return p ? true : false
                        }
                        text: {
                            const p = projectViewModel.projectById(modelData.projectId)
                            return p ? "  #" + p.name : ""
                        }
                        color: ThemeManager.secondaryTextColor
                        font.pixelSize: FontSize.caption
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true
                    onEntered: root._selectedIndex = index
                    onClicked: root._activateSelected()
                }
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
                text: "↑ ↓ — навигация · Enter — открыть · Esc — закрыть"
                font.pixelSize: FontSize.caption
                color: ThemeManager.secondaryTextColor
            }
        }
    }

    function _activateSelected() {
        if (root._selectedIndex < 0 || root._selectedIndex >= root._results.length) return
        const sel = root._results[root._selectedIndex]
        if (sel && sel.id) {
            taskViewModel.openTaskById(sel.id)
            root.close()
        }
    }
}
