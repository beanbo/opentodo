import QtQuick
import QtQuick.Controls
import OpenTodoUI

// MProductivityScreen — full-page mobile productivity / karma view. Ported from
// m-screens3.jsx MProductivityScreen (README section 10). Big collapsing-style
// top bar + scrollable content: streak card + today progress-ring card, a
// weekly bar chart, and a karma card with a milestone progress bar.
// Reads the `karmaViewModel` context property.
Item {
    id: root
    anchors.fill: parent

    signal menuRequested()

    // ── Density token shared with task rows (none here, but kept per convention) ──
    MDensity { id: mdens }

    // ── Live karma fields ────────────────────────────────────────────────────
    readonly property int    karmaScore:        karmaViewModel.karmaScore
    readonly property string karmaLevel:         karmaViewModel.karmaLevel
    readonly property int    dailyStreak:        karmaViewModel.dailyStreak
    readonly property int    completedToday:     karmaViewModel.completedToday
    readonly property int    dailyGoal:          karmaViewModel.dailyGoal
    readonly property int    completedThisWeek:  karmaViewModel.completedThisWeek
    readonly property real   dailyProgress:      karmaViewModel.dailyProgress
    readonly property int    nextMilestone:      karmaViewModel.nextMilestone
    readonly property int    previousMilestone:  karmaViewModel.previousMilestone

    readonly property int    dailyPct: Math.round(dailyProgress * 100)
    readonly property int    toNextMilestone: Math.max(0, nextMilestone - karmaScore)
    readonly property real   milestoneProgress: {
        var span = nextMilestone - previousMilestone
        if (span <= 0) return 1
        return Math.max(0, Math.min(1, (karmaScore - previousMilestone) / span))
    }

    // ── Weekly data (oldest→newest, 7 days) ──────────────────────────────────
    property var week: []
    function _refreshWeek() { week = karmaViewModel.completionsByDay(7) }
    Component.onCompleted: _refreshWeek()
    Connections {
        target: karmaViewModel
        function onKarmaChanged() { root._refreshWeek() }
    }

    readonly property var weekdayLabels: ["Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"]
    readonly property int weekMax: {
        var m = 0
        for (var i = 0; i < week.length; i++)
            if (week[i].count > m) m = week[i].count
        return Math.max(m, dailyGoal, 1)
    }

    function _weekdayLabel(dateStr) {
        var d = new Date(dateStr)
        var idx = d.getDay()
        if (isNaN(idx)) return ""
        return weekdayLabels[idx]
    }

    Rectangle { anchors.fill: parent; color: ThemeManager.backgroundColor }

    // ── Top bar ───────────────────────────────────────────────────────────────
    MTopBar {
        id: topBar
        big: true
        title: qsTr("Продуктивность")
        titleIconName: "star"
        titleColor: ThemeManager.accentColor
        subtitle: qsTr("Уровень: %1 · %2 кармы").arg(root.karmaLevel).arg(root.karmaScore)
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        onMenuClicked: root.menuRequested()
    }

    // ── Scrollable content ─────────────────────────────────────────────────────
    Flickable {
        id: flick
        anchors.top: topBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        contentWidth: width
        contentHeight: content.implicitHeight
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: content
            width: flick.width
            topPadding: 4
            bottomPadding: 96
            leftPadding: 14
            rightPadding: 14
            spacing: 14

            // ── 1. Top cards row: streak + today ring ────────────────────────
            Row {
                width: parent.width - parent.leftPadding - parent.rightPadding
                spacing: 12

                readonly property real cardW: (width - spacing) / 2

                // (a) Streak card
                Rectangle {
                    width: parent.cardW
                    height: streakCol.implicitHeight + 32
                    radius: 16
                    color: ThemeManager.surfaceColor
                    border.width: 1
                    border.color: ThemeManager.borderColor

                    Column {
                        id: streakCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 6

                        Row {
                            spacing: 8
                            IconSvg {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "flame"
                                size: 22
                                color: ThemeManager.warningColor
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: root.dailyStreak
                                font.pixelSize: 30
                                font.weight: Font.Bold
                                color: ThemeManager.textColor
                            }
                        }
                        Text {
                            text: qsTr("дней подряд")
                            font.pixelSize: 13
                            color: ThemeManager.secondaryTextColor
                        }
                    }
                }

                // (b) Today ring card
                Rectangle {
                    width: parent.cardW
                    height: 88
                    radius: 16
                    color: ThemeManager.surfaceColor
                    border.width: 1
                    border.color: ThemeManager.borderColor

                    Row {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 16
                        anchors.rightMargin: 14
                        spacing: 14

                        // Progress ring
                        Item {
                            width: 56
                            height: 56
                            anchors.verticalCenter: parent.verticalCenter

                            Canvas {
                                id: ring
                                anchors.fill: parent
                                property real prog: root.dailyProgress
                                property color trackColor: ThemeManager.hairlineColor
                                property color arcColor: ThemeManager.accentColor
                                property color holeColor: ThemeManager.surfaceColor
                                onProgChanged: requestPaint()
                                onTrackColorChanged: requestPaint()
                                onArcColorChanged: requestPaint()
                                onHoleColorChanged: requestPaint()
                                onPaint: {
                                    var ctx = getContext("2d")
                                    ctx.reset()
                                    var cx = width / 2
                                    var cy = height / 2
                                    var rOuter = Math.min(width, height) / 2
                                    var rInner = rOuter - 7   // ring thickness
                                    var start = -Math.PI / 2  // top
                                    var end = start + Math.max(0, Math.min(1, prog)) * 2 * Math.PI

                                    // Track circle
                                    ctx.beginPath()
                                    ctx.arc(cx, cy, rOuter, 0, 2 * Math.PI)
                                    ctx.fillStyle = trackColor
                                    ctx.fill()

                                    // Accent arc (clockwise from top)
                                    if (prog > 0) {
                                        ctx.beginPath()
                                        ctx.moveTo(cx, cy)
                                        ctx.arc(cx, cy, rOuter, start, end, false)
                                        ctx.closePath()
                                        ctx.fillStyle = arcColor
                                        ctx.fill()
                                    }

                                    // Inner hole → reads as a ring
                                    ctx.beginPath()
                                    ctx.arc(cx, cy, rInner, 0, 2 * Math.PI)
                                    ctx.fillStyle = holeColor
                                    ctx.fill()
                                }
                            }

                            Text {
                                anchors.centerIn: parent
                                text: root.dailyPct + "%"
                                font.pixelSize: 13
                                font.weight: Font.Bold
                                color: ThemeManager.textColor
                            }
                        }

                        // N/M + label
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Text {
                                text: root.completedToday + "/" + root.dailyGoal
                                font.pixelSize: 20
                                font.weight: Font.Bold
                                color: ThemeManager.textColor
                            }
                            Text {
                                text: qsTr("сегодня")
                                font.pixelSize: 13
                                color: ThemeManager.secondaryTextColor
                            }
                        }
                    }
                }
            }

            // ── 2. Weekly bar chart card ─────────────────────────────────────
            Rectangle {
                width: parent.width - parent.leftPadding - parent.rightPadding
                height: weekCol.implicitHeight + 28
                radius: 16
                color: ThemeManager.surfaceColor
                border.width: 1
                border.color: ThemeManager.borderColor

                Column {
                    id: weekCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    anchors.topMargin: 16
                    spacing: 14

                    // Header
                    Item {
                        width: parent.width
                        height: weekTitle.implicitHeight

                        Text {
                            id: weekTitle
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Эта неделя")
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            color: ThemeManager.textColor
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("%1 задач").arg(root.completedThisWeek)
                            font.pixelSize: 13
                            color: ThemeManager.secondaryTextColor
                        }
                    }

                    // Bars
                    Row {
                        id: barsRow
                        width: parent.width
                        height: 120
                        spacing: 8

                        readonly property real slotW:
                            (width - spacing * Math.max(0, root.week.length - 1))
                            / Math.max(1, root.week.length)

                        Repeater {
                            model: root.week
                            delegate: Column {
                                width: barsRow.slotW
                                height: barsRow.height
                                spacing: 7

                                readonly property int dayCount: modelData.count
                                readonly property bool metGoal: dayCount >= root.dailyGoal

                                // Bar area (fills remaining vertical space above label)
                                Item {
                                    width: parent.width
                                    height: parent.height - 7 - weekdayText.implicitHeight

                                    Rectangle {
                                        anchors.bottom: parent.bottom
                                        width: parent.width
                                        height: Math.max(6, parent.height
                                                * (dayCount / root.weekMax))
                                        radius: 7
                                        color: metGoal
                                               ? ThemeManager.accentColor
                                               : Qt.rgba(ThemeManager.accentColor.r,
                                                         ThemeManager.accentColor.g,
                                                         ThemeManager.accentColor.b,
                                                         0.4)
                                    }
                                }

                                Text {
                                    id: weekdayText
                                    width: parent.width
                                    horizontalAlignment: Text.AlignHCenter
                                    text: root._weekdayLabel(modelData.date)
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    color: ThemeManager.secondaryTextColor
                                }
                            }
                        }
                    }
                }
            }

            // ── 3. Karma card ─────────────────────────────────────────────────
            Rectangle {
                width: parent.width - parent.leftPadding - parent.rightPadding
                height: 88
                radius: 16
                color: ThemeManager.surfaceColor
                border.width: 1
                border.color: ThemeManager.borderColor

                Row {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 14

                    // Accent star tile
                    Rectangle {
                        width: 44
                        height: 44
                        radius: 22
                        anchors.verticalCenter: parent.verticalCenter
                        color: Qt.rgba(ThemeManager.accentColor.r,
                                       ThemeManager.accentColor.g,
                                       ThemeManager.accentColor.b,
                                       0.12)
                        IconSvg {
                            anchors.centerIn: parent
                            name: "star"
                            size: 22
                            color: ThemeManager.accentColor
                        }
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 44 - parent.spacing
                        spacing: 8

                        Text {
                            width: parent.width
                            text: qsTr("%1 кармы · %2").arg(root.karmaScore).arg(root.karmaLevel)
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            color: ThemeManager.textColor
                            elide: Text.ElideRight
                        }

                        // Milestone progress bar
                        Rectangle {
                            width: parent.width
                            height: 6
                            radius: 3
                            color: ThemeManager.hairlineColor
                            clip: true
                            Rectangle {
                                width: parent.width * root.milestoneProgress
                                height: parent.height
                                radius: 3
                                color: ThemeManager.accentColor
                            }
                        }

                        Text {
                            width: parent.width
                            text: qsTr("До следующего уровня: %1").arg(root.toNextMilestone)
                            font.pixelSize: 12
                            color: ThemeManager.secondaryTextColor
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
}
