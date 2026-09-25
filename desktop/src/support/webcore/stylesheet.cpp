#include "stylesheet.hpp"

#include "icons.hpp"
#include "rules.hpp"
#include "../skinPrefs.hpp"
#include "themeTokens.hpp"
#include "theme.hpp"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QStandardPaths>
#include <initializer_list>
#include <iterator>

namespace stencil::support {

  using stencil::gui::Palette;

  namespace {
    const QString& overlayTemplate() {
      static const QString tpl = [] {
        stencil::gui::ensureThemeResources();
        QFile f(QStringLiteral(":/qss/webcore.qss"));
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
      }();
      return tpl;
    }

    // --wc-title-ink → %WC_TITLE_INK%
    QString tokenName(const QString& css) {
      QString t = css.mid(2).toUpper();
      t.replace('-', '_');
      return '%' + t + '%';
    }

    QColor tok(bool dark, const char* css) {
      return webcoreConfig().tokens[dark ? 1 : 0].value(QString::fromLatin1(css));
    }

    QString cachePath(const QString& file) {
      const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
      QDir().mkpath(dir);
      return dir + '/' + file;
    }
    // A QSS sub-control takes only an image: the stepped arrow, 7x4 down or up or 4x7 right
    // (browser ctx/arrow.js chevron at 12px), cached at 1x and @2x.
    QString arrowImagePath(const QColor& colour, char dir) {
      const QString base = cachePath("wc-arrow-" + QLatin1Char(dir) + colour.name(QColor::HexArgb).mid(1));
      const QString path = base + ".png";
      if (QFile::exists(path)) return path;
      const bool right = dir == 'r';
      for (const int scale : {1, 2}) {
        QImage img((right ? 4 : 7) * scale, (right ? 7 : 4) * scale, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        for (int i = 0; i < 4; ++i) {
          const int step = dir == 'u' ? 3 - i : i;
          if (right) p.fillRect(i * scale, step * scale, scale, (7 - 2 * step) * scale, colour);
          else p.fillRect(step * scale, i * scale, (7 - 2 * step) * scale, scale, colour);
        }
        p.end();
        img.save(scale == 1 ? path : base + "@2x.png", "PNG");
      }
      return path;
    }
    // A glyph of iconsWebcore.json as a PNG for QSS url(); empty leaves the style's own mark.
    QString pixelGlyphPath(const QString& name, bool dark) {
      if (!hasPixelIcon(name)) return {};
      const QString base = cachePath("wc-glyph-" + name + (dark ? "-d" : "-l"));
      for (const int scale : {1, 2})
        pixelIconImage(name, dark, scale).save(scale == 1 ? base + ".png" : base + "@2x.png", "PNG");
      return base + ".png";
    }
    // One band of a CSS inset box-shadow on a 5x5 nine-slice: `tl` = +k +k (top rows, left
    // columns), else -k -k. Listed in the CSS order and painted last-first, as the browser layers them.
    struct Band { QColor colour; int k; bool tl; };
    QString bevelImagePath(const QString& name, std::initializer_list<Band> bands, bool openTop = false) {
      QString key = name;
      for (const Band& b : bands) key += b.colour.name().mid(1);
      const QString path = cachePath("wc-bevel-" + key + ".png");
      if (QFile::exists(path)) return path;
      QImage img(5, 5, QImage::Format_ARGB32_Premultiplied);
      img.fill(Qt::transparent);
      QPainter p(&img);
      for (auto it = std::rbegin(bands); it != std::rend(bands); ++it) {
        const int k = it->k;
        if (it->tl) { if (!openTop) p.fillRect(0, 0, 5, k, it->colour); p.fillRect(0, 0, k, 5, it->colour); }
        else { p.fillRect(0, 5 - k, 5, k, it->colour); p.fillRect(5 - k, 0, k, 5, it->colour); }
      }
      p.end();
      img.save(path, "PNG");
      return path;
    }
    QString etchImagePath(const QColor& sh, const QColor& hi) {
      const QString path = cachePath("wc-etch-" + sh.name().mid(1) + hi.name().mid(1) + ".png");
      if (QFile::exists(path)) return path;
      QImage img(1, 3, QImage::Format_ARGB32_Premultiplied);
      img.fill(Qt::transparent);
      img.setPixelColor(0, 0, sh);
      img.setPixelColor(0, 1, hi);
      img.save(path, "PNG");
      return path;
    }
    // A hovered tab's bevel around its inset ring: a 9x7 nine-slice cut 4 4 2 4.
    QString tabRingPath(const QColor& hi, const QColor& dk, const QColor& ring) {
      const QString path = cachePath("wc-tabring-" + hi.name().mid(1) + dk.name().mid(1) + ring.name().mid(1) + ".png");
      if (QFile::exists(path)) return path;
      QImage img(9, 7, QImage::Format_ARGB32_Premultiplied);
      img.fill(Qt::transparent);
      QPainter p(&img);
      p.fillRect(0, 0, 2, 7, hi);
      p.fillRect(7, 0, 2, 7, dk);
      p.fillRect(0, 0, 8, 1, hi);
      p.fillRect(0, 1, 7, 1, hi);
      p.fillRect(2, 2, 5, 5, ring);
      p.end();
      img.setPixelColor(4, 4, Qt::transparent);
      img.save(path, "PNG");
      return path;
    }
  }  // namespace

  QString webcoreOverlay(bool dark, const QString& accentKey) {
    const WebcoreConfig& c = webcoreConfig();
    QHash<QString, QString> values;
    const QHash<QString, QColor>& tokens = c.tokens[dark ? 1 : 0];
    for (auto it = tokens.begin(); it != tokens.end(); ++it) values.insert(tokenName(it.key()), it.value().name());
    QStringList families;
    for (const QString& f : c.fontFamilies) families << ('"' + f + '"');
    values.insert(QStringLiteral("%WC_FONT%"), families.join(", "));
    values.insert(QStringLiteral("%WC_FONT_PX%"), QString::number(c.fontPx));
    // The mark on a white box is the dark tick every light accent already bakes.
    values.insert(QStringLiteral("%WC_TICK%"), dark ? QStringLiteral(":/icons/check.png") : QStringLiteral(":/icons/check-dark.png"));
    values.insert(QStringLiteral("%WC_RADIO%"), dark ? QStringLiteral(":/icons/radio-dot.png") : QStringLiteral(":/icons/radio-dot-dark.png"));
    values.insert(QStringLiteral("%MENU_PAD_R%"), QString::number(stencil::gui::MENU_ITEM_RIGHT_PAD_PX));
    values.insert(QStringLiteral("%SEP_W%"), QString::number(stencil::gui::DOCK_SEPARATOR_PX));
    values.insert(QStringLiteral("%CHAT_GAP%"), QString::number(stencil::gui::CHAT_PAGE_GAP_PX));
    values.insert(QStringLiteral("%WC_DISABLED%"), tok(dark, dark ? "--wc-muted" : "--wc-shadow").name());
    values.insert(QStringLiteral("%WC_ARROW%"), arrowImagePath(tok(dark, "--wc-ink"), 'd'));
    {
      QColor sub = tok(dark, "--wc-ink");
      sub.setAlphaF(0.55);
      values.insert(QStringLiteral("%WC_SUBMENU_ARROW%"), arrowImagePath(sub, 'r'));
    }
    values.insert(QStringLiteral("%WC_MENU_CHECK%"), pixelGlyphPath(QStringLiteral("check"), dark));
    values.insert(QStringLiteral("%WC_ARROW_UP%"), arrowImagePath(tok(dark, "--wc-ink"), 'u'));
    values.insert(QStringLiteral("%WC_ARROW_DIM%"), arrowImagePath(tok(dark, "--wc-shadow"), 'd'));
    // The pointer's frame is the accent the user chose; --wc-focus is only its fallback.
    const QColor chosen = stencil::gui::accentPrimary(accentKey);
    if (chosen.isValid()) values.insert(QStringLiteral("%WC_FOCUS%"), chosen.name());
    // css/webcore/tokens.css --wc-raised / --wc-sunken, band for band; QSS has no box-shadow.
    const QColor hi = tok(dark, "--wc-hilight"), lt = tok(dark, "--wc-light"), sh = tok(dark, "--wc-shadow"),
                 dk = tok(dark, "--wc-dark"), focus(values.value(QStringLiteral("%WC_FOCUS%")));
    values.insert(QStringLiteral("%WC_RAISED%"), bevelImagePath("r", {{dk, 1, false}, {hi, 1, true}, {sh, 2, false}, {lt, 2, true}}));
    values.insert(QStringLiteral("%WC_RAISED_OPEN%"), bevelImagePath("ro", {{dk, 1, false}, {hi, 1, true}, {sh, 2, false}, {lt, 2, true}}, true));
    values.insert(QStringLiteral("%WC_ETCH%"), etchImagePath(sh, hi));
    values.insert(QStringLiteral("%WC_SUNKEN%"), bevelImagePath("s", {{sh, 1, true}, {hi, 1, false}, {dk, 2, true}, {lt, 2, false}}));
    values.insert(QStringLiteral("%WC_DANGER_SOFT%"), dark ? QStringLiteral("#24ff6060") : QStringLiteral("#24800000"));   // --danger at 14%
    values.insert(QStringLiteral("%WC_RING%"), bevelImagePath("f", {{focus, 2, true}, {focus, 2, false}}));
    values.insert(QStringLiteral("%WC_TAB_RING%"), tabRingPath(hi, dk, focus));
    return stencil::gui::fillStylesheetTokens(overlayTemplate(), values);
  }

  QString buildWebcoreStylesheet(bool dark, const QString& accentKey) {
    setSkinAccent(stencil::gui::accentPrimary(accentKey));   // the palette hook below reads it
    return stencil::gui::buildStylesheet(dark, accentKey) + webcoreOverlay(dark, accentKey);
  }

  // Every field set here: themePalette() answers through the hook while the skin is on.
  Palette webcorePalette(bool dark) {
    // …and the bevel rides along, for the painters that draw a raised box without this table
    // (support/tip/tipContentKeys.cpp).
    setSkinBevel({tok(dark, "--wc-face"), tok(dark, "--wc-ink"), tok(dark, "--wc-title-a"),
                  tok(dark, "--wc-title-b"), tok(dark, "--wc-hilight"), tok(dark, "--wc-dark"),
                  tok(dark, "--wc-light"), tok(dark, "--wc-shadow")});
    Palette p;
    p.selGlow = stencil::gui::displayColor(QColor("#ffc800"));
    p.hoverRing = stencil::gui::displayColor(QColor(stencil::gui::DEFAULT_ACCENT_HEX));
    p.bgPage = tok(dark, "--wc-desktop");
    p.bgContainer = tok(dark, "--wc-face");
    p.bgControls = tok(dark, "--wc-face");
    p.bgSelPanel = tok(dark, "--wc-face");
    p.borderMain = tok(dark, "--wc-shadow");
    p.borderCanvas = tok(dark, "--wc-shadow");
    p.borderSel = tok(dark, "--wc-shadow");
    p.textMain = tok(dark, "--wc-ink");
    p.textMuted = tok(dark, "--wc-muted");
    p.textSelLabel = tok(dark, "--wc-ink");
    p.bgSelBtn = tok(dark, "--wc-face");
    p.bgSelBtnHov = tok(dark, "--wc-light");
    p.textSelBtn = tok(dark, "--wc-ink");
    p.textKey = skinAccent().isValid() ? skinAccent() : tok(dark, "--wc-focus");
    p.inputBg = tok(dark, "--wc-window");
    p.inputText = tok(dark, "--wc-ink");
    p.accent = tok(dark, "--wc-select");
    p.onAccent = tok(dark, "--wc-on-select");
    p.bgCoordHover = tok(dark, "--wc-light");
    // The shadow greys a label on silver; on the dark face it is near-black (browser --disabled-text).
    p.disabledText = tok(dark, dark ? "--wc-muted" : "--wc-shadow");
    p.danger = QColor(dark ? "#ff6060" : "#800000");
    p.warning = QColor(dark ? "#d0d040" : "#808000");
    return p;
  }

}  // namespace stencil::support
