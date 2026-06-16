import QtQuick
import QtQuick.Controls
import OpenTodoUI

// MTrashScreen — full-page mobile Trash view. Ported from m-screens3.jsx
// MTrashScreen (README §11). Big top bar ("Корзина"), an auto-purge note, a
// list of soft-deleted tasks (strikethrough title + project · relative deleted
// time + round restore button), and an empty state. Data comes from
// taskViewModel.deletedTasks(); restore/empty go through the view-model and
// re-fetch the cached list.
Item {
    id: root
    anchors.fill: parent

    signal menuRequested()
    signal taskClicked(string id)

    // Cached snapshot of taskViewModel.deletedTasks() — re-fetched on store
    // mutations rather than per-delegate so the C++ call isn't re-run on every
    // rebind. Mirrors the desktop TrashView pattern.
    property var _items: []
    function refresh() { root._items = taskViewModel.deletedTasks() }

    Component.onCompleted: refresh()

    MDensity { id: mdens }

    Connections {
        target: taskViewModel
        ignoreUnknownSignals: true
        function onTaskChanged()  { Qt.callLater(root.refresh) }
        function onTaskRemoved()  { Qt.callLater(root.refresh) }
        function onTasksRemoved() { Qt.callLater(root.refresh) }
    }

    // ── Russian relative-deleted-time helper ─────────────────────────────────
    // "сегодня" / "вчера" / "N дн назад", computed from updatedAt vs now. The
    // 1-minute tick is consumed as a binding dependency so the sublines age
    // without a manual rebuild.
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
        if (h >= 11 && h <= 19) return qsTr("дн")
        if (m === 1)            return qsTr("дн")
        return qsTr("дн")
    }

    function relativeDeletedTime(isoString) {
        void(refreshTimer.tick)   // binding dependency
        if (!isoString || isoString === "") return ""
        const dt = new Date(isoString)
        if (isNaN(dt.getTime())) return ""
        const diffMs = Date.now() - dt.getTime()
        if (diffMs < 0) return qsTr("только что")
        const diffMin = Math.floor(diffMs / 60000)
        if (diffMin < 60) return qsTr("сегодня")
        const diffH = Math.floor(diffMin / 60)
        if (diffH < 24)   return qsTr("сегодня")
        const diffD = Math.floor(diffH / 24)
        if (diffD === 0)  return qsTr("сегодня")
        if (diffD === 1)  return qsTr("вчера")
        if (diffD < 30)   return qsTr("%1 %2 назад").arg(diffD).arg(_dayWord(diffD))
        return dt.toLocaleDateString(Qt.locale("ru-RU"))
    }

    // Background
    Rectangle { anchors.fill: parent; color: ThemeManager.backgroundColor }

    // ── Top bar ──────────────────────────────────────────────────────────────
    MTopBar {
        id: topBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        big: true
        title: qsTr("Корзина")
        titleIconName: "trash"
        titleColor: ThemeManager.secondaryTextColor
        onMenuClicked: root.menuRequested()

        MIconButton {
            iconName: "trash"
            iconColor: ThemeManager.errorColor
            iconSize: 20
            onClicked: { taskViewModel.emptyTrash(); root.refresh() }
        }
    }

    // ── Body ─────────────────────────────────────────────────────────────────
    ListView {
        id: listView
        anchors.top: topBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        model: root._items
        boundsBehavior: Flickable.StopAtBounds
        bottomMargin: 96

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        // Auto-purge note above the list.
        header: Text {
            width: listView.width
            leftPadding: 20
            rightPadding: 20
            topPadding: 4
            bottomPadding: 10
            text: qsTr("Удалённые задачи хранятся 30 дней, затем стираются автоматически.")
            font.pixelSize: 13
            lineHeight: 1.4
            wrapMode: Text.WordWrap
            color: ThemeManager.secondaryTextColor
        }

        delegate: Item {
            id: rowItem
            required property var modelData

            readonly property string taskId:      modelData.id          || ""
            readonly property string taskContent:  modelData.content     || ""
            readonly property string projectName:  modelData.projectName || ""
            readonly property string updatedAt:    modelData.updatedAt   || ""

            width: ListView.view.width
            height: Math.max(64, contentCol.implicitHeight + 26)

            // Bottom hairline separator (matches the JSX 1px borderBottom).
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: ThemeManager.hairlineColor
            }

            Row {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 12

                // Empty 18px circle (border faintTextColor).
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 18
                    height: 18
                    radius: 9
                    color: "transparent"
                    border.width: 2
                    border.color: ThemeManager.faintTextColor
                }

                Column {
                    id: contentCol
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 18 - 40 - 12 * 2
                    spacing: 3

                    Text {
                        width: parent.width
                        text: rowItem.taskContent
                        font.pixelSize: 15   // 14.5 → rounded
                        font.strikeout: true
                        color: ThemeManager.secondaryTextColor
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }
                    Text {
                        width: parent.width
                        text: (rowItem.projectName.length > 0 ? rowItem.projectName + " · " : "")
                              + qsTr("удалено ") + root.relativeDeletedTime(rowItem.updatedAt)
                        font.pixelSize: 12
                        color: ThemeManager.faintTextColor
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }
                }

                // Round restore button (40px, accentContainer bg, "reset" icon).
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40
                    height: 40
                    radius: 20
                    color: restoreMa.pressed
                           ? Qt.darker(ThemeManager.accentContainer, 1.1)
                           : ThemeManager.accentContainer
                    Behavior on color { ColorAnimation { duration: Duration.fast } }

                    IconSvg {
                        anchors.centerIn: parent
                        name: "reset"
                        size: 18
                        color: ThemeManager.accentColor
                    }

                    MouseArea {
                        id: restoreMa
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            taskViewModel.restoreTask(rowItem.taskId)
                            root.refresh()
                        }
                    }
                }
            }
        }
    }

    // ── Empty state ──────────────────────────────────────────────────────────
    Column {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 28   // nudge below the top bar visually
        spacing: 12
        visible: root._items.length === 0

        IconSvg {
            anchors.horizontalCenter: parent.horizontalCenter
            name: "trash"
            size: 40
            color: ThemeManager.faintTextColor
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Корзина пуста")
            font.pixelSize: 15
            color: ThemeManager.secondaryTextColor
        }
    }
}
