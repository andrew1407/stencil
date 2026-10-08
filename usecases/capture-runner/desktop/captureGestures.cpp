// The chrome drags, filmed step by step for the GIFs desktop.mjs assembles: a toolbar icon carried
// out to open its dialog where it drops, the header mark dropped for the clean view, the theme
// lens, and one colour chip dropped on another. Driven as tests/app/drag/iconDragGui.hpp drives
// the suites, so the real drag machines run.
#include "captureShared.hpp"

#include "CanvasWidget.hpp"
#include "app/drag/iconDragGui.hpp"

#include <QFile>
#include <QScrollArea>
#include <QToolButton>

using namespace stencil::gui;
using namespace stencil::guitest;

namespace {
  constexpr int CARRY_STEPS = 12;   // frames along one carry
  constexpr int DWELL = 10;         // frames a state is held for, at the clip's 10 fps

  // Numbered frames of the window as the screen shows it: a modal up is drawn where it opened.
  class Strip {
   public:
    Strip(QWidget* win, const QString& name) : win(win), dir(framesDir() + "/" + name) {
      QDir(dir).removeRecursively();
      QDir().mkpath(dir);
    }
    void frame(int times = 1) {
      QCoreApplication::processEvents();
      QImage shot = win->grab().toImage();
      if (QWidget* dlg = QApplication::activeModalWidget(); dlg && dlg->isVisible()) {
        QPainter p(&shot);
        p.drawPixmap(dlg->geometry().topLeft() - win->geometry().topLeft(), dlg->grab());
      }
      const QString first = path(++count);
      shot.save(first, "PNG");
      for (int i = 1; i < times; ++i) QFile::copy(first, path(++count));
    }

   private:
    QString path(int i) const { return QStringLiteral("%1/frame-%2.png").arg(dir).arg(i, 4, 10, QLatin1Char('0')); }
    QWidget* win;
    QString dir;
    int count = 0;
  };

  // Lifted past the slop, then carried from where it is to `to`, a frame per step.
  void carry(Strip& strip, QWidget* source, const QPoint& to) {
    const QPoint from = iconCentre(source);
    iconMouse(source, QEvent::MouseButtonPress, from);
    strip.frame(3);
    for (int i = 1; i <= CARRY_STEPS; ++i) {
      const double t = double(i) / CARRY_STEPS;
      const double ease = t * t * (3 - 2 * t);
      iconMouse(source, QEvent::MouseMove, from + (to - from) * ease + QPoint(0, i == 1 ? 40 : 0));
      strip.frame();
    }
  }

  void release(QWidget* source, const QPoint& at) { dropIcon(source, at); }

  QToolButton* switchFor(const QWidget& win, const QAction* act) {
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->defaultAction() == act && b->isVisible()) return b;
    return nullptr;
  }

  // A dusk over a field: flat fills would hide what a filter, a tint or the clean view changes.
  QImage scene() {
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

  QPoint inCanvas(QWidget* scroll, double fx, double fy) {
    return scroll->mapToGlobal(QPoint(int(scroll->width() * fx), int(scroll->height() * fy)));
  }
}  // namespace

void MainWindowGuiTest::gestureShots(MainWindow& win, const ShotSet& shots) {
  auto* canvas = win.findChild<CanvasWidget*>();
  const auto dress = [&] {
    win.parts.sourceOpener.createBlankImage(Qt::white, 960, 640);
    waitUntil([canvas] { return canvas->hasImage(); });
    canvas->loadFromImage(scene());
    win.acts.showLines->setChecked(true);
    win.acts.showPoints->setChecked(false);
    canvas->setLines({line({{140, 150}, {520, 260}, {330, 520}}, "#c81e1e", 4, "solid"),
                      line({{610, 170}, {860, 540}}, "#1e63c8", 3, "dashed")});
    fit(win);
    pumpFor(300);
  };

  if (shots.has("drag-icon-dialog")) {
    dress();
    QWidget* icon = win.tools.openImageBtn;
    if (!icon) std::printf("  drag-icon-dialog SKIPPED (no icon)\n");
    else {
      Strip strip(&win, QStringLiteral("drag-icon-dialog"));
      strip.frame(DWELL / 2);
      const QPoint drop = inCanvas(win.scroll, 0.22, 0.015);
      carry(strip, icon, drop);
      strip.frame(DWELL / 2);
      // Polled, not waited for: the drop exec()s the dialog inside the release's own event turn.
      bool held = false;
      QTimer poll;
      poll.setInterval(20);
      QObject::connect(&poll, &QTimer::timeout, [&] {
        QWidget* dlg = QApplication::activeModalWidget();
        if (!dlg || !dlg->isVisible()) return;
        poll.stop();
        pumpFor(300);
        strip.frame(DWELL * 2);
        held = true;
        if (auto* d = qobject_cast<QDialog*>(dlg)) d->reject(); else dlg->close();
      });
      poll.start();
      release(icon, drop);
      waitUntil([&] { return held; }, 6000);
      pumpFor(200);
      std::printf("  drag-icon-dialog/ (frames)%s\n", held ? "" : " — NO DIALOG");
    }
  }

  if (shots.has("drag-logo-clean")) {
    dress();
    win.applyImageFilter(QStringLiteral("sepia"));
    win.acts.showPoints->setChecked(true);
    pumpFor(200);
    clearToasts(&win);
    Strip strip(&win, QStringLiteral("drag-logo-clean"));
    strip.frame(DWELL / 2);
    const QPoint drop = inCanvas(win.scroll, 0.5, 0.5);
    carry(strip, win.tools.logoBtn, drop);
    strip.frame(DWELL);
    release(win.tools.logoBtn, drop);
    pumpFor(200);
    clearToasts(&win);
    strip.frame(DWELL * 2);
    std::printf("  drag-logo-clean/ (frames)\n");
  }

  if (shots.has("theme-lens")) {
    dress();
    QToolButton* button = switchFor(win, win.acts.theme);
    if (!button) std::printf("  theme-lens SKIPPED (no switch)\n");
    else {
      Strip strip(&win, QStringLiteral("theme-lens"));
      strip.frame(DWELL / 2);
      carry(strip, button, inCanvas(win.scroll, 0.45, 0.35));
      strip.frame(DWELL / 2);
      for (int i = 1; i <= CARRY_STEPS; ++i) {
        const double t = double(i) / CARRY_STEPS;
        iconMouse(button, QEvent::MouseMove, inCanvas(win.scroll, 0.45 + 0.15 * t, 0.35 + 0.25 * t));
        strip.frame();
      }
      strip.frame(DWELL);
      release(button, inCanvas(win.scroll, 0.6, 0.6));
      pumpFor(200);
      strip.frame(DWELL);
      std::printf("  theme-lens/ (frames)\n");
    }
  }

  if (shots.has("drag-color")) {
    dress();
    win.applyImageFilter(QStringLiteral("custom"));
    win.applyTintColor(QColor("#7c3aed"));
    pumpFor(200);
    clearToasts(&win);
    QWidget* from = win.tools.lineColorBtn;
    QWidget* onto = win.tools.filterColorBtn;
    if (QApplication::widgetAt(iconCentre(onto)) != onto)
      std::printf("  drag-color: the tint chip is not under its own centre\n");
    Strip strip(&win, QStringLiteral("drag-color"));
    strip.frame(DWELL / 2);
    carry(strip, from, iconCentre(onto));
    strip.frame(DWELL);
    release(from, iconCentre(onto));
    pumpFor(200);
    clearToasts(&win);
    strip.frame(DWELL * 2);
    std::printf("  drag-color/ (frames)\n");
  }
}
