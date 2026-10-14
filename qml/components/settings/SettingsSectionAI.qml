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

            // Q4.01 (redesign): "✨" заменён на IconSvg "sparkles".
            IconSvg {
                name: "sparkles"
                size: 16
                color: ThemeManager.accentColor
                Layout.alignment: Qt.AlignVCenter
            }
            Text {
                text: qsTr("AI помощник")
                font.pixelSize: FontSize.body
                font.weight: Font.DemiBold
                color: ThemeManager.textColor
                Layout.fillWidth: true
            }

            // Q4.01 (redesign): "▾"/"▸" → stroke chevron IconSvg
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
                            font.pixelSize: FontSize.bodySmall
                            color: ThemeManager.textColor
                            Layout.fillWidth: true
                        }
                        Text {
                            text: qsTr("Какой LLM использовать для генерации. ● — ключ уже сохранён, ○ — пусто.")
                            font.pixelSize: FontSize.caption
                            color: ThemeManager.secondaryTextColor
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    DesignSelect {
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
                    font.pixelSize: FontSize.bodySmall
                    color: ThemeManager.textColor
                    Layout.fillWidth: true
                }
                Text {
                    text: qsTr("Корень OpenAI-совместимого API (без /v1). Пресеты: openrouter.ai/api, api.groq.com/openai, localhost:11434 для Ollama.")
                    font.pixelSize: FontSize.caption
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
                        font.pixelSize: FontSize.bodySmall
                        color: ThemeManager.textColor
                        Layout.fillWidth: true
                    }
                    Text {
                        text: aiAssistant.providerHasKey(root.activeProviderId)
                            ? qsTr("ключ сохранён")
                            : qsTr("не настроен")
                        font.pixelSize: FontSize.caption
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
                        Material.foreground: ThemeManager.accentForeground
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
                    font.pixelSize: FontSize.caption
                    color: ThemeManager.secondaryTextColor
                    onLinkActivated: (url) => Qt.openUrlExternally(url)
                    textFormat: Text.RichText
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                // Anthropic keys created for several workspaces must name one
                // per request (anthropic-workspace-id).
                ColumnLayout {
                    visible: root.activeProviderId === "anthropic"
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    spacing: 6

                    Text {
                        text: qsTr("Workspace ID")
                        font.pixelSize: FontSize.bodySmall
                        color: ThemeManager.textColor
                        Layout.fillWidth: true
                    }
                    Text {
                        text: qsTr("Нужен, только если ключ не привязан к одному workspace (API отвечает «anthropic-workspace-id is required»). «Найти» спросит у API доступные workspace.")
                        font.pixelSize: FontSize.caption
                        color: ThemeManager.secondaryTextColor
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        TextField {
                            id: workspaceField
                            Layout.fillWidth: true
                            placeholderText: "wrkspc_…"
                            text: aiAssistant.anthropicWorkspaceId
                            onEditingFinished: {
                                if (text.trim() !== aiAssistant.anthropicWorkspaceId)
                                    aiAssistant.anthropicWorkspaceId = text.trim()
                            }
                        }
                        Button {
                            text: qsTr("Найти")
                            flat: true
                            enabled: aiAssistant.providerHasKey("anthropic")
                            onClicked: {
                                workspaceStatus.text = qsTr("Запрашиваю список…")
                                aiAssistant.fetchAnthropicWorkspaces()
                            }
                        }
                    }
                    DesignSelect {
                        id: workspaceCombo
                        visible: count > 0
                        Layout.fillWidth: true
                        textRole: "label"
                        valueRole: "id"
                        model: []
                        onActivated: (index) => aiAssistant.anthropicWorkspaceId = model[index].id
                    }
                    Text {
                        id: workspaceStatus
                        visible: text.length > 0
                        font.pixelSize: FontSize.caption
                        color: ThemeManager.secondaryTextColor
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Connections {
                        target: aiAssistant
                        // Typing into the field breaks its binding; keep it in
                        // sync when «Найти» picks a workspace.
                        function onAnthropicWorkspaceIdChanged() {
                            workspaceField.text = aiAssistant.anthropicWorkspaceId
                        }
                        function onAnthropicWorkspacesFetched(workspaces, error) {
                            if (error.length > 0) {
                                workspaceStatus.text = error
                                workspaceCombo.model = []
                                return
                            }
                            const items = workspaces.map(w => ({ id: w.id, label: w.name + " — " + w.id }))
                            workspaceCombo.model = items
                            // Nothing chosen yet: take the Default Workspace.
                            let idx = items.findIndex(w => w.id === aiAssistant.anthropicWorkspaceId)
                            if (idx < 0) {
                                idx = Math.max(0, workspaces.findIndex(w => w.name === "Default"))
                                aiAssistant.anthropicWorkspaceId = items[idx].id
                            }
                            workspaceCombo.currentIndex = idx
                            workspaceStatus.text = qsTr("Выбран workspace: ") + workspaces[idx].name
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
                    font.pixelSize: FontSize.bodySmall
                    color: ThemeManager.textColor
                    Layout.fillWidth: true
                }
                Text {
                    text: qsTr("Выбери из пресетов или впиши свой идентификатор. Для Gemini рекомендую gemini-2.5-flash-lite — самый щедрый бесплатный тариф.")
                    font.pixelSize: FontSize.caption
                    color: ThemeManager.secondaryTextColor
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                DesignSelect {
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

        // ── Proxy + connection test ─────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: proxyContent.implicitHeight + 20
            color: "transparent"

            ColumnLayout {
                id: proxyContent
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
                            text: qsTr("Прокси")
                            font.pixelSize: FontSize.bodySmall
                            color: ThemeManager.textColor
                            Layout.fillWidth: true
                        }
                        Text {
                            text: qsTr("Из РФ и РБ API Anthropic, Google и OpenAI напрямую недоступны. «Авто» берёт переменную HTTPS_PROXY, а без неё системный прокси. Для Xray/v2ray укажи его HTTP-вход, например http://127.0.0.1:10809.")
                            font.pixelSize: FontSize.caption
                            color: ThemeManager.secondaryTextColor
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    DesignSelect {
                        id: proxyModeCombo
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        textRole: "label"
                        valueRole: "id"
                        model: [
                            { id: "auto",   label: qsTr("Авто") },
                            { id: "manual", label: qsTr("Вручную") },
                            { id: "none",   label: qsTr("Без прокси") }
                        ]
                        currentIndex: ["auto", "manual", "none"].indexOf(aiAssistant.proxyMode)
                        onActivated: (index) => aiAssistant.proxyMode = model[index].id
                    }
                }

                TextField {
                    id: proxyUrlField
                    visible: aiAssistant.proxyMode === "manual"
                    Layout.fillWidth: true
                    placeholderText: "http://127.0.0.1:10809"
                    text: aiAssistant.proxyUrl
                    property string error: ""
                    onEditingFinished: {
                        error = aiAssistant.validateProxyUrl(text)
                        if (text.trim() !== aiAssistant.proxyUrl)
                            aiAssistant.proxyUrl = text.trim()
                    }
                }
                Text {
                    visible: proxyUrlField.visible && proxyUrlField.error.length > 0
                    text: proxyUrlField.error
                    font.pixelSize: FontSize.caption
                    color: "#d1453b"
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Text {
                    text: qsTr("Сейчас: ") + aiAssistant.proxyStatus
                    font.pixelSize: FontSize.caption
                    color: ThemeManager.secondaryTextColor
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Button {
                        text: qsTr("Проверить соединение")
                        enabled: !aiAssistant.isTestingConnection
                                 && aiAssistant.providerHasKey(root.activeProviderId)
                        onClicked: {
                            testResult.text = ""
                            aiAssistant.testConnection()
                        }
                    }
                    BusyIndicator {
                        running: aiAssistant.isTestingConnection
                        visible: running
                        implicitWidth: 24
                        implicitHeight: 24
                    }
                    Item { Layout.fillWidth: true }
                }

                Text {
                    id: testResult
                    property bool ok: false
                    visible: text.length > 0
                    font.pixelSize: FontSize.caption
                    color: ok ? "#3fa34d" : "#d1453b"
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true

                    Connections {
                        target: aiAssistant
                        function onConnectionTestFinished(ok, message) {
                            testResult.ok = ok
                            testResult.text = message
                        }
                    }
                }
            }
        }
    }
}
