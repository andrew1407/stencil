#pragma once

// A hotkey string from hotkeysConfig.json, spelled the way this platform's QKeySequence reads it.

#include <QLatin1String>
#include <QString>

namespace stencil::gui {

  // On macOS the primary delete key emits Backspace, so "Alt+Delete" binds to Backspace (browser platformizeCombo).
  inline QString platformizeSeq(QString seq) {
#ifdef Q_OS_MACOS
    seq.replace(QStringLiteral("Delete"), QStringLiteral("Backspace"),
                Qt::CaseInsensitive);
#endif
    return seq;
  }

  // hotkeysConfig.json spells arrows the browser way ("Alt+ArrowUp"); QKeySequence reads "Alt+Up".
  inline QString qtKeySeq(QString seq) {
    for (const char* dir : {"Up", "Down", "Left", "Right"})
      seq.replace(QStringLiteral("Arrow") + QLatin1String(dir), QLatin1String(dir));
    return seq;
  }

}  // namespace stencil::gui
