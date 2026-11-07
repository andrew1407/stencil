#pragma once
// The face a skin lends the application for its session, and the one it found given back. Header-only;
// one held font for every skin, as one skin is on at a time.
#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QStringList>

namespace stencil::support {

  namespace detail {
    inline QFont& priorFontState() { static QFont f; return f; }
    inline bool& fontHeldState() { static bool held = false; return held; }
  }  // namespace detail

  // The first family the host has of `families`; the last entry is the generic fallback.
  inline QFont skinFont(const QStringList& families, int px, bool antialiased) {
    const QStringList have = QFontDatabase::families();
    QString family = families.isEmpty() ? QString() : families.last();
    for (const QString& f : families)
      if (have.contains(f, Qt::CaseInsensitive)) { family = f; break; }
    QFont font(family);
    font.setPixelSize(px);   // the QSS sizes every control to the same number
    if (!antialiased) font.setStyleStrategy(QFont::NoAntialias);
    return font;
  }

  inline void holdSkinFont(const QFont& font) {
    if (!detail::fontHeldState()) {
      detail::priorFontState() = QApplication::font();
      detail::fontHeldState() = true;
    }
    QApplication::setFont(font);
  }

  inline void releaseSkinFont() {
    if (!detail::fontHeldState()) return;
    QApplication::setFont(detail::priorFontState());
    detail::fontHeldState() = false;
  }

}  // namespace stencil::support
