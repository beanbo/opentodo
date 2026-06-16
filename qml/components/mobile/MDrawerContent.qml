import QtQuick
import QtQuick.Controls
import OpenTodoUI

// MDrawerContent — mobile navigation drawer. Ported from m-screens.jsx MDrawer:
// profile header + smart lists + projects (with pin toggles) + Метки/Фильтры/
// Корзина + Settings footer. Navigation via sidebarViewModel; pin toggles and
// browse-screen requests bubble up to MobileLayout.
Rectangle {
    id: root
    color: ThemeManager.surfaceColor

    property var pins: []
    signal pinToggleRequested(string id)
    signal browseRequested(string which)   // "labels" | "filters"

    function isActive(view, itemId) {
        if (sidebarViewModel.currentView !== view) return false
        if (itemId === undefined || itemId === null) return true
        return sidebarViewModel.currentItemId === itemId
    }

    // Profile header
    Item {
        id: profile
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: 78
        Rectangle {
            id: avatar
            anchors.left: parent.left; anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: 12
            width: 46; height: 46; radius: 23
            color: ThemeManager.accentColor
            Text { anchors.centerIn: parent; text: "O"; color: ThemeManager.accentForeground; font.pixelSize: 18; font.weight: Font.Bold }
        }
        Column {
            anchors.left: avatar.right; anchors.leftMargin: 13
            anchors.right: syncIc.left; anchors.rightMargin: 8
            anchors.verticalCenter: avatar.verticalCenter
            spacing: 3
            Text { text: "OpenTodo"; font.pixelSize: 15; font.weight: Font.Bold; color: ThemeManager.textColor }
            Row {
                spacing: 5
                Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 8; height: 8; radius: 4; color: ThemeManager.secondaryTextColor }
                Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Офлайн · локально"); font.pixelSize: 12; color: ThemeManager.secondaryTextColor }
            }
        }
        IconSvg {
            id: syncIc
            anchors.right: parent.right; anchors.rightMargin: 18
            anchors.verticalCenter: avatar.verticalCenter
            name: "sync"; size: 20; color: ThemeManager.secondaryTextColor
        }
    }

    Flickable {
        id: flick
        anchors.top: profile.bottom; anchors.left: parent.left; anchors.right: parent.right
        anchors.bottom: settingsFooter.top
        contentHeight: navCol.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: navCol
            width: parent.width
            leftPadding: 12; rightPadding: 12; topPadding: 6; bottomPadding: 12
            spacing: 0

            MDrawerItem { width: parent.width - 24; itemIcon: "inbox"; itemLabel: qsTr("Входящие"); count: sidebarViewModel.inboxCount; active: root.isActive("inbox"); onClicked: sidebarViewModel.navigateToInbox() }
            MDrawerItem { width: parent.width - 24; itemIcon: "today"; itemLabel: qsTr("Сегодня"); count: sidebarViewModel.todayCount; active: root.isActive("today"); onClicked: sidebarViewModel.navigateToToday() }
            MDrawerItem { width: parent.width - 24; itemIcon: "upcoming"; itemLabel: qsTr("Предстоящие"); active: root.isActive("upcoming"); onClicked: sidebarViewModel.navigateToUpcoming() }
            MDrawerItem {
                width: parent.width - 24; itemIcon: "bell"; itemLabel: qsTr("Уведомления")
                badge: (typeof notificationsViewModel !== "undefined" && notificationsViewModel.unreadCount > 0) ? notificationsViewModel.unreadCount : -1
                active: root.isActive("notifications"); onClicked: sidebarViewModel.navigateTo("notifications")
            }
            MDrawerItem { width: parent.width - 24; itemIcon: "star"; itemLabel: qsTr("Продуктивность"); active: root.isActive("productivity"); onClicked: sidebarViewModel.navigateTo("productivity") }
            MDrawerItem { width: parent.width - 24; itemIcon: "sparkles"; itemLabel: qsTr("Помощник"); active: root.isActive("ai"); onClicked: sidebarViewModel.navigateTo("ai") }

            // Projects header
            Item {
                width: parent.width - 24; height: 34
                Text { anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter; text: qsTr("Проекты"); font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 0.4; font.capitalization: Font.AllUppercase; color: ThemeManager.secondaryTextColor }
            }
            Text {
                width: parent.width - 24; leftPadding: 14; bottomPadding: 8
                wrapMode: Text.WordWrap
                text: qsTr("Нажмите на булавку, чтобы закрепить проект в нижней панели (%1/3)").arg(root.pins.length)
                font.pixelSize: 12; color: ThemeManager.faintTextColor
            }

            Repeater {
                model: projectViewModel
                delegate: MDrawerItem {
                    required property var projectObject
                    required property string id
                    visible: projectObject && !projectObject.isInbox && !projectObject.isArchived
                    height: visible ? 50 : 0
                    width: navCol.width - 24
                    itemIcon: (projectObject && projectObject.icon && projectObject.icon.length > 0) ? projectObject.icon : "hash"
                    itemIconColor: (projectObject && projectObject.color && projectObject.color.length > 0) ? projectObject.color : ThemeManager.accentColor
                    itemLabel: projectObject ? projectObject.name : ""
                    active: root.isActive("project", id)
                    showPin: true
                    pinned: root.pins.indexOf(id) >= 0
                    onClicked: sidebarViewModel.navigateToProject(id)
                    onPinClicked: root.pinToggleRequested(id)
                }
            }

            Rectangle { width: parent.width - 24; height: 1; color: ThemeManager.borderColor; y: 0 }
            Item { width: 1; height: 12 }

            MDrawerItem { width: parent.width - 24; itemIcon: "tag"; itemLabel: qsTr("Метки"); active: root.isActive("label"); onClicked: root.browseRequested("labels") }
            MDrawerItem { width: parent.width - 24; itemIcon: "filter"; itemLabel: qsTr("Фильтры"); active: root.isActive("filter"); onClicked: root.browseRequested("filters") }
            MDrawerItem { width: parent.width - 24; itemIcon: "trash"; itemLabel: qsTr("Корзина"); active: root.isActive("trash"); onClicked: sidebarViewModel.navigateTo("trash") }
        }
    }

    // Settings footer
    Rectangle {
        id: settingsFooter
        anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
        height: 56
        color: ThemeManager.surfaceColor
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: ThemeManager.borderColor }
        Row {
            anchors.left: parent.left; anchors.leftMargin: 26
            anchors.verticalCenter: parent.verticalCenter
            spacing: 15
            IconSvg { anchors.verticalCenter: parent.verticalCenter; name: "settings"; size: 21; color: ThemeManager.secondaryTextColor }
            Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Настройки"); font.pixelSize: 15; font.weight: Font.DemiBold; color: ThemeManager.textColor }
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: sidebarViewModel.navigateTo("settings") }
    }
}
