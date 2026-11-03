import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import OpenTodoUI

// One in-app notification banner. Instantiated by the bannerRepeater in Main.qml.
// Auto-fades after 4 s; clicking it either opens the related task or just dismisses.
Rectangle {
    id: root

    // Data bound by the Repeater delegate
    required property int    bannerIndex
    required property string bannerTitle
    required property string bannerBody
    required property string bannerTaskId

    // Emitted so Main.qml can splice the banner out of the queue
    signal dismissRequested(int index)

    // Emitted when the user taps «Отложить»; Main.qml re-schedules via NotificationService
    signal snoozeRequested(string taskId, int minutes)

    // Internal: true while ANY action button is hovered — gates the auto-dismiss timer
    property bool _anyButtonHovered: false

    // Computes the dynamic tooltip text for the «🌅 До утра» button.
    // If it's currently before 9:00 AM local time, "до утра" means today 9:00;
    // otherwise it means tomorrow 9:00.
    function _morningTooltip() {
        var now = new Date();
        var target = new Date();
        target.setHours(9, 0, 0, 0);
        if (now >= target) {
            return qsTr("Отложить до 9:00 завтра");
        }
        return qsTr("Отложить до 9:00 сегодня");
    }

    // Computes the number of minutes from now until the next 9:00 AM
    // (today 9:00 if we're earlier, otherwise tomorrow 9:00). Always >= 1.
    function _minutesUntilMorning() {
        var now = new Date();
        var target = new Date();
        target.setHours(9, 0, 0, 0);
        if (now >= target) {
            target.setDate(target.getDate() + 1);
        }
        var diffMs = target.getTime() - now.getTime();
        var minutes = Math.round(diffMs / 60000);
        return minutes < 1 ? 1 : minutes;
    }

    // Computes the number of minutes from now until the next occurrence of a
    // given day-of-week at the given local time. dayOfWeek uses ISO numbering
    // (1=Mon..7=Sun); JS Date uses 0=Sun..6=Sat. If today is already the target
    // day-of-week and the target time hasn't passed yet, returns minutes until
    // today (not next week). Always >= 1.
    function _minutesUntilNextDayOfWeek(dayOfWeek /* 1=Mon..7=Sun */, hour, minute) {
        var now = new Date();
        var t = new Date();
        t.setHours(hour, minute, 0, 0);
        // JS: 0=Sun, 1=Mon, ..., 6=Sat. Convert: dayOfWeek 1=Mon -> JS 1.
        var jsTarget = (dayOfWeek === 7) ? 0 : dayOfWeek;
        var diff = (jsTarget - now.getDay() + 7) % 7;
        if (diff === 0 && now >= t) diff = 7;  // already past today
        t.setDate(t.getDate() + diff);
        return Math.max(1, Math.round((t - now) / 60000));
    }

    // Geometry
    radius: Radius.md
    implicitWidth:  Math.min(bannerContent.implicitWidth + Spacing.lg * 2,
                             400)
    implicitHeight: bannerContent.implicitHeight + Spacing.md * 2

    // Accent background (slightly translucent so theme shows through)
    color: Qt.rgba(ThemeManager.accentColor.r,
                   ThemeManager.accentColor.g,
                   ThemeManager.accentColor.b, 0.92)

    // Subtle shadow via a slightly larger, darker rect underneath
    Rectangle {
        anchors.fill:   parent
        anchors.topMargin:    2
        anchors.leftMargin:  -1
        anchors.rightMargin: -1
        radius:  parent.radius + 1
        color:   Qt.rgba(0, 0, 0, 0.18)
        z:       -1
    }

    // ── Content ───────────────────────────────────────────────────────────────
    ColumnLayout {
        id: bannerContent
        anchors {
            left:        parent.left
            right:       parent.right
            top:         parent.top
            leftMargin:  Spacing.lg
            rightMargin: Spacing.lg
            topMargin:   Spacing.md
        }
        spacing: 2

        RowLayout {
            Layout.fillWidth: true
            spacing: Spacing.sm

            Text {
                id: titleText
                text: root.bannerTitle
                color: ThemeManager.accentForeground
                font.pixelSize: FontSize.body
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                elide: Text.ElideRight
            }

            // Dismiss button — task 17cea65b iter-3: было unicode "✕".
            // Q4.01 night iter-24: was Qt.rgba(1,1,1,0.75) — white-on-yellow
            // invisible for sunflower. Use accentForeground + opacity instead.
            IconSvg {
                name: "close"
                size: 14
                color: ThemeManager.accentForeground
                opacity: 0.75
                Layout.alignment: Qt.AlignTop

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.dismissRequested(root.bannerIndex)
                }
            }
        }

        Text {
            id: bodyText
            visible: root.bannerBody.length > 0
            text: root.bannerBody
            // Q4.01 night iter-24: theme-aware contrast on accent gradient bg.
            color: ThemeManager.accentForeground
            opacity: 0.88
            font.pixelSize: FontSize.small
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        // "Открыть задачу" hint row — only shown when a taskId is set
        Text {
            visible: root.bannerTaskId.length > 0
            text: qsTr("Открыть задачу →")
            color: ThemeManager.accentForeground
            font.pixelSize: FontSize.small
            font.underline: true

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    taskViewModel.openTaskById(root.bannerTaskId)
                    root.dismissRequested(root.bannerIndex)
                }
            }
        }

        // ── Action buttons row ────────────────────────────────────────────────
        // Shown at the bottom of the banner. «Готово» is hidden when there is
        // no associated task. Each button pauses the auto-dismiss timer while
        // hovered so the user has time to read and act.
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Spacing.xs
            Layout.bottomMargin: Spacing.xs
            spacing: Spacing.xs

            // Helper component: a small flat action button
            // (defined inline so it can reference root without extra files)
            component ActionButton: Rectangle {
                id: btn
                property string label: ""
                property bool   visible_: true
                // Set to true by the outer HoverHandler so the color can react
                property bool   isHovered: false

                visible: visible_
                implicitHeight: 28
                implicitWidth:  btnLabel.implicitWidth + 20
                radius: Radius.sm

                // Base fill: surface color at low opacity; hover lightens it
                color: btn.isHovered
                       ? Qt.rgba(ThemeManager.surfaceColor.r,
                                 ThemeManager.surfaceColor.g,
                                 ThemeManager.surfaceColor.b, 0.32)
                       : Qt.rgba(ThemeManager.surfaceColor.r,
                                 ThemeManager.surfaceColor.g,
                                 ThemeManager.surfaceColor.b, 0.15)

                Behavior on color { ColorAnimation { duration: Duration.fast } }

                signal clicked()

                Text {
                    id: btnLabel
                    anchors.centerIn: parent
                    text: btn.label
                    color: ThemeManager.accentForeground
                    font.pixelSize: FontSize.small
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: btn.clicked()
                }
            }

            // «Готово» — completes the task and dismisses
            ActionButton {
                id: doneBtn
                label: qsTr("Готово")
                visible_: root.bannerTaskId.length > 0
                isHovered: doneHover.hovered

                HoverHandler {
                    id: doneHover
                    onHoveredChanged: root._anyButtonHovered = doneHover.hovered || snoozeHover.hovered || morningHover.hovered || closeHover.hovered || snoozeMenu.opened
                }

                onClicked: {
                    taskViewModel.completeTask(root.bannerTaskId)
                    root.dismissRequested(root.bannerIndex)
                }
            }

            // «Отложить» — opens a submenu so the user can pick 5/10/15/30/60 min
            ActionButton {
                id: snoozeBtn
                label: qsTr("Отложить")
                isHovered: snoozeHover.hovered || snoozeMenu.opened

                HoverHandler {
                    id: snoozeHover
                    onHoveredChanged: root._anyButtonHovered = doneHover.hovered || snoozeHover.hovered || morningHover.hovered || snoozeMenu.opened || closeHover.hovered
                }

                onClicked: snoozeMenu.open()

                Menu {
                    id: snoozeMenu
                    // Appear above the button so it doesn't overlap other banners below
                    y: -implicitHeight - 4

                    onOpened: root._anyButtonHovered = true
                    onClosed: root._anyButtonHovered = doneHover.hovered || snoozeHover.hovered || morningHover.hovered || closeHover.hovered

                    Repeater {
                        model: [5, 10, 15, 30, 60]
                        delegate: MenuItem {
                            text: modelData < 60
                                ? qsTr("На %1 минут").arg(modelData)
                                : qsTr("На 1 час")
                            onTriggered: {
                                root.snoozeRequested(root.bannerTaskId, modelData)
                                root.dismissRequested(root.bannerIndex)
                            }
                        }
                    }

                    MenuSeparator {}

                    // «📅 До понедельника 9:00» — snooze until next Monday 9 AM
                    // (today 9 AM if today is Monday and it's still before 9).
                    MenuItem {
                        // Q4.01 (redesign): эмодзи-префикс "📅" убран —
                        // контекст-меню snooze без иконок.
                        text: qsTr("До понедельника 9:00")
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                        ToolTip.text: qsTr("Отложить до понедельника 9:00")
                        onTriggered: {
                            var minutes = root._minutesUntilNextDayOfWeek(1, 9, 0);
                            // Telemetry's invokable is trackEvent(); track() never existed.
                            if (typeof telemetry !== "undefined" && telemetry !== null && telemetry.enabled) {
                                try {
                                    telemetry.trackEvent("banner_snooze_workweek",
                                                    { taskId: root.bannerTaskId });
                                } catch (e) { /* telemetry must never break UX */ }
                            }
                            root.snoozeRequested(root.bannerTaskId, minutes)
                            root.dismissRequested(root.bannerIndex)
                        }
                    }

                    // «🏖 До выходных» — snooze until next Saturday 9 AM
                    // (today 9 AM if today is Saturday and it's still before 9).
                    MenuItem {
                        text: qsTr("🏖 До выходных")
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                        ToolTip.text: qsTr("Отложить до субботы 9:00")
                        onTriggered: {
                            var minutes = root._minutesUntilNextDayOfWeek(6, 9, 0);
                            if (typeof telemetry !== "undefined" && telemetry !== null && telemetry.enabled) {
                                try {
                                    telemetry.trackEvent("banner_snooze_weekend",
                                                    { taskId: root.bannerTaskId });
                                } catch (e) { /* telemetry must never break UX */ }
                            }
                            root.snoozeRequested(root.bannerTaskId, minutes)
                            root.dismissRequested(root.bannerIndex)
                        }
                    }

                    MenuSeparator {}

                    MenuItem {
                        text: qsTr("Свой…")
                        onTriggered: customSnoozeDialog.open()
                    }
                }
            }

            // «До утра» — one-click snooze until the next 9:00 AM
            // (today 9:00 if it's still before morning, otherwise tomorrow 9:00).
            // Q4.01 (redesign): "🌅" префикс убран — соседние snooze-кнопки
            // тоже без иконок (Готово / Отложить / меню).
            ActionButton {
                id: morningBtn
                label: qsTr("До утра")
                visible_: root.bannerTaskId.length > 0
                isHovered: morningHover.hovered

                HoverHandler {
                    id: morningHover
                    onHoveredChanged: root._anyButtonHovered = doneHover.hovered || snoozeHover.hovered || morningHover.hovered || snoozeMenu.opened || closeHover.hovered
                }

                ToolTip.visible: morningHover.hovered
                ToolTip.delay: 400
                ToolTip.text: root._morningTooltip()

                onClicked: {
                    var minutes = root._minutesUntilMorning();
                    if (typeof telemetry !== "undefined" && telemetry !== null && telemetry.enabled) {
                        try {
                            telemetry.trackEvent("banner_snooze_morning",
                                            { taskId: root.bannerTaskId });
                        } catch (e) { /* telemetry must never break UX */ }
                    }
                    root.snoozeRequested(root.bannerTaskId, minutes)
                    root.dismissRequested(root.bannerIndex)
                }
            }

            // «Закрыть» — explicit dismiss button
            ActionButton {
                id: closeBtn
                label: qsTr("Закрыть")
                isHovered: closeHover.hovered

                HoverHandler {
                    id: closeHover
                    onHoveredChanged: root._anyButtonHovered = doneHover.hovered || snoozeHover.hovered || morningHover.hovered || snoozeMenu.opened || closeHover.hovered
                }

                onClicked: root.dismissRequested(root.bannerIndex)
            }
        }
    }

    // ── Tap anywhere on the banner body ───────────────────────────────────────
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        // Only consume the click if there's a task to open; otherwise let
        // the dismiss "×" handle it. We use z stacking rather than propagate
        // so the child MouseAreas (dismiss / open-task / action buttons) take priority.
        z: -1
        onClicked: {
            if (root.bannerTaskId.length > 0)
                taskViewModel.openTaskById(root.bannerTaskId)
            root.dismissRequested(root.bannerIndex)
        }
    }

    // ── Slide-in animation ────────────────────────────────────────────────────
    property real slideTarget: 0
    y: slideTarget
    Behavior on y { NumberAnimation { duration: Duration.normal; easing.type: Easing.OutCubic } }

    opacity: 1
    Behavior on opacity { NumberAnimation { duration: Duration.slow } }

    // ── Custom snooze dialog ──────────────────────────────────────────────────
    Dialog {
        id: customSnoozeDialog
        title: qsTr("Отложить на...")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel

        contentItem: ColumnLayout {
            spacing: Spacing.sm

            Label {
                text: qsTr("Минут:")
                color: ThemeManager.textColor
            }

            SpinBox {
                id: customSnoozeSpin
                from: 1
                to: 180
                stepSize: 5
                value: 15
                editable: true
                Layout.preferredWidth: 160
            }

            Label {
                text: customSnoozeSpin.value < 60
                    ? qsTr("Уведомление через %1 минут").arg(customSnoozeSpin.value)
                    : (customSnoozeSpin.value === 60
                        ? qsTr("Уведомление через 1 час")
                        : qsTr("Уведомление через %1 ч %2 мин")
                          .arg(Math.floor(customSnoozeSpin.value / 60))
                          .arg(customSnoozeSpin.value % 60))
                font.pixelSize: FontSize.small
                color: ThemeManager.secondaryTextColor
            }
        }

        onOpened: {
            root._anyButtonHovered = true
            customSnoozeSpin.forceActiveFocus()
        }

        onClosed: {
            root._anyButtonHovered = doneHover.hovered || snoozeHover.hovered || morningHover.hovered || snoozeMenu.opened || closeHover.hovered
        }

        onAccepted: {
            const minutes = customSnoozeSpin.value
            root.snoozeRequested(root.bannerTaskId, minutes)
            root.dismissRequested(root.bannerIndex)
        }
    }

    // ── Auto-fade timer (4 s) ─────────────────────────────────────────────────
    // Paused while _anyButtonHovered is true so the user can interact with the
    // action buttons without the banner disappearing under their cursor.
    Timer {
        id: dismissTimer
        interval: 4000
        running: !root._anyButtonHovered && !customSnoozeDialog.opened
        repeat: false
        onTriggered: root.dismissRequested(root.bannerIndex)
    }

    // F-7.18: pause dismiss timer while the user hovers over the banner body.
    // Action buttons set _anyButtonHovered directly via their own HoverHandlers;
    // this MouseArea covers the rest of the banner surface.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        // Let child MouseAreas (dismiss × / open-task / action buttons) receive clicks
        z: -2
        propagateComposedEvents: true
        onClicked: (mouse) => { mouse.accepted = false }

        onEntered: root._anyButtonHovered = true
        onExited: {
            // Only clear the flag if none of the named button HoverHandlers are active
            // and the snooze submenu is not open
            root._anyButtonHovered = doneHover.hovered || snoozeHover.hovered || morningHover.hovered || snoozeMenu.opened || closeHover.hovered
        }
    }
}
