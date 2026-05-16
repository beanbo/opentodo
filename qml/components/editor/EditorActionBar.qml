import QtQuick
import QtQuick.Layouts
import OpenTodoUI

// Bottom action bar with Delete and Duplicate buttons.
Rectangle {
    id: root

    property string taskId: ""
    property var    task:   null

    signal deleteRequested()

    // ── Timestamp helpers ────────────────────────────────────────────────────
    // _timestampKick is incremented by the 60-second timer and by onUpdatedAtChanged
    // so that the text: binding re-evaluates whenever time passes or the task
    // is saved.
    property int _timestampKick: 0

    function _wMin(n) {
        const m100 = n % 100, m10 = n % 10
        if (m100 >= 11 && m100 <= 14) return qsTr("минут")
        if (m10 === 1) return qsTr("минуту")
        if (m10 >= 2 && m10 <= 4) return qsTr("минуты")
        return qsTr("минут")
    }
    function _wHour(n) {
        const m100 = n % 100, m10 = n % 10
        if (m100 >= 11 && m100 <= 14) return qsTr("часов")
        if (m10 === 1) return qsTr("час")
        if (m10 >= 2 && m10 <= 4) return qsTr("часа")
        return qsTr("часов")
    }
    function _wDay(n) {
        const m100 = n % 100, m10 = n % 10
        if (m100 >= 11 && m100 <= 14) return qsTr("дней")
        if (m10 === 1) return qsTr("день")
        if (m10 >= 2 && m10 <= 4) return qsTr("дня")
        return qsTr("дней")
    }
    function _relativeRus(iso) {
        if (!iso || iso === "") return ""
        const d = new Date(iso)
        if (isNaN(d.getTime())) return ""
        const now = new Date()
        const diff = (now - d) / 1000   // seconds
        if (diff < 0) return ""
        if (diff < 30) return qsTr("только что")
        if (diff < 60) return qsTr("%1 секунд назад").arg(Math.floor(diff))
        if (diff < 3600) {
            const m = Math.floor(diff / 60)
            return qsTr("%1 %2 назад").arg(m).arg(_wMin(m))
        }
        if (diff < 86400) {
            const h = Math.floor(diff / 3600)
            return qsTr("%1 %2 назад").arg(h).arg(_wHour(h))
        }
        if (diff < 7 * 86400) {
            const dd = Math.floor(diff / 86400)
            return qsTr("%1 %2 назад").arg(dd).arg(_wDay(dd))
        }
        return Qt.formatDate(d, "dd.MM.yyyy")
    }

    // Refresh every minute so "2 минуты назад" stays current even with no edits.
    Timer {
        interval: 60000
        running:  true
        repeat:   true
        onTriggered: root._timestampKick = (root._timestampKick + 1) % 1000
    }

    // Also refresh immediately whenever the task's updatedAt changes (save event).
    Connections {
        target: root.task
        ignoreUnknownSignals: true
        function onUpdatedAtChanged() {
            root._timestampKick = (root._timestampKick + 1) % 1000
        }
    }

    // Height: 52 px for the button row + 28 px for the timestamp footer
    // (visible only when a task with a createdAt is loaded).
    height: root.task && root.task.createdAt ? 80 : 52
    color:  ThemeManager.surfaceColor

    // Top separator
    Rectangle {
        anchors.top:   parent.top
        anchors.left:  parent.left
        anchors.right: parent.right
        height: 1
        color:  ThemeManager.borderColor
    }

    RowLayout {
        anchors {
            fill:        parent
            leftMargin:  Spacing.md
            rightMargin: Spacing.md
        }
        spacing: Spacing.sm

        // Delete button
        Rectangle {
            height: 32
            width:  deleteText.implicitWidth + 24
            radius: Radius.sm
            color:  deleteHover.containsMouse
                    ? ThemeManager.accentColor + "22" : "transparent"
            border.color: ThemeManager.accentColor
            border.width: 1

            Text {
                id: deleteText
                anchors.centerIn: parent
                text:          qsTr("Удалить")
                font.pixelSize: 13
                color:         ThemeManager.accentColor
            }

            MouseArea {
                id: deleteHover
                anchors.fill: parent
                hoverEnabled: true
                cursorShape:  Qt.PointingHandCursor
                onClicked: {
                    if (root.taskId !== "") {
                        taskViewModel.deleteTask(root.taskId)
                        root.deleteRequested()
                    }
                }
            }
        }

        Item { Layout.fillWidth: true }

        // Duplicate button
        Rectangle {
            height: 32
            width:  duplicateText.implicitWidth + 24
            radius: Radius.sm
            color:  dupHover.containsMouse
                    ? ThemeManager.hoverColor : "transparent"
            border.color: ThemeManager.borderColor
            border.width: 1

            Text {
                id: duplicateText
                anchors.centerIn: parent
                text:          qsTr("Дублировать")
                font.pixelSize: 13
                color:         ThemeManager.textColor
            }

            MouseArea {
                id: dupHover
                anchors.fill: parent
                hoverEnabled: true
                cursorShape:  Qt.PointingHandCursor
                onClicked: {
                    if (root.taskId !== "")
                        taskViewModel.duplicateTask(root.taskId)
                }
            }
        }

        // Save button — explicitly flushes any in-progress field edit and
        // shows a brief "Сохранено" toast. Auto-save still happens on field
        // blur, but users coming from Todoist expect a Save button to exist.
        Rectangle {
            height: 32
            width:  saveText.implicitWidth + 28
            radius: Radius.sm
            color:  saveHover.containsMouse
                    ? Qt.darker(ThemeManager.accentColor, 1.1)
                    : ThemeManager.accentColor

            Text {
                id: saveText
                anchors.centerIn: parent
                text:           qsTr("Сохранить")
                font.pixelSize: 13
                font.weight:    Font.Medium
                color:          "#ffffff"
            }

            MouseArea {
                id: saveHover
                anchors.fill: parent
                hoverEnabled: true
                cursorShape:  Qt.PointingHandCursor
                onClicked: {
                    if (root.taskId === "") return
                    // B-7.5: blur previous input first (fires onEditingFinished),
                    // then show the toast in the next event loop tick so autosave
                    // triggered by editingFinished completes before the toast.
                    //
                    // `parent.forceActiveFocus()` used to be called on the
                    // Save-button Rectangle — Rectangle doesn't accept focus,
                    // so the blur didn't fire. Force focus on the action-bar
                    // root (root.forceActiveFocus()) or use Qt.inputMethod.commit
                    // to commit any pending IME composition. The action bar's
                    // root Item DOES take focus, which is enough to trigger
                    // QQuickItem::focusOutEvent on any TextField/TextArea
                    // currently focused inside the editor.
                    root.forceActiveFocus()
                    Qt.callLater(function() {
                        let w = root.Window.window
                        if (w && typeof w.showToast === "function")
                            w.showToast(qsTr("Сохранено"), "success")
                    })
                }
            }
        }
    }

    // ── Timestamp footer ──────────────────────────────────────────────────────
    // Shown below the button row when a task with a known createdAt is loaded.
    // References _timestampKick so the binding re-evaluates on every timer tick
    // and on every save without requiring an explicit refresh call.
    Text {
        id: timestampFooter

        // Position directly under the button row.
        anchors {
            left:        parent.left
            right:       parent.right
            bottom:      parent.bottom
            leftMargin:  Spacing.md
            rightMargin: Spacing.md
            bottomMargin: 6
        }

        visible: root.task !== null && root.task !== undefined &&
                 root.task.createdAt !== undefined && root.task.createdAt !== ""

        text: {
            // Touch _timestampKick so QML tracks it as a dependency and
            // re-evaluates whenever the timer fires or a save happens.
            const _k = root._timestampKick

            if (!root.task || !root.task.createdAt) return ""

            const cs = root._relativeRus(root.task.createdAt)
            if (cs === "") return ""

            const justCreated = (new Date() - new Date(root.task.createdAt)) < 30000
            if (justCreated) return qsTr("Создана %1").arg(cs)

            const ms = root._relativeRus(root.task.updatedAt)
            if (!ms || ms === "" || ms === cs)
                return qsTr("Создана %1").arg(cs)
            return qsTr("Создана %1 · Изменена %2").arg(cs).arg(ms)
        }

        color:              ThemeManager.secondaryTextColor
        font.pixelSize:     FontSize.caption
        horizontalAlignment: Text.AlignRight
        elide:              Text.ElideRight
    }
}
