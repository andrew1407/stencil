#pragma once
// The modal-chrome suite's sections, one TU each behind this header, called in this order
// from main(); both take the shown host widget every modal is parented to.
#include "modalChrome.hpp"
#include "OpenInDialog.hpp"
#include "ShimmerOverlay.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <cstdio>
#include <functional>

using namespace stencil::gui;

#include "../../support/check.hpp"

inline void pumpFor(int ms) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// Wait for the named modal to come up, then hand it to `act` (which must close it).
inline void whenModal(const char* name, std::function<void(QDialog*)> act) {
  QTimer::singleShot(0, [name, act] {
    for (int i = 0; i < 300; ++i) {
      auto* m = qobject_cast<QDialog*>(QApplication::activeModalWidget());
      if (m && m->objectName() == QLatin1String(name)) { act(m); return; }
      pumpFor(5);
    }
    std::printf("  [FAIL] modal %s never appeared\n", name);
    ++failures;
  });
}

inline QPushButton* btnByText(QWidget* root, const QString& text) {
  for (QPushButton* b : root->findChildren<QPushButton*>())
    if (b->text() == text) return b;
  return nullptr;
}

inline void pressEnter(QWidget* w) {
  QKeyEvent down(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
  QApplication::sendEvent(w, &down);
  QKeyEvent up(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
  QApplication::sendEvent(w, &up);
}

namespace modalchrome {

  void checkChooseAndPrompt(QWidget& host);
  void checkIconsAndFooter(QWidget& host);

}  // namespace modalchrome
