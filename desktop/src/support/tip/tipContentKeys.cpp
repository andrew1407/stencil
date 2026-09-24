// The key vocabulary and the painted keycaps: character for character the browser's tip/content.js,
// case-SENSITIVE on purpose so an app verb never wears a cap. Qt rich text gives a span only a
// background, so a cap is an <img> data URI drawn here.
#include "tipContentParts.hpp"
#include "../skinPrefs.hpp"
#include <QBuffer>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QRegularExpression>
#include <QScreen>
#include <QToolTip>
#include <QtMath>

namespace stencil::gui {

  namespace {
    // Character for character the browser's (tip/content.js). Case-SENSITIVE on purpose:
    // "Delete every saved project" must not put a keycap on its verb.
    const QString MOD = QStringLiteral("Ctrl|Control|Cmd|Command|Meta|Win|Alt|Option|Shift");
    const QString NAMED = QStringLiteral(
        "Enter|Return|Escape|Esc|Tab|Space|Backspace|Delete|Del|Home|End|PageUp|PageDown"
        "|Arrow(?:Up|Down|Left|Right)|F\\d{1,2}");
    // The gesture word comes BEFORE the single character so "Alt+click" is not "Alt+c"+"lick".
    // KEY_GLYPH must cover what QKeySequence::NativeText emits on macOS (⎋ ⇥ ↵ ⌤ ⇞ ⇟ ↖ ↘ ⌦ ⌫).
    const QString KEY_GLYPH = QStringLiteral(
        "[\u232B\u2326\u2191\u2193\u2190\u2192\u238B\u21E5\u21B5\u2324"
        "\u21DE\u21DF\u2196\u2198\u2423]");
    QString atom() {
      return "(?:" + NAMED + "|" + MOD + "|[a-z][a-z-]{1,11}|[A-Za-z0-9]"
             "|" + KEY_GLYPH + "|[-+=\\[\\]/\\\\.,;'`])";
    }
    const QString END = QStringLiteral("(?![\\w-])");
    QString chained() { return "(?:(?:" + MOD + ")\\+)+" + atom() + END; }          // Ctrl+Shift+Z
    QString glyphs() { return "[\u2303\u2325\u21E7\u2318]+" + atom() + END; }        // ⇧⌘Z
    const QString KEY_PROSE = QStringLiteral("(?:hold|press|hit|tap|with|then|or)\\s+");

    // On a Mac a modifier is DRAWN. Ctrl → ⌃, not ⌘: a spelled "Ctrl" is a literal Control key.
    QString macGlyphFor(const QString& key, bool mac) {
      if (!mac) return key;
      static const QHash<QString, QString> MAP{
          {"Ctrl", "\u2303"},  {"Control", "\u2303"},  {"Alt", "\u2325"},     {"Option", "\u2325"},
          {"Shift", "\u21E7"}, {"Cmd", "\u2318"},      {"Command", "\u2318"}, {"Meta", "\u2318"},
      };
      return MAP.value(key, key);
    }

    // STATED, not grown off QToolTip::font(): that font is a different size on every platform
    // (11pt here). Browser twin: components/tooltip.css .tip-key 14px, in either skin.
    int capPx(qreal scale) { return qMax(1, qRound(14.0 * scale)); }

    QColor orElse(const QColor& c, const QColor& fallback) { return c.isValid() ? c : fallback; }

    // The raised box of css/webcore/tokens.css --wc-raised: white top-left, black bottom-right,
    // light and shadow one pixel in (support/skinPrefs.hpp carries the four).
    void paintBevel(QPainter& p, const QRectF& r, const Palette& pal) {
      p.fillRect(r, pal.bgContainer);
      const support::SkinBevel b = support::skinBevel();
      const QPair<QColor, QColor> bands[2] = {
          {orElse(b.hilight, Qt::white), orElse(b.dark, Qt::black)},
          {orElse(b.light, pal.bgContainer), orElse(b.shadow, pal.borderMain)}};
      for (int i = 0; i < 2; ++i) {
        const QRectF band = r.adjusted(i, i, -i, -i);
        p.fillRect(QRectF(band.left(), band.top(), band.width(), 1), bands[i].first);
        p.fillRect(QRectF(band.left(), band.top(), 1, band.height()), bands[i].first);
        p.fillRect(QRectF(band.left(), band.bottom() - 1, band.width(), 1), bands[i].second);
        p.fillRect(QRectF(band.right() - 1, band.top(), 1, band.height()), bands[i].second);
      }
    }

    // PAINTED as an <img> data URI: Qt rich text gives a span only a background.
    // `joiner` = the "+" between caps: no face, muted, same picture so it centres.
    QString capHtml(const QString& label, const Palette& pal, bool joiner = false,
                    qreal scale = 1.0) {
      // Browser: 14px cap over 12px prose. `scale` < 1 is a table-cell cap (comboKeycapsHtml).
      QFont f = QToolTip::font();
      f.setBold(!joiner);
      f.setPixelSize(capPx(scale));

      const QScreen* scr = QGuiApplication::primaryScreen();
      const qreal dpr = qBound(1.0, scr ? scr->devicePixelRatio() : 1.0, 3.0);
      const QString key = label + (joiner ? "|+|" : "|k|") + f.toString() + '|' +
                          QString::number(dpr) + '|' + QString::number(scale) + '|' +
                          QString::number(support::skinGeneration()) + '|' +
                          pal.bgContainer.name(QColor::HexArgb) +   // a transparent face is a face
                          pal.borderMain.name() + pal.textKey.name() + pal.textMuted.name() +
                          pal.textMain.name();
      static QHash<QString, QString> cache;
      const auto hit = cache.constFind(key);
      if (hit != cache.constEnd()) return *hit;

      const QFontMetricsF fm(f);
      // The "+" carries 3px of air each side (browser .tip-plus margin), so caps read as ' + '.
      const qreal padX = 6 * scale, padY = 3 * scale, radius = 5, gapX = 0.5 * scale, joinPad = 3 * scale;
      const qreal h = fm.height() + 2 * padY;
      const qreal w = joiner ? fm.horizontalAdvance(label) + 2 * joinPad
                             : qMax(fm.horizontalAdvance(label) + 2 * padX, h * 0.9);

      QImage img(qRound((w + 2 * gapX) * dpr), qRound(h * dpr), QImage::Format_ARGB32_Premultiplied);
      img.fill(Qt::transparent);
      {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.scale(dpr, dpr);
        const QRectF r(gapX, 0, w, h);
        if (!joiner && support::isWebcore()) {
          p.setRenderHint(QPainter::Antialiasing, false);
          paintBevel(p, r, pal);
        } else if (!joiner) {
          p.setPen(Qt::NoPen);
          p.setBrush(pal.borderMain);
          p.drawRoundedRect(r, radius, radius);
          p.setBrush(pal.bgContainer);  // the face, a shade off the tooltip behind it
          // A see-through face must CLEAR the border slab — a transparent brush paints nothing.
          if (pal.bgContainer.alpha() == 0) {
            p.setCompositionMode(QPainter::CompositionMode_Clear);
            p.setBrush(Qt::black);
          }
          p.drawRoundedRect(r.adjusted(1, 1, -1, -2), radius - 1, radius - 1);  // 2px bottom edge
          p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        }
        p.setFont(f);
        // The skin's cap carries the page ink; its own key colour is the link blue.
        p.setPen(joiner ? pal.textMuted : (support::isWebcore() ? pal.textMain : pal.textKey));
        p.drawText(r.adjusted(0, 0, 0, joiner ? 0 : -1), Qt::AlignCenter, label);
      }
      QByteArray png;
      QBuffer buf(&png);
      buf.open(QIODevice::WriteOnly);
      img.save(&buf, "PNG");
      // The alt text is the key itself. vertical-align: middle, else words hang off the cap's bottom.
      const QString html = QStringLiteral("<img alt=\"%1\" class=\"%2\" width=\"%3\" height=\"%4\" "
                                          "style=\"vertical-align: middle;\" "
                                          "src=\"data:image/png;base64,%5\">")
                               .arg(label.toHtmlEscaped())
                               .arg(QLatin1String(joiner ? JOINER_CLASS : KEYCAP_CLASS))
                               .arg(qRound(w + 2 * gapX))
                               .arg(qRound(h))
                               .arg(QString::fromLatin1(png.toBase64()));
      cache.insert(key, html);
      return html;
    }

  }  // namespace

  namespace tipdetail {

    QString keysHtml(const QString& combo, const Palette& pal, bool mac, qreal scale) {
      const QString s = combo.trimmed();
      const QString plus = capHtml("+", pal, true, scale);
      static const QRegularExpression lead("^[⌃⌥⇧⌘]+");
      const auto m = lead.match(s);
      QStringList caps;
      if (m.hasMatch()) {
        const QString run = m.captured(0);
        for (const QChar c : run) caps << capHtml(QString(c), pal, false, scale);
        const QString rest = s.mid(run.size());
        if (!rest.isEmpty()) caps << capHtml(rest, pal, false, scale);
        // Apple prints ⇧⌘S with no joiner, but a run of bare glyphs reads as one symbol.
        return caps.join(plus);
      }
      for (const QString& part : s.split('+'))
        if (!part.isEmpty()) caps << capHtml(macGlyphFor(part, mac), pal, false, scale);
      return caps.join(plus);
    }

    // Combos with "+" (or Apple glyphs) always count; a lone "Shift"/"Enter" only in a key
    // context ("hold Shift"), or the app's own verbs would wear keycaps.
    QString highlightKeys(const QString& text, const Palette& pal, bool mac) {
      const QRegularExpression re("(" + KEY_PROSE + ")?(" + chained() + "|" + glyphs() + "|" +
                                  MOD + "|" + NAMED + ")");
      const QRegularExpression whole("^(?:" + chained() + "|" + glyphs() + ")$");
      QString out;
      int last = 0;
      auto it = re.globalMatch(text);
      while (it.hasNext()) {
        const auto m = it.next();
        const QString lead = m.captured(1);
        const QString token = m.captured(2);
        if (!whole.match(token).hasMatch() && lead.isEmpty()) continue;  // a bare word, no context
        out += text.mid(last, m.capturedStart() - last).toHtmlEscaped();
        out += lead.toHtmlEscaped();
        out += keysHtml(token, pal, mac);
        last = m.capturedEnd();
      }
      return out + text.mid(last).toHtmlEscaped();
    }
  }  // namespace tipdetail

  bool isKeyCombo(const QString& s) {
    // A lone key glyph counts here — "Close (⎋)" — but NOT in running prose (highlightKeys).
    const QRegularExpression re("^(?:" + chained() + "|" + glyphs() + "|" + MOD + "|" + NAMED +
                                "|" + KEY_GLYPH + ")$");
    return re.match(s.trimmed()).hasMatch();
  }
}  // namespace stencil::gui
