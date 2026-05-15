import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

ApplicationWindow {
    id: root

    visible: true

    // ── Window title: "OpenTodo — N задач" ──────────────────────────────────
    // Cached count of pending (uncompleted, non-deleted) tasks. Recomputed on
    // dataStore signals via Qt.callLater so we never iterate the full task list
    // inside the title binding itself.
    property int _pendingCount: 0

    function _ruTaskPlural(n) {
        var m100 = n % 100, m10 = n % 10
        if (m100 >= 11 && m100 <= 14) return "задач"
        if (m10 === 1)                return "задача"
        if (m10 >= 2 && m10 <= 4)     return "задачи"
        return "задач"
    }

    function _pendingCountCompute() {
        if (typeof dataStore === "undefined" || !dataStore) return 0
        var tasks = dataStore.allTasks()
        if (!tasks) return 0
        var n = 0
        for (var i = 0; i < tasks.length; i++) {
            var t = tasks[i]
            if (!t) continue
            if (!t.isCompleted && !t.isDeleted) n++
        }
        return n
    }

    function _recomputePending() {
        Qt.callLater(function() { root._pendingCount = root._pendingCountCompute() })
    }

    title: _pendingCount > 0
           ? ("OpenTodo — " + _pendingCount + " " + _ruTaskPlural(_pendingCount))
           : "OpenTodo"

    // Reactivity: recompute when task data changes.
    Connections {
        target: typeof dataStore !== "undefined" ? dataStore : null
        function onTasksChanged() { root._recomputePending() }
        function onDataChanged()  { root._recomputePending() }
    }

    // ── Global toast for sync / OAuth feedback ────────────────────────────────
    function showToast(message, kind) {
        toast.text = message
        toast.kind = kind || "info"
        toast.opacity = 1
        toastFade.restart()
    }

    Connections {
        target: syncViewModel
        function onSyncError(message) { root.showToast("Ошибка: " + message, "error") }
    }

    // GDrive auth state — read directly from the GDriveClient context property
    // so SyncViewModel doesn't need wrapper Q_PROPERTYs.
    Connections {
        target: typeof gdriveClient !== "undefined" ? gdriveClient : null
        function onAuthenticatedChanged() {
            if (gdriveClient && gdriveClient.authenticated)
                root.showToast("Google Drive подключён: " + gdriveClient.accountEmail, "success")
        }
    }

    Rectangle {
        id: toast
        property string text: ""
        property string kind: "info"
        z: 9999
        visible: opacity > 0
        opacity: 0
        radius: Radius.md
        color: kind === "error"   ? "#d33"
             : kind === "success" ? "#2e8b57"
                                  : ThemeManager.surfaceColor
        border.color: ThemeManager.borderColor
        border.width: 1
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Spacing.lg
        anchors.horizontalCenter: parent.horizontalCenter
        implicitWidth: Math.max(0, Math.min(toastText.implicitWidth + Spacing.xl, parent.width - Spacing.xxl))  // B-6.2: clamp underflow guard
        implicitHeight: toastText.implicitHeight + 20
        Behavior on opacity { NumberAnimation { duration: Duration.slow } }

        Text {
            id: toastText
            anchors.centerIn: parent
            text: parent.text
            color: parent.kind === "info" ? ThemeManager.textColor : "white"
            font.pixelSize: FontSize.small
            wrapMode: Text.WordWrap
            width: parent.width - Spacing.xl
            horizontalAlignment: Text.AlignHCenter
        }

        Timer {
            id: toastFade
            interval: 5000
            onTriggered: toast.opacity = 0
        }
    }

    width: Qt.platform.os === "android" ? Screen.width : 1280
    height: Qt.platform.os === "android" ? Screen.height : 800

    // Window min size: 200 px wide so the responsive DesktopLayout can collapse
    // its sidebar entirely and degrade gracefully. The ≥ 800 / 500-799 / < 500
    // breakpoints in DesktopLayout.qml handle the visual reflow.
    minimumWidth: Qt.platform.os === "android" ? 0 : 200
    minimumHeight: Qt.platform.os === "android" ? 0 : 320

    Material.theme: ThemeManager.isDark ? Material.Dark : Material.Light
    Material.accent: ThemeManager.accentColor
    Material.primary: ThemeManager.accentColor
    Material.background: ThemeManager.backgroundColor
    Material.foreground: ThemeManager.textColor

    color: ThemeManager.backgroundColor

    // Keep ThemeManager.currentTheme in sync with persisted settings.
    // Singletons can't access context properties directly, so we wire it here.
    Component.onCompleted: {
        ThemeManager.currentTheme = settingsViewModel.theme
        applyTitleBarChrome()
        // Telemetry stays opt-in via the Settings page; mark the first-run
        // flag so any code that checks `telemetry.prompted` sees it as
        // already handled (the nag dialog has been removed).
        telemetry.markPrompted()
        // Initial pending-tasks count for the window title.
        _recomputePending()
    }
    Connections {
        target: settingsViewModel
        function onThemeChanged() {
            ThemeManager.currentTheme = settingsViewModel.theme
            applyTitleBarChrome()
        }
    }

    // Re-apply title bar chrome whenever the *resolved* dark/light state
    // changes — covers OS toggles when "Системная" is selected.
    Connections {
        target: ThemeManager
        function onIsDarkChanged() { applyTitleBarChrome() }
    }

    function applyTitleBarChrome() {
        // Only meaningful on Windows; the helper is a no-op elsewhere.
        WindowChrome.setDarkTitleBar(root, ThemeManager.isDark)
    }

    Loader {
        id: layoutLoader
        anchors.fill: parent
        source: Qt.platform.os === "android" ? "layouts/MobileLayout.qml" : "layouts/DesktopLayout.qml"
    }

    // ── Undo Snackbar ─────────────────────────────────────────────────────────
    Rectangle {
        id: undoSnackbar

        property string message: ""

        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: Spacing.lg
        width: Math.min(360, parent.width - 2*Spacing.lg)  // B-6.3: don't overshoot narrow windows
        height: 48
        radius: Radius.lg
        color: ThemeManager.isDark ? "#404040" : "#202020"
        z: 1000

        visible: opacity > 0
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: Duration.normal } }

        function showFor(msg) {
            message = msg
            opacity = 1
            undoHideTimer.restart()
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Spacing.md
            anchors.rightMargin: Spacing.sm
            spacing: Spacing.sm

            Text {
                text: undoSnackbar.message
                color: "white"
                font.pixelSize: FontSize.body
                Layout.fillWidth: true
                elide: Text.ElideRight
            }

            ToolButton {
                id: undoButton
                contentItem: Text {
                    text: "ОТМЕНИТЬ"
                    color: ThemeManager.accentColor
                    font.bold: true
                    font.pixelSize: FontSize.body
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                }
                onClicked: {
                    undoStack.undo()
                    undoSnackbar.opacity = 0
                    undoHideTimer.stop()
                }
            }
        }

        Timer {
            id: undoHideTimer
            interval: 5000
            onTriggered: undoSnackbar.opacity = 0
        }
    }

    // Wiring 1: global hotkey → raise window + focus QuickAdd input.
    // DesktopLayout exposes quickAddLoader as a child of layoutLoader.item;
    // we reach it via the Loader's item property.
    // B-6.6: guard against hotkeyService being absent (non-desktop platforms / WASM)
    Loader {
        active: typeof hotkeyService !== "undefined"
        sourceComponent: Component {
            Connections {
                target: hotkeyService
                function onQuickAddRequested() {
                    root.raise()
                    root.requestActivate()
                    const layout = layoutLoader.item
                    if (layout && layout.hasOwnProperty("quickAddLoader")) {
                        const qa = layout.quickAddLoader
                        if (qa && qa.item && typeof qa.item.focusInput === "function")
                            qa.item.focusInput()
                    }
                }
            }
        }
    }

    // Wiring 2: notification click → raise window + open task editor.
    // The C++ side already calls taskVM.selectTask(taskId); we only need to
    // raise the window here so it becomes visible.
    Connections {
        target: typeof notificationService !== "undefined" ? notificationService : null
        function onNotificationClicked(taskId) {
            root.raise()
            root.requestActivate()
        }
    }

    // ── In-app banner queue ───────────────────────────────────────────────────
    // Receives inAppBannerRequested from the C++ NotificationService and stacks
    // banners at the top-centre of the window. Each banner auto-dismisses after
    // 4 s; clicking it opens the related task if a taskId is set.
    //
    // The queue is a plain JS array: [{title, body, taskId}, ...]. A Repeater
    // turns each entry into an InAppBanner instance that slides down from y = -8
    // (just off the top bar) and stacks vertically with a 6 px gap.

    property var bannerQueue: []   // array of {id, title, body, taskId}
    property int _bannerIdCounter: 0  // B-6.5: monotonic id to avoid index desync

    // Re-schedule a reminder N minutes from now via NotificationService.
    // Called when the user taps «Отложить» on a banner.
    function snoozeBanner(taskId, minutes) {
        if (!taskId) return
        if (typeof notificationService === "undefined" || !notificationService) return
        const t = dataStore.taskById(taskId)
        const body = t ? t.content : ""
        const due = new Date(Date.now() + minutes * 60 * 1000)
        // Use "snooze-<taskId>" as the notification id so a repeated snooze
        // replaces the previous pending schedule for the same task.
        notificationService.scheduleNotification(
            "Отложенное напоминание", body, due, "snooze-" + taskId)
    }

    function pushBanner(title, body, taskId) {
        // Cap queue at 5 so the screen doesn't overflow
        let q = root.bannerQueue.slice(-4)
        q.push({ id: root._bannerIdCounter++, title: title, body: body || "", taskId: taskId || "" })
        root.bannerQueue = q
    }

    function removeBanner(bannerId) {  // B-6.5: look up by id, not by index
        let q = root.bannerQueue.filter(function(b) { return b.id !== bannerId })
        root.bannerQueue = q
    }

    Connections {
        target: typeof notificationService !== "undefined" ? notificationService : null
        function onInAppBannerRequested(title, body, taskId) {
            root.pushBanner(title, body, taskId)
        }
    }

    // Container anchored to the top-centre, above everything (z: 10000)
    Item {
        id: bannerContainer
        z: 10000
        anchors.top:              parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin:        Spacing.md
        width: Math.min(parent.width - Spacing.xxl, 400)

        // Dynamic height: Column auto-sizes from children (B-6.4)
        height: bannerColumn.height
        clip: false

        Column {
            id: bannerColumn
            width: parent.width
            spacing: 6

            Repeater {
                id: bannerRepeater
                model: root.bannerQueue

                delegate: InAppBanner {
                    id: bannerItem
                    width: bannerColumn.width

                    // B-6.5: capture banner id into delegate so removal is id-based, not index-based
                    required property var modelData
                    required property int index

                    bannerIndex:  index
                    bannerTitle:  modelData.title
                    bannerBody:   modelData.body
                    bannerTaskId: modelData.taskId

                    onSnoozeRequested: function(taskId, minutes) {
                        root.snoozeBanner(taskId, minutes)
                    }
                    onDismissRequested: function(idx) {
                        root.removeBanner(modelData.id)  // B-6.5: use stable id, not positional index
                    }
                }
            }
        }
    }

    // Show snackbar when a task is removed or completed.
    Connections {
        target: taskViewModel
        function onTaskRemoved(id) {
            undoSnackbar.showFor(undoStack.lastDescription || "Задача удалена")
        }
        // ITER3 #1: bulk-delete fires this instead of N taskRemoved events;
        // show a single snackbar with the count rather than N stacked toasts.
        function onTasksRemoved(ids) {
            const n = ids ? ids.length : 0
            if (n <= 0) return
            undoSnackbar.showFor(undoStack.lastDescription
                                 || ("Удалено задач: " + n))
        }
        function onTaskChanged(id) {
            const t = dataStore.taskById(id)
            if (t && t.isCompleted)
                undoSnackbar.showFor(undoStack.lastDescription || "Задача завершена")
        }
    }

    // Show brief "Действие отменено" toast when undo fires.
    Connections {
        target: undoStack
        function onUndoExecuted(description) {
            root.showToast("Отменено: " + description, "info")
        }
    }

    // Telemetry first-run prompt removed — opt-in lives in Настройки →
    // Резервная копия only.

    // ── Karma milestone achievement banner ───────────────────────────────────
    // Shown when karmaViewModel emits achievementUnlocked. Lives above everything
    // (z: 10001 > bannerContainer's z: 10000). Auto-dismisses after 8 s; click
    // to dismiss early. Milestones are persisted in QSettings so they never
    // fire again on restart.

    Connections {
        target: karmaViewModel
        function onAchievementUnlocked(name, description, milestone) {
            achievementBanner.show(name, description)
        }
    }

    // ── Daily-goal confetti ───────────────────────────────────────────────────
    // Fires confettiOverlay.celebrate() the first time dailyGoalMet flips to
    // true within a session.  _goalCelebrated resets to false whenever the
    // count drops back below the goal (e.g. user uncompletes a task, then
    // completes it again later — the burst fires once more).
    //
    // Note: dailyGoalHit() signal does not exist on KarmaViewModel; we drive
    // the trigger from the existing Q_PROPERTY dailyGoalMet (bool) which is
    // already emitted via karmaChanged() every time completedToday changes.
    property bool _goalCelebrated: false

    Connections {
        target: typeof karmaViewModel !== "undefined" ? karmaViewModel : null
        function onKarmaChanged() {
            if (karmaViewModel.dailyGoalMet) {
                if (!root._goalCelebrated) {
                    root._goalCelebrated = true
                    confettiOverlay.celebrate()
                }
            } else {
                // Goal no longer met (task un-completed or goal raised mid-day):
                // allow the burst to fire again if it's met once more.
                root._goalCelebrated = false
            }
        }
    }

    ConfettiOverlay {
        id: confettiOverlay
        anchors.fill: parent
        // z: 9999 is set inside ConfettiOverlay itself; this sits below the
        // achievement banner (z: 10001) and the in-app banner container
        // (z: 10000) so those still render on top.
    }

    Rectangle {
        id: achievementBanner
        z: 10001
        width: 360
        height: 84
        radius: Radius.lg
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        // Offset below the in-app banner container so they don't collide.
        // bannerContainer uses Spacing.md at top; 100 px gives comfortable clearance
        // for up to two stacked in-app banners.
        anchors.topMargin: 100
        color: "#FFD700"
        visible: opacity > 0
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: 400 } }

        property string title: ""
        property string description: ""

        function show(t, d) {
            title = t
            description = d
            opacity = 1
            achievementHide.restart()
        }

        Timer {
            id: achievementHide
            interval: 8000
            onTriggered: achievementBanner.opacity = 0
        }

        RowLayout {
            anchors.fill: parent
            anchors.margins: Spacing.md
            spacing: Spacing.sm

            Text {
                text: "🏆"
                font.pixelSize: 36
            }

            ColumnLayout {
                spacing: 2
                Layout.fillWidth: true

                Text {
                    text: achievementBanner.title
                    font.bold: true
                    font.pixelSize: FontSize.subtitle
                    color: "#3a2d00"
                }
                Text {
                    text: achievementBanner.description
                    font.pixelSize: FontSize.body
                    color: "#3a2d00"
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: achievementBanner.opacity = 0
        }
    }

    // ── Auto-updater dialog ───────────────────────────────────────────────────
    // B-6.7: updateChecker is only present on desktop builds — guard the whole block
    Loader {
        active: typeof updateChecker !== "undefined"
        sourceComponent: Component {
            Item {
                Dialog {
                    id: updateDialog
                    title: "Доступно обновление"
                    visible: updateChecker.updateAvailable
                    anchors.centerIn: Overlay.overlay
                    width: 380
                    modal: true
                    standardButtons: Dialog.NoButton

                    background: Rectangle {
                        color: ThemeManager.surfaceColor
                        radius: Radius.md
                        border.color: ThemeManager.borderColor
                        border.width: 1
                    }

                    ColumnLayout {
                        width: parent.width
                        spacing: Spacing.sm

                        Label {
                            text: "OpenTodo " + updateChecker.latestVersion + " доступен."
                            font.pixelSize: FontSize.subtitle
                            font.bold: true
                            color: ThemeManager.textColor
                        }
                        Label {
                            text: "У вас сейчас " + updateChecker.currentVersion + "."
                            font.pixelSize: FontSize.body
                            color: ThemeManager.secondaryTextColor
                        }

                        RowLayout {
                            Layout.alignment: Qt.AlignRight
                            Layout.topMargin: Spacing.sm
                            spacing: Spacing.sm

                            Button {
                                text: "Скачать"
                                Material.background: ThemeManager.accentColor
                                Material.foreground: "white"
                                onClicked: {
                                    updateChecker.openReleaseInBrowser()
                                    updateDialog.close()
                                }
                            }
                            Button {
                                text: "Позже"
                                onClicked: updateDialog.close()
                            }
                            Button {
                                text: "Пропустить"
                                onClicked: {
                                    updateChecker.skipVersion(updateChecker.latestVersion)
                                    updateDialog.close()
                                }
                            }
                        }
                    }
                }

                // Trigger update check 5 seconds after startup (non-blocking, 24 h cap)
                Timer {
                    interval: 5000
                    running: true
                    repeat: false
                    onTriggered: updateChecker.checkForUpdates(false)
                }
            }
        }
    }
}
