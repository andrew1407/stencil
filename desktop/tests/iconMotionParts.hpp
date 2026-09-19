#pragma once
// Shared ground for the icon-motion headless TUs: the frame renderer, the ink probes and the
// section each .cpp contributes. check.hpp's `failures` is inline, so the TUs share one count.
#include "iconMotion.hpp"
#include "idleCardMotion.hpp"   // the idle card mirrors the "image" entry by hand

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QEventLoop>
#include <QHash>
#include <QImage>
#include <QRect>
#include <QToolButton>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

#include "support/check.hpp"

using namespace stencil::gui;

constexpr int PX = 96;   // big enough for sub-unit moves to show up in the ink box

inline QImage frame(const QString& glyph, const IconMotionSpec& spec,
                    const QVector<IconMotionPart>& parts, double t) {
  const QString posed = iconMotionMarkup(glyph, spec, parts, t);
  return iconFromMarkup(posed, QColor(Qt::black), PX, 1.0)
      .pixmap(PX, PX)
      .toImage()
      .convertToFormat(QImage::Format_ARGB32);
}

inline QImage frame(const QString& glyph, const IconMotionSpec& spec, double t) {
  return frame(glyph, spec, spec.parts, t);
}

// The tight box of everything the glyph actually inked.
inline QRect inkBox(const QImage& im) {
  int l = im.width(), r = -1, t = im.height(), b = -1;
  for (int y = 0; y < im.height(); ++y)
    for (int x = 0; x < im.width(); ++x)
      if (qAlpha(im.pixel(x, y)) > 40) {
        l = std::min(l, x); r = std::max(r, x);
        t = std::min(t, y); b = std::max(b, y);
      }
  return r < 0 ? QRect() : QRect(QPoint(l, t), QPoint(r, b));
}

inline int inkCount(const QImage& im) {
  int n = 0;
  for (int y = 0; y < im.height(); ++y)
    for (int x = 0; x < im.width(); ++x)
      if (qAlpha(im.pixel(x, y)) > 40) ++n;
  return n;
}

// Topmost inked row within a column band — how high a part sits there.
inline int topIn(const QImage& im, double x0, double x1) {
  const int a = int(im.width() * x0), b = int(im.width() * x1);
  for (int y = 0; y < im.height(); ++y)
    for (int x = a; x < b; ++x)
      if (qAlpha(im.pixel(x, y)) > 40) return y;
  return im.height();
}

inline void pumpFor(int ms) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

inline bool pumpUntil(const std::function<bool()>& done, int budgetMs = 4000) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < budgetMs) {
    if (done()) return true;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
  }
  return done();
}

inline void hover(QWidget* w, bool in) {
  if (in) {
    QEnterEvent e(QPointF(2, 2), QPointF(2, 2), w->mapToGlobal(QPoint(2, 2)));
    QApplication::sendEvent(w, &e);
  } else {
    QEvent e(QEvent::Leave);
    QApplication::sendEvent(w, &e);
  }
}

void tableAgainstTheCanon(const QHash<QString, IconMotionSpec>& table);
void glyphMeanings();
void driverOnALiveButton();
void idleCardGlyph();
