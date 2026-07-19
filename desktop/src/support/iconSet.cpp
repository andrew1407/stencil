#include "iconSet.hpp"
#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QSize>
#include <QSvgRenderer>

namespace stencil::gui {

  namespace {
    // name → inner SVG markup, a verbatim port of ICONS in browser/js/ui/icons.js.
    // Drawn in a 0 0 24 24 viewBox. Keep in sync with the browser/extension sets
    // when adding a glyph. Shapes that read as a solid fill use
    // `fill="currentColor" stroke="none"` (filled here by themedIcon()).
    const QHash<QString, QString>& iconTable() {
      static const QHash<QString, QString> t = {
          // chevrons / carets
          {"chevron-up", R"(<polyline points="18 15 12 9 6 15"/>)"},
          {"chevron-down", R"(<polyline points="6 9 12 15 18 9"/>)"},
          {"chevron-right", R"(<polyline points="9 18 15 12 9 6"/>)"},
          {"chevron-left", R"(<polyline points="15 18 9 12 15 6"/>)"},
          // edit / confirm / cancel
          {"pencil",
           R"(<path d="M12 20h9"/><path d="M16.5 3.5a2.121 2.121 0 0 1 3 3L7 19l-4 1 1-4 12.5-12.5z"/>)"},
          {"check", R"(<polyline points="20 6 9 17 4 12"/>)"},
          {"x",
           R"(<line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/>)"},
          // image transforms
          {"crop",
           R"(<path d="M6.13 1L6 16a2 2 0 0 0 2 2h15"/><path d="M1 6.13L16 6a2 2 0 0 1 2 2v15"/>)"},
          {"rotate-ccw",
           R"(<polyline points="1 4 1 10 7 10"/><path d="M3.51 15a9 9 0 1 0 2.13-9.36L1 10"/>)"},
          {"rotate-cw",
           R"(<polyline points="23 4 23 10 17 10"/><path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10"/>)"},
          // drawing
          {"play",
           R"(<polygon points="6 4 20 12 6 20 6 4" fill="currentColor" stroke="none"/>)"},
          {"stop",
           R"(<rect x="5" y="5" width="14" height="14" rx="2" fill="currentColor" stroke="none"/>)"},
          {"rect", R"(<rect x="3" y="5" width="18" height="14" rx="1.5"/>)"},
          {"rect-filled",
           R"(<rect x="4" y="6" width="16" height="12" rx="1.5" fill="currentColor" stroke="none"/>)"},
          {"undo",
           R"(<path d="M9 14L4 9l5-5"/><path d="M4 9h11a5 5 0 0 1 0 10h-4"/>)"},
          {"redo",
           R"(<path d="M15 14l5-5-5-5"/><path d="M20 9H9a5 5 0 0 0 0 10h4"/>)"},
          // data / files
          {"trash",
           R"(<polyline points="3 6 5 6 21 6"/><path d="M19 6l-1 14a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2L5 6"/><path d="M10 6V4a2 2 0 0 1 2-2h0a2 2 0 0 1 2 2v2"/><line x1="10" y1="11" x2="10" y2="17"/><line x1="14" y1="11" x2="14" y2="17"/>)"},
          {"download",
           R"(<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/>)"},
          {"upload",
           R"(<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="17 8 12 3 7 8"/><line x1="12" y1="3" x2="12" y2="15"/>)"},
          {"copy",
           R"(<rect x="9" y="9" width="13" height="13" rx="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/>)"},
          {"clipboard",
           R"(<path d="M16 4h2a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2h2"/><rect x="8" y="2" width="8" height="4" rx="1"/>)"},
          {"paste",
           R"(<path d="M16 4h2a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2h2"/><rect x="8" y="2" width="8" height="4" rx="1"/><polyline points="9 13 12 16 15 13"/><line x1="12" y1="10" x2="12" y2="16"/>)"},
          {"save",
           R"(<path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z"/><polyline points="17 21 17 13 7 13 7 21"/><polyline points="7 3 7 8 15 8"/>)"},
          {"file-text",
           R"(<path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/><line x1="16" y1="13" x2="8" y2="13"/><line x1="16" y1="17" x2="8" y2="17"/>)"},
          {"folder",
           R"(<path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"/>)"},
          {"layers",
           R"(<polygon points="12 2 2 7 12 12 22 7 12 2"/><polyline points="2 17 12 22 22 17"/><polyline points="2 12 12 17 22 12"/>)"},
          {"more",
           R"(<circle cx="5" cy="12" r="1.6" fill="currentColor" stroke="none"/><circle cx="12" cy="12" r="1.6" fill="currentColor" stroke="none"/><circle cx="19" cy="12" r="1.6" fill="currentColor" stroke="none"/>)"},
          // app / view
          {"moon",
           R"(<path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"/>)"},
          {"sun",
           R"(<circle cx="12" cy="12" r="5"/><line x1="12" y1="1" x2="12" y2="3"/><line x1="12" y1="21" x2="12" y2="23"/><line x1="4.22" y1="4.22" x2="5.64" y2="5.64"/><line x1="18.36" y1="18.36" x2="19.78" y2="19.78"/><line x1="1" y1="12" x2="3" y2="12"/><line x1="21" y1="12" x2="23" y2="12"/><line x1="4.22" y1="19.78" x2="5.64" y2="18.36"/><line x1="18.36" y1="5.64" x2="19.78" y2="4.22"/>)"},
          {"maximize",
           R"(<path d="M8 3H5a2 2 0 0 0-2 2v3m18 0V5a2 2 0 0 0-2-2h-3m0 18h3a2 2 0 0 0 2-2v-3M3 16v3a2 2 0 0 0 2 2h3"/>)"},
          {"fit",
           R"(<rect x="3" y="4" width="18" height="16" rx="2"/><rect x="8" y="9" width="8" height="6" rx="1"/>)"},
          {"link",
           R"(<path d="M10 13a5 5 0 0 0 7.54.54l3-3a5 5 0 0 0-7.07-7.07l-1.72 1.71"/><path d="M14 11a5 5 0 0 0-7.54-.54l-3 3a5 5 0 0 0 7.07 7.07l1.71-1.71"/>)"},
          {"incognito",
           R"(<path d="M2 12h20"/><path d="M5 12l1.6-5.3A2 2 0 0 1 8.5 5.3h7a2 2 0 0 1 1.9 1.4L19 12"/><circle cx="6.5" cy="15.5" r="2.8"/><circle cx="17.5" cy="15.5" r="2.8"/><path d="M9.3 15a2.8 2.8 0 0 1 5.4 0"/>)"},
          {"lock",
           R"(<rect x="3" y="11" width="18" height="11" rx="2" ry="2"/><path d="M7 11V7a5 5 0 0 1 10 0v4"/>)"},
          {"gear",
           R"(<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/>)"},
          {"palette",
           R"(<path d="M12 2C6.5 2 2 6.5 2 12s4.5 10 10 10c1.4 0 2.5-1.1 2.5-2.5 0-.6-.2-1.1-.6-1.5-.4-.4-.6-.9-.6-1.5 0-1.1.9-2 2-2H17c2.8 0 5-2.2 5-5 0-4.7-4.5-8.5-10-8.5z"/><circle cx="6.5" cy="11.5" r="1.4" fill="currentColor" stroke="none"/><circle cx="9.5" cy="7.5" r="1.4" fill="currentColor" stroke="none"/><circle cx="14.5" cy="7.5" r="1.4" fill="currentColor" stroke="none"/><circle cx="17.5" cy="11.5" r="1.4" fill="currentColor" stroke="none"/>)"},
          {"help",
           R"(<circle cx="12" cy="12" r="10"/><path d="M9.09 9a3 3 0 0 1 5.83 1c0 2-3 3-3 3"/><line x1="12" y1="17" x2="12.01" y2="17"/>)"},
          {"info",
           R"(<circle cx="12" cy="12" r="10"/><line x1="12" y1="16" x2="12" y2="12"/><line x1="12" y1="8" x2="12.01" y2="8"/>)"},
          {"refresh",
           R"(<polyline points="23 4 23 10 17 10"/><polyline points="1 20 1 14 7 14"/><path d="M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15"/>)"},
          {"calendar",
           R"(<rect x="3" y="4" width="18" height="18" rx="2" ry="2"/><line x1="16" y1="2" x2="16" y2="6"/><line x1="8" y1="2" x2="8" y2="6"/><line x1="3" y1="10" x2="21" y2="10"/>)"},
          {"image",
           R"(<rect x="3" y="3" width="18" height="18" rx="2"/><circle cx="8.5" cy="8.5" r="1.5"/><polyline points="21 15 16 10 5 21"/>)"},
          {"function",
           R"(<path d="M16 5a3 3 0 0 0-3 3v8a3 3 0 0 1-3 3"/><line x1="8" y1="11" x2="15" y2="11"/>)"},
          {"message",
           R"(<path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"/>)"},
          {"eye",
           R"(<path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/><circle cx="12" cy="12" r="3"/>)"},
          {"external",
           R"(<path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"/><polyline points="15 3 21 3 21 9"/><line x1="10" y1="14" x2="21" y2="3"/>)"},
          {"share",
           R"(<circle cx="18" cy="5" r="3"/><circle cx="6" cy="12" r="3"/><circle cx="18" cy="19" r="3"/><line x1="8.59" y1="13.51" x2="15.42" y2="17.49"/><line x1="15.41" y1="6.51" x2="8.59" y2="10.49"/>)"},
          {"monitor",
           R"(<rect x="2" y="3" width="20" height="14" rx="2"/><line x1="8" y1="21" x2="16" y2="21"/><line x1="12" y1="17" x2="12" y2="21"/>)"},
          {"server",
           R"(<rect x="2" y="2" width="20" height="8" rx="2"/><rect x="2" y="14" width="20" height="8" rx="2"/><line x1="6" y1="6" x2="6.01" y2="6"/><line x1="6" y1="18" x2="6.01" y2="18"/>)"},
          {"cloud",
           R"(<path d="M18 10h-1.26A8 8 0 1 0 9 20h9a5 5 0 0 0 0-10z"/>)"},
          {"lightbulb",
           R"(<line x1="9" y1="18" x2="15" y2="18"/><line x1="10" y1="22" x2="14" y2="22"/><path d="M12 2a7 7 0 0 0-4 12.7c.6.5 1 1.3 1 2.3h6c0-1 .4-1.8 1-2.3A7 7 0 0 0 12 2z"/>)"},
          {"alert",
           R"(<path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/>)"},
          {"flag",
           R"(<path d="M4 15s1-1 4-1 5 2 8 2 4-1 4-1V3s-1 1-4 1-5-2-8-2-4 1-4 1z"/><line x1="4" y1="22" x2="4" y2="15"/>)"},
          {"ruler",
           R"(<path d="M3 8l5-5 13 13-5 5z"/><line x1="7" y1="7" x2="9" y2="9"/><line x1="10" y1="4" x2="12" y2="6"/><line x1="4" y1="11" x2="6" y2="13"/>)"},
          // zoom
          {"plus",
           R"(<line x1="12" y1="5" x2="12" y2="19"/><line x1="5" y1="12" x2="19" y2="12"/>)"},
          {"minus", R"(<line x1="5" y1="12" x2="19" y2="12"/>)"},
          {"plus-circle",
           R"(<circle cx="12" cy="12" r="10"/><line x1="12" y1="8" x2="12" y2="16"/><line x1="8" y1="12" x2="16" y2="12"/>)"},
          {"swap",
           R"(<polyline points="17 1 21 5 17 9"/><path d="M3 11V9a4 4 0 0 1 4-4h14"/><polyline points="7 23 3 19 7 15"/><path d="M21 13v2a4 4 0 0 1-4 4H3"/>)"},
          // Desktop-only extras (no browser counterpart — the web app can't quit /
          // has no native menu). Keep these AFTER the mirrored set above.
          {"power",
           R"(<path d="M18.36 6.64a9 9 0 1 1-12.73 0"/><line x1="12" y1="2" x2="12" y2="12"/>)"},
          {"search",
           R"(<circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/>)"},
          {"more-vertical",
           R"(<circle cx="12" cy="5" r="1.6" fill="currentColor" stroke="none"/><circle cx="12" cy="12" r="1.6" fill="currentColor" stroke="none"/><circle cx="12" cy="19" r="1.6" fill="currentColor" stroke="none"/>)"},
      };
      return t;
    }
  }  // namespace

  bool hasIcon(const QString& name) { return iconTable().contains(name); }

  QIcon themedIcon(const QString& name, const QColor& color, int size) {
    const QString inner = iconTable().value(name);
    if (inner.isEmpty()) return QIcon();

    const QString hex = color.name();
    // Cache by (name, color, size): the same glyph is requested for many actions
    // on every theme change, so rasterizing once per key keeps it cheap.
    static QHash<QString, QIcon> cache;
    const QString key = name + '|' + hex + '|' + QString::number(size);
    const auto it = cache.constFind(key);
    if (it != cache.constEnd()) return it.value();

    // Bake the color in: QSvgRenderer can't resolve `currentColor`, so set the stroke
    // explicitly and swap any inner fill="currentColor" for the same hex.
    QString resolved = inner;
    resolved.replace("currentColor", hex);
    const QString svg =
        QString(
            R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" )"
            R"(fill="none" stroke="%1" stroke-width="2" stroke-linecap="round" )"
            R"(stroke-linejoin="round">%2</svg>)")
            .arg(hex, resolved);

    QSvgRenderer renderer(svg.toUtf8());
    // Render at the device pixel ratio so the line-art stays crisp on Retina /
    // fractional-scale displays, then tag the pixmap with that ratio.
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pm(QSize(size, size) * dpr);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter);
    painter.end();
    pm.setDevicePixelRatio(dpr);

    QIcon icon(pm);
    cache.insert(key, icon);
    return icon;
  }

}  // namespace stencil::gui
