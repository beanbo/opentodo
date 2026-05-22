import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
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

    Behavior on implicitHeight {
        NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
    }

    // Snapshot the current provider id so child rows don't read aiAssistant
    // mid-update during a setCurrentProviderId() call (which fires several
    // change signals back-to-back).
    property string activeProviderId: aiAssistant.currentProviderId

    Connections {
        target: aiAssistant
        function onCurrentProviderChanged() {
            root.activeProviderId = aiAssistant.currentProviderId
            keyField.text = ""  // never show stored keys; only the user's new input
            modelCombo.syncWithProvider()
            providerCombo.model = providerCombo.buildModel()
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
                text: qsTr("✨ AI помощник")
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

        // ── Provider picker ─────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: providerContent.implicitHeight + 20
            color: "transparent"

            ColumnLayout {
                id: providerContent
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                anchors.topMargin: 10
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            text: qsTr("Провайдер")
                            font.pixelSize: 14
                            color: ThemeManager.textColor
                            Layout.fillWidth: true
                        }
                        Text {
                            text: qsTr("Какой LLM использовать для генерации. ● — ключ уже сохранён, ○ — пусто.")
                            font.pixelSize: 11
                            color: ThemeManager.secondaryTextColor
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    ComboBox {
                        id: providerCombo
                        implicitWidth: 260
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

                        function buildModel() {
                            const ids = aiAssistant.providerIds
                            const items = []
                            for (let i = 0; i < ids.length; i++) {
                                const id = ids[i]
                                const dn = aiAssistant.providerDisplayName(id)
                                const has = aiAssistant.providerHasKey(id)
                                items.push({
                                    id: id,
                                    label: (has ? "● " : "○ ") + dn
                                })
                            }
                            return items
                        }

                        textRole: "label"
                        valueRole: "id"
                        model: buildModel()

                        currentIndex: {
                            const ids = aiAssistant.providerIds
                            for (let i = 0; i < ids.length; i++)
                                if (ids[i] === root.activeProviderId) return i
                            return 0
                        }

                        onActivated: {
                            const id = model[currentIndex].id
                            if (id !== aiAssistant.currentProviderId)
                                aiAssistant.currentProviderId = id
                        }

                        contentItem: Text {
                            leftPadding: 10
                            text: providerCombo.displayText
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

        // ── OpenAI-compatible base URL (conditional) ────────────────────────
        Rectangle {
            visible: root.activeProviderId === "openai_compat"
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? (baseUrlContent.implicitHeight + 20) : 0
            color: "transparent"

            ColumnLayout {
                id: baseUrlContent
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                anchors.topMargin: 10
                spacing: 6

                Text {
                    text: qsTr("Base URL")
                    font.pixelSize: 14
                    color: ThemeManager.textColor
                    Layout.fillWidth: true
                }
                Text {
                    text: qsTr("Корень OpenAI-совместимого API (без /v1). Пресеты: openrouter.ai/api, api.groq.com/openai, localhost:11434 для Ollama.")
                    font.pixelSize: 11
                    color: ThemeManager.secondaryTextColor
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    TextField {
                        id: baseUrlField
                        Layout.fillWidth: true
                        placeholderText: "https://api.openai.com"
                        text: aiAssistant.openAiCompatBaseUrl
                        onEditingFinished: {
                            if (text.trim() !== aiAssistant.openAiCompatBaseUrl)
                                aiAssistant.openAiCompatBaseUrl = text.trim()
                        }
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6

                    Repeater {
                        model: [
                            { label: "OpenAI",     url: "https://api.openai.com" },
                            { label: "OpenRouter", url: "https://openrouter.ai/api" },
                            { label: "Groq",       url: "https://api.groq.com/openai" },
                            { label: "Ollama",     url: "http://localhost:11434" }
                        ]
                        delegate: Rectangle {
                            required property var modelData
                            height: 26
                            width: presetText.implicitWidth + 16
                            radius: 13
                            color: presetMouse.containsMouse
                                   ? ThemeManager.hoverColor
                                   : "transparent"
                            border.color: ThemeManager.borderColor
                            border.width: 1

                            Text {
                                id: presetText
                                anchors.centerIn: parent
                                text: modelData.label
                                font.pixelSize: 11
                                color: ThemeManager.secondaryTextColor
                            }

                            MouseArea {
                                id: presetMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    baseUrlField.text = modelData.url
                                    aiAssistant.openAiCompatBaseUrl = modelData.url
                                }
                            }
                        }
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

        // ── API key ─────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: keyContent.implicitHeight + 20
            color: "transparent"

            ColumnLayout {
                id: keyContent
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                anchors.topMargin: 10
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: qsTr("API ключ")
                        font.pixelSize: 14
                        color: ThemeManager.textColor
                        Layout.fillWidth: true
                    }
                    Text {
                        text: aiAssistant.providerHasKey(root.activeProviderId)
                            ? qsTr("ключ сохранён")
                            : qsTr("не настроен")
                        font.pixelSize: 11
                        color: aiAssistant.providerHasKey(root.activeProviderId)
                            ? "#3fa34d"
                            : ThemeManager.secondaryTextColor
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    TextField {
                        id: keyField
                        Layout.fillWidth: true
                        placeholderText: aiAssistant.providerApiKeyPlaceholder(root.activeProviderId)
                        echoMode: showKeyToggle.checked ? TextInput.Normal : TextInput.Password
                        Keys.onReturnPressed: saveBtn.clicked()
                    }
                    CheckBox {
                        id: showKeyToggle
                        text: qsTr("Показать")
                        checked: false
                    }
                    Button {
                        id: saveBtn
                        text: aiAssistant.providerHasKey(root.activeProviderId)
                            ? qsTr("Заменить")
                            : qsTr("Сохранить")
                        enabled: keyField.text.trim().length > 0
                        Material.background: ThemeManager.accentColor
                        Material.foreground: "white"
                        onClicked: {
                            aiAssistant.setApiKeyFor(root.activeProviderId, keyField.text.trim())
                            keyField.text = ""
                            providerCombo.model = providerCombo.buildModel()
                        }
                    }
                    Button {
                        text: qsTr("Удалить")
                        visible: aiAssistant.providerHasKey(root.activeProviderId)
                        flat: true
                        onClicked: {
                            aiAssistant.setApiKeyFor(root.activeProviderId, "")
                            keyField.text = ""
                            providerCombo.model = providerCombo.buildModel()
                        }
                    }
                }

                Text {
                    text: qsTr("Получить ключ: ") + "<a href=\"" + aiAssistant.providerConsoleUrl(root.activeProviderId) + "\">"
                          + aiAssistant.providerConsoleUrl(root.activeProviderId) + "</a>"
                    font.pixelSize: 11
                    color: ThemeManager.secondaryTextColor
                    onLinkActivated: (url) => Qt.openUrlExternally(url)
                    textFormat: Text.RichText
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
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

        // ── Model ───────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: modelContent.implicitHeight + 20
            color: "transparent"

            ColumnLayout {
                id: modelContent
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                anchors.topMargin: 10
                spacing: 6

                Text {
                    text: qsTr("Модель")
                    font.pixelSize: 14
                    color: ThemeManager.textColor
                    Layout.fillWidth: true
                }
                Text {
                    text: qsTr("Выбери из пресетов или впиши свой идентификатор. Для Gemini рекомендую gemini-2.5-flash-lite — самый щедрый бесплатный тариф.")
                    font.pixelSize: 11
                    color: ThemeManager.secondaryTextColor
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                ComboBox {
                    id: modelCombo
                    Layout.fillWidth: true
                    editable: true

                    // Pull the suggestions for the current provider each time
                    // the provider changes. The currently-saved model goes
                    // into editText; if it isn't in the suggestion list the
                    // user can still pick from the dropdown or keep their
                    // custom value.
                    function syncWithProvider() {
                        const suggested = aiAssistant.providerSuggestedModels(root.activeProviderId)
                        model = suggested
                        editText = aiAssistant.currentModel
                    }
                    Component.onCompleted: syncWithProvider()

                    // Item picked from the dropdown popup.
                    onActivated: {
                        if (editText.trim() !== aiAssistant.currentModel)
                            aiAssistant.currentModel = editText.trim()
                    }
                    // Enter pressed in the editable text field.
                    onAccepted: {
                        if (editText.trim() !== aiAssistant.currentModel)
                            aiAssistant.currentModel = editText.trim()
                    }
                }
            }
        }
    }
}
