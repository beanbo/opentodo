import QtQuick
import QtQuick.Controls
import OpenTodoUI

// MLabelsScreen — full-page mobile "Метки" (labels) browse screen.
// Ported from design_handoff_android_mobile/m-screens3.jsx MLabelsScreen
// (README section 12). Big collapsing-style top bar + a list of 60px rows:
// a tonal rounded tile with a "tag" icon, the label name, a per-label active
// task count, and a faint chevron. Tapping a row navigates the app to that
// label via sidebarViewModel.navigateToLabel(name).
Item {
    id: root
    anchors.fill: parent

    // Wired by MTopBar.onMenuClicked — host opens the drawer.
    signal menuRequested()
    // Emitted after navigating so the host can close this screen.
    signal labelChosen()

    // [{ name, color, count }] — rebuilt on load and whenever tasks change.
    property var _labels: []

    MDensity { id: mdens }

    function _rebuild() {
        var src = (typeof sidebarViewModel !== "undefined" && sidebarViewModel)
                  ? sidebarViewModel.labels : []
        if (!src) src = []

        // Count active (non-deleted, non-completed) tasks referencing each name.
        var counts = ({})
        var tasks = (typeof dataStore !== "undefined" && dataStore)
                    ? dataStore.allTasksData() : []
        if (tasks) {
            for (var i = 0; i < tasks.length; i++) {
                var tk = tasks[i]
                if (!tk || tk.isDeleted || tk.isCompleted) continue
                var ls = tk.labels
                if (!ls) continue
                for (var j = 0; j < ls.length; j++) {
                    var nm = ls[j]
                    counts[nm] = (counts[nm] || 0) + 1
                }
            }
        }

        var out = []
        for (var k = 0; k < src.length; k++) {
            var l = src[k]
            if (!l) continue
            out.push({
                name:  l.name,
                color: l.color,
                count: counts[l.name] || 0
            })
        }
        root._labels = out
    }

    function _scheduleRebuild() { Qt.callLater(root._rebuild) }

    Component.onCompleted: _rebuild()

    Connections {
        target: typeof dataStore !== "undefined" ? dataStore : null
        function onTasksChanged() { root._scheduleRebuild() }
    }
    Connections {
        target: typeof sidebarViewModel !== "undefined" ? sidebarViewModel : null
        function onLabelsChanged() { root._scheduleRebuild() }
    }

    // Background.
    Rectangle { anchors.fill: parent; color: ThemeManager.backgroundColor }

    MTopBar {
        id: topBar
        big: true
        title: qsTr("Метки")
        titleIconName: "tag"
        titleColor: ThemeManager.accentColor
        onMenuClicked: root.menuRequested()

        MIconButton {
            iconName: "plus"
            iconColor: ThemeManager.textColor
            iconSize: 22
            // New-label creation is not wired yet (no-op per spec).
            onClicked: {}
        }
    }

    ListView {
        id: list
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: topBar.bottom
        anchors.bottom: parent.bottom
        clip: true
        bottomMargin: 96
        boundsBehavior: Flickable.StopAtBounds
        model: root._labels

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        delegate: Item {
            id: rowItem
            width: list.width
            height: 60

            required property var modelData

            readonly property color _labelColor:
                rowItem.modelData && rowItem.modelData.color
                    ? rowItem.modelData.color
                    : ThemeManager.secondaryTextColor

            Rectangle {  // hairline separator at the bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: ThemeManager.hairlineColor
            }

            Row {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                spacing: 14

                // Tonal rounded tile with the tag icon.
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 38
                    height: 38
                    radius: 19
                    color: Qt.rgba(rowItem._labelColor.r,
                                   rowItem._labelColor.g,
                                   rowItem._labelColor.b, 0.12)

                    IconSvg {
                        anchors.centerIn: parent
                        name: "tag"
                        size: 19
                        color: rowItem._labelColor
                    }
                }

                // Label name (fills remaining width).
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 38 - 14   // tile + spacing
                           - countText.implicitWidth - chevron.width
                           - parent.spacing * 2
                    text: rowItem.modelData ? rowItem.modelData.name : ""
                    font.pixelSize: 16          // design 15.5 → rounded
                    font.weight: Font.DemiBold
                    color: ThemeManager.textColor
                    elide: Text.ElideRight
                }

                // Active task count.
                Text {
                    id: countText
                    anchors.verticalCenter: parent.verticalCenter
                    text: rowItem.modelData ? rowItem.modelData.count : 0
                    font.pixelSize: 14          // design 13.5 → rounded
                    font.weight: Font.DemiBold
                    color: ThemeManager.secondaryTextColor
                }

                IconSvg {
                    id: chevron
                    anchors.verticalCenter: parent.verticalCenter
                    name: "chevron-right"
                    size: 17
                    color: ThemeManager.faintTextColor
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (rowItem.modelData
                        && typeof sidebarViewModel !== "undefined"
                        && sidebarViewModel) {
                        sidebarViewModel.navigateToLabel(rowItem.modelData.name)
                    }
                    root.labelChosen()
                }
            }
        }
    }

    // Empty state.
    Column {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 24
        spacing: Spacing.md
        visible: !root._labels || root._labels.length === 0

        IconSvg {
            anchors.horizontalCenter: parent.horizontalCenter
            name: "tag"
            size: 48
            color: ThemeManager.faintTextColor
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Меток пока нет")
            font.pixelSize: FontSize.body
            font.weight: Font.Medium
            color: ThemeManager.secondaryTextColor
        }
    }
}
