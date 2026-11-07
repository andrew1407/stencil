#include "sheet.hpp"

#include "artCache.hpp"
#include "gradient.hpp"
#include "indicators.hpp"
#include "table.hpp"
#include "still.hpp"
#include "../skinPrefs.hpp"
#include "defaultVisuals.hpp"
#include "themeTokens.hpp"
#include "theme.hpp"

#include <QHash>
#include <QPainter>
#include <QPolygonF>

#include <optional>

namespace stencil::support {

  using stencil::gui::Palette;

  namespace {
    const QString& overlayTemplate() {
      static const QString tpl = stencil::gui::readStylesheet(QStringLiteral("aqua"));
      return tpl;
    }

    // --aq-gel-top → %AQ_GEL_TOP%
    QString tokenName(const QString& css) {
      QString t = css.mid(2).toUpper();
      t.replace('-', '_');
      return '%' + t + '%';
    }

    // The table's face with the gel the accent paints laid over it (an invalid accent: the table's), kept
    // per face for the accent last asked: the palette, the tokens and the sheet all read it.
    const QHash<QString, QColor>& faceTokens(bool dark, const QColor& accent) {
      static QHash<QString, QColor> faces[2];
      static QString accents[2];
      const int face = dark ? 1 : 0;
      const QString key = accent.isValid() ? accent.name() : QString();
      if (faces[face].isEmpty() || accents[face] != key) {
        QHash<QString, QColor> t = aquaConfig().tokens[face];
        const QHash<QString, QColor> gel = gelTokens(accent, dark);
        for (auto it = gel.begin(); it != gel.end(); ++it) t.insert(it.key(), it.value());
        faces[face] = t;
        accents[face] = key;
      }
      return faces[face];
    }

    // QSS tiles a background image: two rows of each stripe (browser --aq-pinstripe, 2px a band).
    QString pinstripePath(const QColor& stripe, const QColor& face) {
      return cachedArtPath("aq-stripe-" + stripe.name().mid(1) + face.name().mid(1), QSize(1, 4), [&](QPainter& p) {
        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillRect(0, 0, 1, 2, stripe);
        p.fillRect(0, 2, 1, 2, face);
      }, /*retina=*/false);
    }

    // A combo's up/down pair, dark on the gel cap (browser tokens.css --aq-arrows).
    QString arrowsPath() {
      return cachedArtPath(QStringLiteral("aq-arrows"), QSize(8, 12), [](QPainter& p) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#111111"));
        p.drawPolygon(QPolygonF({{4, 0}, {8, 4.5}, {0, 4.5}}));
        p.drawPolygon(QPolygonF({{4, 12}, {0, 7.5}, {8, 7.5}}));
      });
    }
  }  // namespace

  QString aquaOverlay(bool dark, const QString& accentKey) {
    const AquaConfig& c = aquaConfig();
    QHash<QString, QString> values;
    const QHash<QString, QColor> tokens =
        faceTokens(dark, accentKey.isEmpty() ? QColor() : stencil::gui::accentPrimary(accentKey));
    const auto tok = [&tokens](const char* css) { return tokens.value(QString::fromLatin1(css)); };
    // Browser color-mix(in srgb, a k%, b): aquaMix(a, k, b).
    const auto mix = [](const QColor& a, double k, const QColor& b) { return aquaMix(a, k, b).name(); };
    for (auto it = tokens.begin(); it != tokens.end(); ++it) values.insert(tokenName(it.key()), it.value().name());
    QStringList families;
    for (const QString& f : c.fontFamilies) families << ('"' + f + '"');
    values.insert(QStringLiteral("%AQ_FONT%"), families.join(", "));
    values.insert(QStringLiteral("%AQ_FONT_PX%"), QString::number(c.fontPx));
    // A danger CTA's word: red ink went dim on the dark glass, so there it reads in ink (browser aqua/chrome.css).
    values.insert(QStringLiteral("%AQ_DANGER_INK%"), tok(dark ? "--aq-ink" : "--aq-red").name());
    values.insert(QStringLiteral("%MENU_PAD_R%"), QString::number(stencil::gui::MENU_ITEM_RIGHT_PAD_PX));
    // The tile's ring: the ink at 45% over the face (browser chrome.css color-mix on --aq-ink).
    values.insert(QStringLiteral("%AQ_TILE_EDGE%"), mix(tok("--aq-ink"), 0.45, tok("--aq-face")));
    values.insert(QStringLiteral("%AQ_ARROWS%"), arrowsPath());
    values.insert(QStringLiteral("%AQ_CLOSE_LIGHT%"), aquaCloseLightQss(tok("--aq-red")));
    values.insert(QStringLiteral("%AQ_GLASS_FILL%"), aquaQssGradient(aquaGlassStops(tok("--aq-glass-top"), tok("--aq-glass-bottom"))));
    // A dead control is no longer glass: a flat well, the face 12% toward the ink (browser css/aqua/chrome.css :disabled).
    values.insert(QStringLiteral("%AQ_DEAD_FILL%"), mix(tok("--aq-face"), 0.88, tok("--aq-ink")));
    values.insert(QStringLiteral("%AQ_GEL_FILL%"),
                  aquaQssGradient(aquaGelStops(tok("--aq-gel-hi"), tok("--aq-gel-top"), tok("--aq-gel-bottom"))));
    {
      // --aq-tick (browser tokens.css): dark on the gel, light on graphite so the overhang reads.
      const AquaIndicatorColors ind{tok("--aq-glass-top"), tok("--aq-glass-bottom"), tok("--aq-edge"), tok("--aq-gel-hi"),
                                    tok("--aq-gel-top"), tok("--aq-gel-bottom"), tok("--aq-gel-edge"),
                                    QColor(dark ? "#f4f4f4" : "#111111")};
      static constexpr std::pair<const char*, AquaIndicator> KINDS[] = {
          {"%AQ_BOX%", AquaIndicator::BOX}, {"%AQ_BOX_ON%", AquaIndicator::BOX_ON},
          {"%AQ_BOX_DEAD_ON%", AquaIndicator::BOX_DEAD_ON}, {"%AQ_RADIO%", AquaIndicator::RADIO},
          {"%AQ_RADIO_ON%", AquaIndicator::RADIO_ON}, {"%AQ_RADIO_DEAD_ON%", AquaIndicator::RADIO_DEAD_ON}};
      for (const auto& [name, kind] : KINDS) values.insert(QString::fromLatin1(name), aquaIndicatorPath(kind, ind));
    }
    // Browser windows.css: the band is the edge at 22% over the window, today's at 45%; a dead word the ink at 60% over the glass.
    values.insert(QStringLiteral("%AQ_BAND%"), mix(tok("--aq-edge"), 0.22, tok("--aq-window")));
    values.insert(QStringLiteral("%AQ_DISABLED%"), mix(tok("--aq-ink"), 0.6, tok("--aq-glass-bottom")));
    values.insert(QStringLiteral("%AQ_BAND_TODAY%"), mix(tok("--aq-edge"), 0.45, tok("--aq-window")));
    // Browser aqua fields.css halos: a field's --aq-focus at 60%, the keywords well's accent (--aq-select) at 25%.
    const auto halo = [](const QColor& c, double a) {
      return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(a, 0, 'f', 2);
    };
    values.insert(QStringLiteral("%AQ_FOCUS_HALO%"), halo(tok("--aq-focus"), 0.6));
    values.insert(QStringLiteral("%AQ_SELECT_HALO%"), halo(tok("--aq-select"), 0.25));
    values.insert(QStringLiteral("%AQ_PINSTRIPE%"), pinstripePath(tok("--aq-stripe"), tok("--aq-face")));
    return stencil::gui::fillStylesheetTokens(overlayTemplate(), values);
  }

  QString buildAquaStylesheet(bool dark, const QString& accentKey) {
    setSkinAccent(stencil::gui::accentPrimary(accentKey));
    const QHash<QString, QColor>& t = faceTokens(dark, skinAccent());
    const auto tok = [&t](const char* css) { return t.value(QString::fromLatin1(css)); };
    setSkinDropGlow({tok("--aq-gel-bottom"), tok("--aq-focus")});
    SkinGlass glass{tok("--aq-glass-top"), tok("--aq-glass-bottom"), tok("--aq-edge"), tok("--aq-ink"),
                    tok("--aq-gel-hi"), tok("--aq-gel-top"), tok("--aq-gel-bottom"), tok("--aq-gel-edge"),
                    tok("--aq-on-gel"), {}, {}};   // the stops follow, from the colours just read
    glass.glassStops = aquaGlassStops(glass.top, glass.bottom);
    glass.gelStops = aquaGelStops(glass.gelHi, glass.gelTop, glass.gelBottom);
    setSkinGlass(glass);
    return withoutHover(stencil::gui::buildStylesheet(dark, accentKey)) + aquaOverlay(dark, accentKey);
  }

  QColor aquaToken(const char* css, bool dark) {
    const QString to = aquaConfig().aliases.value(QString::fromLatin1(css));
    return to.isEmpty() ? QColor() : faceTokens(dark, skinAccent()).value(to);
  }

  // Built once per face and accent: a painter reads it per row.
  Palette aquaPalette(bool dark) {
    static std::optional<Palette> palettes[2];
    static QString accents[2];
    const int face = dark ? 1 : 0;
    const QString key = skinAccent().isValid() ? skinAccent().name() : QString();
    if (palettes[face] && accents[face] == key) return *palettes[face];
    const QHash<QString, QColor>& tokens = faceTokens(dark, skinAccent());
    const auto tok = [&tokens](const char* css) { return tokens.value(QString::fromLatin1(css)); };
    Palette p;
    p.selGlow = stencil::gui::displayColor(QColor(stencil::gui::defaultVisuals::table().selGlow));
    p.hoverRing = stencil::gui::displayColor(QColor(stencil::gui::DEFAULT_ACCENT_HEX));
    p.bgPage = tok("--aq-desktop");
    p.bgCanvas = tok("--aq-canvas");   // never the accent-tinted desk: a picture may share the accent
    p.bgContainer = tok("--aq-face");
    p.bgControls = tok("--aq-face");
    p.bgSelPanel = tok("--aq-face");
    p.borderMain = tok("--aq-edge");
    p.borderCanvas = tok("--aq-edge");
    p.borderSel = tok("--aq-edge");
    p.textMain = tok("--aq-ink");
    p.textMuted = tok("--aq-muted");
    p.textSelLabel = tok("--aq-ink");
    p.bgSelBtn = tok("--aq-glass-top");
    p.bgSelBtnHov = tok("--aq-gel-hi");
    p.textSelBtn = tok("--aq-ink");
    p.textKey = tok("--aq-link");
    p.inputBg = tok("--aq-window");
    p.inputText = tok("--aq-ink");
    p.accent = tok("--aq-select");
    p.onAccent = tok("--aq-on-select");
    p.bgCoordHover = tok("--aq-gel-hi");
    p.disabledText = tok("--aq-muted");
    p.danger = tok("--aq-red");
    p.warning = tok("--aq-yellow");
    palettes[face] = p;
    accents[face] = key;
    return p;
  }

}  // namespace stencil::support
