import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

Item {
    id: root
    anchors.fill: parent

    Rectangle { anchors.fill: parent; color: ThemeManager.backgroundColor }

    // ── Clear-chat confirmation dialog ────────────────────────────────────────
    Dialog {
        id: clearChatDialog
        title: qsTr("Очистить диалог")
        anchors.centerIn: Overlay.overlay
        width: 320
        modal: true

        background: Rectangle {
            color: ThemeManager.surfaceColor
            radius: Radius.md
            border.color: ThemeManager.borderColor
            border.width: 1
        }

        Label {
            width: parent.width
            text: qsTr("Очистить весь диалог?")
            wrapMode: Text.WordWrap
            font.pixelSize: FontSize.body
            color: ThemeManager.textColor
        }

        footer: DialogButtonBox {
            Button {
                text: qsTr("Очистить")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                Material.background: ThemeManager.accentColor
                Material.foreground: "white"
            }
            Button {
                text: qsTr("Отмена")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                flat: true
            }
        }

        onAccepted: {
            messages.clear()
            aiAssistant.clearConversation()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Spacing.lg
        spacing: Spacing.md

        RowLayout {
            Layout.fillWidth: true
            // Q4.01 pixel-perfect: gap header (icon→title) — 12, не Spacing.sm (8).
            spacing: 12

            // Q4.01 (redesign): эмодзи "✨" заменён на IconSvg "sparkles" из
            // stroke-пака. Иконка + чистый заголовок — единый визуальный вес.
            // Q4.01 pixel-perfect: цвет header icon в дизайне = t.text (не accent).
            IconSvg {
                name: "sparkles"
                size: 22
                color: ThemeManager.textColor
                Layout.alignment: Qt.AlignVCenter
            }

            Label {
                text: qsTr("Помощник")
                font.pixelSize: FontSize.viewHeader
                font.weight: Font.DemiBold
                color: ThemeManager.textColor
                Layout.fillWidth: true
            }

            // «Очистить» button — only visible when there are messages
            Button {
                visible: aiAssistant.isConfigured && messages.count > 0
                text: qsTr("Очистить")
                flat: true
                implicitHeight: 30
                implicitWidth: 90

                contentItem: Text {
                    text: parent.text
                    color: ThemeManager.secondaryTextColor
                    font.pixelSize: FontSize.small
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    radius: Radius.xs
                    color: parent.hovered ? ThemeManager.hoverColor : "transparent"
                    border.color: ThemeManager.borderColor
                    border.width: 1
                }

                onClicked: clearChatDialog.open()
            }
        }

        // API key setup banner — visible if the *current* provider has no key.
        // The banner doubles as a mini provider selector so the first-run flow
        // doesn't require opening Settings.
        Rectangle {
            visible: !aiAssistant.isConfigured
            Layout.fillWidth: true
            Layout.preferredHeight: apiKeyColumn.implicitHeight + Spacing.md * 2
            radius: Radius.lg
            color: ThemeManager.surfaceColor
            border.color: ThemeManager.accentColor
            border.width: 1

            ColumnLayout {
                id: apiKeyColumn
                anchors.fill: parent
                anchors.margins: Spacing.md
                spacing: Spacing.sm

                Label {
                    text: qsTr("Настрой API ключ помощника")
                    font.pixelSize: FontSize.subtitle
                    color: ThemeManager.textColor
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Spacing.sm

                    Label {
                        text: qsTr("Провайдер:")
                        font.pixelSize: FontSize.body
                        color: ThemeManager.secondaryTextColor
                    }

                    ComboBox {
                        id: providerCombo
                        Layout.fillWidth: true
                        textRole: "label"
                        valueRole: "id"

                        function buildModel() {
                            const ids = aiAssistant.providerIds
                            const items = []
                            for (let i = 0; i < ids.length; i++) {
                                items.push({
                                    id: ids[i],
                                    label: aiAssistant.providerDisplayName(ids[i])
                                })
                            }
                            return items
                        }

                        model: buildModel()

                        currentIndex: {
                            const ids = aiAssistant.providerIds
                            for (let i = 0; i < ids.length; i++)
                                if (ids[i] === aiAssistant.currentProviderId) return i
                            return 0
                        }

                        onActivated: {
                            const id = model[currentIndex].id
                            if (id !== aiAssistant.currentProviderId)
                                aiAssistant.currentProviderId = id
                        }
                    }
                }

                Label {
                    text: qsTr("Получи ключ: ") + "<a href=\"" + aiAssistant.providerConsoleUrl(aiAssistant.currentProviderId) + "\">"
                          + aiAssistant.providerConsoleUrl(aiAssistant.currentProviderId) + "</a>. "
                          + qsTr("Ключ хранится только на этом устройстве.")
                    font.pixelSize: FontSize.small
                    color: ThemeManager.secondaryTextColor
                    wrapMode: Text.WordWrap
                    textFormat: Text.RichText
                    onLinkActivated: (url) => Qt.openUrlExternally(url)
                    Layout.fillWidth: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Spacing.sm

                    TextField {
                        id: keyField
                        Layout.fillWidth: true
                        placeholderText: aiAssistant.providerApiKeyPlaceholder(aiAssistant.currentProviderId)
                        echoMode: TextInput.Password
                        Keys.onReturnPressed: saveKeyBtn.clicked()
                    }
                    Button {
                        id: saveKeyBtn
                        text: qsTr("Сохранить")
                        enabled: keyField.text.trim().length > 0
                        onClicked: {
                            aiAssistant.setApiKey(keyField.text.trim())
                            keyField.text = ""
                        }
                    }
                }
            }
        }

        // Suggestion chips (one-click prompts, shown when no messages yet)
        // Q4.01 (redesign): pill-shape per design-canvas — radius 18, padding 7/14,
        // FontSize.small (12), surfaceColor bg, borderColor stroke.
        Flow {
            visible: aiAssistant.isConfigured && messages.count === 0
            Layout.fillWidth: true
            spacing: Spacing.sm

            Repeater {
                model: [
                    qsTr("Распланируй мою неделю"),
                    qsTr("Что важнее всего сегодня?"),
                    qsTr("Помоги разбить большую задачу"),
                    qsTr("Какие задачи я могу делегировать?")
                ]
                delegate: Rectangle {
                    required property string modelData
                    // padding: 7 vertical, 14 horizontal → height = textHeight + 14
                    height: chipText.implicitHeight + 14
                    width: chipText.implicitWidth + 28
                    radius: 18
                    color: ThemeManager.surfaceColor
                    border.color: ThemeManager.borderColor
                    border.width: 1

                    Text {
                        id: chipText
                        anchors.centerIn: parent
                        text: parent.modelData
                        color: ThemeManager.textColor
                        font.pixelSize: FontSize.small
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            messages.append({ role: "user", content: parent.modelData })
                            aiAssistant.sendMessage(parent.modelData)
                            Qt.callLater(() => chatList.positionViewAtEnd())
                            if (typeof telemetry !== "undefined" && telemetry.enabled)
                                telemetry.trackEvent("ai_message_sent", {})
                        }
                    }
                }
            }
        }

        // B-6.84 — drop outer ScrollView; ListView manages its own scrolling.
        // ScrollBar attached directly to the ListView.
        ListView {
            id: chatList
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: aiAssistant.isConfigured
            clip: true
            model: ListModel { id: messages }
            spacing: Spacing.sm

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                required property string role
                required property string content

                width: chatList.width
                height: msgRow.implicitHeight + Spacing.md
                color: role === "user"
                    ? Qt.rgba(ThemeManager.accentColor.r,
                              ThemeManager.accentColor.g,
                              ThemeManager.accentColor.b, 0.1)
                    : ThemeManager.surfaceColor
                radius: Radius.md

                // Q4.01 (redesign): префикс собеседника — иконка из stroke-пака
                // ("user" для пользователя, "sparkles" для AI), а не эмодзи "✨".
                RowLayout {
                    id: msgRow
                    anchors.fill: parent
                    anchors.margins: Spacing.sm
                    spacing: Spacing.sm

                    IconSvg {
                        name: role === "user" ? "projects/user" : "sparkles"
                        size: 16
                        color: role === "user"
                               ? ThemeManager.secondaryTextColor
                               : ThemeManager.accentColor
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 2
                    }

                    Label {
                        id: msgText
                        text: content
                        wrapMode: Text.WordWrap
                        color: ThemeManager.textColor
                        font.pixelSize: FontSize.body
                        Layout.fillWidth: true
                    }
                }
            }

            // R-6.82 — wrap positionViewAtEnd in Qt.callLater so it fires
            // after the new delegate has been fully laid out.
            Connections {
                target: messages
                function onCountChanged() {
                    Qt.callLater(() => chatList.positionViewAtEnd())
                }
            }
        }

        // Input row
        // Q4.01 (redesign): input + send переделаны под design-canvas —
        // высота 38, radius Radius.lg, custom Rectangle background для
        // полного контроля поверх Material-стилей.
        RowLayout {
            visible: aiAssistant.isConfigured
            Layout.fillWidth: true
            spacing: Spacing.sm

            TextField {
                id: inputField
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                placeholderText: qsTr("Спроси что-нибудь… например, 'распланируй мою неделю'")
                placeholderTextColor: ThemeManager.secondaryTextColor
                color: ThemeManager.textColor
                font.pixelSize: FontSize.bodySmall
                enabled: !aiAssistant.isResponding
                leftPadding: 14
                rightPadding: 14
                topPadding: 0
                bottomPadding: 0
                verticalAlignment: TextInput.AlignVCenter
                background: Rectangle {
                    radius: Radius.lg
                    color: ThemeManager.surfaceColor
                    border.color: ThemeManager.borderColor
                    border.width: 1
                }
                Keys.onReturnPressed: sendBtn.clicked()
            }
            Button {
                id: sendBtn
                enabled: !aiAssistant.isResponding && inputField.text.trim().length > 0
                implicitHeight: 38
                implicitWidth: sendRow.implicitWidth + 32  // 16 padding on each side

                background: Rectangle {
                    radius: Radius.lg
                    color: sendBtn.enabled
                           ? (sendBtn.hovered
                              ? Qt.darker(ThemeManager.accentColor, 1.1)
                              : ThemeManager.accentColor)
                           : Qt.rgba(ThemeManager.accentColor.r,
                                     ThemeManager.accentColor.g,
                                     ThemeManager.accentColor.b, 0.4)
                }

                contentItem: RowLayout {
                    id: sendRow
                    spacing: 6
                    IconSvg {
                        name: "send"
                        size: 13
                        color: "white"
                        Layout.alignment: Qt.AlignVCenter
                    }
                    Text {
                        text: aiAssistant.isResponding ? "..." : qsTr("Отправить")
                        color: "white"
                        font.pixelSize: FontSize.bodySmall
                        verticalAlignment: Text.AlignVCenter
                        Layout.alignment: Qt.AlignVCenter
                    }
                }

                onClicked: {
                    const txt = inputField.text.trim()
                    if (txt.length === 0)
                        return
                    messages.append({ role: "user", content: txt })
                    inputField.text = ""
                    aiAssistant.sendMessage(txt)
                    Qt.callLater(() => chatList.positionViewAtEnd())
                    if (typeof telemetry !== "undefined" && telemetry.enabled)
                        telemetry.trackEvent("ai_message_sent", {})
                }
            }
        }
    }

    Connections {
        target: aiAssistant
        function onResponseReceived(text) {
            messages.append({ role: "assistant", content: text })
            Qt.callLater(() => chatList.positionViewAtEnd())
        }
        function onErrorReceived(err) {
            messages.append({ role: "assistant", content: qsTr("❌ Ошибка: %1").arg(err) })
            Qt.callLater(() => chatList.positionViewAtEnd())
        }
    }
}
