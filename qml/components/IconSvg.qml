import QtQuick
import QtQuick.Effects
import OpenTodoUI

// Task 17cea65b: thin wrapper around Image + MultiEffect that tints an SVG
// from resources/icons/ui/<name>.svg by ThemeManager colour. The whole point
// of the stroke-style set is that `currentColor` becomes whatever colour we
// hand it via MultiEffect.colorization, so callers say `name + color` and
// don't have to remember the qrc path or the layer plumbing.
//
// Usage:
//   IconSvg {
//       name: "chevron-right"     // file name without .svg
//       size: 16                  // square; pass to sourceSize for crisp render
//       color: ThemeManager.textColor
//   }
//
// Notes:
//  - sourceSize is pinned to the rendered size so the SVG rasteriser doesn't
//    waste pixels on the original 24×24 viewBox when we draw at 12 px.
//  - The underlying Image is hidden behind the MultiEffect's source layer; we
//    only render the colorised output. Without the layer.effect wiring the
//    pure-currentColor SVGs paint black on light themes / white on dark only
//    if the file explicitly declares stroke="…". Keeping the recolour at the
//    QML side lets one SVG serve every theme.
Item {
    id: root

    property string name: ""
    property int    size: 16
    property color  color: ThemeManager.textColor

    implicitWidth:  size
    implicitHeight: size

    // Q4.01 (redesign): name resolution understands two shapes —
    //   "calendar"          → resources/icons/ui/calendar.svg     (legacy)
    //   "projects/briefcase"→ resources/icons/projects/briefcase.svg
    // The latter lets the redesign's project-category pack live under
    // resources/icons/projects/ without forcing callers to deal with a
    // separate component or qrc prefix.
    Image {
        id: iconImage
        anchors.fill: parent
        source: {
            if (root.name === "")
                return ""
            if (root.name.indexOf("/") >= 0)
                return "qrc:/icons/resources/icons/" + root.name + ".svg"
            return "qrc:/icons/resources/icons/ui/" + root.name + ".svg"
        }
        sourceSize.width:  root.size
        sourceSize.height: root.size
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        // Hide the un-tinted version — the MultiEffect renders the colorised
        // pixels on top. Without this the original stroke colour would peek
        // through when the colour matches transparency.
        visible: false
    }

    MultiEffect {
        anchors.fill: iconImage
        source: iconImage
        // 1.0 = full replacement; the source's alpha channel still controls
        // the silhouette, so anti-aliased edges remain smooth.
        colorization: 1.0
        colorizationColor: root.color
    }
}
