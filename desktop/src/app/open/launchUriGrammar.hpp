#pragma once
// The launch URI's vocabulary, common/config/launchQuery.json through the qrc: the browser's own
// words, read once (cli twin: src/params/uriKeys.zig). Private to the launchUri TUs.
#include <QHash>
#include <QString>
#include <QStringList>

namespace stencil::gui::launchUri {

  struct Grammar {
    QHash<QString, QString> canonical;   // spelling → canonical key
    QStringList secret, docks, trueWords, falseWords;
    QHash<QString, QString> themes;
  };
  const Grammar& grammar();

}  // namespace stencil::gui::launchUri
