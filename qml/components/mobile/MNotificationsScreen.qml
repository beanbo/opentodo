import QtQuick
import QtQuick.Controls
import OpenTodoUI

// MNotificationsScreen — full-screen mobile notifications centre.
// Ported from m-screens3.jsx MNotificationsScreen (README §9).
//
// Big top bar ("Уведомления", bell icon, accent) with a "mark all read"
// (check-circle) action. Below: a ListView of notification rows — a tinted
// kind-coloured tile + icon, bold title, body, relative time, and an unread
// accent dot. Unread rows get a faint background tint. Tapping a row marks it
// read and, if it links to a task, opens that task. Empty state is a centred
// bell + "Нет уведомлений".
Item {
    id: root
    anchors.fill: parent

    // ── Signals ────────────────────────────────────────────────────────────
    signal menuRequested()
    signal taskClicked(string id)

    // ── kind enum → icon + colour (mirrors NotificationsViewModel::Kind) ─────
    function _kindIcon(kind) {
        switch (kind) {
        case 0: return "check-circle"  // TaskCompleted
        case 1: return "flame"         // TaskOverdue
        case 2: return "sync"          // SyncSuccess
        case 3: return "sync"          // SyncFailure
        case 4: return "sparkles"      // AchievementUnlocked
        case 5: return "alarm"         // ReminderFired
        default: return "bell"
        }
    }
    function _kindColor(kind) {
        switch (kind) {
        case 0: return ThemeManager.successColor   // TaskCompleted
        case 1: return ThemeManager.errorColor     // TaskOverdue
        case 2: return ThemeManager.successColor   // SyncSuccess
        case 3: return ThemeManager.errorColor     // SyncFailure
        case 4: return ThemeManager.accentColor    // AchievementUnlocked
        case 5: return ThemeManager.warningColor   // ReminderFired
        default: return ThemeManager.secondaryTextColor
        }
    }

    // ── Background ───────────────────────────────────────────────────────────
    Rectangle { anchors.fill: parent; color: ThemeManager.backgroundColor }

    // ── Top bar ──────────────────────────────────────────────────────────────
    MTopBar {
        id: topBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        big: true
        title: qsTr("Уведомления")
        titleIconName: "bell"
        titleColor: ThemeManager.accentColor

        MIconButton {
            iconName: "check-circle"
            iconColor: ThemeManager.textColor
            iconSize: 21
            onClicked: notificationsViewModel.markAllRead()
        }
    }

    // ── List ─────────────────────────────────────────────────────────────────
    ListView {
        id: list
        anchors.top: topBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        model: notificationsViewModel
        bottomMargin: 96
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        delegate: Item {
            id: rowItem
            width: list.width
            implicitHeight: Math.max(64, rowCol.implicitHeight + 30)
            height: implicitHeight

            required property string id
            required property string title
            required property string body
            required property var    timestamp
            required property int    kind
            required property string relatedTaskId
            required property bool   isRead

            readonly property color _tile: root._kindColor(kind)

            // Unread background tint.
            Rectangle {
                anchors.fill: parent
                color: rowItem.isRead
                       ? "transparent"
                       : Qt.rgba(ThemeManager.textColor.r,
                                 ThemeManager.textColor.g,
                                 ThemeManager.textColor.b,
                                 ThemeManager.isDark ? 0.03 : 0.02)
            }

            // Bottom hairline.
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: ThemeManager.hairlineColor
            }

            // Colour tile.
            Rectangle {
                id: tile
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.top: parent.top
                anchors.topMargin: 15
                width: 40
                height: 40
                radius: 12
                color: Qt.rgba(rowItem._tile.r, rowItem._tile.g, rowItem._tile.b, 0.12)
                IconSvg {
                    anchors.centerIn: parent
                    name: root._kindIcon(rowItem.kind)
                    size: 20
                    color: rowItem._tile
                }
            }

            // Text column.
            Column {
                id: rowCol
                anchors.left: tile.right
                anchors.leftMargin: 14
                anchors.right: parent.right
                anchors.rightMargin: 18
                anchors.top: parent.top
                anchors.topMargin: 15
                spacing: 3

                Item {
                    width: parent.width
                    height: titleText.implicitHeight

                    Text {
                        id: titleText
                        anchors.left: parent.left
                        anchors.right: dot.visible ? dot.left : parent.right
                        anchors.rightMargin: dot.visible ? 8 : 0
                        anchors.verticalCenter: parent.verticalCenter
                        text: rowItem.title
                        color: ThemeManager.textColor
                        font.pixelSize: 15
                        font.weight: Font.Bold
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        id: dot
                        visible: !rowItem.isRead
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8
                        height: 8
                        radius: 4
                        color: ThemeManager.accentColor
                    }
                }

                Text {
                    width: parent.width
                    visible: rowItem.body !== ""
                    text: rowItem.body
                    color: ThemeManager.secondaryTextColor
                    font.pixelSize: 14   // design 13.5 → rounded
                    lineHeight: 1.4
                    lineHeightMode: Text.ProportionalHeight
                    wrapMode: Text.WordWrap
                }

                Text {
                    topPadding: 2
                    text: rowItem.timestamp ? Qt.formatDateTime(rowItem.timestamp, "dd.MM HH:mm") : ""
                    color: ThemeManager.faintTextColor
                    font.pixelSize: 12
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    notificationsViewModel.markReadById(rowItem.id)
                    if (rowItem.relatedTaskId !== "")
                        root.taskClicked(rowItem.relatedTaskId)
                }
            }
        }
    }

    // ── Empty state ──────────────────────────────────────────────────────────
    Column {
        anchors.centerIn: list
        visible: notificationsViewModel.count === 0
        spacing: 12

        IconSvg {
            anchors.horizontalCenter: parent.horizontalCenter
            name: "bell"
            size: 40
            color: ThemeManager.faintTextColor
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Нет уведомлений")
            color: ThemeManager.secondaryTextColor
            font.pixelSize: 15
        }
    }
}
