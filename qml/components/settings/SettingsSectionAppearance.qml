import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtCore
import OpenTodoUI

Rectangle {
    id: root
    radius: 8
    color: ThemeManager.surfaceColor
    border.color: ThemeManager.borderColor
    border.width: 1
    implicitHeight: sectionHeader.height + (expanded ? sectionContent.implicitHeight : 0)
    clip: true

    property bool expanded: true

    // Quick-pick swatch palette for the custom-accent picker (red, orange,
    // yellow, green, blue, purple).
    readonly property var quickAccentSwatches: [
        "#E53935", // red
        "#FB8C00", // orange
        "#FDD835", // yellow
        "#43A047", // green
        "#1E88E5", // blue
        "#8E24AA"  // purple
    ]

    // Localised display name for a theme key
    function themeLabel(name) {
        switch (name) {
        case "system":        return qsTr("Системная")
        case "classic_light": return qsTr("Светлая")
        case "dark":          return qsTr("Тёмная")
        case "kale":          return qsTr("Капуста")
        case "blueberry":     return qsTr("Черника")
        case "lavender":      return qsTr("Лаванда")
        case "raspberry":     return qsTr("Малина")
        case "graphite":      return qsTr("Графит")
        case "sunflower":     return qsTr("Подсолнух")
        case "highcontrast":  return qsTr("Высокая контрастность")
        default:              return name.charAt(0).toUpperCase() + name.slice(1)
        }
    }

    Behavior on implicitHeight {
        NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
    }

    // ── Header ────────────────────────────────────────────────────────────────
    Rectangle {
        id: sectionHeader
        width: parent.width
        height: 48
        color: "transparent"
        radius: 8

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20

            Text {
                text: qsTr("Тема")
                font.pixelSize: FontSize.body
                font.weight: Font.DemiBold
                color: ThemeManager.textColor
                Layout.fillWidth: true
            }

            // Q4.01 (redesign): chevron unicode → stroke IconSvg
            IconSvg {
                name: root.expanded ? "chevron-down" : "chevron-right"
                size: 14
                color: ThemeManager.secondaryTextColor
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            height: 1
            color: ThemeManager.borderColor
            visible: root.expanded
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.expanded = !root.expanded
        }
    }

    // ── Content ───────────────────────────────────────────────────────────────
    ColumnLayout {
        id: sectionContent
        anchors.top: sectionHeader.bottom
        width: parent.width
        spacing: 0

        // Theme cards
        Flow {
            Layout.fillWidth: true
            Layout.margins: 20
            spacing: 12

            Repeater {
                model: settingsViewModel.availableThemes()

                delegate: Rectangle {
                    id: themeCard
                    required property string modelData
                    required property int index

                    // Resolve real palette colours from ThemeManager.
                    // "system" is not a real palette — preview the current
                    // system-resolved palette (dark or classic_light).
                    readonly property QtObject themeData:
                        modelData === "system"
                        ? ThemeManager.themes[ThemeManager.systemPrefersDark ? "dark" : "classic_light"]
                        : (ThemeManager.themes[modelData] || null)

                    width: 130
                    height: 90
                    radius: 8
                    color: themeData ? themeData.backgroundColor
                                     : (modelData === "dark" ? "#1f1f1f" : "#fafafa")
                    border.color: settingsViewModel.theme === modelData
                                  ? ThemeManager.accentColor
                                  : ThemeManager.borderColor
                    border.width: settingsViewModel.theme === modelData ? 2 : 1

                    // Mini UI preview strips
                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 5

                        Rectangle {
                            width: 56; height: 5; radius: 2.5
                            color: themeCard.themeData ? themeCard.themeData.surfaceColor : "#d8d8d8"
                            Layout.alignment: Qt.AlignHCenter
                        }
                        Rectangle {
                            width: 72; height: 5; radius: 2.5
                            color: themeCard.themeData ? themeCard.themeData.cardColor : "#c8c8c8"
                            Layout.alignment: Qt.AlignHCenter
                        }
                        Rectangle {
                            width: 40; height: 5; radius: 2.5
                            color: themeCard.themeData ? themeCard.themeData.accentColor : ThemeManager.accentColor
                            opacity: 0.85
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }

                    // Theme name label
                    // Q4.01 night iter-55: was anchored to horizontalCenter with no
                    // width constraint, so the long "Высокая контрастность" overflowed
                    // both edges of the 130px tile (short names looked fine). Constrain
                    // to the tile width and auto-shrink long names via HorizontalFit
                    // (down to 9px, then elide) so the full accessibility name stays
                    // readable and contained.
                    Text {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottomMargin: 8
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        horizontalAlignment: Text.AlignHCenter
                        text: root.themeLabel(modelData)
                        font.pixelSize: 12
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: 9
                        elide: Text.ElideRight
                        font.bold: settingsViewModel.theme === modelData
                        color: themeCard.themeData ? themeCard.themeData.textColor : ThemeManager.textColor
                    }

                    // Selected checkmark
                    Rectangle {
                        visible: settingsViewModel.theme === modelData
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.topMargin: 6
                        anchors.rightMargin: 6
                        width: 18; height: 18; radius: 9
                        color: themeCard.themeData ? themeCard.themeData.accentColor : ThemeManager.accentColor

                        // Task 17cea65b iter-3: было unicode "✓".
                        // Iter-night-23: was hardcoded "white". The badge
                        // sits on the THEME PREVIEW's own accentColor
                        // (themeData.accentColor), not the current theme's
                        // — so the foreground must come from the preview
                        // palette's accentForeground too. Otherwise the
                        // checkmark on the high-contrast / sunflower
                        // swatches would be white-on-yellow / white-on-gold
                        // and fail WCAG even though the active theme is
                        // something else entirely.
                        IconSvg {
                            anchors.centerIn: parent
                            name: "check"
                            size: 11
                            color: themeCard.themeData
                                   ? themeCard.themeData.accentForeground
                                   : ThemeManager.accentForeground
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (modelData === settingsViewModel.theme)
                                return
                            settingsViewModel.updateSetting("theme", modelData)
                            if (typeof telemetry !== "undefined" && telemetry.enabled)
                                telemetry.trackEvent("settings_theme_changed", { "theme": modelData })
                        }
                    }
                }
            }
        }

        // ── Divider above the custom-accent block ────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            height: 1
            color: ThemeManager.borderColor
        }

        // ── Custom accent override ───────────────────────────────────────────
        ColumnLayout {
            id: customAccentBlock
            Layout.fillWidth: true
            Layout.margins: 20
            spacing: 10

            // Persistence for the custom accent override. The ThemeManager
            // singleton can't import Qt.labs.settings (would fail at engine
            // startup for the singleton), so persistence lives here. We mirror
            // the persisted value into ThemeManager.customAccentHex on startup
            // and on every change.
            //
            // We also push the value through SettingsViewModel.customAccentHex
            // so it lands in DataStore (state.json) where C++ services —
            // notably TrayIconService for the badge tint — can read it without
            // having to reach into QML. The Qt.labs.settings backing stays as
            // the authoritative QML-side store so the previous on-disk format
            // continues to load cleanly for upgrades.
            Settings {
                id: accentSettings
                category: "appearance"
                property string customAccentHex: ""
            }

            Component.onCompleted: {
                // On first launch after upgrade the QSettings value may exist
                // while SettingsVM has nothing — prefer the QSettings value so
                // we don't lose the user's chosen accent. Otherwise prefer the
                // SettingsVM value (canonical source going forward).
                var vmHex = settingsViewModel.customAccentHex
                var qsHex = accentSettings.customAccentHex
                var initial = (vmHex && vmHex.length === 7) ? vmHex
                            : (qsHex && qsHex.length === 7) ? qsHex
                            : ""
                if (initial.length === 7) {
                    ThemeManager.setCustomAccent(initial)
                    if (vmHex !== initial)
                        settingsViewModel.customAccentHex = initial
                    if (qsHex !== initial)
                        accentSettings.customAccentHex = initial
                }
            }

            Connections {
                target: ThemeManager
                function onCustomAccentHexChanged() {
                    accentSettings.customAccentHex = ThemeManager.customAccentHex
                    // Mirror into the C++ settings VM so DataStore (and the
                    // tray-icon badge) sees the same value. The setter
                    // accepts "" to mean "reset to palette default".
                    if (settingsViewModel.customAccentHex !== ThemeManager.customAccentHex)
                        settingsViewModel.customAccentHex = ThemeManager.customAccentHex
                }
            }

            // Section title
            Text {
                text: qsTr("Свой цвет акцента")
                font.pixelSize: FontSize.body
                font.weight: Font.DemiBold
                color: ThemeManager.textColor
            }

            // Row 1: live swatch + hex input + Apply + Reset
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                // Live swatch — reflects whatever ThemeManager.accentColor
                // currently resolves to (override or palette default).
                Rectangle {
                    id: accentPreviewSwatch
                    width: 24
                    height: 24
                    radius: 6
                    color: ThemeManager.accentColor
                    border.color: ThemeManager.borderColor
                    border.width: 1
                }

                TextField {
                    id: accentHexField
                    Layout.preferredWidth: 130
                    placeholderText: "#FF6F00"
                    selectByMouse: true
                    color: ThemeManager.textColor
                    // Show whatever override is currently active so the user
                    // can edit it. Empty if no override.
                    text: ThemeManager.customAccentHex
                    onAccepted: applyAccentButton.applyHex()
                }

                Button {
                    id: applyAccentButton
                    text: qsTr("Применить")
                    Material.background: ThemeManager.accentColor
                    Material.foreground: ThemeManager.accentForeground

                    function applyHex() {
                        var ok = ThemeManager.setCustomAccent(accentHexField.text)
                        if (ok) {
                            customAccentError.visible = false
                            // Brief visual feedback: flash the caption green.
                            feedbackFlash.start()
                        } else {
                            customAccentError.visible = true
                        }
                    }

                    onClicked: applyHex()
                }

                Button {
                    text: qsTr("Сбросить")
                    flat: true
                    Material.foreground: ThemeManager.textColor
                    onClicked: {
                        ThemeManager.resetCustomAccent()
                        accentHexField.text = ""
                        customAccentError.visible = false
                    }
                }

                Item { Layout.fillWidth: true }
            }

            // Row 2: quick-pick swatches
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: qsTr("Быстрый выбор:")
                    font.pixelSize: FontSize.caption
                    color: ThemeManager.secondaryTextColor
                }

                Repeater {
                    model: root.quickAccentSwatches

                    delegate: Rectangle {
                        id: swatchDelegate
                        required property string modelData
                        width: 24
                        height: 24
                        radius: 12
                        color: swatchDelegate.modelData
                        border.color: ThemeManager.customAccentHex.toLowerCase() === swatchDelegate.modelData.toLowerCase()
                                      ? ThemeManager.textColor
                                      : ThemeManager.borderColor
                        border.width: ThemeManager.customAccentHex.toLowerCase() === swatchDelegate.modelData.toLowerCase()
                                      ? 2 : 1

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            hoverEnabled: true
                            onClicked: {
                                ThemeManager.setCustomAccent(swatchDelegate.modelData)
                                accentHexField.text = swatchDelegate.modelData
                                customAccentError.visible = false
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true }
            }

            // Inline validation error — hidden until the user attempts an
            // invalid hex via the Apply button.
            Text {
                id: customAccentError
                visible: false
                text: qsTr("Неверный формат: используйте #RRGGBB")
                font.pixelSize: FontSize.caption
                color: ThemeManager.errorColor
            }

            // Caption — explains the override semantics. Doubles as the
            // "applied!" visual feedback channel via the flash animation.
            Text {
                id: customAccentCaption
                text: qsTr("Перекрывает цвет акцента темы")
                font.pixelSize: FontSize.caption
                color: ThemeManager.secondaryTextColor

                // Brief green flash when an Apply call succeeds.
                SequentialAnimation {
                    id: feedbackFlash
                    ColorAnimation {
                        target: customAccentCaption
                        property: "color"
                        to: ThemeManager.successColor
                        duration: 150
                    }
                    PauseAnimation { duration: 600 }
                    ColorAnimation {
                        target: customAccentCaption
                        property: "color"
                        to: ThemeManager.secondaryTextColor
                        duration: 250
                    }
                }
            }
        }
    }
}
