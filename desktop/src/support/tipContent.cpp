#include "tipContent.hpp"
#include <QApplication>
#include <QBuffer>
#include <QtMath>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QScreen>
#include <QTextDocument>
#include <QToolTip>
#include <QWidget>
#include <QRegularExpression>

namespace stencil::gui {

  namespace {

    // The key vocabulary, character for character the browser's (tipContent.js). Matching is
    // case-SENSITIVE on purpose: "Delete every saved project" must not put a keycap on its
    // verb, so a bare key word only counts inside a key context or a trailing "(…)".
    const QString kMod = QStringLiteral("Ctrl|Control|Cmd|Command|Meta|Win|Alt|Option|Shift");
    const QString kNamed = QStringLiteral(
        "Enter|Return|Escape|Esc|Tab|Space|Backspace|Delete|Del|Home|End|PageUp|PageDown"
        "|Arrow(?:Up|Down|Left|Right)|F\\d{1,2}");
    // What can end a combo: named key, modifier, lower-case gesture word, single
    // character, key glyph, or punctuation. The gesture word comes BEFORE the single
    // character so "Alt+click" is not read as "Alt+c" plus "lick".
    // kKeyGlyph must cover what QKeySequence::NativeText actually emits on macOS
    // (⎋ ⇥ ↵ ⌤ ⇞ ⇟ ↖ ↘ ⌦ ⌫), or those shortcuts never become keycaps.
    const QString kKeyGlyph = QStringLiteral(
        "[\u232B\u2326\u2191\u2193\u2190\u2192\u238B\u21E5\u21B5\u2324"
        "\u21DE\u21DF\u2196\u2198\u2423]");
    QString atom() {
      return "(?:" + kNamed + "|" + kMod + "|[a-z][a-z-]{1,11}|[A-Za-z0-9]"
             "|" + kKeyGlyph + "|[-+=\\[\\]/\\\\.,;'`])";
    }
    // Nothing may run on past the key, or half a word would end up wearing a keycap.
    const QString kEnd = QStringLiteral("(?![\\w-])");
    QString chained() { return "(?:(?:" + kMod + ")\\+)+" + atom() + kEnd; }          // Ctrl+Shift+Z
    QString glyphs() { return "[\u2303\u2325\u21E7\u2318]+" + atom() + kEnd; }        // ⇧⌘Z
    const QString kKeyProse = QStringLiteral("(?:hold|press|hit|tap|with|then|or)\\s+");

    // The app's own separators: " · " lists alternatives, " — " splits a term from
    // its description — but NOT inside parentheses, where a hint like
    // "(Alt+O cycles · hold Alt+Shift+O to peek)" must stay one heading.
    QStringList dotParts(const QString& line) {
      QStringList out;
      int depth = 0, start = 0;
      for (int i = 0; i < line.size(); i++) {
        const QChar ch = line.at(i);
        if (ch == '(') depth++;
        else if (ch == ')') depth = qMax(0, depth - 1);
        else if (ch == QChar(0x00B7) && depth == 0 && i > 0 && i + 1 < line.size() &&
                 line.at(i - 1).isSpace() && line.at(i + 1).isSpace()) {
          out << line.mid(start, i - start).trimmed();
          start = i + 1;
        }
      }
      out << line.mid(start).trimmed();
      out.removeAll(QString());
      return out;
    }
    bool dashSplit(const QString& line, QString* term, QString* desc) {
      const int i = line.indexOf(QStringLiteral(" \u2014 "));  // " — "
      if (i < 0) return false;
      *term = line.left(i).trimmed();
      *desc = line.mid(i + 3).trimmed();
      return true;
    }
    bool isNoteLine(const QString& l) {
      static const QRegularExpression re("^[\u2014\u2013-]{1,2}\\s+");
      return re.match(l).hasMatch();
    }

    // On a Mac a modifier is DRAWN, not spelled: a hand-written "Alt+O" must render
    // as ⌥, mapped at render time exactly as the browser/extension ports do.
    // Ctrl → ⌃, not ⌘ — a spelled "Ctrl" that reaches here is a literal Control key.
    QString macGlyphFor(const QString& key, bool mac) {
      if (!mac) return key;
      static const QHash<QString, QString> kMap{
          {"Ctrl", "\u2303"},  {"Control", "\u2303"},  {"Alt", "\u2325"},     {"Option", "\u2325"},
          {"Shift", "\u21E7"}, {"Cmd", "\u2318"},      {"Command", "\u2318"}, {"Meta", "\u2318"},
      };
      return kMap.value(key, key);
    }

    // One keycap, PAINTED as an <img> data URI (Qt rich text gives a span only a
    // background — no border/radius/padding), drawn to the browser's .tip-key look.
    // `joiner` = the "+" between caps: no face, muted, same picture so it centres.
    QString capHtml(const QString& label, const Palette& pal, bool joiner = false) {
      // Only a shade bigger than the tooltip's own type, like the browser's 14px cap over
      // 12px prose — the shape is what makes it read as a key now, not the size.
      QFont f = QToolTip::font();
      f.setBold(!joiner);
      if (f.pointSizeF() > 0) f.setPointSizeF(f.pointSizeF() * 1.15);
      else f.setPixelSize(qMax(1, qRound(f.pixelSize() * 1.15)));

      const QScreen* scr = QGuiApplication::primaryScreen();
      const qreal dpr = qBound(1.0, scr ? scr->devicePixelRatio() : 1.0, 3.0);
      const QString key = label + (joiner ? "|+|" : "|k|") + f.toString() + '|' +
                          QString::number(dpr) + '|' + pal.bgContainer.name() +
                          pal.borderMain.name() + pal.textKey.name() + pal.textMuted.name();
      static QHash<QString, QString> cache;
      const auto hit = cache.constFind(key);
      if (hit != cache.constEnd()) return *hit;

      const QFontMetricsF fm(f);
      const qreal padX = 6, padY = 3, radius = 5, gapX = 3;
      const qreal h = fm.height() + 2 * padY;
      const qreal w = joiner ? fm.horizontalAdvance(label) + 2
                             : qMax(fm.horizontalAdvance(label) + 2 * padX, h * 0.9);

      QImage img(qRound((w + 2 * gapX) * dpr), qRound(h * dpr), QImage::Format_ARGB32_Premultiplied);
      img.fill(Qt::transparent);
      {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.scale(dpr, dpr);
        const QRectF r(gapX, 0, w, h);
        if (!joiner) {
          p.setPen(Qt::NoPen);
          p.setBrush(pal.borderMain);
          p.drawRoundedRect(r, radius, radius);
          p.setBrush(pal.bgContainer);  // the face, a shade off the tooltip behind it
          p.drawRoundedRect(r.adjusted(1, 1, -1, -2), radius - 1, radius - 1);  // 2px bottom edge
        }
        p.setFont(f);
        p.setPen(joiner ? pal.textMuted : pal.textKey);
        p.drawText(r.adjusted(0, 0, 0, joiner ? 0 : -1), Qt::AlignCenter, label);
      }
      QByteArray png;
      QBuffer buf(&png);
      buf.open(QIODevice::WriteOnly);
      img.save(&buf, "PNG");
      // The alt text is the key itself — what tests and screen readers get.
      // vertical-align: middle centres the cap in running prose (browser .tip-key);
      // on the baseline the words would hang off its bottom edge.
      const QString html = QStringLiteral("<img alt=\"%1\" class=\"%2\" width=\"%3\" height=\"%4\" "
                                          "style=\"vertical-align: middle;\" "
                                          "src=\"data:image/png;base64,%5\">")
                               .arg(label.toHtmlEscaped())
                               .arg(QLatin1String(kKeycapClass))
                               .arg(qRound(w + 2 * gapX))
                               .arg(qRound(h))
                               .arg(QString::fromLatin1(png.toBase64()));
      cache.insert(key, html);
      return html;
    }

    QString keysHtml(const QString& combo, const Palette& pal, bool mac) {
      const QString s = combo.trimmed();
      const QString plus = capHtml("+", pal, true);
      static const QRegularExpression lead("^[⌃⌥⇧⌘]+");
      const auto m = lead.match(s);
      QStringList caps;
      if (m.hasMatch()) {
        const QString run = m.captured(0);
        for (const QChar c : run) caps << capHtml(QString(c), pal);
        const QString rest = s.mid(run.size());
        if (!rest.isEmpty()) caps << capHtml(rest, pal);
        // Apple prints ⇧⌘S with no joiner, but a tooltip is read at a glance and a run of
        // bare glyphs looks like one symbol — so every key gets the "+" here too.
        return caps.join(plus);
      }
      for (const QString& part : s.split('+'))
        if (!part.isEmpty()) caps << capHtml(macGlyphFor(part, mac), pal);
      return caps.join(plus);
    }

    // Escape `text` and put keycaps on the combos inside it. Combos written with "+" (or in
    // Apple glyphs) always count; a lone "Shift"/"Enter" only when the prose says it is a key
    // ("hold Shift", "press Esc") — otherwise the app's own verbs would wear keycaps.
    QString highlightKeys(const QString& text, const Palette& pal, bool mac) {
      const QRegularExpression re("(" + kKeyProse + ")?(" + chained() + "|" + glyphs() + "|" +
                                  kMod + "|" + kNamed + ")");
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

    // The palette the app-wide filter renders with; replaced on every theme change.
    Palette g_pal = themePalette(false);

  }  // namespace

  bool isKeyCombo(const QString& s) {
    // A lone key glyph counts here — "Close (⎋)" is a shortcut — but NOT in running prose,
    // where an arrow is usually a separator (see highlightKeys).
    const QRegularExpression re("^(?:" + chained() + "|" + glyphs() + "|" + kMod + "|" + kNamed +
                                "|" + kKeyGlyph + ")$");
    return re.match(s.trimmed()).hasMatch();
  }

  Tip parseTip(const QString& text) {
    Tip tip;
    QStringList lines;
    for (const QString& l : QString(text).remove('\r').split('\n'))
      if (!l.trimmed().isEmpty()) lines << l.trimmed();
    if (lines.isEmpty()) return tip;

    // ── the shortcut ──
    // The app appends " (combo)" to the END of the base text with the "— reason"
    // line after it, so the shortcut sits on the last non-reason line — not the
    // heading. A line that was nothing but the combo goes away with it.
    for (int i = lines.size() - 1; i >= 0; i--) {
      if (isNoteLine(lines[i])) continue;
      static const QRegularExpression paren("\\s*\\(([^()]*)\\)\\s*$");
      const auto m = paren.match(lines[i]);
      if (m.hasMatch()) {
        QStringList parts;
        for (const QString& p : m.captured(1).split(QRegularExpression("\\s*/\\s*|\\s+or\\s+")))
          if (!p.trimmed().isEmpty()) parts << p.trimmed();
        bool allKeys = !parts.isEmpty();
        for (const QString& p : parts) allKeys = allKeys && isKeyCombo(p);
        if (allKeys) {
          tip.keys = parts;
          const QString rest = lines[i].left(m.capturedStart()).trimmed();
          if (rest.isEmpty()) lines.removeAt(i);
          else lines[i] = rest;
        }
      }
      break;
    }
    if (lines.isEmpty()) return tip;

    // ── heading ──
    QString head = lines[0];
    const QStringList headParts = dotParts(head);          // "a · b" → heading + bullets
    const QStringList tail = headParts.mid(1);
    if (!headParts.isEmpty()) head = headParts.first();
    QString term, desc;
    if (dashSplit(head, &term, &desc)) {                   // "Shared project — <url>"
      tip.title = term;
      if (!desc.isEmpty()) tip.blocks.push_back({TipBlock::Kind::Hint, {}, desc});
    } else {
      tip.title = head;
    }
    for (const QString& t : tail) tip.blocks.push_back({TipBlock::Kind::Bullet, {}, t});

    // ── body ──
    for (int i = 1; i < lines.size(); i++) {
      const QString raw = lines[i];
      static const QRegularExpression note("^[\u2014\u2013-]{1,2}\\s+(.*)$");
      const auto nm = note.match(raw);
      if (nm.hasMatch()) {  // the disabled-reason line the app appends
        tip.blocks.push_back({TipBlock::Kind::Note, {}, nm.captured(1)});
        continue;
      }
      // A fully parenthesised line is a hint (the compare control's "(hold Alt+Shift+O …)").
      static const QRegularExpression wrapped("^\\(([^()]*)\\)$");
      const auto wm = wrapped.match(raw);
      const bool isHint = wm.hasMatch();
      const QString line = isHint ? wm.captured(1).trimmed() : raw;
      // A bullet marker is decoration — rows are drawn as a bulleted list anyway — so strip
      // it before deciding what the line IS.
      QString bare = line;
      static const QRegularExpression marker("^[\u2022*]\\s+");
      bare.remove(marker);
      // A row wins over the "·" split: "Vertical split — original left, edit right" is ONE
      // row whose description happens to list two halves, not two bullets.
      if (!isHint && dashSplit(bare, &term, &desc)) {
        tip.blocks.push_back({TipBlock::Kind::Row, term, desc});
        continue;
      }
      const QStringList parts = dotParts(bare);
      if (parts.size() > 1) {
        for (const QString& p : parts)
          tip.blocks.push_back({isHint ? TipBlock::Kind::Hint : TipBlock::Kind::Bullet, {}, p});
        continue;
      }
      if (bare != line) {  // a marked bullet with no description
        tip.blocks.push_back({TipBlock::Kind::Bullet, {}, bare});
        continue;
      }
      tip.blocks.push_back({isHint ? TipBlock::Kind::Hint : TipBlock::Kind::Text, {}, line});
    }
    return tip;
  }

  QString renderTip(const QString& text, const Palette& pal, bool mac) {
    const Tip tip = parseTip(text);
    if (tip.title.isEmpty() && tip.blocks.isEmpty()) return {};
    QString caps;
    for (const QString& k : tip.keys) caps += keysHtml(k, pal, mac);
    const QString title =
        tip.title.isEmpty() ? QString() : "<b>" + highlightKeys(tip.title, pal, mac) + "</b>";

    QString body;
    // Consecutive rows share one table so their two columns line up down the list; the same
    // for bullets in one list.
    QString open;
    auto close = [&] {
      if (open == "rows") body += "</table>";
      open.clear();
    };
    for (const TipBlock& b : tip.blocks) {
      if (b.kind == TipBlock::Kind::Row) {
        if (open != "rows") {
          close();
          body += "<table cellspacing=\"0\" cellpadding=\"1\">";
          open = "rows";
        }
        body += "<tr><td><b>• " + highlightKeys(b.term, pal, mac) + "</b>&nbsp;&nbsp;</td><td>" +
                highlightKeys(b.text, pal, mac) + "</td></tr>";
        continue;
      }
      if (b.kind == TipBlock::Kind::Bullet) {
        // A drawn "•" in a plain line, not a <ul>: Qt indents a real list by a whole
        // 40px step and counts it in the width even when the style drops it, marooning
        // a lone bullet mid-tooltip. Rows already draw their own marker this way.
        close();
        body += "<div style=\"margin-top:2px;\">• " + highlightKeys(b.text, pal, mac) + "</div>";
        continue;
      }
      close();
      const QString colour = b.kind == TipBlock::Kind::Note   ? pal.borderSel.name()
                             : b.kind == TipBlock::Kind::Hint ? pal.textMuted.name()
                                                              : pal.textMain.name();
      body += "<div style=\"color:" + colour + "; margin-top:3px;\">" +
              highlightKeys(b.text, pal, mac) + "</div>";
    }
    close();

    // A word-wrapped QLabel (which QToolTip is) does not lay text out to its natural
    // width: it searches for a roughly SQUARE block. Measuring the content and pinning
    // that width defeats the search; the cap is the browser's #app-tooltip max-width.
    const int width = [&] {
      QTextDocument doc;
      doc.setDefaultFont(QToolTip::font());
      // Margin ZERO, or the width comes back with the document's own 4px margins
      // baked in — and the tooltip renders INSIDE a document that adds them again,
      // so every extra pixel lands between the name and its right-pinned keycaps.
      doc.setDocumentMargin(0);
      doc.setHtml(title + (caps.isEmpty() ? QString() : "&nbsp;&nbsp;" + caps) + body);
      doc.setTextWidth(-1);
      return qBound(1, qCeil(doc.idealWidth()), 380);
    }();

    // The heading is a row of its own: name left, keycaps pinned right (browser
    // .tip-head). Both cells middle-aligned: a keycap is taller than the type, so
    // Qt's default (top) alignment makes the row read as crooked.
    QString head = title;
    if (!caps.isEmpty())
      head = "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\"><tr>"
             "<td style=\"vertical-align: middle;\">" + title +
             "</td><td align=\"right\" style=\"vertical-align: middle;\">" + caps +
             "</td></tr></table>";
    return "<table width=\"" + QString::number(width) +
           "\" cellspacing=\"0\" cellpadding=\"0\"><tr><td>" + head + body + "</td></tr></table>";
  }

  void setTooltipPalette(const Palette& pal) {
    g_pal = pal;
    // The rendered html carries LITERAL colours, so a tooltip built under the old
    // palette keeps it forever. Every enriched tooltip remembers the plain text it
    // came from, so they can all be rebuilt here.
    for (QWidget* w : QApplication::allWidgets()) {
      if (!w) continue;
      const QVariant plain = w->property(kPlainTipProperty);
      if (!plain.isValid()) continue;
      const QString rich = renderTip(plain.toString(), g_pal);
      if (!rich.isEmpty()) w->setToolTip(rich);
    }
  }

  bool hasKeycaps(const QString& richText) {
    return richText.contains(QLatin1String(kKeycapClass));
  }

  QString enrichedToolTip(const QString& plain) {
    const QString t = plain.trimmed();
    if (t.isEmpty() || t.startsWith('<')) return {};  // empty, or already someone's own HTML
    return renderTip(plain, g_pal);
  }

}
