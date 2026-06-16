import QtQuick
import QtQuick.Controls
import OpenTodoUI

// MFiltersScreen — full-page "Фильтры" browse view (mobile Android redesign).
// Ported from design_handoff_android_mobile/m-screens3.jsx MFiltersScreen
// (README section 12). Big collapsing-style top bar with a "plus" action,
// then a ListView of 64px rows: a tinted rounded tile holding the "filter"
// icon, a name + monospace query column, and a faint chevron. Tapping a row
// navigates the sidebar to that saved filter.
//
// The count number is intentionally omitted — filterViewModel exposes no
// reliable per-filter task count role (id/name/query/color/order/isFavorite/
// filterObject only), so the design's trailing count is dropped rather than
// faked.
Item {
    id: root
    anchors.fill: parent

    // Host-wired signals (mirror the other mobile screens).
    signal menuRequested()
    signal taskClicked(string id)

    // One density instance, drilled down to any density-aware children.
    MDensity { id: mdens }

    // Resolve a stored filter color string to a usable colour. The model's
    // default is the named token "charcoal" (also "grey"); treat those and the
    // empty string as "unset" → fall back to the accent colour, matching how
    // MTaskRow / TaskItem handle unset project & label colours.
    function _filterColor(c) {
        if (!c || c === "" || c === "charcoal" || c === "grey")
            return ThemeManager.accentColor
        return c
    }

    Rectangle {
        anchors.fill: parent
        color: ThemeManager.backgroundColor
    }

    MTopBar {
        id: topBar
        big: true
        title: qsTr("Фильтры")
        titleIconName: "filter"
        titleColor: ThemeManager.accentColor

        MIconButton {
            iconName: "plus"
            iconColor: ThemeManager.textColor
            iconSize: 22
            // Создание фильтра — пикер вне этого экрана (no-op here).
        }
    }

    ListView {
        id: list
        anchors.top: topBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        topMargin: mdens.listTopGap
        bottomMargin: 96          // clearance for the floating nav bar / FAB
        boundsBehavior: Flickable.StopAtBounds
        model: filterViewModel

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        delegate: Item {
            id: row
            width: ListView.view ? ListView.view.width : 0
            height: 64

            required property string id
            required property string name
            required property string query
            required property string color

            readonly property color _tileColor: root._filterColor(row.color)

            Rectangle {  // bottom hairline
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: ThemeManager.hairlineColor
            }

            // Tinted rounded tile with the "filter" icon.
            Rectangle {
                id: tile
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                width: 38
                height: 38
                radius: 12
                color: Qt.rgba(row._tileColor.r, row._tileColor.g, row._tileColor.b, 0.12)
                IconSvg {
                    anchors.centerIn: parent
                    name: "filter"
                    size: 19
                    color: row._tileColor
                }
            }

            // Name + monospace query.
            Column {
                anchors.left: tile.right
                anchors.leftMargin: 14
                anchors.right: chevron.left
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

                Text {
                    width: parent.width
                    text: row.name
                    color: ThemeManager.textColor
                    font.pixelSize: 16          // design 15.5 → rounded
                    font.weight: Font.DemiBold  // 600
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    visible: row.query !== ""
                    text: row.query
                    color: ThemeManager.faintTextColor
                    font.pixelSize: 13          // design 12.5 → rounded
                    font.family: "Consolas"
                    font.styleName: "monospace"
                    elide: Text.ElideRight
                }
            }

            IconSvg {
                id: chevron
                anchors.right: parent.right
                anchors.rightMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                name: "chevron-right"
                size: 17
                color: ThemeManager.faintTextColor
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: sidebarViewModel.navigateToFilter(row.id)
            }
        }

        // Empty state: "filter" icon + "Фильтров пока нет".
        Column {
            anchors.centerIn: parent
            visible: list.count === 0
            spacing: 12

            IconSvg {
                anchors.horizontalCenter: parent.horizontalCenter
                name: "filter"
                size: 40
                color: ThemeManager.faintTextColor
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Фильтров пока нет")
                color: ThemeManager.secondaryTextColor
                font.pixelSize: 15
            }
        }
    }
}
