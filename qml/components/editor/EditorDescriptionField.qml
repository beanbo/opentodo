import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

// Multiline description TextArea with an optional AI-generate button.
// The button is visible only when aiAssistant is available and configured.
// Q4.01 (redesign): wrapped in a Card-like surface (border + radius 10)
// matching the Card pattern from view-others.jsx so the description block
// reads as its own section.
ColumnLayout {
    id: root

    property string taskId: ""
    property var    task:   null

    Layout.fillWidth:    true
    Layout.leftMargin:   12
    Layout.rightMargin:  12
    Layout.bottomMargin: 0

    spacing: 0

    // ── AI generate button ────────────────────────────────────────────────
    ToolButton {
        id: aiGenerateBtn

        // Resolve aiAssistant safely — it may not be present in every context
        // (e.g. WASM builds without a key, or test harnesses).
        readonly property bool _aiAvailable: typeof aiAssistant !== "undefined"
                                             && aiAssistant !== null
                                             && aiAssistant.isConfigured

        visible: _aiAvailable

        // Local busy flag — true while the HTTP round-trip is in flight.
        property bool aiGeneratingNow: false

        Layout.alignment: Qt.AlignRight
        Layout.topMargin: Spacing.xs

        text: aiGeneratingNow ? qsTr("Думаю...") : qsTr("Сгенерировать описание")
        flat: true
        enabled: !aiGeneratingNow

        font.pixelSize: FontSize.caption

        // Q4.01 (redesign): "✨" из text-строки убран; вместо него рендерим
        // настоящий IconSvg "sparkles" слева. Скрываем во время "Думаю..."
        // чтобы спиннер-state читался текстом без иконки.
        contentItem: RowLayout {
            spacing: 4

            IconSvg {
                visible: !aiGenerateBtn.aiGeneratingNow
                name: "sparkles"
                size: 12
                color: aiGenerateBtn.enabled
                       ? ThemeManager.accentColor
                       : ThemeManager.secondaryTextColor
                Layout.alignment: Qt.AlignVCenter
            }

            Text {
                text:            aiGenerateBtn.text
                font:            aiGenerateBtn.font
                color:           aiGenerateBtn.enabled
                                     ? ThemeManager.accentColor
                                     : ThemeManager.secondaryTextColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment:   Text.AlignVCenter
                elide:           Text.ElideRight
                Layout.alignment: Qt.AlignVCenter
            }
        }

        background: Rectangle {
            color:       "transparent"
            radius:      Radius.sm
            border.color: aiGenerateBtn.hovered && aiGenerateBtn.enabled
                              ? ThemeManager.accentColor
                              : "transparent"
            border.width: 1
        }

        onClicked: {
            const title = root.task ? (root.task.content || "").trim() : ""
            if (title === "") {
                const w = Window.window
                if (w && typeof w.showToast === "function")
                    w.showToast(qsTr("Сначала введите название задачи"), "info")
                return
            }
            aiGeneratingNow = true
            aiAssistant.generateDescription(title)
        }

        Connections {
            target: (typeof aiAssistant !== "undefined" && aiAssistant !== null)
                    ? aiAssistant : null

            function onDescriptionGenerated(text) {
                // Write directly into the TextArea and persist via updateTask
                descriptionField.text = text
                if (root.taskId !== "" && root.task)
                    taskViewModel.updateTask(root.taskId, "description", text)
                aiGenerateBtn.aiGeneratingNow = false
            }

            function onDescriptionGenerationFailed(reason) {
                const w = Window.window
                if (w && typeof w.showToast === "function")
                    w.showToast("AI: " + reason, "error")
                aiGenerateBtn.aiGeneratingNow = false
            }
        }
    }

    // ── Markdown formatting toolbar ───────────────────────────────────────
    // Helper functions live on the toolbar Item so the Shortcuts and
    // ToolButtons can call them without duplicating logic.
    Item {
        id: mdToolbar

        Layout.fillWidth: true
        Layout.preferredHeight: toolbarRow.implicitHeight
        Layout.topMargin: Spacing.xs

        // Q3.25: opacity-based show/hide (not `visible`). When the toolbar
        // collapses via visible:false the ColumnLayout shifts descriptionField
        // up under the cursor — descriptionField.hovered flips back on,
        // toolbar reappears, layout shifts down again, hover flips off →
        // infinite flicker loop. Keeping the slot allocated (opacity instead
        // of visible) makes the layout stable, and a HoverHandler on the
        // toolbar itself keeps it visible while the cursor is over its area.
        opacity: descriptionField.activeFocus
                 || descriptionField.hovered
                 || mdToolbarHover.hovered
                 || descriptionField.text.length > 0 ? 1.0 : 0.0
        enabled: opacity > 0

        Behavior on opacity { NumberAnimation { duration: 120 } }

        HoverHandler { id: mdToolbarHover }

        // Safely fire a telemetry event if the optional context property exists.
        function _track(action) {
            if (typeof telemetry !== "undefined" && telemetry) {
                try {
                    telemetry.trackEvent("md_toolbar_used", { action: action })
                } catch (e) { /* telemetry must never crash the UI */ }
            }
        }

        // Wrap the current selection in `prefix`+`suffix`. If nothing is
        // selected, insert the wrappers and place the cursor between them.
        function wrapSelection(prefix, suffix) {
            if (descriptionField.selectionStart === descriptionField.selectionEnd) {
                const p = descriptionField.cursorPosition
                descriptionField.insert(p, prefix + suffix)
                descriptionField.cursorPosition = p + prefix.length
            } else {
                const s = descriptionField.selectionStart
                const e = descriptionField.selectionEnd
                const sel = descriptionField.getText(s, e)
                descriptionField.remove(s, e)
                descriptionField.insert(s, prefix + sel + suffix)
                descriptionField.select(s + prefix.length,
                                        s + prefix.length + sel.length)
            }
            descriptionField.forceActiveFocus()
        }

        // Prepend `marker` to the start of the line that currently contains
        // the cursor (used for bullet / numbered list buttons).
        function prependLineMarker(marker) {
            const pos = descriptionField.cursorPosition
            const t   = descriptionField.text
            // Find start of current line
            let lineStart = pos
            while (lineStart > 0 && t.charAt(lineStart - 1) !== "\n")
                lineStart--
            descriptionField.insert(lineStart, marker)
            descriptionField.cursorPosition = pos + marker.length
            descriptionField.forceActiveFocus()
        }

        function openLinkPopup() {
            if (descriptionField.selectionStart !== descriptionField.selectionEnd) {
                linkTextField.text = descriptionField.getText(
                    descriptionField.selectionStart,
                    descriptionField.selectionEnd)
            } else {
                linkTextField.text = ""
            }
            linkUrlField.text = ""
            linkPopup.open()
        }

        RowLayout {
            id: toolbarRow
            anchors.left:  parent.left
            anchors.right: parent.right
            spacing: 2

            // Bold
            ToolButton {
                id: boldBtn
                flat: true
                text: "B"
                font.pixelSize: FontSize.body
                font.bold: true
                Layout.preferredHeight: 28
                Layout.preferredWidth:  Spacing.xl
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Полужирный (Ctrl+B)")
                contentItem: Text {
                    text: parent.text
                    font: parent.font
                    color: ThemeManager.textColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment:   Text.AlignVCenter
                }
                background: Rectangle {
                    color: boldBtn.hovered ? ThemeManager.hoverColor
                                           : "transparent"
                    radius: Radius.sm
                }
                onClicked: {
                    mdToolbar.wrapSelection("**", "**")
                    mdToolbar._track("bold")
                }
            }

            // Italic
            ToolButton {
                id: italicBtn
                flat: true
                text: "I"
                font.pixelSize: FontSize.body
                font.italic: true
                Layout.preferredHeight: 28
                Layout.preferredWidth:  Spacing.xl
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Курсив (Ctrl+I)")
                contentItem: Text {
                    text: parent.text
                    font: parent.font
                    color: ThemeManager.textColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment:   Text.AlignVCenter
                }
                background: Rectangle {
                    color: italicBtn.hovered ? ThemeManager.hoverColor
                                             : "transparent"
                    radius: Radius.sm
                }
                onClicked: {
                    mdToolbar.wrapSelection("*", "*")
                    mdToolbar._track("italic")
                }
            }

            // Inline code
            ToolButton {
                id: codeBtn
                flat: true
                text: "</>"
                font.pixelSize: FontSize.caption
                Layout.preferredHeight: 28
                Layout.preferredWidth:  36
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Код")
                contentItem: Text {
                    text: parent.text
                    font: parent.font
                    color: ThemeManager.textColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment:   Text.AlignVCenter
                }
                background: Rectangle {
                    color: codeBtn.hovered ? ThemeManager.hoverColor
                                           : "transparent"
                    radius: Radius.sm
                }
                onClicked: {
                    mdToolbar.wrapSelection("`", "`")
                    mdToolbar._track("code")
                }
            }

            // Link
            // Q4.01 (redesign): "🔗" emoji → stroke link icon.
            ToolButton {
                id: linkBtn
                flat: true
                Layout.preferredHeight: 28
                Layout.preferredWidth:  Spacing.xl
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Вставить ссылку (Ctrl+K)")
                contentItem: IconSvg {
                    name: "link"
                    size: 14
                    color: ThemeManager.textColor
                    anchors.centerIn: parent
                }
                background: Rectangle {
                    color: linkBtn.hovered ? ThemeManager.hoverColor
                                           : "transparent"
                    radius: Radius.sm
                }
                onClicked: {
                    mdToolbar.openLinkPopup()
                    mdToolbar._track("link_open")
                }
            }

            // Bullet list
            ToolButton {
                id: bulletBtn
                flat: true
                text: "•"
                font.pixelSize: FontSize.body
                Layout.preferredHeight: 28
                Layout.preferredWidth:  Spacing.xl
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Маркированный список")
                contentItem: Text {
                    text: parent.text
                    font: parent.font
                    color: ThemeManager.textColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment:   Text.AlignVCenter
                }
                background: Rectangle {
                    color: bulletBtn.hovered ? ThemeManager.hoverColor
                                             : "transparent"
                    radius: Radius.sm
                }
                onClicked: {
                    mdToolbar.prependLineMarker("- ")
                    mdToolbar._track("bullet")
                }
            }

            // Numbered list
            ToolButton {
                id: numberedBtn
                flat: true
                text: "1."
                font.pixelSize: FontSize.body
                Layout.preferredHeight: 28
                Layout.preferredWidth:  Spacing.xl
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Нумерованный список")
                contentItem: Text {
                    text: parent.text
                    font: parent.font
                    color: ThemeManager.textColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment:   Text.AlignVCenter
                }
                background: Rectangle {
                    color: numberedBtn.hovered ? ThemeManager.hoverColor
                                               : "transparent"
                    radius: Radius.sm
                }
                onClicked: {
                    mdToolbar.prependLineMarker("1. ")
                    mdToolbar._track("numbered")
                }
            }

            Item { Layout.fillWidth: true } // pusher
        }

        // Keyboard shortcuts — only fire when the description field has focus.
        Shortcut {
            sequence: "Ctrl+B"
            context:  Qt.WindowShortcut
            enabled:  descriptionField.activeFocus
            onActivated: {
                mdToolbar.wrapSelection("**", "**")
                mdToolbar._track("bold")
            }
        }
        Shortcut {
            sequence: "Ctrl+I"
            context:  Qt.WindowShortcut
            enabled:  descriptionField.activeFocus
            onActivated: {
                mdToolbar.wrapSelection("*", "*")
                mdToolbar._track("italic")
            }
        }
        // Ctrl+K (insert link) is NOT a Shortcut here: DesktopLayout's
        // window-wide Ctrl+K (command palette) made the pair ambiguous and
        // neither fired. descriptionField claims it via Keys.onShortcutOverride
        // and handles it in Keys.onPressed instead.
    }

    // ── Link insertion popup ──────────────────────────────────────────────
    Popup {
        id: linkPopup

        // Snap to the toolbar so the popup looks attached to the link button.
        parent: mdToolbar
        x: 0
        y: mdToolbar.height
        width: 320
        padding: Spacing.sm
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color:        ThemeManager.surfaceColor
            border.color: ThemeManager.borderColor
            border.width: 1
            radius:       Radius.md
        }

        function _accept() {
            const txt = (linkTextField.text || "").trim()
            const url = (linkUrlField.text  || "").trim()
            if (url.length === 0) {
                linkUrlField.forceActiveFocus()
                return
            }
            const display = txt.length > 0 ? txt : url
            const md = "[" + display + "](" + url + ")"

            if (descriptionField.selectionStart !== descriptionField.selectionEnd) {
                const s = descriptionField.selectionStart
                const e = descriptionField.selectionEnd
                descriptionField.remove(s, e)
                descriptionField.insert(s, md)
                descriptionField.cursorPosition = s + md.length
            } else {
                const p = descriptionField.cursorPosition
                descriptionField.insert(p, md)
                descriptionField.cursorPosition = p + md.length
            }
            descriptionField.forceActiveFocus()
            mdToolbar._track("link_insert")
            linkPopup.close()
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: Spacing.xs

            Label {
                text:  qsTr("Текст")
                color: ThemeManager.secondaryTextColor
                font.pixelSize: FontSize.caption
            }
            TextField {
                id: linkTextField
                Layout.fillWidth: true
                placeholderText: qsTr("Текст ссылки")
                color:            ThemeManager.textColor
                placeholderTextColor: ThemeManager.secondaryTextColor
                font.pixelSize:   FontSize.body
                selectByMouse:    true
                onAccepted: linkUrlField.forceActiveFocus()
            }
            Label {
                text:  "URL"
                color: ThemeManager.secondaryTextColor
                font.pixelSize: FontSize.caption
            }
            TextField {
                id: linkUrlField
                Layout.fillWidth: true
                placeholderText: "https://"
                color:            ThemeManager.textColor
                placeholderTextColor: ThemeManager.secondaryTextColor
                font.pixelSize:   FontSize.body
                selectByMouse:    true
                onAccepted: linkPopup._accept()
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Spacing.xs
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Отмена")
                    flat: true
                    onClicked: linkPopup.close()
                }
                Button {
                    text: qsTr("Вставить")
                    highlighted: true
                    onClicked: linkPopup._accept()
                }
            }
        }

        onOpened: linkTextField.forceActiveFocus()
    }

    // ── Description TextArea ──────────────────────────────────────────────
    // Q4.01 (redesign): surface bg + 1px border + radius 10 + padding 12
    // matching the Card pattern from view-others.jsx so the description
    // reads as a self-contained block.
    TextArea {
        id: descriptionField

        // Q4.01 (iter-8): дизайн-канвас показывает description-блок ТОЛЬКО
        // в Card-стиле когда у задачи есть содержимое (или сейчас фокус).
        // Когда description пуст и пользователь не редактирует — выглядит
        // как plain-text placeholder без border/bg, минимальная высота.
        // Раньше пустое поле занимало ~80 px высоты с большим Card-фоном
        // + placeholder — это сильно отличалось от макета.
        readonly property bool _isEmpty: text.length === 0 && !activeFocus

        Layout.fillWidth: true
        Layout.topMargin: _isEmpty ? 0 : 4
        // Q4.01 (iter-11): empty-state почти нулевой — 20px достаточно для
        // тонкой click-зоны "type here" без visual gap'а перед Meta Card.
        // Раньше preferredHeight=32 + topMargin=4 + spacing=12 давало ~50px
        // пустого пространства между AI-link и meta-полями, которого в
        // дизайне нет. maximumHeight закрепляет высоту даже если TextArea
        // implicitHeight больше.
        Layout.preferredHeight: _isEmpty ? 20 : implicitHeight
        Layout.maximumHeight:   _isEmpty ? 20 : -1

        text:            root.task ? (root.task.description || "") : ""
        // Q4.01 (iter-9): дизайн-канвас не показывает placeholder в пустом
        // description-поле — над meta-полями видна только AI-link
        // "✦ Сгенерировать описание". Раньше placeholder висел даже при
        // hidden Card-стиле и портил вид. Скрываем когда _isEmpty.
        placeholderText: _isEmpty ? "" : qsTr("Описание (поддерживается Markdown)")
        font.pixelSize:  FontSize.body
        color:           ThemeManager.textColor
        wrapMode:        TextEdit.Wrap

        leftPadding:   _isEmpty ? 4 : 12
        rightPadding:  _isEmpty ? 4 : 12
        topPadding:    _isEmpty ? 6 : 12
        bottomPadding: _isEmpty ? 6 : 12

        background: Rectangle {
            color:        descriptionField._isEmpty ? "transparent" : ThemeManager.surfaceColor
            border.color: descriptionField._isEmpty
                          ? "transparent"
                          : (descriptionField.activeFocus
                             ? ThemeManager.accentColor
                             : ThemeManager.borderColor)
            border.width: 1
            radius:       10
        }

        // Render inline Markdown so [text](url) links appear styled and emit
        // linkActivated when clicked. Qt preserves the underlying Markdown
        // source on read-back, so onEditingFinished still receives valid
        // Markdown for persistence.
        textFormat: TextEdit.MarkdownText

        // NOTE: Qt 6.11's TextArea (Material style) does NOT expose a public
        // `linkColor` property — assigning to it produced
        //   "Cannot assign to non-existent property "linkColor""
        // which made the WHOLE EditorDescriptionField type unavailable, so
        // TaskEditor.qml would error out with
        //   "Type EditorDescriptionField unavailable".
        // Markdown link colour is instead controlled by a CSS-in-HTML stylesheet
        // injected via the underlying QTextDocument. We set it on completion
        // (and on theme change) so links pick up the accent colour without
        // tripping the missing-property error.
        Component.onCompleted: _applyLinkColor()
        Connections {
            target: ThemeManager
            function onAccentColorChanged() { descriptionField._applyLinkColor() }
        }
        function _applyLinkColor() {
            if (!textDocument)
                return;
            // Inline CSS works for QTextDocument's HTML-derived rendering.
            // Markdown links are converted to <a href="..."> internally.
            textDocument.defaultStyleSheet =
                "a { color: " + ThemeManager.accentColor + "; }";
        }

        // Reject anything other than http/https/mailto/file. In particular
        // javascript: and data: URLs must never reach openUrlExternally.
        function _isSafeUrl(url) {
            if (!url) return false;
            var s = String(url).toLowerCase();
            return s.startsWith("http://")
                || s.startsWith("https://")
                || s.startsWith("mailto:")
                || s.startsWith("file:");
        }

        // file:// targets a local resource — give the user a chance to
        // confirm before launching whatever app is registered for that
        // path. http/https/mailto are opened directly.
        function _openUrlSafely(url) {
            if (!_isSafeUrl(url))
                return
            var s = String(url).toLowerCase()
            if (s.startsWith("file:")) {
                linkConfirmPopup.pendingUrl = String(url)
                linkConfirmPopup.open()
            } else {
                Qt.openUrlExternally(url)
            }
        }

        onLinkActivated: function(link) {
            _openUrlSafely(link)
        }

        // Ctrl+K → link popup while editing (see the note by the Ctrl+B/I
        // Shortcuts). Overriding wins the key over the window Ctrl+K.
        function _isLinkChord(event) {
            return event.key === Qt.Key_K
                && (event.modifiers & (Qt.ControlModifier | Qt.ShiftModifier
                                       | Qt.AltModifier | Qt.MetaModifier)) === Qt.ControlModifier
        }
        Keys.onShortcutOverride: (event) => {
            if (_isLinkChord(event)) event.accepted = true
        }
        Keys.onPressed: (event) => {
            if (_isLinkChord(event)) {
                mdToolbar.openLinkPopup()
                mdToolbar._track("link_open")
                event.accepted = true
            }
        }

        // Pointing-hand cursor when hovering a link; default I-beam
        // otherwise so text editing still feels natural.
        HoverHandler {
            id: linkHover
            cursorShape: descriptionField.hoveredLink && descriptionField.hoveredLink.length > 0
                             ? Qt.PointingHandCursor
                             : Qt.IBeamCursor
        }

        onEditingFinished: {
            if (root.taskId !== "" && root.task && text !== root.task.description)
                taskViewModel.updateTask(root.taskId, "description", text)
        }

        // ITER1 #23: flush description when the component is destroyed even if
        // editingFinished was never emitted (e.g. user pressed Save/Delete while
        // the TextArea still had focus).
        Component.onDestruction: {
            if (root.taskId !== "" && root.task && text !== root.task.description)
                taskViewModel.updateTask(root.taskId, "description", text)
        }
    }

    // ── Link confirmation popup (file:// only) ────────────────────────────
    Popup {
        id: linkConfirmPopup

        property string pendingUrl: ""

        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        width: Math.min(420, parent ? parent.width - Spacing.md * 2 : 420)
        padding: Spacing.md
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color:        ThemeManager.surfaceColor
            border.color: ThemeManager.borderColor
            border.width: 1
            radius:       Radius.md
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: Spacing.sm

            Label {
                Layout.fillWidth: true
                text: qsTr("Открыть ссылку?")
                color: ThemeManager.textColor
                font.pixelSize: FontSize.body
                font.weight: Font.DemiBold
            }
            Label {
                Layout.fillWidth: true
                text: linkConfirmPopup.pendingUrl
                color: ThemeManager.secondaryTextColor
                font.pixelSize: FontSize.caption
                wrapMode: Text.WrapAnywhere
                elide: Text.ElideMiddle
                maximumLineCount: 3
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Spacing.xs
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Нет")
                    flat: true
                    onClicked: {
                        linkConfirmPopup.pendingUrl = ""
                        linkConfirmPopup.close()
                    }
                }
                Button {
                    text: qsTr("Да")
                    highlighted: true
                    onClicked: {
                        var u = linkConfirmPopup.pendingUrl
                        linkConfirmPopup.pendingUrl = ""
                        linkConfirmPopup.close()
                        if (u && u.length > 0)
                            Qt.openUrlExternally(u)
                    }
                }
            }
        }
    }

    // Sync text back when the task object is replaced (navigating between tasks)
    Connections {
        target: root
        function onTaskChanged() {
            if (root.task) descriptionField.text = root.task.description || ""
        }
    }
}
