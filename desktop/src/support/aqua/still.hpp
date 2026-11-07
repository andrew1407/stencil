#pragma once
// The aqua skin's still pointer (browser css/aqua/chrome.css "Nothing sweeps, glows, lifts…"): a
// sheet with its hover selectors taken out, so a hovered control keeps its rest face. Menu and
// popup-list rows keep theirs: the gel bar under the pointer is the era's own.
#include "../skinPrefs.hpp"

#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace stencil::support {

  inline bool isHoverSelector(const QString& sel) {
    if (!sel.contains(QLatin1String(":hover")) && !sel.contains(QLatin1String("[hovered=\"true\"]"))) return false;
    for (const char* row : {"QMenu", "QAbstractItemView::item", "searchComboList::item"})
      if (sel.contains(QLatin1String(row))) return false;
    return true;
  }

  // A sheet of bare declarations (no braces) has no selector to drop and comes back as it is.
  inline QString withoutHover(const QString& sheet) {
    static const QRegularExpression comment(QStringLiteral("/\\*.*?\\*/"),
                                            QRegularExpression::DotMatchesEverythingOption);
    if (!sheet.contains('{')) return sheet;
    QString text = sheet;
    text.remove(comment);
    QString out;
    out.reserve(text.size());
    int from = 0;
    for (int open = text.indexOf('{'); open >= 0; open = text.indexOf('{', from)) {
      const int close = text.indexOf('}', open);
      if (close < 0) break;
      QStringList kept;
      for (const QString& sel : text.mid(from, open - from).split(',')) {
        const QString s = sel.trimmed();
        if (!s.isEmpty() && !isHoverSelector(s)) kept << s;
      }
      if (!kept.isEmpty()) out += kept.join(QStringLiteral(", ")) + text.mid(open, close - open + 1) + '\n';
      from = close + 1;
    }
    return out;
  }

  inline QString stillUnderAqua(const QString& sheet) { return isAqua() ? withoutHover(sheet) : sheet; }

}  // namespace stencil::support
