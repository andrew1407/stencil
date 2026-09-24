#include "theme.hpp"
#include "themeTokens.hpp"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStyleHints>
#include <algorithm>
#include <array>
#include <cmath>
#ifdef Q_OS_LINUX
#include <QProcess>
#endif

// A pre-main caller (tipContent.cpp's static initializer) can run before the resource's
// own global initializer, so force registration on first read.
static void ensureAppResources() { Q_INIT_RESOURCE(app); }

namespace stencil::gui {

  namespace {
    // Drawn into the cache dir and referenced by path — the only way to put a shape in a
    // QSS sub-control. Written at "@2x" too so it stays sharp on retina.
    QString caretImagePath(const QColor& colour) {
      const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
      QDir().mkpath(dir);
      const QString base = dir + "/caret-" + colour.name(QColor::HexRgb).mid(1);
      const QString path = base + ".png";
      if (!QFile::exists(path)) {
        for (const int scale : {1, 2}) {
          const int px = 9 * scale;
          QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
          img.fill(Qt::transparent);
          QPainter p(&img);
          p.setRenderHint(QPainter::Antialiasing);
          QPen pen(colour, 1.5 * scale);
          pen.setCapStyle(Qt::RoundCap);
          pen.setJoinStyle(Qt::RoundJoin);
          p.setPen(pen);
          const qreal m = 1.5 * scale, mid = px / 2.0;
          p.drawPolyline(QPolygonF({QPointF(m, mid - m / 1.5), QPointF(mid, px - m * 1.6),
                                    QPointF(px - m, mid - m / 1.5)}));
          p.end();
          img.save(scale == 1 ? path : base + "@2x.png", "PNG");
        }
      }
      return path;
    }

    // tests/pins/stylesheets.txt hashes the finished sheet, whitespace included.
    const QString& stylesheetTemplate() {
      static const QString tpl = [] {
        ensureAppResources();
        QFile f(QStringLiteral(":/qss/app.qss"));
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
      }();
      return tpl;
    }
  }  // namespace

  QString fillStylesheetTokens(const QString& tpl, const QHash<QString, QString>& values) {
    static const QRegularExpression token(QStringLiteral("%[A-Z0-9_]+%"));
    QString out;
    out.reserve(tpl.size() + 2048);
    int pos = 0;
    QRegularExpressionMatchIterator it = token.globalMatch(tpl);
    while (it.hasNext()) {
      const QRegularExpressionMatch m = it.next();
      const auto v = values.constFind(m.captured(0));
      if (v == values.constEnd()) continue;
      out.append(QStringView(tpl).mid(pos, m.capturedStart() - pos));
      out.append(*v);
      pos = m.capturedEnd();
    }
    out.append(QStringView(tpl).mid(pos));
    return out;
  }

  QString buildStylesheet(bool dark, const QString& accentKey) {
    const Palette p = themePalette(dark, accentKey);
    auto c = [](const QColor& q) { return q.name(); };
    // QSS has no color-mix: the browser's translucent accent hovers are alpha over the surface.
    auto rgba = [](const QColor& q, double a) {
      return QString("rgba(%1,%2,%3,%4)")
          .arg(q.red())
          .arg(q.green())
          .arg(q.blue())
          .arg(a, 0, 'f', 3);
    };
    const QColor accent2 = p.textKey;  // the derived hover/active accent shade
    const bool darkGlyph = accentNeedsDarkGlyph(p.accent);   // which indicator mark to bake in
    const QColor disabledBg = dark ? p.bgControls : p.bgContainer.darker(108);
    const QColor caretDim = mixSrgb(p.textMuted, disabledBg, 0.55);
    const QColor sbThumb = canvasScrollThumb(dark);
    // Browser --bg-drop-hint/--border-hint. NB: CSS color-mix(accent P%, base) == mixSrgb(base, accent, P).
    const QColor dropHintBg = dark ? mixSrgb(p.bgPage, p.accent, 0.16)
                                    : mixSrgb(QColor(Qt::white), p.accent, 0.09);
    // Browser --bg-keywords: the keywords well's NEUTRAL fill; the accent is its border.
    const QString keywordsBg = themeToken("--bg-keywords", dark).name();
    const QColor dropHintBorder = dark ? mixSrgb(QColor("#2a2a2a"), p.accent, 0.50)
                                        : mixSrgb(QColor(Qt::white), p.accent, 0.35);
    // Browser --border-tooltip.
    const QColor borderTooltip = themeToken("--border-tooltip", dark);
    // Browser --bg-info.
    const QColor bgInfo = infoBackground(dark);
    // Browser --success, lifted toward the light on dark like the reds are; --success-cta is
    // the deeper green the one filled GO button wears, so white ink reads on it.
    const QColor success = themeToken("--success", dark);
    const QColor successCta = themeToken("--success-cta", dark);

    // Tracks browser/css. The sheet is resources/app.qss; only its %TOKEN% values are
    // computed here and filled in ONE pass — previewAccent() rebuilds it per hovered accent row.
    return fillStylesheetTokens(stylesheetTemplate(), {
        {"%BTN_FLAT%", c(dark ? p.bgContainer.lighter(112) : p.bgContainer.darker(103))},
        // Geometry the code measures against (theme.hpp) — interpolated, never retyped.
        {"%MENU_PAD_R%", QString::number(MENU_ITEM_RIGHT_PAD_PX)},
        {"%SEP_W%", QString::number(DOCK_SEPARATOR_PX)},
        {"%FACE_PAD_X%", QString::number(FACE_PAD_X_PX)},
        // theme.hpp onAccentInk; the indicator marks are baked PNGs, so they come as a pair.
        {"%ON_ACCENT%", c(p.onAccent)},
        {"%TICK_IMG%", darkGlyph ? ":/icons/check-dark.png" : ":/icons/check.png"},
        {"%RADIO_IMG%", darkGlyph ? ":/icons/radio-dot-dark.png" : ":/icons/radio-dot.png"},
        {"%ACCENT_SOFT2%", rgba(p.accent, dark ? 0.30 : 0.20)},
        {"%ACCENT_SOFT%", rgba(p.accent, dark ? 0.18 : 0.11)},
        {"%ACCENT_RING%", rgba(p.accent, 0.45)},
        // A quarter of the control's own ink: light-grey on light, dark-grey on dark.
        {"%UI_OUTLINE%", rgba(p.textMain, 0.25)},
        {"%ACCENT2%", c(accent2)},
        // Browser --danger/--danger-2 (per theme).
        {"%SUCCESS_RING%", rgba(success, 0.55)},
        {"%SUCCESS_CTA%", c(successCta)},
        {"%SUCCESS_CTA2%", c(dark ? successCta.lighter(112) : successCta.darker(108))},
        {"%DANGER2%", dangerHover(dark).name()},
        {"%DANGER%", c(p.danger)},
        // Browser --bg-sel-panel / --border-sel / --text-sel-label. Routed through the
        // Palette: a raw sRGB literal reads over-saturated on a wide-gamut (P3) display.
        {"%SEL_BG%", c(p.bgSelPanel)},
        {"%SEL_BORDER%", c(p.borderSel)},
        {"%SEL_LABEL%", c(p.textSelLabel)},
        {"%SEL_BTN_TEXT%", c(p.textSelBtn)},
        {"%SEL_BTN_HOV%", c(p.bgSelBtnHov)},
        {"%SEL_BTN%", c(p.bgSelBtn)},
        // Browser layout.css .deselect-btn orange, display-space converted for the same reason.
        {"%DESELECT_BG%", c(p.bgSelBtn)},
        {"%DESELECT_HOVER%", c(p.bgSelBtnHov)},
        {"%DISABLED_BG%", c(disabledBg)},
        {"%BG_PAGE%", c(p.bgPage)},
        {"%BG_CONTAINER%", c(p.bgContainer)},
        {"%BG_CONTROLS%", c(p.bgControls)},
        {"%BG_COORD_HOVER%", c(p.bgCoordHover)},
        {"%BG_ROW_HOVER_SOFT%", c(mixSrgb(p.bgContainer, p.bgCoordHover, 0.5))},
        // Browser --bg-coord-even / --text-info (`inherit` on light = the main ink).
        {"%BG_COORD_EVEN%", c(themeToken("--bg-coord-even", dark))},
        {"%TEXT_INFO%", c(dark ? themeToken("--text-info", dark) : p.textMain)},
        {"%BG_INFO%", c(bgInfo)},
        {"%BG_DROP_HINT%", c(dropHintBg)},
        {"%BG_KEYWORDS%", keywordsBg},
        {"%BORDER_HINT%", c(dropHintBorder)},
        {"%BORDER_TOOLTIP%", c(borderTooltip)},
        {"%BORDER_CANVAS%", c(p.borderCanvas)},
        {"%BORDER%", c(p.borderMain)},
        {"%SB_THUMB%", c(sbThumb)},
        {"%TEXT%", c(p.textMain)},
        {"%CARET_DIM%", caretImagePath(caretDim)},
        {"%CARET%", caretImagePath(p.textMuted)},
        {"%MUTED%", c(p.textMuted)},
        {"%WARNING%", c(p.warning)},
        {"%SECTIONTITLE%", rgba(p.textMain, 0.62)},
        {"%DISABLED_TEXT%", c(p.disabledText)},
        {"%ACCENT%", c(p.accent)},
        {"%INPUT_BG%", c(p.inputBg)},
        {"%INPUT_TEXT%", c(p.inputText)}});
  }
}

