// Parsing a composed `title` string into a Tip: the "·" alternatives, the " — " term/description
// split, the "— reason" note line and the sentence-casing, all twins of browser/js/ui/tipContent.js.
#include "tipContent.hpp"
#include <QRegularExpression>

namespace stencil::gui {

  namespace {
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

  }  // namespace

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
      if (!desc.isEmpty()) tip.blocks.push_back({TipBlock::Kind::HINT, {}, sentenceCase(desc)});
    } else {
      tip.title = head;
    }
    // A single trailing piece reads as a hint, not a bullet list of one.
    if (tail.size() > 1) {
      for (const QString& t : tail) tip.blocks.push_back({TipBlock::Kind::BULLET, {}, sentenceCase(t)});
    } else if (tail.size() == 1) {
      tip.blocks.push_back({TipBlock::Kind::HINT, {}, sentenceCase(tail.first())});
    }

    for (int i = 1; i < lines.size(); i++) {
      const QString raw = lines[i];
      static const QRegularExpression note("^[\u2014\u2013-]{1,2}\\s+(.*)$");
      const auto nm = note.match(raw);
      if (nm.hasMatch()) {  // the disabled-reason line the app appends
        tip.blocks.push_back({TipBlock::Kind::NOTE, {}, sentenceCase(nm.captured(1))});
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
        tip.blocks.push_back({TipBlock::Kind::ROW, term, desc});
        continue;
      }
      const QStringList parts = dotParts(bare);
      if (parts.size() > 1) {
        for (const QString& p : parts)
          tip.blocks.push_back({isHint ? TipBlock::Kind::HINT : TipBlock::Kind::BULLET, {}, sentenceCase(p)});
        continue;
      }
      if (bare != line) {  // a marked bullet with no description
        tip.blocks.push_back({TipBlock::Kind::BULLET, {}, sentenceCase(bare)});
        continue;
      }
      tip.blocks.push_back({isHint ? TipBlock::Kind::HINT : TipBlock::Kind::TEXT, {}, sentenceCase(line)});
    }
    return tip;
  }
}  // namespace stencil::gui
