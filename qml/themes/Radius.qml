pragma Singleton
import QtQuick

QtObject {
    // F-8.42: extended scale — xs was previously undefined (silent 0-radius bug B-7.1)
    readonly property int none: 0
    readonly property int xs:   2
    readonly property int sm:   4
    readonly property int md:   6
    readonly property int lg:   8
    readonly property int xl:   12
    readonly property int xxl:  16
    readonly property int pill: 11
    // Effectively infinite — use for circular/pill shapes
    readonly property int round: 999
}
