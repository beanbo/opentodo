import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import OpenTodoUI

// =============================================================================
// SettingsSectionSupport.qml
// =============================================================================
//
// PURPOSE
// -------
// "Поддержать разработчика" — donation page with crypto wallet addresses for
// BTC / ETH / TON / USDT (both TRC-20 and ERC-20). Each row has an emoji
// glyph, currency name, network notes, the wallet address (monospace) and a
// "Скопировать" button. After a successful copy an inline "Скопировано ✓"
// hint shows for ~2 seconds.
//
// CLIPBOARD MECHANISM
// -------------------
// Qt 6 QML has no `Qt.clipboard` property and no built-in QML clipboard
// module that ships on every supported platform (desktop + Android + WASM).
// The portable trick is a hidden, off-tree `TextEdit` that holds the
// wallet text, selects all of it, then calls `copy()` — which routes the
// selection into `QGuiApplication::clipboard()` on every Qt platform. No new
// C++ surface needed, no platform `#ifdef`s, and we don't have to extend
// the Telemetry service. The hidden TextEdit lives inside each row so each
// wallet has its own clipboard handle.
//
// LAYOUT
// ------
// The section follows the same expand/collapse "card" pattern as the eight
// existing sections (header row with chevron, ColumnLayout under it for the
// content). All visual tokens (radii, colours, spacing) come from the
// shared singletons — no hard-coded colours.
//
// TODO before public release
// --------------------------
// Replace the PLACEHOLDER wallet addresses below with real ones.
// =============================================================================

Rectangle {
    id: root
    radius: 8
    color: ThemeManager.surfaceColor
    border.color: ThemeManager.borderColor
    border.width: 1
    implicitHeight: sectionHeader.height + (expanded ? sectionContent.implicitHeight : 0)
    clip: true

    property bool expanded: false

    Behavior on implicitHeight {
        NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
    }

    // ── Wallet addresses ──────────────────────────────────────────────────────
    readonly property string btcAddress:     "bc1q7uq4s4j2ze0cqr8d00jgj4fqkry068zrfre56j"
    readonly property string ethAddress:     "0xB613541324129f144DdE64C57E6332Ad813cA0e8"
    readonly property string tonAddress:     "UQAdzBUGiQx5T3DFDNGLcImgzgZvyUumR5msZwLuQhjVrAag"
    readonly property string usdtTrcAddress: "TRgzkiKdhLXiftfPNymreHMhio3vQJW9wf"
    readonly property string usdtErcAddress: "0xB613541324129f144DdE64C57E6332Ad813cA0e8"

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

            Text {
                text: qsTr("Поддержать разработчика")
                font.pixelSize: 16
                font.bold: true
                color: ThemeManager.textColor
                Layout.fillWidth: true
            }

            Text {
                text: root.expanded ? "▾" : "▸"
                font.pixelSize: 14
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
        spacing: 12

        // Intro / large header card
        Rectangle {
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.bottomMargin: 0
            implicitHeight: introCol.implicitHeight + 24
            radius: 8
            color: ThemeManager.hoverColor

            ColumnLayout {
                id: introCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 16
                spacing: 6

                Text {
                    text: qsTr("Поддержать разработчика")
                    font.pixelSize: 22
                    font.bold: true
                    color: ThemeManager.textColor
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }

                Text {
                    text: qsTr("Если приложение оказалось полезным — можно поддержать разработку:")
                    font.pixelSize: 14
                    color: ThemeManager.secondaryTextColor
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }

        // ── Wallet cards ─────────────────────────────────────────────────────
        WalletCard {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            glyph: "₿"
            currencyName: "Bitcoin"
            networkLabel: qsTr("Сеть: Bitcoin")
            walletAddress: root.btcAddress
        }

        WalletCard {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            glyph: "Ξ"
            currencyName: "Ethereum"
            networkLabel: qsTr("Сеть: Ethereum (ERC-20)")
            walletAddress: root.ethAddress
        }

        WalletCard {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            glyph: "💎"
            currencyName: "TON"
            networkLabel: qsTr("Сеть: The Open Network")
            walletAddress: root.tonAddress
        }

        WalletCard {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            glyph: "₮"
            currencyName: "USDT (TRC-20)"
            networkLabel: qsTr("Сеть: Tron (TRC-20)")
            walletAddress: root.usdtTrcAddress
        }

        WalletCard {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            glyph: "₮"
            currencyName: "USDT (ERC-20)"
            networkLabel: qsTr("Сеть: Ethereum (ERC-20)")
            walletAddress: root.usdtErcAddress
        }

        // ── Footer ───────────────────────────────────────────────────────────
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 4
            Layout.bottomMargin: 20
            text: qsTr("Спасибо! 🙏")
            font.pixelSize: 14
            font.bold: true
            color: ThemeManager.textColor
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
    }

    // ── Inline component: one wallet row card ────────────────────────────────
    // Defined inline (not a separate .qml file) so a single registration in
    // CMakeLists.txt covers the whole "Support" surface. Each card owns its
    // own hidden TextEdit + toast timer, so multiple "Скопировано ✓" hints
    // can be visible simultaneously without state leaking between rows.
    component WalletCard: Rectangle {
        id: cardRoot

        // Public properties — set from the call site above.
        property string glyph:         ""
        property string currencyName:  ""
        property string networkLabel:  ""
        property string walletAddress: ""

        implicitHeight: cardCol.implicitHeight + 24
        radius: 8
        color: ThemeManager.hoverColor

        // Hidden TextEdit holds the wallet text for the OS-level copy.
        // off-screen + zero size + visible:false so it doesn't intercept
        // hits, doesn't render, and isn't tab-focusable. selectAll() + copy()
        // routes the selection into QGuiApplication::clipboard() on every
        // Qt platform — desktop, Android, WASM.
        TextEdit {
            id: clipboardProxy
            visible: false
            width: 0
            height: 0
            x: -10000
            y: -10000
            readOnly: true
            text: cardRoot.walletAddress
            // Block any chance of stealing focus.
            activeFocusOnPress: false
        }

        // Toast-flag timer: after "Скопировано ✓" shows it goes back to
        // "Скопировать" 2 s later.
        Timer {
            id: copiedToastTimer
            interval: 2000
            onTriggered: cardRoot._justCopied = false
        }

        // Local flag rendered by the button label below.
        property bool _justCopied: false

        ColumnLayout {
            id: cardCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 16
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                // Currency glyph in a coloured tile, same shape as the
                // app-icon tile in SettingsSectionAbout.
                Rectangle {
                    width: 36; height: 36; radius: 8
                    color: ThemeManager.accentColor
                    Layout.alignment: Qt.AlignTop

                    Text {
                        anchors.centerIn: parent
                        text: cardRoot.glyph
                        color: "white"
                        font.pixelSize: 20
                        font.bold: true
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: cardRoot.currencyName
                        font.pixelSize: 16
                        font.bold: true
                        color: ThemeManager.textColor
                    }

                    Text {
                        text: cardRoot.networkLabel
                        font.pixelSize: 12
                        color: ThemeManager.secondaryTextColor
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }

            // Address + copy button row.
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 8

                // Use a selectable TextEdit (not a Text) so the user can
                // manually highlight/copy the address with the mouse too.
                TextEdit {
                    Layout.fillWidth: true
                    text: cardRoot.walletAddress
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    font.family: "Consolas"
                    font.pixelSize: 12
                    color: ThemeManager.textColor
                    selectionColor: ThemeManager.accentColor
                    selectedTextColor: "white"
                }

                Button {
                    implicitHeight: 32
                    implicitWidth: copyBtnText.implicitWidth + 28

                    contentItem: Text {
                        id: copyBtnText
                        text: cardRoot._justCopied ? qsTr("Скопировано ✓") : qsTr("Скопировать")
                        color: ThemeManager.accentColor
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 4
                        color: parent.hovered ? Qt.darker(ThemeManager.hoverColor, 1.05)
                                              : "transparent"
                        border.color: ThemeManager.accentColor
                        border.width: 1
                    }

                    onClicked: {
                        // Standard QML clipboard trick: load text into a
                        // hidden TextEdit, select all, copy() → routes
                        // through QGuiApplication::clipboard() on every
                        // Qt platform.
                        clipboardProxy.selectAll()
                        clipboardProxy.copy()
                        clipboardProxy.deselect()

                        cardRoot._justCopied = true
                        copiedToastTimer.restart()

                        // Optional global toast for visibility outside the
                        // section (e.g. when the user has scrolled).
                        const w = cardRoot.Window.window
                        if (w && typeof w.showToast === "function")
                            w.showToast(qsTr("Адрес %1 скопирован").arg(cardRoot.currencyName), "success")

                        if (typeof telemetry !== "undefined"
                            && telemetry && telemetry.enabled)
                            telemetry.trackEvent("settings_support_copy_address",
                                                 { "currency": cardRoot.currencyName })
                    }
                }
            }
        }
    }
}
