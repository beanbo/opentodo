import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

// Self-contained dialog: opens via .open(), shows a spinner while the upload
// is in flight, then displays the share URL + a Copy-to-Clipboard button.
// Owned by whichever view triggered the share — wire it up in onCompleted:
//
//   ShareLinkDialog { id: shareDlg }
//   Connections {
//       target: shareManager
//       function onShareLinkReady(url, fileId) { shareDlg.showLink(url, fileId) }
//       function onShareError(reason)          { shareDlg.showError(reason) }
//   }
//
// The dialog manages its own busy / link / error state — callers only call
// .startBusy(<title>) when they kick off the upload, then showLink/showError
// when the manager fires its signal.
Dialog {
    id: root

    title: qsTr("Поделиться через Google Drive")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: (Overlay.overlay ? Math.min(560, Overlay.overlay.width - Spacing.xl) : 560)
    standardButtons: Dialog.Close

    // ── Public API ───────────────────────────────────────────────────────────
    property string itemLabel: ""   // e.g. "Проект «Дом»" or "3 задачи"
    property string fileId:    ""

    function startBusy(label) {
        root.itemLabel = label || ""
        root.fileId = ""
        root._state = "busy"
        root._url = ""
        root._error = ""
        root.open()
    }
    function showLink(url, id) {
        root._url = url
        root.fileId = id
        root._state = "link"
    }
    function showError(reason) {
        root._error = reason
        root._state = "error"
    }

    // ── Internal state ───────────────────────────────────────────────────────
    property string _state: "busy"      // "busy" | "link" | "error"
    property string _url: ""
    property string _error: ""

    background: Rectangle {
        color: ThemeManager.surfaceColor
        border.color: ThemeManager.borderColor
        border.width: 1
        radius: Radius.md
    }

    contentItem: ColumnLayout {
        spacing: Spacing.md
        // Width must be bounded so the Label wraps; Dialog.width minus the
        // standard 24 px padding on each side.
        width: root.availableWidth

        // ── BUSY ──────────────────────────────────────────────────────────────
        ColumnLayout {
            visible: root._state === "busy"
            spacing: Spacing.sm
            Layout.fillWidth: true

            BusyIndicator {
                running: visible
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
            }
            Label {
                text: qsTr("Публикую %1 на Google Drive…").arg(root.itemLabel || qsTr("элемент"))
                color: ThemeManager.textColor
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }
            Label {
                text: qsTr("Ссылка позволит ЛЮБОМУ открыть снимок данных в браузере.")
                color: ThemeManager.secondaryTextColor
                font.pixelSize: FontSize.caption
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }
        }

        // ── LINK ──────────────────────────────────────────────────────────────
        ColumnLayout {
            visible: root._state === "link"
            spacing: Spacing.sm
            Layout.fillWidth: true

            Label {
                text: qsTr("Готово. %1 опубликован — поделитесь ссылкой:")
                          .arg(root.itemLabel || qsTr("Элемент"))
                color: ThemeManager.textColor
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Spacing.sm

                TextField {
                    id: urlField
                    Layout.fillWidth: true
                    text: root._url
                    readOnly: true
                    selectByMouse: true
                    color: ThemeManager.textColor
                    background: Rectangle {
                        color: ThemeManager.backgroundColor
                        border.color: ThemeManager.borderColor
                        border.width: 1
                        radius: Radius.sm
                    }
                }

                Button {
                    id: copyBtn
                    text: copyBtn._copied
                          ? qsTr("Скопировано")
                          : qsTr("Копировать")
                    Material.background: ThemeManager.accentColor
                    Material.foreground: ThemeManager.accentForeground
                    property bool _copied: false
                    onClicked: {
                        urlField.selectAll()
                        urlField.copy()
                        copyBtn._copied = true
                        copyResetTimer.restart()
                    }
                    Timer {
                        id: copyResetTimer
                        interval: 1500
                        onTriggered: copyBtn._copied = false
                    }
                }
            }

            // Open-in-browser convenience
            Button {
                text: qsTr("Открыть в браузере")
                flat: true
                Material.foreground: ThemeManager.accentColor
                onClicked: Qt.openUrlExternally(root._url)
                Layout.alignment: Qt.AlignLeft
            }

            // Revoke action
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Spacing.sm
                spacing: Spacing.sm

                Label {
                    text: qsTr("Доступ — у всех, у кого есть ссылка (только чтение).")
                    color: ThemeManager.secondaryTextColor
                    font.pixelSize: FontSize.caption
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Button {
                    text: qsTr("Отозвать")
                    enabled: root.fileId !== ""
                    flat: true
                    Material.foreground: ThemeManager.errorColor
                    onClicked: {
                        if (root.fileId !== "") {
                            shareManager.revokeShare(root.fileId)
                            // The Connections in the parent view picks up
                            // shareRevoked and closes the dialog.
                        }
                    }
                }
            }
        }

        // ── ERROR ─────────────────────────────────────────────────────────────
        ColumnLayout {
            visible: root._state === "error"
            spacing: Spacing.sm
            Layout.fillWidth: true

            Label {
                text: "⚠"
                font.pixelSize: 32
                horizontalAlignment: Text.AlignHCenter
                Layout.alignment: Qt.AlignHCenter
                color: ThemeManager.errorColor
            }
            Label {
                text: qsTr("Не удалось опубликовать ссылку.")
                color: ThemeManager.textColor
                font.bold: true
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }
            Label {
                text: root._error
                color: ThemeManager.secondaryTextColor
                font.pixelSize: FontSize.small
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }
        }
    }
}
