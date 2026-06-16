import QtQuick
import OpenTodoUI

// MBottomNav — bottom navigation. Ported from m-app.jsx MBottomNav.
// Slot 0 is the fixed Inbox; slots 1–3 are pinned smart-lists or projects
// (pins is an array of up to 3 ids: "sv:today" / "sv:upcoming" / <projectId>).
// Empty slots show a dashed "Закрепить" affordance. Long-press any slot (or
// tap an empty one) → managePins().
Rectangle {
    id: root

    property var    pins: []
    property string activeKind: "inbox"   // inbox | today | upcoming | project | (other)
    property string activeProjectId: ""
    property bool   searchOpen: false
    signal tabSelected(string kind, string projectId)
    signal managePins()

    height: 66
    color: ThemeManager.navBarColor

    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: 1
        color: ThemeManager.borderColor
    }

    Row {
        anchors.fill: parent
        anchors.bottomMargin: 2

        Repeater {
            model: 4
            delegate: Item {
                id: slot
                required property int index

                width: root.width / 4
                height: parent.height

                readonly property bool   isInbox: index === 0
                readonly property string pinId: isInbox ? "" : (root.pins[index - 1] || "")
                readonly property bool   isSmart: !isInbox && (pinId === "sv:today" || pinId === "sv:upcoming")
                // projectById() is a plain function (not reactive); touch
                // projectViewModel.count so this re-evaluates once the project
                // model has finished loading and on add/remove.
                readonly property var    proj: {
                    var _dep = projectViewModel.count
                    return (!isInbox && !isSmart && pinId !== "")
                           ? projectViewModel.projectById(pinId) : null
                }
                readonly property bool   isEmpty: !isInbox && pinId === ""
                readonly property string smartKind: pinId === "sv:today" ? "today" : "upcoming"

                MNavItem {
                    anchors.fill: parent
                    iconName: slot.isInbox ? "inbox"
                              : slot.isSmart ? (slot.pinId === "sv:today" ? "today" : "upcoming")
                              : slot.proj ? ((slot.proj.icon && slot.proj.icon.length > 0) ? slot.proj.icon : "hash")
                              : "pin"
                    label: slot.isInbox ? qsTr("Входящие")
                           : slot.isSmart ? (slot.pinId === "sv:today" ? qsTr("Сегодня") : qsTr("Предстоящие"))
                           : slot.proj ? slot.proj.name
                           : qsTr("Закрепить")
                    dim: slot.isEmpty || (!slot.isInbox && !slot.isSmart && !slot.proj)
                    active: {
                        if (root.searchOpen) return false
                        if (slot.isInbox)  return root.activeKind === "inbox"
                        if (slot.isSmart)  return root.activeKind === slot.smartKind
                        if (slot.proj)     return root.activeKind === "project"
                                                  && root.activeProjectId === slot.pinId
                        return false
                    }
                    onClicked: {
                        if (slot.isInbox)      root.tabSelected("inbox", "")
                        else if (slot.isSmart) root.tabSelected(slot.smartKind, "")
                        else if (slot.proj)    root.tabSelected("project", slot.pinId)
                        else                   root.managePins()
                    }
                    onLongPressed: root.managePins()
                }
            }
        }
    }
}
