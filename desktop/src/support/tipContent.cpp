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

#include <QAction>
#include <QEvent>
#include <QKeySequence>
#include <QPointer>
#include <QToolTip>
#include <QWidget>
#include <QRegularExpression>

namespace stencil::gui {

  namespace {

    // Character for character the browser's (tipContent.js). Case-SENSITIVE on purpose:
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

    // " · " lists alternatives, " — " splits term from description — NOT inside parentheses.
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
    // Only a plain lowercase first WORD is lifted: a token with a dot, slash, colon,
    // bracket or quote is a URL/filename/code fragment. Browser twin: tipContent.js sentenceCase.
    QString sentenceCase(const QString& s) {
      static const QRegularExpression ws("\\s");
      const int end = s.indexOf(ws);   // no second token: a VALUE, not a sentence
      if (end < 1) return s;
      static const QRegularExpression word("^[a-z][a-z-]*$");
      if (!word.matchView(QStringView(s).left(end)).hasMatch()) return s;
      QString out = s;
      out[0] = out[0].toUpper();
      return out;
    }

    bool isNoteLine(const QString& l) {
      static const QRegularExpression re("^[\u2014\u2013-]{1,2}\\s+");
      return re.match(l).hasMatch();
    }

    // On a Mac a modifier is DRAWN. Ctrl → ⌃, not ⌘: a spelled "Ctrl" is a literal Control key.
    QString macGlyphFor(const QString& key, bool mac) {
      if (!mac) return key;
      static const QHash<QString, QString> MAP{
          {"Ctrl", "\u2303"},  {"Control", "\u2303"},  {"Alt", "\u2325"},     {"Option", "\u2325"},
          {"Shift", "\u21E7"}, {"Cmd", "\u2318"},      {"Command", "\u2318"}, {"Meta", "\u2318"},
      };
      return MAP.value(key, key);
    }

    // PAINTED as an <img> data URI: Qt rich text gives a span only a background.
    // `joiner` = the "+" between caps: no face, muted, same picture so it centres.
    QString capHtml(const QString& label, const Palette& pal, bool joiner = false,
                    qreal scale = 1.0) {
      // Browser: 14px cap over 12px prose. `scale` < 1 is a table-cell cap (comboKeycapsHtml).
      QFont f = QToolTip::font();
      f.setBold(!joiner);
      if (f.pointSizeF() > 0) f.setPointSizeF(f.pointSizeF() * 1.15 * scale);
      else f.setPixelSize(qMax(1, qRound(f.pixelSize() * 1.15 * scale)));

      const QScreen* scr = QGuiApplication::primaryScreen();
      const qreal dpr = qBound(1.0, scr ? scr->devicePixelRatio() : 1.0, 3.0);
      const QString key = label + (joiner ? "|+|" : "|k|") + f.toString() + '|' +
                          QString::number(dpr) + '|' + QString::number(scale) + '|' +
                          pal.bgContainer.name(QColor::HexArgb) +   // a transparent face is a face
                          pal.borderMain.name() + pal.textKey.name() + pal.textMuted.name();
      static QHash<QString, QString> cache;
      const auto hit = cache.constFind(key);
      if (hit != cache.constEnd()) return *hit;

      const QFontMetricsF fm(f);
      const qreal padX = 6 * scale, padY = 3 * scale, radius = 5, gapX = 3 * scale;
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
          // A see-through face must CLEAR the border slab — a transparent brush paints nothing.
          if (pal.bgContainer.alpha() == 0) {
            p.setCompositionMode(QPainter::CompositionMode_Clear);
            p.setBrush(Qt::black);
          }
          p.drawRoundedRect(r.adjusted(1, 1, -1, -2), radius - 1, radius - 1);  // 2px bottom edge
          p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        }
        p.setFont(f);
        p.setPen(joiner ? pal.textMuted : pal.textKey);
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

    QString keysHtml(const QString& combo, const Palette& pal, bool mac, qreal scale = 1.0) {
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

    Palette g_pal = themePalette(false);

  }  // namespace

  bool isKeyCombo(const QString& s) {
    // A lone key glyph counts here — "Close (⎋)" — but NOT in running prose (highlightKeys).
    const QRegularExpression re("^(?:" + chained() + "|" + glyphs() + "|" + MOD + "|" + NAMED +
                                "|" + KEY_GLYPH + ")$");
    return re.match(s.trimmed()).hasMatch();
  }

  Tip parseTip(const QString& text) {
    Tip tip;
    QStringList lines;
    for (const QString& l : QString(text).remove('\r').split('\n'))
      if (!l.trimmed().isEmpty()) lines << l.trimmed();
    if (lines.isEmpty()) return tip;

    // The shortcut sits on the last non-reason line, not the heading (the app appends
    // " (combo)" then the "— reason" line).
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

    QString head = lines[0];
    const QStringList headParts = dotParts(head);          // "a · b" → heading + bullets
    const QStringList tail = headParts.mid(1);
    if (!headParts.isEmpty()) head = headParts.first();
    QString term, desc;
    if (dashSplit(head, &term, &desc)) {                   // "Shared project — <url>"
      tip.title = term;
      if (!desc.isEmpty()) tip.blocks.push_back({TipBlock::Kind::Hint, {}, sentenceCase(desc)});
    } else {
      tip.title = head;
    }
    // A single trailing piece reads as a hint, not a bullet list of one.
    if (tail.size() > 1) {
      for (const QString& t : tail) tip.blocks.push_back({TipBlock::Kind::Bullet, {}, sentenceCase(t)});
    } else if (tail.size() == 1) {
      tip.blocks.push_back({TipBlock::Kind::Hint, {}, sentenceCase(tail.first())});
    }

    for (int i = 1; i < lines.size(); i++) {
      const QString raw = lines[i];
      static const QRegularExpression note("^[\u2014\u2013-]{1,2}\\s+(.*)$");
      const auto nm = note.match(raw);
      if (nm.hasMatch()) {  // the disabled-reason line the app appends
        tip.blocks.push_back({TipBlock::Kind::Note, {}, sentenceCase(nm.captured(1))});
        continue;
      }
      static const QRegularExpression wrapped("^\\(([^()]*)\\)$");
      const auto wm = wrapped.match(raw);
      const bool isHint = wm.hasMatch();
      const QString line = isHint ? wm.captured(1).trimmed() : raw;
      QString bare = line;
      static const QRegularExpression marker("^[\u2022*]\\s+");
      bare.remove(marker);
      // A row wins over the "·" split: a description may list two halves.
      if (!isHint && dashSplit(bare, &term, &desc)) {
        tip.blocks.push_back({TipBlock::Kind::Row, term, desc});
        continue;
      }
      const QStringList parts = dotParts(bare);
      if (parts.size() > 1) {
        for (const QString& p : parts)
          tip.blocks.push_back({isHint ? TipBlock::Kind::Hint : TipBlock::Kind::Bullet, {}, sentenceCase(p)});
        continue;
      }
      if (bare != line) {  // a marked bullet with no description
        tip.blocks.push_back({TipBlock::Kind::Bullet, {}, sentenceCase(bare)});
        continue;
      }
      tip.blocks.push_back({isHint ? TipBlock::Kind::Hint : TipBlock::Kind::Text, {}, sentenceCase(line)});
    }
    return tip;
  }

  QString renderTip(const QString& text, const Palette& pal, bool mac, const QFont* font) {
    const Tip tip = parseTip(text);
    if (tip.title.isEmpty() && tip.blocks.isEmpty()) return {};
    QString caps;
    for (const QString& k : tip.keys) caps += keysHtml(k, pal, mac);
    const QString title =
        tip.title.isEmpty() ? QString() : "<b>" + highlightKeys(tip.title, pal, mac) + "</b>";

    QString body;
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
        // A drawn "•", not a <ul>: Qt indents a real list by 40px and counts it in the width.
        close();
        body += "<div style=\"margin-top:2px;\">• " + highlightKeys(b.text, pal, mac) + "</div>";
        continue;
      }
      close();
      // Browser .tip-note: --warning, 5px above.
      const bool note = b.kind == TipBlock::Kind::Note;
      const QString colour = note                             ? pal.warning.name()
                             : b.kind == TipBlock::Kind::Hint ? pal.textMuted.name()
                                                              : pal.textMain.name();
      body += "<div style=\"color:" + colour + "; margin-top:" + (note ? "5" : "3") + "px;\">" +
              highlightKeys(b.text, pal, mac) + "</div>";
    }
    close();

    // A word-wrapped QLabel lays out to a roughly SQUARE block; pinning the measured
    // width defeats that. The cap is the browser's #app-tooltip max-width.
    const int width = [&] {
      QTextDocument doc;
      doc.setDefaultFont(font ? *font : QToolTip::font());
      // Margin ZERO, or the document's 4px margins are baked in and added again at render.
      doc.setDocumentMargin(0);
      doc.setHtml(title + (caps.isEmpty() ? QString() : "&nbsp;&nbsp;" + caps) + body);
      doc.setTextWidth(-1);
      return qBound(1, qCeil(doc.idealWidth()), 380);
    }();

    // Browser .tip-head. Middle-aligned because a keycap is taller than the type; the
    // caps cell never breaks, a chord is one thing.
    QString head = title;
    if (!caps.isEmpty())
      head = "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\"><tr>"
             "<td style=\"vertical-align: middle;\">" + title +
             "</td><td align=\"right\" style=\"vertical-align: middle; white-space: nowrap;\">" +
             caps + "</td></tr></table>";
    return "<table width=\"" + QString::number(width) +
           "\" cellspacing=\"0\" cellpadding=\"0\"><tr><td>" + head + body + "</td></tr></table>";
  }

  void setTooltipPalette(const Palette& pal) {
    g_pal = pal;
    // The html carries LITERAL colours, so every enriched tooltip is rebuilt from its plain text.
    for (QWidget* w : QApplication::allWidgets()) {
      if (!w) continue;
      const QVariant plain = w->property(PLAIN_TIP_PROPERTY);
      if (!plain.isValid()) continue;
      const QString rich = renderTip(plain.toString(), g_pal);
      if (!rich.isEmpty()) w->setToolTip(rich);
    }
  }

  bool hasKeycaps(const QString& richText) {
    return richText.contains(QLatin1String(KEYCAP_CLASS));
  }

  QString blankKeycaps(const QString& richText) {
    static const QRegularExpression cap("(<img alt=\"[^\"]*\" class=\"" +
                                        QString::fromLatin1(KEYCAP_CLASS) +
                                        "\"[^>]*base64,)[^\"]*(\">)");
    static const QString empty = [] {  // 1x1 transparent, drawn to whatever the box asks
      QImage img(1, 1, QImage::Format_ARGB32);
      img.fill(Qt::transparent);
      QByteArray png;
      QBuffer buf(&png);
      buf.open(QIODevice::WriteOnly);
      img.save(&buf, "PNG");
      return QString::fromLatin1(png.toBase64());
    }();
    QString out = richText;
    out.replace(cap, "\\1" + empty + "\\2");
    return out == richText ? QString() : out;
  }

  QString enrichedToolTip(const QString& plain, const QFont* font) {
    const QString t = plain.trimmed();
    if (t.isEmpty() || t.startsWith('<')) return {};  // empty, or already someone's own HTML
    return renderTip(plain, g_pal, ON_MAC, font);
  }

  // Composed control tooltips (browser utils.js composeControlTitle)

  QString composeControlTitle(const QString& base, const QString& combo, bool disabled,
                              const QString& reason) {
    QString out = base;
    if (!combo.isEmpty()) out += (out.isEmpty() ? "" : " ") + QString("(%1)").arg(combo);
    if (disabled && !reason.isEmpty()) out += (out.isEmpty() ? "" : "\n") + QString("\u2014 ") + reason;
    return out;
  }

  namespace {

    // Or, for a plain-tooltip control, that tooltip stripped of "(combo)" and "— reason" (browser data-title).
    QString tipBaseOf(QObject* t) {
      const QVariant v = t->property(TIP_BASE_PROPERTY);
      if (v.isValid()) return v.toString();
      QString base = t->property(PLAIN_TIP_PROPERTY).toString();
      if (base.isEmpty()) {
        if (auto* a = qobject_cast<QAction*>(t)) base = a->toolTip();
        else if (auto* w = qobject_cast<QWidget*>(t)) base = w->toolTip();
      }
      if (base.trimmed().startsWith('<')) base.clear();   // someone's own HTML: no base to read
      static const QRegularExpression note("\\n[\\s\\S]*$");
      base.remove(note);
      static const QRegularExpression paren("\\s*\\(([^()]*)\\)\\s*$");
      const auto m = paren.match(base);
      if (m.hasMatch() && isKeyCombo(m.captured(1))) base = base.left(m.capturedStart()).trimmed();
      t->setProperty(TIP_BASE_PROPERTY, base);
      return base;
    }

    QString tipComboOf(QObject* t) {
      QAction* a = qobject_cast<QAction*>(t);
      if (!a) a = qobject_cast<QAction*>(t->property(TIP_HOTKEY_PROPERTY).value<QObject*>());
      return a ? a->shortcut().toString(QKeySequence::NativeText) : QString();
    }

    // A widget's enabled state has no signal; this rides its EnabledChange event.
    class TipStateWatch : public QObject {
     public:
      explicit TipStateWatch(QWidget* w) : QObject(w) {}
     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::EnabledChange) syncControlTip(o);
        return QObject::eventFilter(o, e);
      }
    };

    void wireControlTip(QObject* t) {
      static constexpr const char* WIRED = "stencilTipWired";
      if (t->property(WIRED).toBool()) return;
      t->setProperty(WIRED, true);
      if (auto* a = qobject_cast<QAction*>(t))
        QObject::connect(a, &QAction::changed, a, [a] { syncControlTip(a); });
      else if (auto* w = qobject_cast<QWidget*>(t))
        w->installEventFilter(new TipStateWatch(w));
    }

  }  // namespace

  void setTipBase(QObject* target, const QString& base) {
    if (!target) return;
    target->setProperty(TIP_BASE_PROPERTY, base);
    wireControlTip(target);
    syncControlTip(target);
  }

  void setTipReason(QObject* target, const QString& reason) {
    if (!target) return;
    tipBaseOf(target);   // pin the heading before the note can land on the tooltip
    target->setProperty(TIP_REASON_PROPERTY, reason);
    wireControlTip(target);
    syncControlTip(target);
  }

  void setTipHotkey(QWidget* target, QAction* hotkey) {
    if (!target) return;
    tipBaseOf(target);
    target->setProperty(TIP_HOTKEY_PROPERTY, QVariant::fromValue<QObject*>(hotkey));
    wireControlTip(target);
    if (hotkey) {
      QPointer<QWidget> w(target);
      QObject::connect(hotkey, &QAction::changed, target, [w] { if (w) syncControlTip(w); });
    }
    syncControlTip(target);
  }

  void syncControlTip(QObject* target) {
    if (!target) return;
    if (!target->property(TIP_BASE_PROPERTY).isValid()) return;
    const QString reason = target->property(TIP_REASON_PROPERTY).toString();
    QString tip;
    if (auto* a = qobject_cast<QAction*>(target)) {
      tip = composeControlTitle(tipBaseOf(a), tipComboOf(a), !a->isEnabled(), reason);
      a->setToolTip(tip);   // a no-op when unchanged, so the `changed` it emits cannot loop
      return;
    }
    auto* w = qobject_cast<QWidget*>(target);
    if (!w) return;
    tip = composeControlTitle(tipBaseOf(w), tipComboOf(w), !w->isEnabled(), reason);
    // The live tooltip may already be the RENDERED form of this text; compare against the plain.
    if (w->property(PLAIN_TIP_PROPERTY).toString() == tip || w->toolTip() == tip) return;
    w->setToolTip(tip);
  }

  Palette currentPalette() { return g_pal; }

  QString comboKeycapsHtml(const QString& combo, const Palette& pal, bool mac, qreal scale) {
    return keysHtml(combo, pal, mac, scale);
  }

}
