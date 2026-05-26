#pragma once

// TintedSvgImageProvider — QML image provider that renders an SVG from
// :/icons/resources/icons/<name>.svg, substitutes `currentColor` for a
// caller-supplied colour, and returns a fully-rendered QImage.
//
// Why we don't just use QtQuick.Effects.MultiEffect for this:
//   Qt's QSvgRenderer has no CSS context, so `stroke="currentColor"` is
//   resolved to BLACK during rasterisation. MultiEffect.colorization is a
//   luminance-preserving HSL tint, so it can never recolour a luminance-0
//   source — every IconSvg in the app was rendering as a flat black
//   silhouette regardless of the `color` property passed in.
//
//   Replacing `currentColor` in the SVG XML before handing it to
//   QSvgRenderer makes the renderer paint the strokes in the desired
//   colour directly. No second-pass effect, no luminance dependency, no
//   layer-graph quirks inside Control.contentItem.
//
// QML usage:
//   Image {
//       source: "image://svg-tinted/inbox?color=aaaaaa"
//       sourceSize: Qt.size(16, 16)
//   }
//
// The id format is "<name>?color=<rrggbb>" where:
//   * <name> may include a subdirectory (e.g. "projects/briefcase") to
//     mirror the layout under resources/icons/.
//   * <rrggbb> is a 6-hex-digit RGB string (no leading #). If absent or
//     malformed we fall back to black, which matches Qt's default
//     currentColor resolution.

#include <QQuickImageProvider>
#include <QImage>
#include <QString>

class TintedSvgImageProvider final : public QQuickImageProvider {
public:
    TintedSvgImageProvider();

    QImage requestImage(const QString &id,
                        QSize *size,
                        const QSize &requestedSize) override;
};
