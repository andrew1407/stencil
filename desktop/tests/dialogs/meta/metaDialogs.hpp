#pragma once
// The meta dialog suites' common ground (the description and keywords editors): a pump, a button
// by its text, the header's glyph, and a key pressed and released.
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>

namespace metatest {

  inline void pumpFor(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }

  inline QPushButton* btnByText(QWidget* root, const QString& text) {
    for (QPushButton* b : root->findChildren<QPushButton*>())
      if (b->text() == text) return b;
    return nullptr;
  }

  inline bool headerHasGlyph(QWidget* root) {
    for (QLabel* l : root->findChildren<QLabel*>())
      if (!l->pixmap().isNull()) return true;
    return false;
  }

  inline void pressKey(QWidget* w, Qt::Key key, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QKeyEvent down(QEvent::KeyPress, key, mods);
    QApplication::sendEvent(w, &down);
    QKeyEvent up(QEvent::KeyRelease, key, mods);
    QApplication::sendEvent(w, &up);
  }

}  // namespace metatest

using namespace metatest;
