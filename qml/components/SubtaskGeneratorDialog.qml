import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

// Modal dialog: AI subtask generator.
// Usage:
//   SubtaskGeneratorDialog {
//       taskId:      "some-uuid"
//       taskContent: "Запустить лендинг"
//       onAccepted:  { /* subtasks already created in C++ */ }
//   }
Popup {
    id: root

    // ── Public API ─────────────────────────────────────────────────────────────
    property string taskId:      ""
    property string taskContent: ""

    signal subtasksAdded(int count)

    // ── Geometry ───────────────────────────────────────────────────────────────
    width:  Math.min(parent.width - Spacing.xl * 2, 540)
    height: contentColumn.implicitHeight + Spacing.xl * 2

    anchors.centerIn: parent
    modal:  true
    focus:  true
    closePolicy: Popup.CloseOnEscape   // no close on press-outside while loading

    // ── Internal state ─────────────────────────────────────────────────────────
    property var    _suggestions: []   // QStringList from AI → JS array
    property bool   _loading:     false
    property string _errorText:   ""
    property int    _countChoice: 5    // default subtask count

    // Reset state every time the dialog opens
    onOpened: {
        _suggestions = []
        _loading     = false
        _errorText   = ""
        _countChoice = 5
        suggestionModel.clear()
    }

    onClosed: {
        if (_loading)
            aiAssistant.cancelSubtaskGeneration()
    }

    // ── Background ─────────────────────────────────────────────────────────────
    background: Rectangle {
        radius:       Radius.lg
        color:        ThemeManager.surfaceColor
        border.color: ThemeManager.borderColor
        border.width: 1
    }

    // ── Content ────────────────────────────────────────────────────────────────
    ColumnLayout {
        id: contentColumn
        anchors {
            left:    parent.left
            right:   parent.right
            top:     parent.top
            margins: Spacing.lg
        }
        spacing: Spacing.md

        // ── Header ─────────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Spacing.sm

            // Q4.01 (redesign): "🤖" заменён на IconSvg "sparkles" — единый
            // визуальный язык с другими AI-точками входа (sidebar Помощник,
            // "Сгенерировать описание" в EditorDescriptionField).
            IconSvg {
                name: "sparkles"
                size: 18
                color: ThemeManager.accentColor
                Layout.alignment: Qt.AlignVCenter
            }

            Text {
                Layout.fillWidth: true
                text: root.taskContent.length > 0
                      ? qsTr("Разбить «%1» на подзадачи").arg(root.taskContent)
                      : qsTr("Разбить на подзадачи")
                font.pixelSize: FontSize.subtitle
                font.weight:    Font.Medium
                color:          ThemeManager.textColor
                elide:          Text.ElideRight
                wrapMode:       Text.NoWrap
            }
        }

        // ── Divider ────────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color:  ThemeManager.borderColor
        }

        // ── Count picker (SpinBox row) ─────────────────────────────────────────
        RowLayout {
            visible:          root._suggestions.length === 0 && !root._loading
            Layout.fillWidth: true
            spacing:          Spacing.sm

            Text {
                text:           qsTr("Количество подзадач:")
                font.pixelSize: FontSize.body
                color:          ThemeManager.textColor
            }

            Item { Layout.fillWidth: true }

            Repeater {
                model: [3, 5, 7, 10]
                delegate: Rectangle {
                    required property int modelData
                    width:  36
                    height: 28
                    radius: Radius.sm
                    color:  root._countChoice === modelData
                            ? ThemeManager.accentColor
                            : ThemeManager.hoverColor
                    border.color: root._countChoice === modelData
                                  ? ThemeManager.accentColor
                                  : ThemeManager.borderColor
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text:             modelData
                        font.pixelSize:   FontSize.body
                        font.weight:      Font.Medium
                        // Q4.01 night iter-59: selected chip text sits on the accent
                        // bg → accentForeground (white was unreadable on yellow themes).
                        color:            root._countChoice === modelData
                                          ? ThemeManager.accentForeground
                                          : ThemeManager.textColor
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    root._countChoice = modelData
                    }
                }
            }
        }

        // ── Loading spinner ────────────────────────────────────────────────────
        ColumnLayout {
            visible:          root._loading
            Layout.fillWidth: true
            spacing:          Spacing.sm

            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                running: root._loading
                Material.accent: ThemeManager.accentColor
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                text:           qsTr("Генерирую подзадачи…")
                font.pixelSize: FontSize.body
                color:          ThemeManager.secondaryTextColor
            }
        }

        // ── Error message ──────────────────────────────────────────────────────
        Rectangle {
            visible:          root._errorText.length > 0
            Layout.fillWidth: true
            height:           errText.implicitHeight + Spacing.sm * 2
            radius:           Radius.sm
            color:            Qt.rgba(1, 0.2, 0.2, 0.08)
            border.color:     ThemeManager.errorColor
            border.width:     1

            Text {
                id: errText
                anchors {
                    left:    parent.left
                    right:   parent.right
                    top:     parent.top
                    margins: Spacing.sm
                }
                text:           root._errorText
                font.pixelSize: FontSize.small
                color:          ThemeManager.errorColor
                wrapMode:       Text.WordWrap
            }
        }

        // ── Suggestion list ────────────────────────────────────────────────────
        ListView {
            id: suggestionList
            visible:          suggestionModel.count > 0
            Layout.fillWidth: true
            implicitHeight:   Math.min(contentHeight, 320)
            clip:             true
            spacing:          Spacing.xs
            model:            ListModel { id: suggestionModel }

            ScrollBar.vertical: ScrollBar {}

            delegate: RowLayout {
                required property int    index
                required property bool   checked
                required property string stepText

                width:   suggestionList.width
                height:  36
                spacing: Spacing.sm

                CheckBox {
                    id: checkBox
                    checked: parent.checked
                    Material.accent: ThemeManager.accentColor
                    onCheckedChanged: suggestionModel.setProperty(parent.index, "checked", checked)
                }

                TextField {
                    id: stepField
                    Layout.fillWidth: true
                    text:             parent.stepText
                    font.pixelSize:   FontSize.body
                    color:            ThemeManager.textColor
                    background: Rectangle {
                        color:        stepField.activeFocus
                                      ? Qt.rgba(ThemeManager.accentColor.r,
                                                ThemeManager.accentColor.g,
                                                ThemeManager.accentColor.b, 0.07)
                                      : "transparent"
                        radius:       Radius.sm
                        border.color: stepField.activeFocus
                                      ? ThemeManager.accentColor
                                      : ThemeManager.borderColor
                        border.width: stepField.activeFocus ? 1 : 0
                    }
                    onTextChanged: suggestionModel.setProperty(parent.index, "stepText", text)
                }

                ToolButton {
                    implicitWidth:  28
                    implicitHeight: 28
                    // Task 17cea65b iter-3: было unicode "✕".
                    contentItem: IconSvg {
                        name: "close"
                        size: 13
                        color: ThemeManager.secondaryTextColor
                    }
                    background: Rectangle {
                        radius: Radius.sm
                        color:  parent.hovered ? ThemeManager.hoverColor : "transparent"
                    }
                    onClicked: suggestionModel.remove(parent.index)
                }
            }
        }

        // ── Divider before actions ─────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color:  ThemeManager.borderColor
        }

        // ── Action row ─────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Spacing.sm

            // Generate button (hidden once suggestions shown).
            // Q4.01 (redesign): эмодзи "✨" убран — иконка sparkles уже
            // несётся заголовком диалога (см. line 75).
            Button {
                visible: root._suggestions.length === 0 && !root._loading
                text:    qsTr("Сгенерировать")
                enabled: !root._loading && root.taskContent.length > 0
                Material.background: ThemeManager.accentColor
                Material.foreground: ThemeManager.accentForeground
                onClicked: _generate()
            }

            // Retry button (only after suggestions shown)
            Button {
                visible: root._suggestions.length > 0
                text:    "↺ Сгенерировать снова"
                flat:    true
                contentItem: Text {
                    text:                parent.text
                    font:                parent.font
                    color:               ThemeManager.secondaryTextColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment:   Text.AlignVCenter
                }
                onClicked: _generate()
            }

            Item { Layout.fillWidth: true }

            // Cancel
            Button {
                text: qsTr("Отмена")
                flat: true
                contentItem: Text {
                    text:                parent.text
                    font:                parent.font
                    color:               ThemeManager.secondaryTextColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment:   Text.AlignVCenter
                }
                onClicked: root.close()
            }

            // Accept (only once we have suggestions)
            Button {
                id: acceptBtn
                visible: suggestionModel.count > 0
                text: {
                    // B-7.8: use proper Russian plural function
                    let n = _checkedCount()
                    return n > 0 ? qsTr("Добавить %1%2").arg(n).arg(pluralize(n)) : qsTr("Ничего не выбрано")
                }
                enabled:  _checkedCount() > 0
                Material.background: ThemeManager.accentColor
                Material.foreground: ThemeManager.accentForeground
                onClicked: _accept()
            }
        }

        // Bottom spacer
        Item { height: 1 }
    }

    // ── AI signal connections ──────────────────────────────────────────────────
    Connections {
        target: aiAssistant

        function onSubtasksGenerated(suggestions, parentId) {
            if (parentId !== root.taskId || !root.visible)
                return
            root._loading     = false
            root._errorText   = ""
            root._suggestions = suggestions

            suggestionModel.clear()
            for (let i = 0; i < suggestions.length; ++i) {
                suggestionModel.append({ checked: true, stepText: suggestions[i] })
            }
        }

        function onSubtaskGenerationFailed(error) {
            if (!root.visible)
                return
            root._loading   = false
            root._errorText = error
        }
    }

    // ── Private helpers ────────────────────────────────────────────────────────
    function _generate() {
        root._loading     = true
        root._errorText   = ""
        root._suggestions = []
        suggestionModel.clear()
        aiAssistant.generateSubtasks(root.taskId, root._countChoice)
    }

    function _checkedCount() {
        let n = 0
        for (let i = 0; i < suggestionModel.count; ++i) {
            if (suggestionModel.get(i).checked)
                ++n
        }
        return n
    }

    // B-7.8: correct Russian plural for 11-14 edge case
    function pluralize(n) {
        var mod10  = n % 10
        var mod100 = n % 100
        if (mod10 === 1 && mod100 !== 11)                           return qsTr(" подзадача")
        if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14)) return qsTr(" подзадачи")
        return qsTr(" подзадач")
    }

    function _accept() {
        let added = 0
        for (let i = 0; i < suggestionModel.count; ++i) {
            const item = suggestionModel.get(i)
            if (item.checked && item.stepText.trim() !== "") {
                taskViewModel.addSubtask(root.taskId, item.stepText.trim())
                ++added
            }
        }
        root.subtasksAdded(added)
        root.close()
    }
}
