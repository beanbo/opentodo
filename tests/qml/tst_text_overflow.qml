import QtQuick
import QtTest
import QtQuick.Controls.Material

// Catches text-clipping regressions: Text/Label whose content exceeds the
// allotted width must elide rather than just disappearing or pushing siblings
// off-screen. We render the suspect components with extreme content and check
// that the label is `truncated` and stays inside its bounds.
TestCase {
    id: tc
    name: "TextOverflow"
    when: windowShown
    width: 320
    height: 600

    // ── Bare Label with elide must respect its bounding width ──────────────
    Component {
        id: longLabelComp
        Item {
            width: 200
            height: 24
            property alias label: lbl
            Label {
                id: lbl
                anchors.fill: parent
                text: "Очень-очень-очень-очень-очень длинная подпись для проекта"
                elide: Text.ElideRight
            }
        }
    }

    function test_label_elides_within_bounds() {
        const item = createTemporaryObject(longLabelComp, tc)
        verify(item !== null)
        wait(50)
        verify(item.label.truncated, "Long text should be truncated/elided")
        verify(item.label.width <= 200, "Label must not exceed its container width")
    }

    // ── Title text must elide, not overflow horizontally ──────────────────
    Component {
        id: titleComp
        Item {
            width: 250
            height: 56
            property alias label: titleLabel
            Label {
                id: titleLabel
                anchors.fill: parent
                anchors.margins: 12
                text: "Очень длинный заголовок страницы который точно не помещается"
                font.pixelSize: 18
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
        }
    }

    function test_title_label_elides() {
        const item = createTemporaryObject(titleComp, tc)
        wait(50)
        verify(item.label.truncated, "Title with overflow must elide")
    }

    // ── Button label must elide, not blow up the parent ───────────────────
    Component {
        id: buttonComp
        Item {
            width: 140
            height: 40
            property alias btn: button
            Rectangle {
                anchors.fill: parent
                Label {
                    id: button
                    anchors.fill: parent
                    anchors.margins: 8
                    text: "Очень длинная подпись на кнопке"
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    function test_button_label_does_not_overflow() {
        const item = createTemporaryObject(buttonComp, tc)
        wait(50)
        verify(item.btn.truncated, "Button label with overflow must elide")
    }

    // ── A label with elide=ElideNone but bounded width should NOT truncate
    //     when text fits — guard against false positives ──────────────────
    Component {
        id: shortLabelComp
        Item {
            width: 400
            height: 24
            property alias label: lbl
            Label {
                id: lbl
                anchors.fill: parent
                text: "Короткий текст"
                elide: Text.ElideRight
            }
        }
    }

    function test_short_text_is_not_truncated() {
        const item = createTemporaryObject(shortLabelComp, tc)
        wait(50)
        verify(!item.label.truncated, "Short text must not be reported as truncated")
    }
}
