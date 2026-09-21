#pragma once
// Shared ground for the control-swap headless TUs: the pump helpers, the overlay/ink probes and
// the section each .cpp contributes. check.hpp's `failures` is inline, so they share one count.
#include "controlReveal.hpp"
#include "controlSwap.hpp"
#include "theme.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QAbstractItemView>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

#include <cstdio>
#include <functional>

#include "../../../support/check.hpp"

using stencil::gui::installControlSwap;
using stencil::gui::CHECK_SWAP_MS;
using stencil::gui::CHECK_SWAP_OBJECT_NAME;
using stencil::gui::CONTROL_SWAP_WIRED_PROPERTY;
using stencil::gui::NO_CONTROL_SWAP_PROPERTY;
using stencil::gui::VALUE_SWAP_PROPERTY;
using stencil::gui::swapCheckIndicator;
using stencil::gui::CONTROL_REVEAL_IN_MS;
using stencil::gui::FACE_SWAP_MS;
using stencil::gui::CONTROL_REVEAL_OUT_MS;
using stencil::gui::revealControls;
using stencil::gui::ValueSwapOverlay;

inline void pumpFor(int ms) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

// Wait for a condition rather than a clock: an offscreen animation driver ticks at
// whatever rate the harness gives it, and none of these assertions are about its speed.
inline bool pumpUntil(const std::function<bool()>& done, int budgetMs = 4000) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < budgetMs) {
    if (done()) return true;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
  }
  return done();
}

// The checkbox's scatter lives in the WINDOW, not in the box (particles have to leave
// the control's own 16px box to read as particles at all).
inline int liveCheckOverlays(QWidget* win) {
  return int(win->findChildren<QWidget*>(QString::fromLatin1(CHECK_SWAP_OBJECT_NAME)).size());
}

// How many pixels of `im` are neither transparent nor the backdrop — a cheap "is
// anything drawn here" probe for the offscreen grabs.
inline int inkedPixels(const QImage& im) {
  int n = 0;
  for (int y = 0; y < im.height(); ++y)
    for (int x = 0; x < im.width(); ++x)
      if (im.pixelColor(x, y).alpha() > 24) ++n;
  return n;
}

void checkScatter(QDialog& host, QCheckBox* box, const QRect& boxGeom);
void comboValueExchange(QDialog& host, QComboBox* combo, QVBoxLayout* lay, const QRect& comboGeom);
void optOutAndReducedMotion(QDialog& host, QVBoxLayout* lay, QCheckBox* box, QComboBox* combo,
                            const QRect& boxGeom, const QRect& comboGeom);
