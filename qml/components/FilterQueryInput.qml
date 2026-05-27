import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

Rectangle {
    id: root

    // Public API
    property string query: ""
    // queryChanged is auto-emitted by the QML property system on text update
    // (see TextField.onTextChanged below); no explicit declaration needed.
    signal querySubmitted(string query)

    // Theme
    property bool isDarkTheme: ThemeManager.isDark

    // Internal validation state — driven by debounce timer
    // F-7.32: _validating was declared but never set; removed.
    property bool   _isValid:       false
    property string _errorText:     ""
    property bool   _hasValidated:  false   // true once first validation ran

    implicitHeight: column.implicitHeight + 16
    implicitWidth:  300
    radius:         8
    // Q4.01 (night iter-10): theme-aware. Раньше всё hardcoded для
    // dark/classic_light, неподходяще для kale/blueberry/raspberry.
    color:          ThemeManager.surfaceColor
    border.color:   textField.activeFocus
                    ? ThemeManager.accentColor
                    : ThemeManager.borderColor
    border.width:   textField.activeFocus ? 2 : 1

    Behavior on border.color { ColorAnimation { duration: 120 } }

    // Debounce timer — fires 200 ms after the user stops typing
    Timer {
        id: debounceTimer
        interval: 200
        repeat:   false
        onTriggered: {
            const q = root.query.trim()
            if (q === "") {
                root._hasValidated = false
                root._isValid      = false
                root._errorText    = ""
            } else {
                root._hasValidated = true
                root._isValid      = filterViewModel.isQueryValid(q)
                root._errorText    = root._isValid ? "" : filterViewModel.queryError(q)
            }
        }
    }

    ColumnLayout {
        id: column
        anchors {
            left:    parent.left
            right:   parent.right
            top:     parent.top
            margins: 8
        }
        spacing: 6

        // ── Query text field ──────────────────────────────────────────────
        TextField {
            id: textField
            Layout.fillWidth: true

            text:        root.query
            placeholderText: qsTr("напр. today & p1, #Работа & overdue")

            // Monospace font for query text
            font.family:  "Consolas, 'Courier New', monospace"
            font.pixelSize: FontSize.body

            color:           ThemeManager.textColor
            placeholderTextColor: ThemeManager.secondaryTextColor

            // Remove the default TextField frame; the parent Rectangle is the border
            background: Item {}

            onTextChanged: {
                root.query = text  // auto-emits root.queryChanged
                debounceTimer.restart()
            }

            // B-7.16: extract submit logic into a function so both Return and
            // Enter (numpad) call the same handler without invoking the
            // non-existent Keys.returnPressed() signal.
            function _submit() {
                debounceTimer.stop()
                const q = root.query.trim()
                if (q !== "") {
                    root._hasValidated = true
                    root._isValid      = filterViewModel.isQueryValid(q)
                    root._errorText    = root._isValid ? "" : filterViewModel.queryError(q)
                    if (root._isValid) {
                        root.querySubmitted(q)
                    }
                }
            }

            Keys.onReturnPressed: _submit()
            Keys.onEnterPressed:  _submit()
        }

        // ── Validation indicator ─────────────────────────────────────────
        RowLayout {
            id: validationRow
            Layout.fillWidth: true
            spacing: 6
            // Only visible after at least one validation has run
            visible: root._hasValidated && root.query.trim() !== ""

            // Icon: checkmark or cross
            Text {
                text:            root._isValid ? "✓" : "✗"
                font.pixelSize:  FontSize.bodySmall
                font.bold:       true
                color:           root._isValid ? ThemeManager.successColor : ThemeManager.errorColor
                Layout.alignment: Qt.AlignVCenter
            }

            // Message text
            Text {
                id: validationMessage
                Layout.fillWidth: true
                text:       root._isValid ? qsTr("Запрос корректен") : root._errorText
                font.pixelSize: FontSize.bodySmall
                color:      root._isValid ? ThemeManager.successColor : ThemeManager.errorColor
                wrapMode:   Text.WordWrap
                Layout.alignment: Qt.AlignVCenter
            }
        }

        // Spacer to maintain consistent bottom padding when validation row is hidden
        Item {
            visible: !validationRow.visible
            height:  0
        }
    }
}
