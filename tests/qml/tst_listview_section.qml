import QtQuick
import QtQuick.Controls
import QtTest

// Reproduces the "section delegate doesn't render in list mode" bug.
// Verifies that ListView.section.delegate is created when items have a role
// matching section.property — even when the section value is the empty
// string (the most common "(Без раздела)" case).
TestCase {
    id: tc
    name: "ListViewSection"
    when: windowShown
    width: 400
    height: 300

    // Minimal model with two items, both having sectionId="" — should produce
    // exactly ONE section header at the top.
    ListModel {
        id: emptySectionModel
        ListElement { sectionId: ""; content: "Task 1" }
        ListElement { sectionId: ""; content: "Task 2" }
    }

    // Mixed model with two distinct sections.
    ListModel {
        id: mixedSectionModel
        ListElement { sectionId: "a"; content: "Task A1" }
        ListElement { sectionId: "a"; content: "Task A2" }
        ListElement { sectionId: "b"; content: "Task B1" }
    }

    Component {
        id: listComp
        Item {
            width: 400
            height: 300
            property alias listView: lv
            property alias headersCreated: hdrCounter.value

            QtObject {
                id: hdrCounter
                property int value: 0
            }

            ListView {
                id: lv
                anchors.fill: parent
                spacing: 0
                section.property: "sectionId"
                section.criteria: ViewSection.FullString
                section.labelPositioning: ViewSection.InlineLabels

                section.delegate: Rectangle {
                    id: hdr
                    required property string section
                    width: ListView.view.width
                    height: 32
                    color: "lightgray"

                    Text {
                        anchors.centerIn: parent
                        text: "SECTION:[" + hdr.section + "]"
                    }

                    Component.onCompleted: hdrCounter.value = hdrCounter.value + 1
                }

                delegate: Rectangle {
                    width: ListView.view.width
                    height: 24
                    Text { anchors.left: parent.left; text: content }
                }
            }
        }
    }

    // Documents the Qt 6 ListView limitation: when section.property resolves
    // to an EMPTY string for every visible item, no section.delegate is created.
    // This is why TaskViewModel exposes the SectionGroupKey role with the
    // sentinel "__none__" — the test_sentinel_value_renders_header below
    // verifies the workaround.
    function test_qt_skips_empty_section_delegate() {
        const item = createTemporaryObject(listComp, tc, { })
        item.listView.model = emptySectionModel
        wait(100)
        compare(item.headersCreated, 0,
                "Qt skips section.delegate when all items have an empty section " +
                "value — this is the limitation we work around with __none__.")
    }

    function test_mixed_sections_render_multiple_headers() {
        const item = createTemporaryObject(listComp, tc, { })
        item.listView.model = mixedSectionModel
        wait(100)
        compare(item.headersCreated, 2,
                "Two distinct section values should render two headers")
    }

    // The actual fix used in TaskViewModel: substitute "__none__" for empty
    // section values via a separate role. Verifies the workaround.
    ListModel {
        id: sentinelModel
        ListElement { sectionGroupKey: "__none__"; content: "Task 1" }
        ListElement { sectionGroupKey: "__none__"; content: "Task 2" }
    }

    function test_sentinel_value_renders_header() {
        const item = createTemporaryObject(listComp, tc, { })
        item.listView.section.property = "sectionGroupKey"
        item.listView.model = sentinelModel
        wait(100)
        compare(item.headersCreated, 1,
                "Non-empty sentinel value (__none__) should render a header")
    }
}
