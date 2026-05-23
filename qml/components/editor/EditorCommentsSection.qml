import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

// Full CRUD comments section for the task editor.
ColumnLayout {
    id: root

    property string taskId: ""

    Layout.fillWidth:    true
    Layout.leftMargin:   Spacing.md
    Layout.rightMargin:  Spacing.md
    Layout.bottomMargin: Spacing.md
    spacing: Spacing.sm

    // ── JS helpers ─────────────────────────────────────────────────────────
    function _relativeTime(dt) {
        if (!dt || !dt.getTime) return ""
        const nowMs  = Date.now()
        const diffMs = nowMs - dt.getTime()
        const diffSec = Math.floor(diffMs / 1000)
        if (diffSec < 60)     return qsTr("только что")
        const diffMin = Math.floor(diffSec / 60)
        if (diffMin < 60)     return qsTr("%1 мин. назад").arg(diffMin)
        const diffH = Math.floor(diffMin / 60)
        if (diffH < 24)       return qsTr("%1 ч. назад").arg(diffH)
        const diffD = Math.floor(diffH / 24)
        if (diffD < 7)        return qsTr("%1 д. назад").arg(diffD)
        return Qt.formatDate(dt, "dd.MM.yyyy")
    }

    function _refreshModel() {
        if (root.taskId !== "")
            commentRepeater.model = dataStore.commentsForTask(root.taskId)
        else
            commentRepeater.model = []
    }

    // ── Auto-refresh ───────────────────────────────────────────────────────
    Component.onCompleted: _refreshModel()

    onTaskIdChanged: _refreshModel()

    Connections {
        target: dataStore
        function onCommentsChanged() { root._refreshModel() }
    }

    // ── Section header ─────────────────────────────────────────────────────
    Text {
        text:          qsTr("Комментарии")
        font.pixelSize: 13
        font.weight:   Font.Medium
        color:         ThemeManager.textColor
    }

    // ── Comment list ───────────────────────────────────────────────────────
    Repeater {
        id: commentRepeater
        // model set by _refreshModel()

        delegate: Rectangle {
            required property var    modelData
            required property int    index

            id: commentRow
            Layout.fillWidth: true
            // Width will be constrained by parent ColumnLayout
            width:  parent ? parent.width : 320
            height: commentRowLayout.implicitHeight + Spacing.sm * 2
            radius: Radius.sm
            color:  rowHover.containsMouse
                    ? ThemeManager.hoverColor : "transparent"

            property bool editing: false

            // B-7.39: single MouseArea handles both hover state and triple-click
            // detection. Two stacked MouseAreas previously caused the outer one
            // to steal events from the inner one unpredictably.
            MouseArea {
                id: rowHover
                anchors.fill: parent
                hoverEnabled: true
                onClicked: function(mouse) {
                    tripleClickTimer.clicks++
                    tripleClickTimer.restart()
                    if (tripleClickTimer.clicks >= 3) {
                        commentRow.editing = true
                        tripleClickTimer.clicks = 0
                        // ITER1 #29: stop the timer immediately when triple-click fires
                        // so the reset doesn't corrupt the next click sequence. Also
                        // accept the event to prevent the third click from triggering
                        // any parent click handlers.
                        tripleClickTimer.stop()
                        mouse.accepted = true
                    }
                }
            }

            Timer {
                id: tripleClickTimer
                interval: 500
                property int clicks: 0
                onTriggered: clicks = 0
            }

            RowLayout {
                id:      commentRowLayout
                anchors {
                    left:   parent.left
                    right:  parent.right
                    top:    parent.top
                    margins: Spacing.sm
                }
                spacing: Spacing.sm

                // ── Avatar ─────────────────────────────────────────────────
                Rectangle {
                    width:  28
                    height: 28
                    radius: 14
                    color:  ThemeManager.accentColor
                    Layout.alignment: Qt.AlignTop

                    Text {
                        anchors.centerIn: parent
                        text:          qsTr("Я")
                        font.pixelSize: 11
                        font.bold:      true
                        color:          "white"
                    }
                }

                // ── Body ───────────────────────────────────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    // Author + time row
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Text {
                            text:          qsTr("Я")
                            font.pixelSize: 12
                            font.weight:   Font.Medium
                            color:         ThemeManager.textColor
                        }

                        Text {
                            // modelData.postedAt is a QDateTime exposed as JS Date
                            text:          root._relativeTime(modelData.postedAt)
                            font.pixelSize: 11
                            color:         ThemeManager.secondaryTextColor
                        }

                        Item { Layout.fillWidth: true }

                        // ⋯ context-menu trigger.
                        // Q3.29: rewrite — the Q3.28 version was unclickable.
                        // Three concrete bugs in the previous attempt:
                        //   (1) Opacity was gated on `commentMenu.opened` which
                        //       in Qt 6 is a Popup *signal*, not a property. A
                        //       binding that references a signal name yields
                        //       the signal-handler function — truthy in JS but
                        //       not reactive — so the binding never updated
                        //       and qmllint / runtime emitted no error. Result:
                        //       the chip's `enabled: opacity > 0` was wedged
                        //       in an unpredictable state because the opacity
                        //       binding was a static truthy expression.
                        //   (2) `enabled: opacity > 0` is dangerous: while
                        //       the 80 ms opacity NumberAnimation is in flight
                        //       the value is between 0 and 1 → enabled flickers,
                        //       which on a fast click eats the press event.
                        //   (3) 20×20 hit area is below Microsoft's 24 px /
                        //       Apple's 28 px minimum-click-target guideline.
                        // Fix: ToolButton (proper Qt Quick Controls widget
                        // with built-in hit-test, accessibility and theme
                        // integration), 28×28 hit area, ALWAYS visible (the
                        // discoverability win outweighs the row noise — the
                        // ⋯ glyph is small and neutral). The triple-click
                        // editing shortcut on rowHover is preserved as-is.
                        ToolButton {
                            id:        commentActionsBtn
                            flat:           true
                            visible:        !commentRow.editing
                            Layout.preferredWidth:  28
                            Layout.preferredHeight: 28
                            padding:        0
                            // Task 17cea65b iter-3: было unicode "⋯".
                            contentItem: IconSvg {
                                anchors.centerIn: parent
                                name: "more-horizontal"
                                size: 16
                                color: ThemeManager.secondaryTextColor
                            }
                            // Slightly muted so it doesn't compete with the
                            // comment body text; the hover state (handled by
                            // the Material style) gives clear affordance.
                            Material.foreground: commentMenu.visible
                                                 ? ThemeManager.accentColor
                                                 : ThemeManager.secondaryTextColor

                            ToolTip.text:    qsTr("Действия с комментарием")
                            ToolTip.visible: hovered
                            ToolTip.delay:   400

                            onClicked: commentMenu.popup()

                            Menu {
                                id: commentMenu
                                MenuItem {
                                    text: qsTr("Редактировать")
                                    onTriggered: {
                                        commentRow.editing = true
                                        editArea.forceActiveFocus()
                                    }
                                }
                                MenuItem {
                                    text: qsTr("Удалить")
                                    onTriggered: {
                                        dataStore.removeComment(modelData.id)
                                        root._refreshModel()
                                    }
                                }
                            }
                        }
                    }

                    // Comment text (view mode)
                    Text {
                        visible:          !commentRow.editing
                        Layout.fillWidth: true
                        text:             modelData.content || ""
                        font.pixelSize:   13
                        color:            ThemeManager.textColor
                        wrapMode:         Text.WordWrap
                        textFormat:       Text.MarkdownText
                        linkColor:        ThemeManager.accentColor
                        onLinkActivated: function(url) { Qt.openUrlExternally(url) }
                    }

                    // Edit mode
                    TextArea {
                        id:               editArea
                        visible:          commentRow.editing
                        Layout.fillWidth: true
                        text:             commentRow.editing ? (modelData.content || "") : ""
                        font.pixelSize:   13
                        color:            ThemeManager.textColor
                        wrapMode:         TextArea.Wrap
                        background: Rectangle {
                            color:        ThemeManager.backgroundColor
                            border.color: ThemeManager.accentColor
                            border.width: 1
                            radius:       Radius.sm
                        }

                        Keys.onEscapePressed: commentRow.editing = false
                    }

                    // Save / Cancel row (edit mode)
                    RowLayout {
                        visible:  commentRow.editing
                        spacing:  Spacing.sm

                        Rectangle {
                            height: 26
                            width:  saveEditText.implicitWidth + 16
                            radius: Radius.sm
                            color:  saveEditHover.containsMouse
                                    ? Qt.darker(ThemeManager.accentColor, 1.1)
                                    : ThemeManager.accentColor

                            Text {
                                id:            saveEditText
                                anchors.centerIn: parent
                                text:          qsTr("Сохранить")
                                font.pixelSize: 12
                                color:         "white"
                            }

                            MouseArea {
                                id:           saveEditHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape:  Qt.PointingHandCursor
                                onClicked: {
                                    const newText = editArea.text.trim()
                                    if (newText.length === 0) return
                                    // B-7.40: dataStore.updateComment(id, content) exists
                                    // (confirmed in datastore.h line 115) — use it to
                                    // preserve the original postedAt timestamp.
                                    dataStore.updateComment(modelData.id, newText)
                                    commentRow.editing = false
                                    root._refreshModel()
                                }
                            }
                        }

                        Rectangle {
                            height: 26
                            width:  cancelEditText.implicitWidth + 16
                            radius: Radius.sm
                            color:  cancelEditHover.containsMouse
                                    ? ThemeManager.hoverColor : "transparent"
                            border.color: ThemeManager.borderColor
                            border.width: 1

                            Text {
                                id:            cancelEditText
                                anchors.centerIn: parent
                                text:          qsTr("Отмена")
                                font.pixelSize: 12
                                color:         ThemeManager.textColor
                            }

                            MouseArea {
                                id:           cancelEditHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape:  Qt.PointingHandCursor
                                onClicked:    commentRow.editing = false
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Empty state ────────────────────────────────────────────────────────
    Text {
        visible:        commentRepeater.count === 0
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        text:          qsTr("Пока нет комментариев")
        font.pixelSize: 13
        font.italic:   true
        color:         ThemeManager.secondaryTextColor
        topPadding:    4
        bottomPadding: 4
    }

    // ── Add comment row ────────────────────────────────────────────────────
    // Q3.26: switched the bordered wrapper from `height: ...` to
    // `Layout.preferredHeight: ...`. ColumnLayout positions children by
    // their Layout-attached sizes (implicit dims as a fallback). A plain
    // `height` binding updated the Rectangle's drawn height but did NOT
    // trigger the parent ColumnLayout to reflow, so the "Добавить" button
    // below stayed pinned at the original Y while the TextArea visibly
    // grew underneath. Switching to Layout.preferredHeight makes the
    // layout track the growing content properly.
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: newCommentArea.implicitHeight + 2
        radius: Radius.sm
        color:  ThemeManager.backgroundColor
        border.color: newCommentArea.activeFocus
                      ? ThemeManager.accentColor : ThemeManager.borderColor
        border.width: 1

        TextArea {
            id:           newCommentArea
            anchors {
                left:   parent.left
                right:  parent.right
                top:    parent.top
            }
            placeholderText: qsTr("Написать комментарий… (Markdown поддерживается)")
            font.pixelSize: 13
            color:          ThemeManager.textColor
            wrapMode:       TextArea.Wrap
            background:     null
            leftPadding:    8
            rightPadding:   8
            topPadding:     6
            bottomPadding:  6
        }
    }

    // Add button (right-aligned)
    RowLayout {
        Layout.fillWidth: true

        Item { Layout.fillWidth: true }

        Rectangle {
            id:     addCommentBtn
            height: 28
            width:  addBtnText.implicitWidth + 20
            radius: Radius.sm

            readonly property bool canAdd:
                newCommentArea.text.trim().length > 0 && root.taskId !== ""

            color: {
                // Qualify with addCommentBtn.* so unscoped lookup doesn't
                // fall back to a parent ListView's "canAdd" role / context.
                if (!addCommentBtn.canAdd)     return ThemeManager.borderColor
                if (addBtnHover.containsMouse) return Qt.darker(ThemeManager.accentColor, 1.1)
                return ThemeManager.accentColor
            }
            opacity: addCommentBtn.canAdd ? 1.0 : 0.5

            Text {
                id:            addBtnText
                anchors.centerIn: parent
                text:          qsTr("Добавить")
                font.pixelSize: 12
                color:         addCommentBtn.canAdd ? "white" : ThemeManager.secondaryTextColor
            }

            MouseArea {
                id:           addBtnHover
                anchors.fill: parent
                hoverEnabled: true
                cursorShape:  addCommentBtn.canAdd ? Qt.PointingHandCursor : Qt.ArrowCursor
                enabled:      addCommentBtn.canAdd
                onClicked: {
                    const trimmed = newCommentArea.text.trim()
                    if (trimmed.length === 0 || root.taskId === "") return
                    // Signature: addComment(content, taskId, projectId)
                    dataStore.addComment(trimmed, root.taskId, "")
                    newCommentArea.text = ""
                    root._refreshModel()
                }
            }
        }
    }
}
