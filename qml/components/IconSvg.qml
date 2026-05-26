import QtQuick
import OpenTodoUI

// SVG icon tinter — renders an icon from resources/icons/<name>.svg in the
// requested colour via the `image://svg-tinted/...` provider registered in
// src/main.cpp (backed by src/services/tintedsvgimageprovider.{h,cpp}).
//
// Usage:
//   IconSvg {
//       name: "chevron-right"     // file name without .svg
//       size: 16                  // square; pinned via sourceSize for crisp render
//       color: ThemeManager.textColor
//   }
//
// Why a C++ image provider instead of QtQuick.Effects.MultiEffect:
//   The icon pack ships as stroke SVGs with `stroke="currentColor"`. Qt's
//   QSvgRenderer has no CSS context, so it resolves `currentColor` to BLACK
//   and the rasterised image is a black silhouette on transparent. The
//   MultiEffect "colorization" effect preserves source luminance and only
//   re-maps hue/saturation — black (luminance 0) cannot be re-tinted that
//   way, so every IconSvg in the app was rendering as a flat black icon
//   regardless of the `color` property. (We chased two layer.enabled /
//   layer.effect variants here before isolating the root cause.)
//
//   Replacing `currentColor` in the SVG XML before rasterisation makes Qt
//   render the strokes in the desired colour directly. The provider also
//   sidesteps the data:image/svg+xml URL approach, which has length and
//   parsing quirks across Qt platforms.
//
// Provider URL format (see TintedSvgImageProvider::requestImage):
//   image://svg-tinted/<name>?color=<rrggbb>
// `<name>` may include a subdirectory (e.g. "projects/briefcase") to mirror
// the layout under resources/icons/; bare names default to the `ui/` pack.
Item {
    id: root

    property string name: ""
    property int    size: 16
    property color  color: ThemeManager.textColor

    implicitWidth:  size
    implicitHeight: size

    // QML color toString() yields "#rrggbb" (or "#aarrggbb" for non-opaque
    // colours). The provider's query parser expects bare 6-hex, so we strip
    // the leading '#' and any 2-digit alpha prefix.
    readonly property string _colorHex: {
        var s = color.toString()
        if (s.length === 0) return "000000"
        if (s.charAt(0) === "#") s = s.substring(1)
        if (s.length === 8) s = s.substring(2)   // drop "aa" alpha prefix
        return s
    }

    Image {
        anchors.fill: parent
        source: root.name === "" ? "" :
                "image://svg-tinted/" + root.name + "?color=" + root._colorHex
        sourceSize.width:  root.size
        sourceSize.height: root.size
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        // Cache misses on first paint are unavoidable, but Qt's pipeline keeps
        // identical (name, color, size) tuples in the QQuickPixmapCache for
        // free — subsequent uses across the app reuse the same QImage.
        cache: true
        asynchronous: false
    }
}
