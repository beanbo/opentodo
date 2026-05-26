import QtQuick
import QtQuick.Window
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

    // Rasterise at PHYSICAL pixel resolution, then let Qt downscale to the
    // logical display size. Qt 6's Image.sourceSize is interpreted in physical
    // pixels by the rasteriser, while the Image's width/height are in logical
    // pixels — so on a 1.5× / 2× display, a sourceSize equal to `size` paints
    // at logical resolution and Qt upscales the result, producing visible blur
    // on stroke-style icons.  Multiplying by Screen.devicePixelRatio aligns
    // rasterised pixels with the screen 1:1.  A 2× floor handles the common
    // 1× → 1.5× transition (a 1.5× rasteriser is sharper than 1×, but a 2×
    // rasteriser downscaled is sharper still and costs nothing for 12-24 px
    // payloads).  Math.ceil so non-integer DPRs round up rather than truncate
    // to the lower res.
    readonly property int _renderDpr:
        Math.max(2, Math.ceil(Screen.devicePixelRatio))

    // CRITICAL: Image is pinned to exactly `size × size` and centred in the
    // root Item — NOT `anchors.fill: parent`.  When IconSvg is used as a
    // Control's contentItem (ToolButton, AbstractButton, …) the control
    // unconditionally rewrites the contentItem's width/height to fill the
    // available content area, overriding the Item's implicitWidth/Height.
    // `anchors.fill: parent` then makes the Image 28×28 (or whatever box the
    // button gives us), which stretches a stroke-style SVG painted at 14×14
    // raster pixels across a 28×28 logical area — producing exactly the
    // "blurry, oversized paperclip / plus / more / check" the user sees.
    //
    // Anchoring the Image to the centre with explicit width/height equal to
    // `size` decouples the visual icon size from whatever geometry the
    // surrounding control imposes.  Result: the icon is always exactly
    // `size × size` regardless of parent (Control contentItem, RowLayout
    // cell, plain Item, etc.) and the high-DPI rasterisation chain stays
    // pixel-aligned.
    Image {
        anchors.centerIn: parent
        width:  root.size
        height: root.size
        source: root.name === "" ? "" :
                "image://svg-tinted/" + root.name + "?color=" + root._colorHex
        sourceSize.width:  root.size * root._renderDpr
        sourceSize.height: root.size * root._renderDpr
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
