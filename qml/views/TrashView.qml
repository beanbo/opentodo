import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

// Корзина — список недавно удалённых задач (soft-delete) с возможностью
// восстановления и окончательного удаления.  Данные читаются через
// taskViewModel.deletedTasks(); список перестраивается на каждом изменении
// dataStore (taskChanged / dataChanged).
Item {
    id: root
    anchors.fill: parent

    readonly property bool isDarkTheme: ThemeManager.isDark

    property color bgColor:        ThemeManager.backgroundColor
    property color contentColor:   ThemeManager.surfaceColor
    property color borderColor:    ThemeManager.borderColor
    property color textPrimary:    ThemeManager.textColor
    property color textSecondary:  ThemeManager.secondaryTextColor
    property color accentColor:    ThemeManager.accentColor

    // Cached snapshot of taskViewModel.deletedTasks().  Re-fetched whenever the
    // store changes — keeping it as a property avoids re-running the C++ call
    // on every delegate creation.
    property var trashItems: []

    function refresh() {
        root.trashItems = taskViewModel.deletedTasks()
    }

    Component.onCompleted: refresh()

    Connections {
        target: taskViewModel
        function onTaskChanged()    { Qt.callLater(root.refresh) }
        function onTaskRemoved()    { Qt.callLater(root.refresh) }
        function onTasksRemoved()   { Qt.callLater(root.refresh) }
    }
    Connections {
        target: dataStore
        function onTasksChanged()   { Qt.callLater(root.refresh) }
        function onDataChanged()    { Qt.callLater(root.refresh) }
    }

    // ── Russian relative-time helper ──────────────────────────────────────────
    // Returns strings like "сегодня", "вчера", "5 дней назад".  refreshTick
    // is consumed as a binding dependency so QML re-evaluates the bindings
    // every minute when the timer fires.
    Timer {
        id: refreshTimer
        interval: 60000
        running: true
        repeat: true
        property int tick: 0
        onTriggered: tick++
    }

    function _dayWord(n) {
        const m = n % 10, h = n % 100
        if (h >= 11 && h <= 19) return "дней"
        if (m === 1)             return "день"
        if (m >= 2 && m <= 4)   return "дня"
        return "дней"
    }
    function _hourWord(n) {
        const m = n % 10, h = n % 100
        if (h >= 11 && h <= 19) return "часов"
        if (m === 1)             return "час"
        if (m >= 2 && m <= 4)   return "часа"
        return "часов"
    }
    function _minuteWord(n) {
        const m = n % 10, h = n % 100
        if (h >= 11 && h <= 19) return "минут"
        if (m === 1)             return "минуту"
        if (m >= 2 && m <= 4)   return "минуты"
        return "минут"
    }

    function relativeDeletedTime(isoString) {
        void(refreshTimer.tick)   // binding dependency
        if (!isoString || isoString === "") return ""
        const dt   = new Date(isoString)
        if (isNaN(dt.getTime())) return ""
        const diffMs  = Date.now() - dt.getTime()
        if (diffMs < 0) return "только что"
        const diffMin = Math.floor(diffMs / 60000)
        if (diffMin < 1)  return "только что"
        if (diffMin < 60) return diffMin + " " + _minuteWord(diffMin) + " назад"
        const diffH = Math.floor(diffMin / 60)
        if (diffH < 24)   return diffH + " " + _hourWord(diffH) + " назад"
        const diffD = Math.floor(diffH / 24)
        if (diffD === 0)  return "сегодня"
        if (diffD === 1)  return "вчера"
        if (diffD < 30)   return diffD + " " + _dayWord(diffD) + " назад"
        // Fallback: locale date
        return dt.toLocaleDateString(Qt.locale("ru-RU"))
    }

    // Confirmation dialogs ────────────────────────────────────────────────────

    Dialog {
        id: emptyConfirmDialog
        title: "Очистить корзину"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(380, Overlay.overlay.width - Spacing.xl)

        background: Rectangle {
            color: root.contentColor
            radius: Radius.md
            border.color: root.borderColor
            border.width: 1
        }

        Label {
            width: parent.width
            text: "Окончательно удалить все задачи из корзины? Действие нельзя отменить."
            wrapMode: Text.WordWrap
            font.pixelSize: FontSize.body
            color: root.textPrimary
        }

        footer: DialogButtonBox {
            Button {
                text: "Очистить"
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                Material.background: ThemeManager.errorColor
                Material.foreground: "white"
            }
            Button {
                text: "Отмена"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                flat: true
            }
        }

        onAccepted: {
            taskViewModel.emptyTrash()
            root.refresh()
        }
    }

    Dialog {
        id: deleteOneDialog
        property string taskId:    ""
        property string taskTitle: ""
        title: "Удалить задачу навсегда"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(380, Overlay.overlay.width - Spacing.xl)

        background: Rectangle {
            color: root.contentColor
            radius: Radius.md
            border.color: root.borderColor
            border.width: 1
        }

        Label {
            width: parent.width
            text: "Окончательно удалить «" + deleteOneDialog.taskTitle +
                  "»? Действие нельзя отменить."
            wrapMode: Text.WordWrap
            font.pixelSize: FontSize.body
            color: root.textPrimary
        }

        footer: DialogButtonBox {
            Button {
                text: "Удалить"
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                Material.background: ThemeManager.errorColor
                Material.foreground: "white"
            }
            Button {
                text: "Отмена"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                flat: true
            }
        }

        onAccepted: {
            if (deleteOneDialog.taskId !== "") {
                taskViewModel.permanentlyDelete(deleteOneDialog.taskId)
                root.refresh()
            }
        }
    }

    // Background ──────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: bgColor
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Header ─────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: contentColor

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Spacing.lg
                anchors.rightMargin: Spacing.md
                spacing: Spacing.sm

                Label {
                    text: "Корзина"
                    font.pixelSize: FontSize.h1
                    font.weight: Font.DemiBold
                    color: textPrimary
                }

                // Count badge — shows how many items are in the trash
                Rectangle {
                    visible: root.trashItems.length > 0
                    width: badgeText.implicitWidth + 12
                    height: 20
                    radius: 10
                    color: Qt.rgba(textSecondary.r, textSecondary.g, textSecondary.b, 0.18)

                    Text {
                        id: badgeText
                        anchors.centerIn: parent
                        text: root.trashItems.length.toString()
                        font.pixelSize: 11
                        font.bold: true
                        color: textPrimary
                    }
                }

                Item { Layout.fillWidth: true }

                // «Очистить корзину»
                Button {
                    flat: true
                    text: "🗑 Очистить корзину"
                    font.pixelSize: FontSize.small
                    visible: root.trashItems.length > 0
                    onClicked: emptyConfirmDialog.open()
                }
            }

            // Bottom separator
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: borderColor
            }
        }

        // ── Body ───────────────────────────────────────────────────────────
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Empty state
            EmptyState {
                anchors.fill: parent
                visible: root.trashItems.length === 0
                emoji: "🗑"
                title: "Корзина пуста"
                message: "Удалённые задачи появятся здесь и будут доступны для восстановления."
            }

            // List of deleted tasks
            ListView {
                id: listView
                anchors.fill: parent
                anchors.margins: Spacing.md
                visible: root.trashItems.length > 0
                model: root.trashItems
                clip: true
                spacing: Spacing.sm
                boundsBehavior: Flickable.StopAtBounds

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: delegateRoot
                    required property var modelData
                    required property int index

                    readonly property string taskId:      modelData.id          || ""
                    readonly property string taskContent: modelData.content     || ""
                    readonly property string projectId:   modelData.projectId   || ""
                    readonly property string projectName: modelData.projectName || ""
                    readonly property int    priority:    modelData.priority    || 1
                    readonly property string updatedAt:   modelData.updatedAt   || ""

                    width: ListView.view.width
                    height: cardCol.implicitHeight + Spacing.md * 2
                    radius: Radius.md
                    color: cardHover.hovered
                           ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b,
                                     root.isDarkTheme ? 0.08 : 0.04)
                           : contentColor
                    border.color: borderColor
                    border.width: 1

                    Behavior on color { ColorAnimation { duration: 150 } }

                    HoverHandler { id: cardHover }

                    // Priority colors / labels mirror ProjectView's helpers
                    // (1 = P1 highest … 4 = P4 lowest).
                    function _priorityColor(p) {
                        switch (p) {
                            case 1:  return ThemeManager.priorityP1Color
                            case 2:  return ThemeManager.priorityP2Color
                            case 3:  return ThemeManager.priorityP3Color
                            default: return ThemeManager.priorityP4Color
                        }
                    }
                    function _priorityLabel(p) {
                        if (p === 1) return "P1"
                        if (p === 2) return "P2"
                        if (p === 3) return "P3"
                        return "P4"
                    }

                    ColumnLayout {
                        id: cardCol
                        anchors.fill: parent
                        anchors.margins: Spacing.md
                        spacing: Spacing.xs

                        // Top row: title (strikethrough) + priority badge
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Spacing.sm

                            // Priority badge (P1–P4)
                            Rectangle {
                                readonly property color _prioColor:
                                    delegateRoot._priorityColor(delegateRoot.priority)
                                Layout.preferredWidth: prioBadgeText.implicitWidth + 10
                                Layout.preferredHeight: 18
                                radius: 9
                                color: Qt.rgba(_prioColor.r, _prioColor.g, _prioColor.b, 0.18)

                                Text {
                                    id: prioBadgeText
                                    anchors.centerIn: parent
                                    text: delegateRoot._priorityLabel(delegateRoot.priority)
                                    font.pixelSize: FontSize.caption
                                    font.bold: true
                                    color: parent._prioColor
                                }
                            }

                            // Task content — slightly muted to communicate "deleted"
                            Label {
                                Layout.fillWidth: true
                                text: delegateRoot.taskContent
                                font.pixelSize: FontSize.body
                                color: textPrimary
                                opacity: 0.8
                                elide: Text.ElideRight
                                font.strikeout: true
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                maximumLineCount: 2
                            }
                        }

                        // Meta row: project chip + relative deleted time
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Spacing.sm

                            // Project chip — only shown when the task had a resolvable project
                            Rectangle {
                                visible: delegateRoot.projectName.length > 0
                                Layout.preferredHeight: 18
                                Layout.preferredWidth: projectChipText.implicitWidth + 16
                                radius: 9
                                color: Qt.rgba(textSecondary.r, textSecondary.g, textSecondary.b,
                                               root.isDarkTheme ? 0.18 : 0.10)

                                Text {
                                    id: projectChipText
                                    anchors.centerIn: parent
                                    text: "# " + delegateRoot.projectName
                                    font.pixelSize: FontSize.caption
                                    color: textSecondary
                                }
                            }

                            // Relative deleted time
                            Text {
                                Layout.fillWidth: true
                                text: "Удалено: " + root.relativeDeletedTime(delegateRoot.updatedAt)
                                font.pixelSize: FontSize.caption
                                color: textSecondary
                                elide: Text.ElideRight
                            }

                            // Actions
                            ToolButton {
                                text: "↩ Восстановить"
                                font.pixelSize: FontSize.caption
                                flat: true
                                ToolTip.text: "Вернуть задачу"
                                ToolTip.visible: hovered
                                ToolTip.delay: 500
                                onClicked: {
                                    taskViewModel.restoreTask(delegateRoot.taskId)
                                    root.refresh()
                                }
                            }

                            ToolButton {
                                text: "🗑 Удалить навсегда"
                                font.pixelSize: FontSize.caption
                                flat: true
                                ToolTip.text: "Удалить безвозвратно"
                                ToolTip.visible: hovered
                                ToolTip.delay: 500
                                onClicked: {
                                    deleteOneDialog.taskId    = delegateRoot.taskId
                                    deleteOneDialog.taskTitle = delegateRoot.taskContent
                                    deleteOneDialog.open()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
