// The frame strips behind the desktop GIFs (captureGestures.cpp, captureEditing.cpp): numbered
// grabs of the window with any open modal drawn where it sits and a drawn pointer (a grab carries
// no cursor), the timed frame loop of the theme clip, the dusk scene, and a point in the canvas.
#pragma once

#include "captureShared.hpp"

#include <QElapsedTimer>
#include <QFile>
#include <QPainterPath>

inline constexpr int DWELL = 10;         // frames a state is held for, at the clip's 10 fps
inline constexpr int RING_FRAMES = 4;    // a press shows as a ring that grows and fades over these

// The arrow cursor, tip at `tip`; the same shape lib/shot/pointer.mjs draws over the web apps.
inline void drawPointer(QPainter& p, const QPointF& tip) {
  QPainterPath arrow;
  arrow.moveTo(0, 0);
  arrow.lineTo(0, 22);
  arrow.lineTo(6, 16.5);
  arrow.lineTo(10, 25.5);
  arrow.lineTo(13.5, 24);
  arrow.lineTo(9.6, 15.2);
  arrow.lineTo(17.5, 15.2);
  arrow.closeSubpath();
  p.save();
  p.setRenderHint(QPainter::Antialiasing, true);
  p.translate(tip);
  p.fillPath(arrow.translated(0.6, 1.2), QColor(0, 0, 0, 90));
  p.setPen(QPen(QColor("#111111"), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.setBrush(Qt::white);
  p.drawPath(arrow);
  p.restore();
}

// A press: a ring around the tip, `t` from 0 (just pressed) to 1 (faded out).
inline void drawPress(QPainter& p, const QPointF& tip, double t) {
  p.save();
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(QPen(QColor(255, 255, 255, int(240 * (1 - t))), 2.0));
  p.setBrush(Qt::NoBrush);
  p.drawEllipse(tip, 5 + 12 * t, 5 + 12 * t);
  p.restore();
}

// Numbered frames of the window as the screen shows it: a modal up is drawn where it opened.
class Strip {
 public:
  Strip(QWidget* win, const QString& name) : win(win), dir(framesDir() + "/" + name) {
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
  }
  // Where the pointer is (global), and a press there: the pointer is drawn from then on.
  void point(const QPoint& global) { at = global; shown = true; }
  void press(const QPoint& global) { point(global); ring = RING_FRAMES; }
  void frame(int times = 1) {
    for (int i = 0; i < times; ++i) {
      // A frame with nothing moving in it is a copy, so a long dwell costs one grab.
      if (i > 0 && ring == 0) { QFile::copy(path(count), path(count + 1)); ++count; continue; }
      grabOne();
    }
  }

 private:
  void grabOne() {
    QCoreApplication::processEvents();
    QImage shot = win->grab().toImage();
    QPainter p(&shot);
    if (QWidget* dlg = QApplication::activeModalWidget(); dlg && dlg->isVisible())
      p.drawPixmap(dlg->geometry().topLeft() - win->geometry().topLeft(), dlg->grab());
    const QPointF tip = win->mapFromGlobal(at);
    if (ring > 0) drawPress(p, tip, 1.0 - double(ring--) / RING_FRAMES);
    if (shown) drawPointer(p, tip);
    p.end();
    shot.save(path(++count), "PNG");
  }
  QString path(int i) const { return QStringLiteral("%1/frame-%2.png").arg(dir).arg(i, 4, 10, QLatin1Char('0')); }
  QWidget* win;
  QString dir;
  int count = 0;
  QPoint at;
  bool shown = false;
  int ring = 0;
};

// PNG frames every `everyMs` for `ms`, for the GIF the Node driver assembles. With `press` (a global
// point), the first frame shows the pointer resting there, then `act` runs as a click on it would.
inline void film(const QString& name, QWidget* w, int ms, int everyMs, const QPoint& press = QPoint(),
                 const std::function<void()>& act = {}) {
  const QString dir = framesDir() + "/" + name;
  QDir(dir).removeRecursively();
  QDir().mkpath(dir);
  const auto shoot = [&](int i, int ringFrame) {
    QImage shot = w->grab().toImage();
    if (!press.isNull()) {
      QPainter p(&shot);
      const QPointF tip = w->mapFromGlobal(press);
      if (ringFrame >= 0 && ringFrame < RING_FRAMES) drawPress(p, tip, double(ringFrame) / RING_FRAMES);
      drawPointer(p, tip);
    }
    shot.save(QStringLiteral("%1/frame-%2.png").arg(dir).arg(i, 4, 10, QLatin1Char('0')), "PNG");
  };
  int first = 1;
  if (act) {
    shoot(first++, -1);
    act();
  }
  QElapsedTimer t;
  t.start();
  for (int i = first; t.elapsed() < ms; ++i) {
    if (w->findChild<QTimer*>(QStringLiteral("toastLife"))) clearToasts(w);   // a "Saved" is not the subject
    shoot(i, i - first);
    // A grab can outlast a frame: the animation's timers still need the loop every time.
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    const qint64 due = qint64(i - first + 1) * everyMs;
    while (t.elapsed() < due) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
  }
  std::printf("  %s/ (frames)\n", qPrintable(name));
}

// A dusk over a field: flat fills would hide what a filter, a tint or the clean view changes.
inline QImage scene() {
  QImage img(960, 640, QImage::Format_RGB32);
  QPainter p(&img);
  QLinearGradient sky(0, 0, 0, 420);
  sky.setColorAt(0, QColor("#1e3a8a"));
  sky.setColorAt(1, QColor("#f59e0b"));
  p.fillRect(0, 0, 960, 420, sky);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(Qt::NoPen);
  p.setBrush(QColor("#fde68a"));
  p.drawEllipse(QPointF(700, 380), 90, 90);
  p.fillRect(0, 420, 960, 220, QColor("#166534"));
  return img;
}

inline QPoint inCanvas(QWidget* scroll, double fx, double fy) {
  return scroll->mapToGlobal(QPoint(int(scroll->width() * fx), int(scroll->height() * fy)));
}
