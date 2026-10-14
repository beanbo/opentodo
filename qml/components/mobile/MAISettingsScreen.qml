import QtQuick
import QtQuick.Controls
import OpenTodoUI

// MAISettingsScreen — mobile AI assistant configuration: provider, API key,
// model, Anthropic workspace, OpenAI-compatible base URL, outbound proxy and a
// connection test. The phone counterpart of the desktop SettingsSectionAI;
// wired to the same aiAssistant properties. Opened from MSettingsScreen (and
// the "not configured" state of MAIScreen) as a back-button overlay.
Item {
    id: root
    signal closeRequested()

    readonly property string _pid: aiAssistant.currentProviderId
    property bool _revealKey: false
    property bool _hasKey: aiAssistant.providerHasKey(_pid)
    property string _testText: ""
    property bool _testOk: false

    function _refreshKeyState() { root._hasKey = aiAssistant.providerHasKey(root._pid) }

    Connections {
        target: aiAssistant
        function onCurrentProviderChanged() { keyBox.text = ""; root._refreshKeyState() }
        function onIsConfiguredChanged()     { root._refreshKeyState() }
        // Typing breaks the fields' bindings; resync on external changes.
        function onCurrentModelChanged()     { modelBox.text = aiAssistant.currentModel }
        function onAnthropicWorkspaceIdChanged() { wsBox.text = aiAssistant.anthropicWorkspaceId }
        function onConnectionTestFinished(ok, message) { root._testOk = ok; root._testText = message }
    }

    Rectangle { anchors.fill: parent; color: ThemeManager.backgroundColor }

    // ── Header (back + title) ──────────────────────────────────────────────
    Rectangle {
        id: header
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: 56
        color: ThemeManager.backgroundColor
        MIconButton {
            id: backBtn
            anchors.left: parent.left; anchors.leftMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            iconName: "chevron-left"; iconColor: ThemeManager.textColor; iconSize: 24
            onClicked: root.closeRequested()
        }
        Text {
            anchors.left: backBtn.right; anchors.leftMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("AI-помощник")
            font.pixelSize: 20; font.weight: Font.Bold; color: ThemeManager.textColor
        }
    }

    // Shared look for the text inputs on this screen.
    component FieldBox: Rectangle {
        id: box
        property alias text: input.text
        property alias placeholderText: input.placeholderText
        property alias echoMode: input.echoMode
        property alias inputMethodHints: input.inputMethodHints
        signal editingFinished()
        width: parent.width; height: 46; radius: 12
        color: ThemeManager.backgroundColor; border.width: 1; border.color: ThemeManager.borderColor
        TextField {
            id: input
            anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14
            verticalAlignment: Text.AlignVCenter
            placeholderTextColor: ThemeManager.faintTextColor
            font.pixelSize: 14; color: ThemeManager.textColor
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            background: Item {}
            onEditingFinished: box.editingFinished()
        }
    }
    component Hint: Text {
        width: parent.width
        wrapMode: Text.WordWrap
        font.pixelSize: 12; color: ThemeManager.secondaryTextColor
    }
    component Card: Rectangle {
        default property alias content: inner.children
        width: parent.width - 24; x: 12
        height: inner.implicitHeight + 28
        radius: 14
        color: ThemeManager.surfaceColor
        border.width: 1; border.color: ThemeManager.borderColor
        Column {
            id: inner
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 14
            spacing: 10
        }
    }

    Flickable {
        anchors.top: header.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        contentHeight: col.implicitHeight + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: col
            width: parent.width
            spacing: 0

            // ── Provider ───────────────────────────────────────────────────
            MSettingsSectionLabel { text: qsTr("Провайдер") }
            Item {
                width: parent.width; height: 56
                MSeg {
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.leftMargin: 16; anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    value: root._pid
                    options: [
                        { "value": "anthropic",     "label": "Claude" },
                        { "value": "gemini",        "label": "Gemini" },
                        { "value": "openai_compat", "label": "OpenAI" }
                    ]
                    onChanged: function(v) { aiAssistant.currentProviderId = v }
                }
            }

            // ── API key ────────────────────────────────────────────────────
            MSettingsSectionLabel { text: qsTr("API-ключ") }
            Card {
                Row {
                    width: parent.width
                    Text {
                        width: parent.width - revealKey.implicitWidth
                        text: root._hasKey ? qsTr("Ключ сохранён") : qsTr("Не настроен")
                        font.pixelSize: 13; font.weight: Font.DemiBold
                        color: root._hasKey ? ThemeManager.successColor : ThemeManager.secondaryTextColor
                    }
                    Text {
                        id: revealKey
                        text: root._revealKey ? qsTr("Скрыть") : qsTr("Показать")
                        font.pixelSize: 12; font.weight: Font.DemiBold; color: ThemeManager.accentColor
                        MouseArea { anchors.fill: parent; anchors.margins: -8; onClicked: root._revealKey = !root._revealKey }
                    }
                }
                FieldBox {
                    id: keyBox
                    placeholderText: aiAssistant.providerApiKeyPlaceholder(root._pid)
                    echoMode: root._revealKey ? TextInput.Normal : TextInput.Password
                }
                Row {
                    spacing: 10
                    Rectangle {
                        width: saveTxt.implicitWidth + 28; height: 38; radius: 10
                        color: keyBox.text.trim().length > 0 ? ThemeManager.accentColor : ThemeManager.hoverColor
                        Text {
                            id: saveTxt; anchors.centerIn: parent
                            text: root._hasKey ? qsTr("Заменить") : qsTr("Сохранить")
                            color: keyBox.text.trim().length > 0 ? ThemeManager.accentForeground : ThemeManager.secondaryTextColor
                            font.pixelSize: 13; font.weight: Font.Bold
                        }
                        MouseArea {
                            anchors.fill: parent
                            enabled: keyBox.text.trim().length > 0
                            onClicked: {
                                aiAssistant.setApiKeyFor(root._pid, keyBox.text.trim())
                                keyBox.text = ""
                                root._refreshKeyState()
                            }
                        }
                    }
                    Rectangle {
                        visible: root._hasKey
                        width: delTxt.implicitWidth + 28; height: 38; radius: 10
                        color: "transparent"; border.width: 1; border.color: ThemeManager.borderColor
                        Text { id: delTxt; anchors.centerIn: parent; text: qsTr("Удалить"); color: ThemeManager.errorColor; font.pixelSize: 13; font.weight: Font.Bold }
                        MouseArea { anchors.fill: parent; onClicked: { aiAssistant.setApiKeyFor(root._pid, ""); root._refreshKeyState() } }
                    }
                }
                Hint { text: qsTr("Получить ключ: %1").arg(aiAssistant.providerConsoleUrl(root._pid)) }
            }

            // ── Model ──────────────────────────────────────────────────────
            MSettingsSectionLabel { text: qsTr("Модель") }
            Card {
                FieldBox {
                    id: modelBox
                    text: aiAssistant.currentModel
                    placeholderText: aiAssistant.providerDefaultModel(root._pid)
                    onEditingFinished: {
                        if (modelBox.text.trim() !== aiAssistant.currentModel)
                            aiAssistant.currentModel = modelBox.text.trim()
                    }
                }
                Flow {
                    width: parent.width
                    spacing: 8
                    Repeater {
                        model: aiAssistant.providerSuggestedModels(root._pid)
                        delegate: Rectangle {
                            required property string modelData
                            readonly property bool on: aiAssistant.currentModel === modelData
                            height: 32; radius: 16; width: mTxt.implicitWidth + 24
                            color: on ? ThemeManager.accentContainer : "transparent"
                            border.width: 1; border.color: on ? ThemeManager.accentColor : ThemeManager.borderColor
                            Text { id: mTxt; anchors.centerIn: parent; text: modelData; font.pixelSize: 12; font.weight: Font.DemiBold; color: on ? ThemeManager.accentColor : ThemeManager.secondaryTextColor }
                            MouseArea { anchors.fill: parent; onClicked: aiAssistant.currentModel = modelData }
                        }
                    }
                }
            }

            // ── Anthropic workspace ────────────────────────────────────────
            MSettingsSectionLabel { visible: root._pid === "anthropic"; text: qsTr("Workspace") }
            Card {
                visible: root._pid === "anthropic"
                FieldBox {
                    id: wsBox
                    text: aiAssistant.anthropicWorkspaceId
                    placeholderText: "wrkspc_…"
                    onEditingFinished: {
                        if (wsBox.text.trim() !== aiAssistant.anthropicWorkspaceId)
                            aiAssistant.anthropicWorkspaceId = wsBox.text.trim()
                    }
                }
                Hint { text: qsTr("Нужен, только если ключ не привязан к одному workspace (API отвечает «anthropic-workspace-id is required»). ID — в колонке ID на platform.claude.com/settings/workspaces.") }
            }

            // ── OpenAI-compatible base URL ─────────────────────────────────
            MSettingsSectionLabel { visible: root._pid === "openai_compat"; text: qsTr("Base URL") }
            Card {
                visible: root._pid === "openai_compat"
                FieldBox {
                    id: baseBox
                    text: aiAssistant.openAiCompatBaseUrl
                    placeholderText: "https://api.openai.com"
                    inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                    onEditingFinished: {
                        if (baseBox.text.trim() !== aiAssistant.openAiCompatBaseUrl)
                            aiAssistant.openAiCompatBaseUrl = baseBox.text.trim()
                    }
                }
            }

            // ── Proxy ──────────────────────────────────────────────────────
            MSettingsSectionLabel { text: qsTr("Прокси") }
            Card {
                MSeg {
                    width: parent.width
                    value: aiAssistant.proxyMode
                    options: [
                        { "value": "auto",   "label": qsTr("Авто") },
                        { "value": "manual", "label": qsTr("Вручную") },
                        { "value": "none",   "label": qsTr("Без прокси") }
                    ]
                    onChanged: function(v) { aiAssistant.proxyMode = v }
                }
                FieldBox {
                    id: proxyBox
                    visible: aiAssistant.proxyMode === "manual"
                    property string error: ""
                    text: aiAssistant.proxyUrl
                    placeholderText: "http://192.168.1.10:10809"
                    inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                    onEditingFinished: {
                        proxyBox.error = aiAssistant.validateProxyUrl(proxyBox.text)
                        if (proxyBox.text.trim() !== aiAssistant.proxyUrl)
                            aiAssistant.proxyUrl = proxyBox.text.trim()
                    }
                }
                Hint {
                    visible: proxyBox.visible && proxyBox.error.length > 0
                    text: proxyBox.error
                    color: ThemeManager.errorColor
                }
                Hint { text: qsTr("Сейчас: %1").arg(aiAssistant.proxyStatus) }
                Hint {
                    text: qsTr("Из РФ и РБ API напрямую недоступны. На телефоне проще всего пустить OpenTodo через прокси AdGuard («Приложения, работающие через прокси») и оставить «Авто». «Вручную» — для обычного http:// или socks5:// прокси; https-прокси (TLS до прокси) Qt не поддерживает.")
                }
            }

            // ── Connection test ────────────────────────────────────────────
            MSettingsSectionLabel { text: qsTr("Проверка") }
            Card {
                Rectangle {
                    width: testTxt.implicitWidth + 28; height: 40; radius: 12
                    color: (root._hasKey && !aiAssistant.isTestingConnection) ? ThemeManager.accentColor : ThemeManager.hoverColor
                    Text {
                        id: testTxt; anchors.centerIn: parent
                        text: aiAssistant.isTestingConnection ? qsTr("Проверяю…") : qsTr("Проверить соединение")
                        color: (root._hasKey && !aiAssistant.isTestingConnection) ? ThemeManager.accentForeground : ThemeManager.secondaryTextColor
                        font.pixelSize: 13; font.weight: Font.Bold
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: root._hasKey && !aiAssistant.isTestingConnection
                        onClicked: { root._testText = ""; aiAssistant.testConnection() }
                    }
                }
                Hint {
                    visible: root._testText.length > 0
                    text: root._testText
                    color: root._testOk ? ThemeManager.successColor : ThemeManager.errorColor
                }
            }

            Item { width: 1; height: 8 }
        }
    }
}
