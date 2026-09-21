// Shared machinery for the appearance pins (tests/uiPins.*): where the baselines live,
// the settle/poll pumps, the tolerant image compare, and pin() itself — grab a widget
// and write or compare its per-platform, per-dpr baseline.
#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QPixmap>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <cstdio>
#include <functional>

#include "check.hpp"

#if defined(Q_OS_MACOS)
inline const char* PLATFORM = "macos";
#elif defined(Q_OS_WIN)
inline const char* PLATFORM = "windows";
#else
inline const char* PLATFORM = "linux";
#endif

inline QString pinsDir() { return QStringLiteral(STENCIL_UI_PINS_DIR); }
inline QString shotsDir() { return pinsDir() + "/" + QLatin1String(PLATFORM); }

inline bool updating() { return qEnvironmentVariable("STENCIL_UPDATE_UI_PINS") == QLatin1String("1"); }

inline void pumpFor(int ms) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// Poll `ready` instead of sleeping a fixed span (the QTRY* idea, usable outside QtTest).
inline bool waitUntil(const std::function<bool()>& ready, int timeoutMs = 4000) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < timeoutMs) {
    if (ready()) return true;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
  return ready();
}

// Two shots match when no more than 0.2% of pixels differ by more than 8/255 on a
// channel — enough slack for antialiasing, far too little to hide a layout change.
inline bool imagesMatch(const QImage& a, const QImage& b, QString* why) {
  if (a.size() != b.size()) {
    *why = QStringLiteral("size %1x%2 vs %3x%4")
               .arg(a.width()).arg(a.height()).arg(b.width()).arg(b.height());
    return false;
  }
  const QImage x = a.convertToFormat(QImage::Format_ARGB32);
  const QImage y = b.convertToFormat(QImage::Format_ARGB32);
  qint64 bad = 0;
  for (int row = 0; row < x.height(); ++row) {
    const QRgb* px = reinterpret_cast<const QRgb*>(x.constScanLine(row));
    const QRgb* py = reinterpret_cast<const QRgb*>(y.constScanLine(row));
    for (int col = 0; col < x.width(); ++col) {
      if (qAbs(qRed(px[col]) - qRed(py[col])) > 8 || qAbs(qGreen(px[col]) - qGreen(py[col])) > 8 ||
          qAbs(qBlue(px[col]) - qBlue(py[col])) > 8 || qAbs(qAlpha(px[col]) - qAlpha(py[col])) > 8)
        ++bad;
    }
  }
  const qint64 total = qint64(x.width()) * x.height();
  const double ratio = total ? double(bad) / double(total) : 0.0;
  if (ratio <= 0.002) return true;
  *why = QStringLiteral("%1 px differ (%2%)").arg(bad).arg(ratio * 100, 0, 'f', 3);
  return false;
}

// Adopting a loaded image flashes a "Saved" toast, and a toast lives 3s — long enough
// to drift into a later grab. Retire every live one first (they carry a "toastLife" timer).
inline void clearToasts(QWidget* host) {
  for (QTimer* life : host->findChildren<QTimer*>(QStringLiteral("toastLife")))
    if (QWidget* toast = qobject_cast<QWidget*>(life->parent())) delete toast;
  pumpFor(20);
}

inline int shotCount = 0;

// Grab `w`, then write or compare its baseline. The file carries the dpr it was taken
// at: offscreen defaults to 1, which hides every hi-dpi bug, so both are pinned.
inline void pin(const char* state, QWidget* w) {
  ++shotCount;
  if (!w) {
    check(false, state);
    std::printf("      no widget to grab\n");
    return;
  }
  pumpFor(30);
  const QPixmap shot = w->grab();
  const int dpr = qRound(shot.devicePixelRatio());
  const QString path = QStringLiteral("%1/%2@%3x.png").arg(shotsDir(), QLatin1String(state)).arg(dpr);
  if (updating()) {
    QDir().mkpath(shotsDir());
    check(shot.save(path, "PNG"), state);
    return;
  }
  QImage baseline;
  if (!baseline.load(path)) {
    check(false, state);
    std::printf("      no baseline at %s (rewrite with STENCIL_UPDATE_UI_PINS=1)\n",
                qPrintable(path));
    return;
  }
  QString why;
  const bool ok = imagesMatch(shot.toImage(), baseline, &why);
  check(ok, state);
  if (!ok) {
    const QString actual = QDir::temp().filePath(QStringLiteral("stencil_uipin_%1@%2x.png")
                                                     .arg(QLatin1String(state)).arg(dpr));
    shot.save(actual, "PNG");
    std::printf("      %s — shot written to %s\n", qPrintable(why), qPrintable(actual));
  }
}
