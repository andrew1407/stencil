#include "iconSet.hpp"

#include <algorithm>
#include <QApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QByteArray>
#include <QColor>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QSize>
#include <QSvgRenderer>

// See theme.cpp ensureAppResources(): a pre-main caller can beat the qrc's own
// global initializer, so force registration before the first table read.
static void ensureAppResources() { Q_INIT_RESOURCE(app); }

namespace stencil::gui {

  namespace {
    // name → inner SVG markup (0 0 24 24 viewBox), parsed once from the shared
    // canon (browser/js/config/icons.json, embedded via app.qrc — the same set
    // the browser and extension render). Shapes that read as a solid fill carry
    // `fill="currentColor" stroke="none"` (resolved by svgDoc() below). The few
    // desktop-only glyphs with no browser counterpart are appended literally.
    const QHash<QString, QString>& iconTable() {
      static const QHash<QString, QString> t = [] {
        ensureAppResources();
        QHash<QString, QString> m;
        QFile f(":/config/icons.json");
        if (f.open(QIODevice::ReadOnly)) {
          const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
          for (auto it = o.begin(); it != o.end(); ++it)
            m.insert(it.key(), it.value().toString());
        }
        // Desktop-only extras (the web app can't quit / has no native menu).
        m.insert("power",
                 R"(<path d="M18.36 6.64a9 9 0 1 1-12.73 0"/><line x1="12" y1="2" x2="12" y2="12"/>)");
        m.insert("search",
                 R"(<circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/>)");
        m.insert("more-vertical",
                 R"(<circle cx="12" cy="5" r="1.6" fill="currentColor" stroke="none"/><circle cx="12" cy="12" r="1.6" fill="currentColor" stroke="none"/><circle cx="12" cy="19" r="1.6" fill="currentColor" stroke="none"/>)");
        return m;
      }();
      return t;
    }

    // Wrap the table's inner markup in a full document with `color` baked in — QSvgRenderer
    // can't resolve the browser's `currentColor`.
    QString svgDoc(const QString& inner, const QString& hex) {
      QString resolved = inner;
      resolved.replace("currentColor", hex);
      return QString(
                 R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" )"
                 R"(fill="none" stroke="%1" stroke-width="2" stroke-linecap="round" )"
                 R"(stroke-linejoin="round">%2</svg>)")
          .arg(hex, resolved);
    }
    // cacheKey → what themedIcon was asked for. See iconRequestForKey().
    QHash<qint64, IconRequest>& requestIndex() {
      static QHash<qint64, IconRequest> m;
      return m;
    }
  }  // namespace

  bool hasIcon(const QString& name) { return iconTable().contains(name); }

  QString iconMarkup(const QString& name) { return iconTable().value(name); }

  QString iconSvgDocument(const QString& inner, const QColor& color) {
    return svgDoc(inner, color.name());
  }

  bool iconRequestForKey(qint64 cacheKey, IconRequest* out) {
    const auto it = requestIndex().constFind(cacheKey);
    if (it == requestIndex().constEnd()) return false;
    if (out) *out = it.value();
    return true;
  }

  // The ink a disabled glyph is drawn in: the theme's --disabled-text (buildQPalette puts
  // it in the DISABLED group), so icon and label grey out together — and to the same value
  // the browser uses. Qt's own default palette until a theme lands.
  static QColor mutedInk() {
    const QColor c = QGuiApplication::palette().color(QPalette::Disabled, QPalette::WindowText);
    return c.isValid() ? c : QColor("#8a8f98");
  }

  QIcon iconFromMarkup(const QString& inner, const QColor& color, int size, bool shadow,
                       qreal dprIn, bool withDisabled, int gap) {
    if (inner.isEmpty() || size <= 0) return QIcon();
    gap = std::max(0, gap);
    const QString hex = color.name();
    const qreal dpr = dprIn > 0 ? dprIn : (qApp ? qApp->devicePixelRatio() : 1.0);
    QSvgRenderer renderer(svgDoc(inner, hex).toUtf8());
    // Rendered at the device pixel ratio so the line-art stays crisp on Retina /
    // fractional-scale displays, then tagged with that ratio.
    // The glyph fills the left size×size square; `gap` is transparent slack after it.
    QPixmap pm(QSize(size + gap, size) * dpr);
    pm.fill(Qt::transparent);
    const QRectF glyphBox(0, 0, size * dpr, size * dpr);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (shadow) {
      // The Qt stand-in for the web's drop-shadow(): paint the same glyph in
      // near-black around the mark to build a tight halo, then the mark on top.
      // Inset by one device pixel so the halo has room instead of being clipped.
      QSvgRenderer dark(svgDoc(inner, QStringLiteral("#000000")).toUtf8());
      const qreal o = dpr;
      const QRectF box = glyphBox.adjusted(o, o, -o, -o);
      painter.setOpacity(0.34);   // browser --glyph-shadow: a tight, LIGHT ring, not a stroke
      for (const QPointF& d : {QPointF(-o, 0), QPointF(o, 0), QPointF(0, -o), QPointF(0, o),
                               QPointF(-o, -o), QPointF(o, -o), QPointF(-o, o), QPointF(o, o)})
        dark.render(&painter, box.translated(d));
      painter.setOpacity(1.0);
      renderer.render(&painter, box);
    } else {
      renderer.render(&painter, glyphBox);
    }
    painter.end();
    pm.setDevicePixelRatio(dpr);

    QIcon icon(pm);
    if (!withDisabled) return icon;   // a posed frame on an enabled control never shows it
    // Disabled: the glyph RE-RENDERED in the muted ink, at full strength — what the
    // stylesheet's `QToolButton:disabled { color: MUTED }` does for the label beside it,
    // and what the browser does (its .ic is currentColor, so a disabled button's icon is
    // --disabled-text at full opacity). Fading the enabled colour instead left a light
    // theme's dark glyph a ghost on the pale disabled chip (user report).
    QPixmap off(pm.size());
    off.fill(Qt::transparent);
    {
      QSvgRenderer dim(svgDoc(inner, mutedInk().name()).toUtf8());
      QPainter dp(&off);
      dp.setRenderHint(QPainter::Antialiasing, true);
      dim.render(&dp, glyphBox);
    }
    off.setDevicePixelRatio(dpr);
    icon.addPixmap(off, QIcon::Disabled);
    return icon;
  }

  QIcon themedIcon(const QString& name, const QColor& color, int size, bool shadow,
                   qreal dprIn, int gap) {
    const QString inner = iconTable().value(name);
    if (inner.isEmpty()) return QIcon();

    // Cache by (name, color, size, shadow): the same glyph is requested for many
    // actions on every theme change, so rasterizing once per key keeps it cheap.
    const qreal dpr = dprIn > 0 ? dprIn : (qApp ? qApp->devicePixelRatio() : 1.0);
    static QHash<QString, QIcon> cache;
    const QString key = name + '|' + color.name() + '|' + QString::number(size)
                        + (shadow ? "|s" : "") + '@' + QString::number(dpr)
                        + (gap > 0 ? "|g" + QString::number(gap) : QString())
                        + '/' + mutedInk().name();   // …the disabled glyph's ink moves with the theme
    const auto it = cache.constFind(key);
    if (it != cache.constEnd()) return it.value();

    const QIcon icon = iconFromMarkup(inner, color, size, shadow, dpr, true, gap);
    cache.insert(key, icon);
    // …and the way back: a QIcon copy keeps its cacheKey, so a button's icon can be
    // traced to the glyph it was made from (iconMotion.hpp's hover lookup).
    requestIndex().insert(icon.cacheKey(), IconRequest{name, color, size, shadow, dpr, gap});
    return icon;
  }

  QIcon rotatedIcon(const QString& name, const QColor& color, int size, qreal degrees,
                    qreal dprIn) {
    if (qFuzzyIsNull(degrees)) return themedIcon(name, color, size, false, dprIn);
    const QString inner = iconTable().value(name);
    if (inner.isEmpty()) return QIcon();

    const qreal dpr = dprIn > 0 ? dprIn : (qApp ? qApp->devicePixelRatio() : 1.0);
    QSvgRenderer renderer(svgDoc(inner, color.name()).toUtf8());
    QPixmap pm(QSize(size, size) * dpr);
    pm.fill(Qt::transparent);
    {
      QPainter painter(&pm);
      painter.setRenderHint(QPainter::Antialiasing, true);
      const QPointF centre(pm.width() / 2.0, pm.height() / 2.0);
      painter.translate(centre);
      painter.rotate(degrees);
      painter.translate(-centre);
      renderer.render(&painter);
    }
    pm.setDevicePixelRatio(dpr);
    return QIcon(pm);
  }

}  // namespace stencil::gui
