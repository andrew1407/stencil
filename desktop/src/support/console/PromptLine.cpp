#include "PromptLine.hpp"

namespace stencil::support {

  void PromptLine::setText(const QString& text) {
    line = text;
    at = line.size();
  }

  void PromptLine::insert(const QString& text) {
    line.insert(at, text);
    at += text.size();
  }

  void PromptLine::backspace() {
    if (at <= 0) return;
    line.remove(--at, 1);
  }

  void PromptLine::del() {
    if (at >= line.size()) return;
    line.remove(at, 1);
  }

  namespace {
    bool wordChar(QChar c) { return !c.isSpace(); }
  }  // namespace

  int PromptLine::wordStart(int from) const {
    int p = qBound(0, from, int(line.size()));
    while (p > 0 && !wordChar(line.at(p - 1))) --p;
    while (p > 0 && wordChar(line.at(p - 1))) --p;
    return p;
  }

  int PromptLine::wordEnd(int from) const {
    int p = qBound(0, from, int(line.size()));
    while (p < line.size() && !wordChar(line.at(p))) ++p;
    while (p < line.size() && wordChar(line.at(p))) ++p;
    return p;
  }

  void PromptLine::deleteWordBack() {
    const int start = wordStart(at);
    line.remove(start, at - start);
    at = start;
  }

  void PromptLine::deleteWordForward() { line.remove(at, wordEnd(at) - at); }

  void PromptLine::killToStart() {
    line.remove(0, at);
    at = 0;
  }

  QString PromptLine::take(const std::function<QString(const QString&)>& remembered) {
    const QString out = line.trimmed();
    line.clear();
    at = 0;
    stash.clear();
    if (out.isEmpty()) return out;
    const QString kept = remembered ? remembered(out) : out;
    if (past.isEmpty() || past.last() != kept) past << kept;
    if (past.size() > MAX_HISTORY) past.removeFirst();
    cursor = past.size();
    return out;
  }

  void PromptLine::historyUp() {
    if (past.isEmpty()) return;
    if (cursor == past.size()) stash = line;
    if (cursor > 0) setText(past.at(--cursor));
  }

  void PromptLine::historyDown() {
    if (cursor < past.size()) ++cursor;
    setText(cursor < past.size() ? past.at(cursor) : stash);
  }

  void PromptLine::completeWith(const Completions& completions, const Lister& list) {
    if (!completions || at != line.size() || line.contains(QLatin1Char(' '))) return;
    const bool slashed = line.startsWith(QLatin1Char('/'));
    const QString base = (slashed ? line.mid(1) : line).toLower();
    const QStringList names = completions(base);
    if (names.isEmpty()) return;
    QString lcp = names.first();
    for (const QString& n : names) {
      int i = 0;
      while (i < lcp.size() && i < n.size() && lcp.at(i) == n.at(i)) ++i;
      lcp.truncate(i);
    }
    const QString slash = slashed ? QStringLiteral("/") : QString();
    if (names.size() == 1) setText(slash + names.first() + QLatin1Char(' '));
    else if (lcp.size() > base.size()) setText(slash + lcp);
    else if (list) list(names);
  }

}  // namespace stencil::support
