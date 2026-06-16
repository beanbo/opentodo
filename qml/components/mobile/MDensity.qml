import QtQuick
import OpenTodoUI

// MDensity — mobile list density tokens (ported from m-tokens.jsx M_DENSITY).
// Two modes: "spacious" (default, large touch targets) and "balanced"
// (compact). Driven by the existing settingsViewModel.compactDensity flag so
// the desktop "compact density" toggle and the mobile density segment share
// one source of truth. Instantiated once in MobileLayout and passed down to
// child components as `dens` (mirrors the design's `d` prop-drilling).
//
// font.pixelSize is an int in Qt; the design's half-pixel sizes (16.5, 12.5)
// are rounded to the nearest practical integer.
QtObject {
    id: root

    readonly property bool spacious:
        !(typeof settingsViewModel !== "undefined" && settingsViewModel
          && settingsViewModel.compactDensity)

    readonly property int  rowPadV:    spacious ? 15  : 11
    readonly property int  rowPadH:    spacious ? 18  : 16
    readonly property int  rowGap:     spacious ? 14  : 12
    readonly property int  rowMinH:    spacious ? 66  : 54
    readonly property int  check:      spacious ? 24  : 22
    readonly property int  checkInner: spacious ? 13  : 12
    readonly property int  title:      spacious ? 16  : 15
    readonly property real titleLh:    spacious ? 1.32 : 1.30
    readonly property int  meta:       spacious ? 13  : 12
    readonly property int  chipH:      spacious ? 23  : 21
    readonly property int  sectionH:   spacious ? 46  : 40
    readonly property int  sectionFs:  spacious ? 13  : 12
    readonly property int  listTopGap: spacious ? 4   : 2
}
