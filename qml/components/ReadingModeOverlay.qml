import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

// Full-screen distraction-free reading view for a task description.
//
// Displays the task title and description (rendered as Markdown) in a wide,
// comfortable layout. Strictly read-only — opening this overlay should never
// emit any update to the underlying task.
//
// Usage from a parent:
//   ReadingModeOverlay { id: readingMode }
//   readingMode.open(taskTitle, taskDescription)
Popup {
    id: root

    // ── Public API ────────────────────────────────────────────────────────
    property string taskTitle:       ""
    property string taskDescription: ""

    // Convenience entry point used by the editor. We shadow Popup.open() with
    // a 2-arg signature; toggling `visible = true` is the supported way to
    // present a Popup without going through the shadowed inherited slot.
    function open(title, description) {
        root.taskTitle       = title || ""
        root.taskDescription = description || ""
        root.visible = true
    }

    // ── Geometry / behaviour ──────────────────────────────────────────────
    // Pattern matches QuickJumpDialog.qml: anchor against Overlay.overlay
    // explicitly (not `parent`) — Popup needs the overlay item by name.
    parent:        Overlay.overlay
    modal:         true
    anchors.centerIn: Overlay.overlay
    width:         Overlay.overlay ? Overlay.overlay.width  * 0.8  : 800
    height:        Overlay.overlay ? Overlay.overlay.height * 0.85 : 600
    padding:       0
    closePolicy:   Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // ── Background card ───────────────────────────────────────────────────
    background: Rectangle {
        color:        ThemeManager.surfaceColor
        border.color: ThemeManager.borderColor
        border.width: 1
        radius:       Radius.xl
    }

    // Dim the rest of the window for focus.
    Overlay.modal: Rectangle {
        color: ThemeManager.scrimColor
    }

    // ── Content layout ────────────────────────────────────────────────────
    contentItem: ColumnLayout {
        spacing: 0

        // ── Header row ────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth:    true
            Layout.leftMargin:   Spacing.lg
            Layout.rightMargin:  Spacing.md
            Layout.topMargin:    Spacing.md
            Layout.bottomMargin: Spacing.sm
            spacing: Spacing.sm

            Text {
                Layout.fillWidth: true
                text:             root.taskTitle.length > 0
                                      ? root.taskTitle
                                      : "Без названия"
                color:            ThemeManager.textColor
                font.pixelSize:   FontSize.h2
                font.weight:      Font.DemiBold
                wrapMode:         Text.WordWrap
                elide:            Text.ElideRight
                maximumLineCount: 2
            }

            // Close ✕ button — mirrors the EditorHeader style.
            Rectangle {
                Layout.alignment: Qt.AlignTop
                width:  32
                height: 32
                radius: Radius.sm
                color:  closeHover.containsMouse
                            ? ThemeManager.hoverColor
                            : "transparent"

                Text {
                    anchors.centerIn: parent
                    text:  "✕"
                    color: ThemeManager.secondaryTextColor
                    font.pixelSize: FontSize.headline
                }

                MouseArea {
                    id: closeHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape:  Qt.PointingHandCursor
                    onClicked:    root.close()
                }
            }
        }

        // Divider under the header
        Rectangle {
            Layout.fillWidth:    true
            Layout.leftMargin:   Spacing.lg
            Layout.rightMargin:  Spacing.lg
            height: 1
            color:  ThemeManager.borderColor
        }

        // ── Body: scrollable rendered description ─────────────────────────
        Flickable {
            id: bodyFlick

            Layout.fillWidth:  true
            Layout.fillHeight: true
            Layout.leftMargin:   Spacing.xl
            Layout.rightMargin:  Spacing.xl
            Layout.topMargin:    Spacing.lg
            Layout.bottomMargin: Spacing.sm

            contentWidth:  width
            contentHeight: descriptionText.implicitHeight + Spacing.lg
            clip:          true
            boundsBehavior: Flickable.StopAtBounds

            Text {
                id: descriptionText

                anchors.left:  parent.left
                anchors.right: parent.right

                text: root.taskDescription.length > 0
                          ? root.taskDescription
                          : "_Описание отсутствует_"

                textFormat:     Text.MarkdownText
                color:          ThemeManager.textColor
                linkColor:      ThemeManager.accentColor
                wrapMode:       Text.WordWrap
                font.pixelSize: 17
                // Comfortable proportional line height for long-form reading.
                lineHeightMode: Text.ProportionalHeight
                lineHeight:     1.45

                onLinkActivated: function(url) {
                    Qt.openUrlExternally(url)
                }
            }

            // Subtle scrollbar — matches the rest of the editor look.
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }
        }

        // ── Footer hint ───────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin:   Spacing.lg
            Layout.rightMargin:  Spacing.lg
            height: 1
            color:  ThemeManager.borderColor
        }

        Text {
            Layout.fillWidth:    true
            Layout.leftMargin:   Spacing.lg
            Layout.rightMargin:  Spacing.lg
            Layout.topMargin:    Spacing.sm
            Layout.bottomMargin: Spacing.md

            text:                "Esc — закрыть"
            color:               ThemeManager.secondaryTextColor
            font.pixelSize:      FontSize.caption
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
