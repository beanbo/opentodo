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
        title: "Очистить диалог"
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
            text: "Очистить весь диалог?"
            wrapMode: Text.WordWrap
            font.pixelSize: FontSize.body
            color: ThemeManager.textColor
        }

        footer: DialogButtonBox {
            Button {
                text: "Очистить"
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                Material.background: ThemeManager.accentColor
                Material.foreground: "white"
            }
            Button {
                text: "Отмена"
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
            spacing: Spacing.sm

            Label {
                text: "✨ Помощник"
                font.pixelSize: FontSize.h1
                font.weight: Font.DemiBold
                color: ThemeManager.textColor
                Layout.fillWidth: true
            }

            // «Очистить» button — only visible when there are messages
            Button {
                visible: aiAssistant.isConfigured && messages.count > 0
                text: "Очистить"
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

        // API key setup banner — visible if not configured
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
                    text: "Настрой Claude API ключ"
                    font.pixelSize: FontSize.subtitle
                    color: ThemeManager.textColor
                }
                Label {
                    text: "Получи ключ на console.anthropic.com и вставь сюда. Хранится только на этом устройстве."
                    font.pixelSize: FontSize.small
                    color: ThemeManager.secondaryTextColor
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Spacing.sm

                    TextField {
                        id: keyField
                        Layout.fillWidth: true
                        placeholderText: "sk-ant-api03-..."
                        echoMode: TextInput.Password
                        Keys.onReturnPressed: saveKeyBtn.clicked()
                    }
                    Button {
                        id: saveKeyBtn
                        text: "Сохранить"
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
        Flow {
            visible: aiAssistant.isConfigured && messages.count === 0
            Layout.fillWidth: true
            spacing: Spacing.sm

            Repeater {
                model: [
                    "Распланируй мою неделю",
                    "Что важнее всего сегодня?",
                    "Помоги разбить большую задачу",
                    "Какие задачи я могу делегировать?"
                ]
                delegate: Rectangle {
                    required property string modelData
                    height: 32
                    width: chipText.implicitWidth + Spacing.lg
                    radius: Radius.pill
                    color: ThemeManager.surfaceColor
                    border.color: ThemeManager.borderColor
                    border.width: 1

                    Text {
                        id: chipText
                        anchors.centerIn: parent
                        text: parent.modelData
                        color: ThemeManager.textColor
                        font.pixelSize: FontSize.body
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
                height: msgText.implicitHeight + Spacing.md
                color: role === "user"
                    ? Qt.rgba(ThemeManager.accentColor.r,
                              ThemeManager.accentColor.g,
                              ThemeManager.accentColor.b, 0.1)
                    : ThemeManager.surfaceColor
                radius: Radius.md

                Label {
                    id: msgText
                    anchors.fill: parent
                    anchors.margins: Spacing.sm
                    text: (role === "user" ? "Ты:  " : "✨  ") + content
                    wrapMode: Text.WordWrap
                    color: ThemeManager.textColor
                    font.pixelSize: FontSize.body
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
        RowLayout {
            visible: aiAssistant.isConfigured
            Layout.fillWidth: true
            spacing: Spacing.sm

            TextField {
                id: inputField
                Layout.fillWidth: true
                placeholderText: "Спроси что-нибудь… например, 'распланируй мою неделю'"
                enabled: !aiAssistant.isResponding
                Keys.onReturnPressed: sendBtn.clicked()
            }
            Button {
                id: sendBtn
                text: aiAssistant.isResponding ? "..." : "Отправить"
                enabled: !aiAssistant.isResponding && inputField.text.trim().length > 0
                Material.background: ThemeManager.accentColor
                Material.foreground: "white"
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
            messages.append({ role: "assistant", content: "❌ Ошибка: " + err })
            Qt.callLater(() => chatList.positionViewAtEnd())
        }
    }
}
