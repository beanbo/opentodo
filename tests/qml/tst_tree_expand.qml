import QtQuick
import QtTest

// Reproduces the "tree expansion doesn't work" bug from the sidebar.
// Tests two property-change-propagation patterns:
//   (a) `property var foo: ({})` reassigned with a new object → fooChanged fires
//   (b) Connections inside a Loader can observe a signal from the outer scope
TestCase {
    id: tc
    name: "TreeExpand"
    when: windowShown
    width: 200
    height: 200

    Component {
        id: comp
        Item {
            id: root
            width: 200; height: 200

            property var expandedSet: ({})
            signal expandedToggled()

            // Counters — the test reads these to verify signals fired.
            property int autoNotifyCount: 0   // expandedSetChanged (auto from `var`)
            property int explicitSignalCount: 0   // expandedToggled (explicit)

            onExpandedSetChanged: root.autoNotifyCount += 1

            // Same Loader-scope pattern that SidebarContent uses for the
            // project-tree Repeater.
            Loader {
                id: ld
                anchors.fill: parent
                active: true
                sourceComponent: Item {
                    Connections {
                        target: root
                        function onExpandedSetChanged() { root.autoNotifyCount += 0 /* observed via outer */ }
                        function onExpandedToggled()   { root.explicitSignalCount += 1 }
                    }
                }
            }

            function toggle(id) {
                const copy = Object.assign({}, root.expandedSet)
                if (copy[id]) delete copy[id]
                else          copy[id] = true
                root.expandedSet = copy
                root.expandedToggled()
            }
        }
    }

    function test_var_property_reassign_fires_changed_signal() {
        const item = createTemporaryObject(comp, tc, { })
        const before = item.autoNotifyCount
        item.toggle("alpha")
        wait(20)
        verify(item.autoNotifyCount > before,
               "expandedSetChanged signal should fire on object reassignment")
    }

    function test_explicit_signal_received_inside_loader() {
        const item = createTemporaryObject(comp, tc, { })
        const before = item.explicitSignalCount
        item.toggle("alpha")
        wait(20)
        compare(item.explicitSignalCount, before + 1,
                "Connections inside Loader should see explicit expandedToggled signal")
    }

    function test_consecutive_toggles_fire_signal_each_time() {
        const item = createTemporaryObject(comp, tc, { })
        item.toggle("a"); item.toggle("b"); item.toggle("c")
        wait(20)
        compare(item.explicitSignalCount, 3,
                "Each toggle should fire the explicit signal")
    }
}
