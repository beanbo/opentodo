#include "tintedsvgimageprovider.h"

#include <QFile>
#include <QPainter>
#include <QRegularExpression>
#include <QSvgRenderer>
#include <QUrlQuery>

TintedSvgImageProvider::TintedSvgImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage TintedSvgImageProvider::requestImage(const QString &id,
                                            QSize *size,
                                            const QSize &requestedSize)
{
    // ── Parse "<name>?color=<rrggbb>" ────────────────────────────────────────
    // Using manual split rather than QUrlQuery so we accept ids with embedded
    // slashes verbatim (e.g. "projects/briefcase?color=aaaaaa"). The Qt URL
    // machinery treats the question mark consistently across platforms even
    // when the path component contains slashes, so a simple split is robust.
    const int q = id.indexOf(QLatin1Char('?'));
    const QString name  = (q < 0) ? id : id.left(q);
    const QString query = (q < 0) ? QString() : id.mid(q + 1);

    QString colorHex = QStringLiteral("000000");
    if (!query.isEmpty()) {
        const auto parts = query.split(QLatin1Char('&'), Qt::SkipEmptyParts);
        for (const QString &kv : parts) {
            const int eq = kv.indexOf(QLatin1Char('='));
            if (eq < 0) continue;
            if (kv.left(eq) == QLatin1String("color")) {
                colorHex = kv.mid(eq + 1).toLower();
                break;
            }
        }
    }

    // Validate the hex — fall back to black on garbage rather than rendering
    // an invalid SVG that the renderer rejects wholesale.
    static const QRegularExpression hexRe(QStringLiteral("^[0-9a-f]{6}$"));
    if (!hexRe.match(colorHex).hasMatch())
        colorHex = QStringLiteral("000000");

    // ── Resolve the SVG file under :/icons/resources/icons/ ──────────────────
    // The qrc layout established by the project's resource files (see
    // resources/icons/icons.qrc) places UI icons in `ui/<name>.svg` and the
    // redesign's project category pack in `projects/<name>.svg`. A bare name
    // gets the `ui/` prefix automatically; a name containing `/` is taken
    // verbatim so callers can target either pack.
    QString path = QStringLiteral(":/icons/resources/icons/");
    if (name.contains(QLatin1Char('/')))
        path += name;
    else
        path += QStringLiteral("ui/") + name;
    path += QStringLiteral(".svg");

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("TintedSvgImageProvider: cannot open %s for id=%s",
                 qUtf8Printable(path), qUtf8Printable(id));
        return {};
    }

    QByteArray svg = f.readAll();
    f.close();

    // Substitute the currentColor placeholder. Done as a byte-level replace
    // because QByteArray::replace is significantly cheaper than going through
    // QString for ~600-byte payloads, and we hit this path 150+ times on the
    // first paint when every IconSvg in the app loads its texture in parallel.
    const QByteArray needle = QByteArrayLiteral("currentColor");
    const QByteArray repl   = QByteArrayLiteral("#") + colorHex.toUtf8();
    svg.replace(needle, repl);

    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) {
        qWarning("TintedSvgImageProvider: invalid SVG payload for id=%s",
                 qUtf8Printable(id));
        return {};
    }

    // ── Decide the rasterisation size ────────────────────────────────────────
    // QML callers set Image.sourceSize to control rasteriser resolution. Qt
    // passes that through `requestedSize`; if either dimension is unset we
    // fall back to the SVG's natural viewBox, scaled to fit a 16 px square so
    // that lazy callers still get a sensible default.
    QSize target = requestedSize;
    if (target.width() <= 0 || target.height() <= 0) {
        const QSize natural = renderer.defaultSize();
        target = natural.isValid() && !natural.isEmpty() ? natural : QSize(16, 16);
    }

    QImage out(target, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&painter);
    painter.end();

    if (size)
        *size = target;
    return out;
}
