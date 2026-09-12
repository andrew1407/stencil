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

// The shared-config canon rides in app.qrc. A pre-main caller (e.g. tipContent.cpp's
// static initializer) can reach the tables before the resource's own global
// initializer ran, so force registration on first read. Global scope for Q_INIT_RESOURCE.
static void ensureAppResources() { Q_INIT_RESOURCE(app); }

namespace stencil::gui {

  namespace {
    // The combo-box caret, drawn once per colour into the app's cache dir and referenced
    // from the stylesheet by path — the only way to put a shape in a QSS sub-control.
    // Written at 2x as well ("@2x", Qt's own high-DPI naming) so it stays sharp on retina.
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

    // The stylesheet template, read once from qrc (resources/app.qss).
    // tests/pins/stylesheets.txt hashes the finished sheet, whitespace included.
    const QString& stylesheetTemplate() {
      static const QString tpl = [] {
        ensureAppResources();
        QFile f(QStringLiteral(":/qss/app.qss"));
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
      }();
      return tpl;
    }

    // Fill every %TOKEN% in one walk. A token the map doesn't know is left alone (the
    // only other `%` in the sheet sits inside a comment, and reads as no token at all).
    QString fillTokens(const QString& tpl, const QHash<QString, QString>& values) {
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
  }

  QString buildStylesheet(bool dark, const QString& accentKey) {
    const Palette p = themePalette(dark, accentKey);
    auto c = [](const QColor& q) { return q.name(); };
    // rgba() string for accent tints — QSS has no color-mix, so the browser's
    // translucent accent hovers/focus rings are reproduced with alpha over the
    // surface beneath. Lets one accent recolour every hover state harmoniously.
    auto rgba = [](const QColor& q, double a) {
      return QString("rgba(%1,%2,%3,%4)")
          .arg(q.red())
          .arg(q.green())
          .arg(q.blue())
          .arg(a, 0, 'f', 3);
    };
    const QColor accent2 = p.textKey;  // the derived hover/active accent shade
    const bool darkGlyph = accentNeedsDarkGlyph(p.accent);   // which indicator mark to bake in
    // The dead-control surface (%DISABLED_BG%), and the caret drawn for one: muted pulled
    // halfway to that surface, so a disabled combo's arrow recedes with its text.
    const QColor disabledBg = dark ? p.bgControls : p.bgContainer.darker(108);
    const QColor caretDim = mixSrgb(p.textMuted, disabledBg, 0.55);
    // The scrollbars' unthemed fallback thumb (the pills read the same helper).
    const QColor sbThumb = canvasScrollThumb(dark);
    // Drag & drop hint strip (browser --bg-drop-hint/--border-hint, css/theme.css).
    // NB argument order: CSS color-mix(accent P%, base) == mixSrgb(base, accent, P).
    const QColor dropHintBg = dark ? mixSrgb(p.bgPage, p.accent, 0.16)
                                    : mixSrgb(QColor(Qt::white), p.accent, 0.09);
    const QColor dropHintBorder = dark ? mixSrgb(QColor("#2a2a2a"), p.accent, 0.50)
                                        : mixSrgb(QColor(Qt::white), p.accent, 0.35);
    // The coord-status readout's own border (browser --border-tooltip, css/theme.css) —
    // a distinct, slightly blue-grey hairline neither borderMain nor borderCanvas carries.
    const QColor borderTooltip = themeToken("--border-tooltip", dark);
    // --bg-info (browser theme.css): the neutral info fill — the projects rows' hover
    // wash and the inline-rename editor's chip/input surface.
    const QColor bgInfo = infoBackground(dark);
    // --success (browser theme.css), lifted toward the light on dark like the reds are.
    // The "today" outline in the expiration calendar is the only thing wearing it so far.
    const QColor success = themeToken("--success", dark);

    // One stylesheet covering the widgets the app uses. Tracks browser/css —
    // page backdrop, gradient controls, the brand accent, rounded inputs/lists,
    // accent-tinted hovers + focus rings — so the desktop matches the web look.
    // The sheet itself is resources/app.qss (embedded via app.qrc); only its %TOKEN%
    // values are computed here, and filled in ONE pass — the chain of ~40 chained
    // QString::replace() calls this grew out of rewrote the whole ~40 KB sheet once per
    // token, and previewAccent() rebuilds it for every accent row the pointer crosses.
    return fillTokens(stylesheetTemplate(), {
        {"%BTN_FLAT%", c(dark ? p.bgContainer.lighter(112) : p.bgContainer.darker(103))},
        // Geometry the code measures against (theme.hpp) — interpolated, never retyped.
        {"%MENU_PAD_R%", QString::number(kMenuItemRightPadPx)},
        {"%SEP_W%", QString::number(kDockSeparatorPx)},
        // The ink an accent-BACKED control paints its label and glyph in (theme.hpp
        // onAccentInk). The indicator marks are baked PNGs, so they come as a pair.
        {"%ON_ACCENT%", c(p.onAccent)},
        {"%TICK_IMG%", darkGlyph ? ":/icons/check-dark.png" : ":/icons/check.png"},
        {"%RADIO_IMG%", darkGlyph ? ":/icons/radio-dot-dark.png" : ":/icons/radio-dot.png"},
        // Dimmed accent fill for disabled accent buttons — still obviously part
        // of the accent group, just muted (see QToolButton[chatAccent]:disabled).
        {"%ACCENT_DIM%", rgba(p.accent, dark ? 0.38 : 0.30)},
        {"%ACCENT_SOFT2%", rgba(p.accent, dark ? 0.30 : 0.20)},
        {"%ACCENT_SOFT%", rgba(p.accent, dark ? 0.18 : 0.11)},
        {"%ACCENT_RING%", rgba(p.accent, 0.45)},
        // The line an input or icon button draws around itself: its OWN ink, well down —
        // full ink was hard and half still read heavy. A quarter keeps
        // the colour family while landing light-grey on the light theme and dark-grey on
        // the dark one, which is what a border should be in each.
        {"%UI_OUTLINE%", rgba(p.textMain, 0.25)},
        {"%ACCENT2%", c(accent2)},
        // Status reds mirror browser/css/theme.css --danger/--danger-2 (per theme).
        {"%SUCCESS_RING%", rgba(success, 0.55)},
        {"%DANGER2%", dangerHover(dark).name()},
        {"%DANGER%", c(p.danger)},
        // "Selected Line:" bar amber, mirroring browser/css/components.css
        // --bg-sel-panel / --border-sel / --text-sel-label (per theme). Routed through
        // the Palette (already display-space converted above), not re-hardcoded here —
        // a raw sRGB literal painted directly by Qt reads over-saturated on a wide-gamut
        // (P3) Mac display, same reason every other palette token goes through displayColor().
        {"%SEL_BG%", c(p.bgSelPanel)},
        {"%SEL_BORDER%", c(p.borderSel)},
        {"%SEL_LABEL%", c(p.textSelLabel)},
        {"%SEL_BTN_TEXT%", c(p.textSelBtn)},
        {"%SEL_BTN_HOV%", c(p.bgSelBtnHov)},
        {"%SEL_BTN%", c(p.bgSelBtn)},
        // The Deselect CTA's browser-hardcoded orange (layout.css .deselect-btn, no
        // light/dark variant) — same display-space conversion, or it reads noticeably
        // more saturated/brighter here than the color-managed browser rendering.
        {"%DESELECT_BG%", c(p.bgSelBtn)},
        {"%DESELECT_HOVER%", c(p.bgSelBtnHov)},
        {"%DISABLED_BG%", c(disabledBg)},
        {"%BG_PAGE%", c(p.bgPage)},
        {"%BG_CONTAINER%", c(p.bgContainer)},
        {"%BG_CONTROLS%", c(p.bgControls)},
        {"%BG_COORD_HOVER%", c(p.bgCoordHover)},
        // The keycap rows' hover: halfway from the container to the row-hover tint.
        {"%BG_ROW_HOVER_SOFT%", c(mixSrgb(p.bgContainer, p.bgCoordHover, 0.5))},
        // --bg-coord-even / --text-info (theme.css): the hotkey table's head and the
        // info rows' description ink (inherit on light).
        {"%BG_COORD_EVEN%", c(themeToken("--bg-coord-even", dark))},
        // --text-info is `inherit` on light, which is the main ink.
        {"%TEXT_INFO%", c(dark ? themeToken("--text-info", dark) : p.textMain)},
        {"%BG_INFO%", c(bgInfo)},
        {"%BG_DROP_HINT%", c(dropHintBg)},
        {"%BORDER_HINT%", c(dropHintBorder)},
        {"%BORDER_TOOLTIP%", c(borderTooltip)},
        {"%BORDER_CANVAS%", c(p.borderCanvas)},
        {"%BORDER%", c(p.borderMain)},
        {"%SB_THUMB%", c(sbThumb)},
        {"%TEXT%", c(p.textMain)},
        {"%CARET_DIM%", caretImagePath(caretDim)},
        {"%CARET%", caretImagePath(p.textMuted)},
        {"%MUTED%", c(p.textMuted)},
        // The captions ride the same softened ink as the outlines, a step stronger — the
        // fixed greys read as washed out beside them (user decision).
        {"%SECTIONTITLE%", rgba(p.textMain, 0.62)},
        {"%DISABLED_TEXT%", c(p.disabledText)},
        {"%ACCENT%", c(p.accent)},
        {"%INPUT_BG%", c(p.inputBg)},
        {"%INPUT_TEXT%", c(p.inputText)}});
  }
}

