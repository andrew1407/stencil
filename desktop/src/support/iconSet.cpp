#include "iconSet.hpp"

#include "LruCache.hpp"
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
    // Parsed once from the shared canon (browser/js/config/icons.json via app.qrc); the
    // desktop-only glyphs are appended literally.
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

    // QSvgRenderer can't resolve the browser's `currentColor`.
    QString svgDoc(const QString& inner, const QString& hex) {
      QString resolved = inner;
      resolved.replace("currentColor", hex);
      return QString(
                 R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" )"
                 R"(fill="none" stroke="%1" stroke-width="2" stroke-linecap="round" )"
                 R"(stroke-linejoin="round">%2</svg>)")
          .arg(hex, resolved);
    }
    // Bounded: an accent preview cycles hues, and every hue mints a fresh QIcon per glyph.
    LruCache<qint64, IconRequest>& requestIndex() {
      static LruCache<qint64, IconRequest> m(512);
      return m;
    }
  }  // namespace

  bool hasIcon(const QString& name) { return iconTable().contains(name); }

  QString iconMarkup(const QString& name) { return iconTable().value(name); }

  QString iconSvgDocument(const QString& inner, const QColor& color) {
    return svgDoc(inner, color.name());
  }

  bool iconRequestForKey(qint64 cacheKey, IconRequest* out) {
    const IconRequest* req = requestIndex().find(cacheKey);
    if (!req) return false;
    if (out) *out = *req;
    return true;
  }

  namespace {
    // The theme's --disabled-text, so icon and label grey out together as in the browser.
    QColor mutedInk() {
      const QColor c = QGuiApplication::palette().color(QPalette::Disabled, QPalette::WindowText);
      return c.isValid() ? c : QColor("#8a8f98");
    }
  }  // namespace

  QIcon iconFromMarkup(const QString& inner, const QColor& color, int size,
                       qreal dprIn, bool withDisabled, int gap) {
    if (inner.isEmpty() || size <= 0) return QIcon();
    gap = std::max(0, gap);
    const QString hex = color.name();
    const qreal dpr = dprIn > 0 ? dprIn : (qApp ? qApp->devicePixelRatio() : 1.0);
    QSvgRenderer renderer(svgDoc(inner, hex).toUtf8());
    // Rendered at the device pixel ratio for Retina; `gap` is transparent slack after the glyph.
    QPixmap pm(QSize(size + gap, size) * dpr);
    pm.fill(Qt::transparent);
    const QRectF glyphBox(0, 0, size * dpr, size * dpr);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, glyphBox);
    painter.end();
    pm.setDevicePixelRatio(dpr);

    QIcon icon(pm);
    if (!withDisabled) return icon;   // a posed frame on an enabled control never shows it
    // Disabled: RE-RENDERED in the muted ink at full strength, as the browser's currentColor
    // does; fading left a light theme's dark glyph a ghost on the pale chip.
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

  QIcon themedIcon(const QString& name, const QColor& color, int size,
                   qreal dprIn, int gap) {
    const QString inner = iconTable().value(name);
    if (inner.isEmpty()) return QIcon();

    const qreal dpr = dprIn > 0 ? dprIn : (qApp ? qApp->devicePixelRatio() : 1.0);
    // Bounded: the logo's picker cycles accents on hover, minting an icon set per hue.
    static LruCache<QString, QIcon> cache(512);
    const QString key = name + '|' + color.name() + '|' + QString::number(size)
                        + '@' + QString::number(dpr)
                        + (gap > 0 ? "|g" + QString::number(gap) : QString())
                        + '/' + mutedInk().name();   // …the disabled glyph's ink moves with the theme
    if (const QIcon* hit = cache.find(key)) return *hit;

    const QIcon icon = iconFromMarkup(inner, color, size, dpr, true, gap);
    cache.insert(key, icon);
    // A QIcon copy keeps its cacheKey (iconMotion.hpp's hover lookup).
    requestIndex().insert(icon.cacheKey(), IconRequest{name, color, size, dpr, gap});
    return icon;
  }

  QIcon rotatedIcon(const QString& name, const QColor& color, int size, qreal degrees,
                    qreal dprIn) {
    if (qFuzzyIsNull(degrees)) return themedIcon(name, color, size, dprIn);
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
