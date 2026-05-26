import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

// TODO TaskViewModel: add `Q_INVOKABLE void openTaskById(const QString &id)`
//   so clicking a notification with a relatedTaskId can navigate to that task.
//   Currently TaskViewModel only exposes setSelectedTask(QObject*); a string-id
//   variant is needed for cross-VM navigation from here.

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

    // B-6.89 — signal re-emitted so DesktopLayout wiring can connect to it
    signal openTaskRequested(string taskId)

    // R-6.91 — global timer drives relativeTime() re-evaluation every minute
    Timer {
        id: refreshTimer
        interval: 60000
        running: true
        repeat: true
        property int tick: 0
        onTriggered: tick++
    }

    // B-6.94 — confirmation dialog before destructive clear
    Dialog {
        id: clearConfirmDialog
        title: qsTr("Очистить уведомления")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 320

        background: Rectangle {
            color: root.contentColor
            radius: Radius.md
            border.color: root.borderColor
            border.width: 1
        }

        Label {
            width: parent.width
            text: qsTr("Удалить все уведомления? Это действие нельзя отменить.")
            wrapMode: Text.WordWrap
            font.pixelSize: FontSize.body
            color: root.textPrimary
        }

        footer: DialogButtonBox {
            Button {
                text: qsTr("Очистить")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                Material.background: ThemeManager.errorColor
                Material.foreground: "white"
            }
            Button {
                text: qsTr("Отмена")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                flat: true
            }
        }

        onAccepted: notificationsViewModel.clear()
    }

    Rectangle {
        anchors.fill: parent
        color: bgColor
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Header ─────────────────────────────────────────────────────────
        // Q4.01 pixel-perfect: дизайн view-header — padding 16 24, gap 12, icon 22.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: contentColor

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Spacing.lg
                anchors.rightMargin: Spacing.md
                spacing: 12

                IconSvg {
                    name: "bell"
                    size: 22
                    color: textPrimary
                    Layout.alignment: Qt.AlignVCenter
                }

                Label {
                    text: qsTr("Уведомления")
                    font.pixelSize: FontSize.viewHeader
                    font.weight: Font.DemiBold
                    color: textPrimary
                }

                // Unread badge
                Rectangle {
                    visible: notificationsViewModel.unreadCount > 0
                    width: badgeText.implicitWidth + 10
                    height: 20
                    radius: 10
                    color: accentColor

                    Text {
                        id: badgeText
                        anchors.centerIn: parent
                        text: notificationsViewModel.unreadCount
                        font.pixelSize: FontSize.caption
                        font.bold: true
                        color: "white"
                    }
                }

                Item { Layout.fillWidth: true }

                // «Прочитать все»
                Button {
                    flat: true
                    text: qsTr("Прочитать все")
                    font.pixelSize: FontSize.small
                    visible: notificationsViewModel.unreadCount > 0
                    onClicked: notificationsViewModel.markAllRead()
                }

                // «Очистить» — B-6.94: opens confirmation dialog instead of calling clear() directly
                Button {
                    flat: true
                    text: qsTr("Очистить")
                    font.pixelSize: FontSize.small
                    // Wave 1C fix: use count Q_PROPERTY (not rowCount() function call)
                    visible: notificationsViewModel.count > 0
                    onClicked: clearConfirmDialog.open()
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

            // ── Empty state ──────────────────────────────────────────────
            // Q4.01 (redesign): эмодзи "🔔" заменён на stroke-иконку "bell".
            EmptyState {
                anchors.fill: parent
                // Wave 1C fix: use count Q_PROPERTY (not rowCount() function call)
                visible: notificationsViewModel.count === 0
                iconName: "bell"
                title: qsTr("Пока ничего нового")
                message: qsTr("Здесь появятся уведомления о выполненных задачах, напоминаниях и синхронизации")
            }

            // ── List ─────────────────────────────────────────────────────
            ListView {
                id: listView
                anchors.fill: parent
                // Wave 1C fix: use count Q_PROPERTY (not rowCount() function call)
                visible: notificationsViewModel.count > 0
                model: notificationsViewModel
                clip: true
                spacing: 0
                boundsBehavior: Flickable.StopAtBounds

                // Smooth momentum scrolling
                flickDeceleration: 1500
                maximumFlickVelocity: 3000

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: NotificationDelegate {
                    width: ListView.view.width
                    isDarkTheme:  root.isDarkTheme
                    contentColor: root.contentColor
                    borderColor:  root.borderColor
                    textPrimary:  root.textPrimary
                    textSecondary: root.textSecondary
                    accentColor:  root.accentColor
                    // pass the refreshTimer tick so relativeTime() re-evaluates
                    refreshTick:  refreshTimer.tick

                    // ITER1 #26: use markReadById so the right notification is
                    // marked even if the model was mutated between the delegate
                    // being created and the click being processed.
                    onMarkReadRequested: notificationsViewModel.markReadById(notifId)
                    // B-6.89 — re-emit to parent after marking read
                    onOpenTaskRequested: function(taskId) {
                        notificationsViewModel.markReadById(notifId)
                        root.openTaskRequested(taskId)
                    }
                }
            }
        }
    }

    // ── Inline delegate component ─────────────────────────────────────────
    component NotificationDelegate: Rectangle {
        id: delegateRoot

        // Required roles from the model
        required property int    index
        required property string notifId
        required property string title
        required property string body
        required property var    timestamp   // QDateTime → JS Date
        required property int    kind
        required property string relatedTaskId
        required property bool   isRead

        // Style inputs (bound from parent ListView)
        property bool  isDarkTheme: false
        property color contentColor
        property color borderColor
        property color textPrimary
        property color textSecondary
        property color accentColor
        // R-6.91 — receive tick so relativeTime() binding refreshes
        property int   refreshTick: 0

        signal markReadRequested()
        signal openTaskRequested(string taskId)

        // Map kind int to emoji and accent tint
        // Q4.01 (redesign): kindEmoji остаётся как fallback (плейн-текстовые
        // тултипы / экспорт), но рендер делегата теперь использует kindIcon
        // (см. ниже) с IconSvg из stroke-пака — единый визуальный вес для
        // всех типов уведомлений.
        readonly property string kindEmoji: {
            switch (delegateRoot.kind) {
                case 0: return "✅"   // TaskCompleted    ✅
                case 1: return "⚠"   // TaskOverdue      ⚠
                case 2: return "🔔" // ReminderFired 🔔
                case 3: return "✔"   // SyncSuccess      ✔
                case 4: return "❌"   // SyncFailure      ❌
                case 5: return "🏆" // AchievementUnlocked 🏆
                default: return "📋"
            }
        }

        // Q4.01 (redesign): соответствие kind → имя SVG-иконки. close для
        // failure временно (нет "X" в паке отдельно — но "close" визуально
        // совпадает); star для achievement (нет trophy/cup в паке).
        readonly property string kindIcon: {
            switch (delegateRoot.kind) {
                case 0: return "check-circle"   // TaskCompleted
                case 1: return "warning"        // TaskOverdue
                case 2: return "bell"           // ReminderFired
                case 3: return "sync"           // SyncSuccess
                case 4: return "close"          // SyncFailure
                case 5: return "star"           // AchievementUnlocked
                default: return "list"
            }
        }

        // Q4.01 (redesign): акцентный цвет иконки для каждого kind — берём
        // непрозрачную версию того же оттенка, что у kindTint фона бубна.
        readonly property color kindIconColor: {
            switch (delegateRoot.kind) {
                case 0: return Qt.rgba(76/255, 175/255,  80/255, 1.0)
                case 1: return Qt.rgba(255/255, 152/255,  0/255, 1.0)
                case 2: return Qt.rgba(33/255,  150/255, 243/255, 1.0)
                case 3: return Qt.rgba(76/255,  175/255,  80/255, 1.0)
                case 4: return Qt.rgba(244/255,  67/255,  54/255, 1.0)
                case 5: return Qt.rgba(255/255, 193/255,   7/255, 1.0)
                default: return textSecondary
            }
        }

        readonly property color kindTint: {
            switch (delegateRoot.kind) {
                case 0: return Qt.rgba(76/255, 175/255,  80/255, 0.15) // green
                case 1: return Qt.rgba(255/255, 152/255,  0/255, 0.15) // amber
                case 2: return Qt.rgba(33/255,  150/255, 243/255, 0.15) // blue
                case 3: return Qt.rgba(76/255,  175/255,  80/255, 0.12) // green light
                case 4: return Qt.rgba(244/255,  67/255,  54/255, 0.15) // red
                case 5: return Qt.rgba(255/255, 193/255,   7/255, 0.15) // gold
                default: return Qt.rgba(128/255, 128/255, 128/255, 0.1)
            }
        }

        // R-6.91 — consumes refreshTick as a binding dependency so Qt re-evaluates
        // this function every minute when the timer fires.
        function relativeTime(dt) {
            void(delegateRoot.refreshTick)   // binding dependency — value discarded
            if (!dt) return ""
            const jsDate  = dt instanceof Date ? dt : new Date(dt)
            const diffMs  = Date.now() - jsDate.getTime()
            const diffSec = Math.round(diffMs / 1000)
            if (diffSec < 60)    return qsTr("только что")
            const diffMin = Math.round(diffSec / 60)
            if (diffMin < 60)    return diffMin + " " + minuteWord(diffMin) + qsTr(" назад")
            const diffH   = Math.round(diffMin / 60)
            if (diffH < 24)      return diffH   + " " + hourWord(diffH)   + qsTr(" назад")
            const diffD   = Math.round(diffH  / 24)
            if (diffD < 30)      return diffD   + " " + dayWord(diffD)    + qsTr(" назад")
            // Fallback: locale date
            return jsDate.toLocaleDateString("ru-RU")
        }

        function minuteWord(n) {
            const m = n % 10, h = n % 100
            if (h >= 11 && h <= 19) return qsTr("минут")
            if (m === 1)             return qsTr("минуту")
            if (m >= 2 && m <= 4)   return qsTr("минуты")
            return qsTr("минут")
        }
        function hourWord(n) {
            const m = n % 10, h = n % 100
            if (h >= 11 && h <= 19) return qsTr("часов")
            if (m === 1)             return qsTr("час")
            if (m >= 2 && m <= 4)   return qsTr("часа")
            return qsTr("часов")
        }
        function dayWord(n) {
            const m = n % 10, h = n % 100
            if (h >= 11 && h <= 19) return qsTr("дней")
            if (m === 1)             return qsTr("день")
            if (m >= 2 && m <= 4)   return qsTr("дня")
            return qsTr("дней")
        }

        // Q4.01 (redesign): height aligned with design padding 12/14 → 12+26+12 = 50,
        // with extra room for the two-line text column. 60 keeps comfortable rhythm.
        height: 60
        radius: Radius.md

        // B-6.92 — state-driven color binding; no direct mutation in onEntered/onExited
        property bool _hovered: hoverArea.containsMouse
        property color unreadColor: isDarkTheme
            ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.06)
            : Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.04)
        // Q4.01 (redesign): base background = surface (matches design `background: t.surface`).
        // Hover/unread overlays preserved on top of surface to keep state cues.
        property color readColor:   contentColor
        property color hoverColor:  Qt.rgba(accentColor.r, accentColor.g, accentColor.b,
                                            isDarkTheme ? 0.08 : 0.06)

        color: _hovered ? hoverColor : (isRead ? readColor : unreadColor)

        // Left unread accent bar
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 3
            visible: !delegateRoot.isRead
            color: accentColor
            radius: Radius.xs
        }

        RowLayout {
            anchors.fill: parent
            // Q4.01 (redesign): padding 12/14 from design canvas (view-others.jsx
            // notification card). Left padding stays at 14 (accent bar is 3 px,
            // visual content still starts at the same offset for read items).
            anchors.topMargin: 12
            anchors.bottomMargin: 12
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 12

            // Kind icon bubble
            // Q4.01 (redesign): 26×26 круг, иконка size 14 (per design canvas
            // notification card). Цвет фона/иконки сохраняется по kind — kindTint
            // фон + kindIconColor иконка (исторически работающее по типу поведение).
            Rectangle {
                width: 26
                height: 26
                radius: 13
                color: delegateRoot.kindTint

                IconSvg {
                    anchors.centerIn: parent
                    name: delegateRoot.kindIcon
                    size: 14
                    color: delegateRoot.kindIconColor
                }
            }

            // Text column
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2  // design: marginTop 2 для subtitle

                Text {
                    text: delegateRoot.title
                    // Q4.01 (redesign): bodySmall (13) + Medium (500) per design canvas.
                    font.pixelSize: FontSize.bodySmall
                    font.weight: delegateRoot.isRead ? Font.Normal : Font.Medium
                    color: textPrimary
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                Text {
                    text: delegateRoot.body
                    // Q4.01 (redesign): caption (11) per design canvas.
                    font.pixelSize: FontSize.caption
                    color: textSecondary
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }

            // Relative timestamp — refreshes every minute via refreshTick binding
            // Q4.01 (redesign): caption (11) per design canvas.
            Text {
                text: delegateRoot.relativeTime(delegateRoot.timestamp)
                font.pixelSize: FontSize.caption
                color: textSecondary
                horizontalAlignment: Text.AlignRight
            }
        }

        // Bottom separator (inset from left edge of icon)
        // Q4.01 (redesign): aligned with new padding (14) + bubble (26) + gap (12) = 52
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 14 + 26 + 12  // align with text column
            height: 1
            color: borderColor
            opacity: 0.5
        }

        // B-6.92 — hoverEnabled MouseArea; color driven by _hovered binding above
        MouseArea {
            id: hoverArea
            anchors.fill: parent
            cursorShape: relatedTaskId.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
            hoverEnabled: true

            onClicked: {
                if (delegateRoot.relatedTaskId.length > 0) {
                    delegateRoot.openTaskRequested(delegateRoot.relatedTaskId)
                } else {
                    delegateRoot.markReadRequested()
                }
            }
        }
    }
}
