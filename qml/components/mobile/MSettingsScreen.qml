import QtQuick
import QtQuick.Controls
import QtCore
import OpenTodoUI

// MSettingsScreen — mobile settings. Ported from m-screens2.jsx MSettingsScreen
// (README section 13): theme grid (9 palettes) + accent swatches + density
// segment + accessibility + notifications/data + about. Wires to settingsViewModel
// and ThemeManager.
Item {
    id: root
    signal menuRequested()
    signal aiRequested()
    signal syncRequested()

    Rectangle { anchors.fill: parent; color: ThemeManager.backgroundColor }

    MDensity { id: mdens }

    readonly property var _themes: [
        { "key": "classic_light", "label": qsTr("Светлая") },
        { "key": "dark",          "label": qsTr("Тёмная") },
        { "key": "graphite",      "label": qsTr("Графит") },
        { "key": "kale",          "label": qsTr("Капуста") },
        { "key": "blueberry",     "label": qsTr("Черника") },
        { "key": "lavender",      "label": qsTr("Лаванда") },
        { "key": "raspberry",     "label": qsTr("Малина") },
        { "key": "sunflower",     "label": qsTr("Подсолнух") },
        { "key": "highcontrast",  "label": qsTr("Контраст") }
    ]
    readonly property var _accents: ["#db4c3f","#e2574b","#e8702a","#eaa72a","#3aa84e","#2c9c8e","#2070d8","#6c88f4","#9b5de5","#e0608a"]

    property bool _push: true
    property bool _sync: false
    function _getBool(key, def) {
        var v = settingsViewModel.getSetting(key)
        if (v === undefined || v === null || v === "") return def
        return (v === true || v === "true" || v === 1 || v === "1")
    }
    Component.onCompleted: { _push = _getBool("mobile/push", true); _sync = _getBool("mobile/sync", false) }

    Settings {
        id: a11ySettings
        category: "mobileA11y"
        property bool largeFont: false
    }

    MTopBar {
        id: topBar
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        big: true
        title: qsTr("Настройки")
        titleIconName: "settings"
        titleColor: ThemeManager.secondaryTextColor
        onMenuClicked: root.menuRequested()
    }

    Flickable {
        anchors.top: topBar.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        contentHeight: col.implicitHeight + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: col
            width: parent.width
            spacing: 0

            // ── Оформление ─────────────────────────────────────────────────
            MSettingsSectionLabel { text: qsTr("Оформление") }
            Rectangle {
                width: parent.width - 24; x: 12
                height: appearanceCol.implicitHeight
                radius: 16; color: ThemeManager.surfaceColor
                border.width: 1; border.color: ThemeManager.borderColor
                clip: true
                Column {
                    id: appearanceCol
                    width: parent.width

                    // Theme grid
                    Column {
                        width: parent.width
                        leftPadding: 16; rightPadding: 16; topPadding: 14; bottomPadding: 14
                        spacing: 12
                        Text { text: qsTr("Тема"); font.pixelSize: 15; font.weight: Font.DemiBold; color: ThemeManager.textColor }
                        Grid {
                            width: parent.width - 32
                            columns: 3
                            columnSpacing: 10; rowSpacing: 10
                            Repeater {
                                model: root._themes
                                delegate: Item {
                                    id: swatch
                                    required property var modelData
                                    readonly property bool on: settingsViewModel.theme === modelData.key
                                    readonly property var pal: ThemeManager.themes[modelData.key]
                                    width: (parent.width - 20) / 3
                                    height: 74
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 12
                                        color: "transparent"
                                        border.width: 2
                                        border.color: swatch.on ? ThemeManager.accentColor : ThemeManager.borderColor
                                        Column {
                                            anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                                            anchors.margins: 4
                                            spacing: 4
                                            Rectangle {
                                                width: parent.width; height: 46; radius: 8
                                                color: swatch.pal ? swatch.pal.backgroundColor : ThemeManager.backgroundColor
                                                Column {
                                                    anchors.left: parent.left; anchors.right: parent.right
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    anchors.margins: 8
                                                    spacing: 4
                                                    Rectangle { width: parent.width * 0.65; height: 5; radius: 3; color: swatch.pal ? swatch.pal.accentColor : ThemeManager.accentColor }
                                                    Rectangle { width: parent.width * 0.9; height: 4; radius: 3; color: swatch.pal ? swatch.pal.textColor : ThemeManager.textColor; opacity: 0.4 }
                                                    Rectangle { width: parent.width * 0.5; height: 4; radius: 3; color: swatch.pal ? swatch.pal.textColor : ThemeManager.textColor; opacity: 0.22 }
                                                }
                                            }
                                            Text {
                                                width: parent.width
                                                horizontalAlignment: Text.AlignHCenter
                                                text: swatch.modelData.label
                                                font.pixelSize: 11; font.weight: Font.DemiBold
                                                color: swatch.on ? ThemeManager.accentColor : ThemeManager.secondaryTextColor
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                    MouseArea { anchors.fill: parent; onClicked: settingsViewModel.theme = swatch.modelData.key }
                                }
                            }
                        }
                    }
                    Rectangle { width: parent.width; height: 1; color: ThemeManager.hairlineColor }

                    // Accent swatches
                    Column {
                        width: parent.width
                        leftPadding: 16; rightPadding: 16; topPadding: 14; bottomPadding: 14
                        spacing: 12
                        Text { text: qsTr("Акцент"); font.pixelSize: 15; font.weight: Font.DemiBold; color: ThemeManager.textColor }
                        Flow {
                            width: parent.width - 32
                            spacing: 11
                            Repeater {
                                model: root._accents
                                delegate: Rectangle {
                                    required property var modelData
                                    readonly property bool on: settingsViewModel.customAccentHex.toLowerCase() === modelData.toLowerCase()
                                    width: 34; height: 34; radius: 17
                                    color: modelData
                                    border.width: on ? 2 : 0
                                    border.color: ThemeManager.textColor
                                    IconSvg { visible: parent.on; anchors.centerIn: parent; name: "check"; size: 16; color: "#ffffff" }
                                    MouseArea { anchors.fill: parent; onClicked: settingsViewModel.customAccentHex = parent.modelData }
                                }
                            }
                        }
                    }
                    Rectangle { width: parent.width; height: 1; color: ThemeManager.hairlineColor }

                    // Density
                    Column {
                        width: parent.width
                        leftPadding: 16; rightPadding: 16; topPadding: 14; bottomPadding: 14
                        spacing: 12
                        Text { text: qsTr("Плотность списка"); font.pixelSize: 15; font.weight: Font.DemiBold; color: ThemeManager.textColor }
                        MSeg {
                            width: parent.width - 32
                            value: settingsViewModel.compactDensity ? "balanced" : "spacious"
                            options: [
                                { "value": "spacious", "label": qsTr("Просторно") },
                                { "value": "balanced", "label": qsTr("Компактнее") }
                            ]
                            onChanged: function(v) { settingsViewModel.compactDensity = (v === "balanced") }
                        }
                    }
                }
            }

            // ── Доступность ────────────────────────────────────────────────
            MSettingsSectionLabel { text: qsTr("Доступность") }
            Rectangle {
                width: parent.width - 24; x: 12
                height: a11yCol.implicitHeight
                radius: 16; color: ThemeManager.surfaceColor
                border.width: 1; border.color: ThemeManager.borderColor
                clip: true
                Column {
                    id: a11yCol
                    width: parent.width
                    MSettingsRow {
                        width: parent.width
                        rowIcon: "eye"; rowIconColor: ThemeManager.priorityP3Color
                        rowLabel: qsTr("Крупный шрифт"); rowValue: qsTr("Увеличить размер текста")
                        controlChecked: ThemeManager.mobileLargeFont
                        onToggled: { ThemeManager.mobileLargeFont = !ThemeManager.mobileLargeFont; a11ySettings.largeFont = ThemeManager.mobileLargeFont }
                    }
                    MSettingsRow {
                        width: parent.width; last: true
                        rowIcon: "warning"; rowIconColor: ThemeManager.priorityP2Color
                        rowLabel: qsTr("Высокий контраст"); rowValue: qsTr("Тема для слабого зрения")
                        controlChecked: settingsViewModel.theme === "highcontrast"
                        onToggled: settingsViewModel.theme = (settingsViewModel.theme === "highcontrast" ? "dark" : "highcontrast")
                    }
                }
            }

            // ── Уведомления и данные ───────────────────────────────────────
            MSettingsSectionLabel { text: qsTr("Уведомления и данные") }
            Rectangle {
                width: parent.width - 24; x: 12
                height: notifCol.implicitHeight
                radius: 16; color: ThemeManager.surfaceColor
                border.width: 1; border.color: ThemeManager.borderColor
                clip: true
                Column {
                    id: notifCol
                    width: parent.width
                    MSettingsRow {
                        width: parent.width
                        rowIcon: "bell"; rowIconColor: ThemeManager.priorityP2Color
                        rowLabel: qsTr("Пуш-уведомления"); rowValue: qsTr("Напоминания о сроках")
                        controlChecked: root._push
                        onToggled: { root._push = !root._push; settingsViewModel.updateSetting("mobile/push", root._push) }
                    }
                    MSettingsRow {
                        width: parent.width
                        rowIcon: "sync"; rowIconColor: ThemeManager.priorityP3Color
                        rowLabel: qsTr("Синхронизация")
                        rowValue: settingsViewModel.syncBackend === "selfhost"
                                  ? qsTr("Свой ПК")
                                  : ((typeof gdriveClient !== "undefined" && gdriveClient.authenticated)
                                     ? qsTr("Google Drive") : qsTr("Настроить"))
                        showChevron: true
                        onRowClicked: root.syncRequested()
                    }
                    MSettingsRow {
                        width: parent.width; last: true
                        rowIcon: "sparkles"; rowIconColor: ThemeManager.accentColor
                        rowLabel: qsTr("AI-помощник"); rowValue: qsTr("Разбивка задач, подсказки")
                        showChevron: true
                        onRowClicked: root.aiRequested()
                    }
                }
            }

            // ── Прочее ─────────────────────────────────────────────────────
            MSettingsSectionLabel { text: qsTr("Прочее") }
            Rectangle {
                width: parent.width - 24; x: 12
                height: aboutCol.implicitHeight
                radius: 16; color: ThemeManager.surfaceColor
                border.width: 1; border.color: ThemeManager.borderColor
                clip: true
                Column {
                    id: aboutCol
                    width: parent.width
                    MSettingsRow {
                        width: parent.width
                        rowIcon: "star"; rowIconColor: ThemeManager.accentColor
                        rowLabel: qsTr("Поддержать разработчика")
                        showChevron: true
                    }
                    MSettingsRow {
                        width: parent.width; last: true
                        rowIcon: "note"; rowIconColor: ThemeManager.secondaryTextColor
                        rowLabel: qsTr("О приложении")
                        rowValue: "OpenTodo" + ((typeof updateChecker !== "undefined" && updateChecker) ? (" " + updateChecker.currentVersion) : "")
                        showChevron: true
                    }
                }
            }

            Item { width: 1; height: 8 }
        }
    }
}
