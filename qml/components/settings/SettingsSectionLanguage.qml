import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

// Dedicated language-selection section. The inline language row in
// SettingsSectionGeneral is kept for backward compatibility, but this
// section is the primary surface — it carries the warning about
// restart-required behaviour because the QTranslator is installed once
// at app boot. Both surfaces persist into the same settings/language key.
Rectangle {
    id: root
    radius: 8
    color: ThemeManager.surfaceColor
    border.color: ThemeManager.borderColor
    border.width: 1
    implicitHeight: sectionHeader.height + (expanded ? sectionContent.implicitHeight : 0)
    clip: true

    property bool expanded: true

    Behavior on implicitHeight {
        NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
    }

    // Human-friendly display name for each language code we support.
    // The codes themselves are stable (used by QTranslator to resolve
    // opentodo_<code>.qm); only the display strings are translated.
    function languageDisplayName(code) {
        switch (code) {
            case "ru": return qsTr("Русский")
            case "en": return qsTr("English")
            case "de": return qsTr("Deutsch")
            case "uk": return qsTr("Українська")
            case "be": return qsTr("Беларуская")
            default:   return code
        }
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
                text: qsTr("Язык интерфейса")
                font.pixelSize: 16
                font.bold: true
                color: ThemeManager.textColor
                Layout.fillWidth: true
            }

            Text {
                text: root.expanded ? "▾" : "▸"
                font.pixelSize: 14
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

        // Picker row
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 68
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 16

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: qsTr("Язык")
                        font.pixelSize: 14
                        color: ThemeManager.textColor
                        Layout.fillWidth: true
                    }

                    Text {
                        text: qsTr("Изменение языка вступает в силу после перезапуска приложения.")
                        font.pixelSize: 11
                        color: ThemeManager.secondaryTextColor
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                ComboBox {
                    id: languagePicker
                    implicitWidth: 220
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

                    // Backend model is the list of language codes; display is
                    // resolved via root.languageDisplayName(code).
                    textRole: "label"
                    valueRole: "id"

                    model: {
                        const codes = settingsViewModel.availableLanguages()
                        const out = []
                        for (let i = 0; i < codes.length; ++i)
                            out.push({ id: codes[i], label: root.languageDisplayName(codes[i]) })
                        return out
                    }

                    currentIndex: {
                        const cur = settingsViewModel.language
                        const codes = settingsViewModel.availableLanguages()
                        for (let i = 0; i < codes.length; ++i)
                            if (codes[i] === cur) return i
                        return 0
                    }

                    onActivated: {
                        const id = model[currentIndex].id
                        settingsViewModel.updateSetting("language", id)
                        if (typeof telemetry !== "undefined" && telemetry && telemetry.enabled)
                            telemetry.trackEvent("settings_language_changed", { "lang": id })
                        restartHint.visible = true
                    }

                    contentItem: Text {
                        leftPadding: 10
                        text: languagePicker.displayText
                        font.pixelSize: 13
                        color: ThemeManager.textColor
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 4
                        color: ThemeManager.hoverColor
                        border.color: ThemeManager.borderColor
                        border.width: 1
                    }
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
            }
        }

        // Restart-needed hint — shown once after the user picks a new language.
        // We use a Rectangle holder so we can colour the strip distinctly.
        Rectangle {
            id: restartHint
            visible: false
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: ThemeManager.hoverColor

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 12

                Text {
                    text: qsTr("Перезапустите приложение, чтобы применить новый язык.")
                    font.pixelSize: 12
                    color: ThemeManager.textColor
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }

                // Inline "restart now" button. Triggers SettingsViewModel.restartApp(),
                // which launches a fresh process via QProcess::startDetached and exits
                // the current one. On platforms where restart isn't available
                // (Android, WASM) the button stays disabled.
                Button {
                    id: restartBtn
                    text: qsTr("Перезапустить")
                    enabled: typeof settingsViewModel !== "undefined"
                             && typeof settingsViewModel.canRestartApp === "function"
                             && settingsViewModel.canRestartApp()
                    Layout.preferredHeight: 32
                    Layout.preferredWidth: 130
                    Layout.alignment: Qt.AlignVCenter

                    contentItem: Text {
                        text: restartBtn.text
                        color: restartBtn.enabled ? "white" : ThemeManager.secondaryTextColor
                        font.pixelSize: 12
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 4
                        color: restartBtn.enabled
                               ? (restartBtn.hovered ? Qt.darker(ThemeManager.accentColor, 1.1)
                                                     : ThemeManager.accentColor)
                               : ThemeManager.borderColor
                    }

                    onClicked: {
                        if (typeof settingsViewModel !== "undefined"
                                && typeof settingsViewModel.restartApp === "function")
                            settingsViewModel.restartApp()
                    }
                }
            }

            // Iter-12 / iter-13: surface restartFailed back to the user.
            // The banner sits INSIDE the sectionContent ColumnLayout so it
            // contributes to implicitHeight — placing it as a sibling under
            // root with anchors.bottom (iter-12) made it geometrically
            // overlap the Restart button when shown.
            Rectangle {
                id: restartErrorBanner
                visible: false
                property string message: ""

                Layout.fillWidth: true
                Layout.preferredHeight: visible ? 36 : 0
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: visible ? 8 : 0
                Layout.bottomMargin: visible ? 8 : 0
                radius: 4
                color: Qt.rgba(0.9, 0.3, 0.3, 0.15)
                border.color: Qt.rgba(0.9, 0.3, 0.3, 0.6)
                border.width: 1

                Text {
                    anchors.left: parent.left
                    anchors.right: bannerClose.left
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 10
                    text: restartErrorBanner.message
                    color: ThemeManager.textColor
                    font.pixelSize: 12
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }

                Text {
                    id: bannerClose
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.rightMargin: 10
                    text: "✕"
                    color: ThemeManager.secondaryTextColor
                    font.pixelSize: 14
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: restartErrorBanner.visible = false
                    }
                }
            }

            Connections {
                target: typeof settingsViewModel !== "undefined" ? settingsViewModel : null
                ignoreUnknownSignals: true
                function onRestartFailed(reason) {
                    restartErrorBanner.message = reason
                    restartErrorBanner.visible = true
                }
            }
        }
    }
}
