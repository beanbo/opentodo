import QtQuick
import QtQuick.Controls
import OpenTodoUI

// MAIScreen — mobile AI assistant chat. Ported from m-app.jsx MAIScreen.
// User bubbles right (accent), assistant bubbles left (surface). Wires to the
// aiAssistant context property (sendMessage + responseReceived/errorReceived).
Item {
    id: root
    signal menuRequested()
    signal openSettingsRequested()

    Rectangle { anchors.fill: parent; color: ThemeManager.backgroundColor }

    readonly property bool _configured: typeof aiAssistant !== "undefined" && aiAssistant.isConfigured

    // Tool-use: a task mutation the assistant proposed, awaiting confirmation.
    property string _pendingAction: ""
    property string _pendingSummary: ""

    MTopBar {
        id: topBar
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        big: true
        title: qsTr("Помощник")
        titleIconName: "sparkles"
        titleColor: ThemeManager.accentColor
        subtitle: qsTr("Разбивка задач и быстрые подсказки")
        onMenuClicked: root.menuRequested()
        MIconButton {
            visible: root._configured && messages.count > 0
            iconName: "reset"; iconColor: ThemeManager.textColor
            onClicked: { messages.clear(); aiAssistant.clearConversation() }
        }
    }

    ListModel { id: messages }

    Connections {
        target: typeof aiAssistant !== "undefined" ? aiAssistant : null
        ignoreUnknownSignals: true
        function onResponseReceived(text) { if (text.trim() !== "") { messages.append({ "role": "assistant", "content": text }); Qt.callLater(function(){ chat.positionViewAtEnd() }) } }
        function onErrorReceived(err)     { messages.append({ "role": "assistant", "content": qsTr("Ошибка: %1").arg(err) }); Qt.callLater(function(){ chat.positionViewAtEnd() }) }
        function onActionProposed(actionJson, summary) { root._pendingAction = actionJson; root._pendingSummary = summary }
        function onActionExecuted(summary) { root._pendingAction = ""; messages.append({ "role": "assistant", "content": qsTr("✓ %1").arg(summary) }); Qt.callLater(function(){ chat.positionViewAtEnd() }) }
    }

    function _send(txt) {
        var t = (txt !== undefined ? txt : input.text).trim()
        if (t === "" || !root._configured) return
        messages.append({ "role": "user", "content": t })
        aiAssistant.sendMessage(t)
        input.text = ""
        Qt.callLater(function(){ chat.positionViewAtEnd() })
    }

    // ── Not configured hint ────────────────────────────────────────────────
    Column {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -20
        width: Math.min(parent.width - 64, 320)
        spacing: 14
        visible: !root._configured
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 80; height: 80; radius: 40
            color: ThemeManager.surfaceColor
            border.width: 1; border.color: ThemeManager.borderColor
            IconSvg { anchors.centerIn: parent; name: "sparkles"; size: 38; color: ThemeManager.accentColor }
        }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: qsTr("Помощник не настроен"); font.pixelSize: 17; font.weight: Font.Bold; color: ThemeManager.textColor }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; text: qsTr("Добавьте API-ключ AI в настройках,\nчтобы начать разговор."); font.pixelSize: 14; color: ThemeManager.secondaryTextColor; lineHeight: 1.5; lineHeightMode: Text.ProportionalHeight }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: btnRow.width + 36; height: 44; radius: 14
            color: ThemeManager.accentColor
            Row { id: btnRow; anchors.centerIn: parent; spacing: 8
                IconSvg { anchors.verticalCenter: parent.verticalCenter; name: "settings"; size: 17; color: ThemeManager.accentForeground }
                Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Открыть настройки"); color: ThemeManager.accentForeground; font.pixelSize: 14; font.weight: Font.Bold } }
            MouseArea { anchors.fill: parent; onClicked: root.openSettingsRequested() }
        }
    }

    // ── Chat ───────────────────────────────────────────────────────────────
    ListView {
        id: chat
        anchors.top: topBar.bottom; anchors.left: parent.left; anchors.right: parent.right
        anchors.bottom: inputBar.top
        anchors.topMargin: 4
        visible: root._configured
        clip: true
        spacing: 12
        leftMargin: 16; rightMargin: 16; topMargin: 6; bottomMargin: 6
        model: messages
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        delegate: Item {
            required property string role
            required property string content
            width: chat.width - 32
            height: bubble.height

            readonly property bool isUser: role === "user"

            Rectangle {
                id: bubble
                width: Math.min(implicitTextWidth + 30, parent.width * 0.85)
                height: msgText.implicitHeight + 24
                anchors.right: parent.isUser ? parent.right : undefined
                anchors.left: parent.isUser ? undefined : parent.left
                radius: 18
                color: parent.isUser ? ThemeManager.accentColor : ThemeManager.surfaceColor
                border.width: parent.isUser ? 0 : 1
                border.color: ThemeManager.borderColor

                readonly property real implicitTextWidth: msgText.implicitWidth
                // Square off one corner (user: bottom-right, assistant: bottom-left).
                Rectangle {
                    width: parent.radius; height: parent.radius
                    anchors.bottom: parent.bottom
                    anchors.right: parent.parent.isUser ? parent.right : undefined
                    anchors.left: parent.parent.isUser ? undefined : parent.left
                    color: parent.color
                }
                Text {
                    id: msgText
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin: 15; anchors.rightMargin: 15; anchors.topMargin: 12
                    text: parent.parent.content
                    wrapMode: Text.WordWrap
                    textFormat: Text.PlainText
                    font.pixelSize: 14
                    lineHeight: 1.45; lineHeightMode: Text.ProportionalHeight
                    color: parent.parent.isUser ? ThemeManager.accentForeground : ThemeManager.textColor
                }
            }
        }

        // Suggestion chips when the conversation is empty.
        Column {
            anchors.centerIn: parent
            width: Math.min(parent.width - 48, 340)
            spacing: 10
            visible: root._configured && messages.count === 0
            Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: qsTr("С чего начнём?"); font.pixelSize: 15; font.weight: Font.DemiBold; color: ThemeManager.secondaryTextColor; bottomPadding: 4 }
            Repeater {
                model: [ qsTr("Разбей мою задачу на шаги"),
                         qsTr("Что важно сделать сегодня?"),
                         qsTr("Предложи приоритеты для списка") ]
                delegate: Rectangle {
                    required property var modelData
                    width: parent.width; height: 46; radius: 14
                    color: cma.pressed ? ThemeManager.hoverColor : ThemeManager.surfaceColor
                    border.width: 1; border.color: ThemeManager.borderColor
                    Text { anchors.left: parent.left; anchors.leftMargin: 16; anchors.right: parent.right; anchors.rightMargin: 16; anchors.verticalCenter: parent.verticalCenter; text: modelData; font.pixelSize: 14; color: ThemeManager.textColor; elide: Text.ElideRight }
                    MouseArea { id: cma; anchors.fill: parent; onClicked: root._send(modelData) }
                }
            }
        }
    }

    // ── Action confirmation card (tool-use) ───────────────────────────────
    Rectangle {
        id: actionCard
        visible: root._pendingAction !== ""
        anchors.bottom: inputBar.top
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: 12; anchors.rightMargin: 12; anchors.bottomMargin: 8
        height: visible ? cardCol.implicitHeight + 24 : 0
        radius: 14
        color: ThemeManager.surfaceColor
        border.width: 1; border.color: ThemeManager.accentColor
        Column {
            id: cardCol
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 12
            spacing: 10
            Row {
                spacing: 8
                IconSvg { anchors.verticalCenter: parent.verticalCenter; name: "sparkles"; size: 16; color: ThemeManager.accentColor }
                Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Подтвердите действие"); font.pixelSize: 13; font.weight: Font.Bold; color: ThemeManager.textColor }
            }
            Text { width: parent.width; text: root._pendingSummary; wrapMode: Text.WordWrap; font.pixelSize: 14; color: ThemeManager.textColor }
            Row {
                anchors.right: parent.right
                spacing: 10
                Rectangle {
                    width: cancTxt.implicitWidth + 28; height: 36; radius: 10
                    color: "transparent"; border.width: 1; border.color: ThemeManager.borderColor
                    Text { id: cancTxt; anchors.centerIn: parent; text: qsTr("Отмена"); color: ThemeManager.textColor; font.pixelSize: 13; font.weight: Font.DemiBold }
                    MouseArea { anchors.fill: parent; onClicked: root._pendingAction = "" }
                }
                Rectangle {
                    width: confTxt.implicitWidth + 28; height: 36; radius: 10
                    color: ThemeManager.accentColor
                    Text { id: confTxt; anchors.centerIn: parent; text: qsTr("Подтвердить"); color: ThemeManager.accentForeground; font.pixelSize: 13; font.weight: Font.Bold }
                    MouseArea { anchors.fill: parent; onClicked: { if (typeof haptics !== "undefined") haptics.medium(); aiAssistant.confirmAction(root._pendingAction); root._pendingAction = "" } }
                }
            }
        }
    }

    // ── Input bar ──────────────────────────────────────────────────────────
    Rectangle {
        id: inputBar
        anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
        height: 64
        color: ThemeManager.backgroundColor
        visible: root._configured
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: ThemeManager.borderColor }

        Rectangle {
            anchors.left: parent.left; anchors.leftMargin: 14
            anchors.right: sendBtn.left; anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            height: 44; radius: 22
            color: ThemeManager.surfaceColor
            border.width: 1; border.color: ThemeManager.borderColor
            TextField {
                id: input
                anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 12
                verticalAlignment: Text.AlignVCenter
                placeholderText: (typeof aiAssistant !== "undefined" && aiAssistant.isResponding) ? qsTr("Помощник печатает…") : qsTr("Спросить помощника…")
                placeholderTextColor: ThemeManager.secondaryTextColor
                font.pixelSize: 14; color: ThemeManager.textColor
                enabled: !(typeof aiAssistant !== "undefined" && aiAssistant.isResponding)
                background: Item {}
                onAccepted: root._send()
            }
        }
        Rectangle {
            id: sendBtn
            anchors.right: parent.right; anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            width: 44; height: 44; radius: 22
            readonly property bool _on: input.text.trim() !== "" && !(typeof aiAssistant !== "undefined" && aiAssistant.isResponding)
            color: _on ? ThemeManager.accentColor : ThemeManager.hoverColor
            IconSvg { anchors.centerIn: parent; name: "send"; size: 18; color: parent._on ? ThemeManager.accentForeground : ThemeManager.faintTextColor }
            MouseArea { anchors.fill: parent; onClicked: root._send() }
        }
    }
}
