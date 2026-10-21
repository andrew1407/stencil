// The chrome gestures, filmed step by step for the GIFs desktop.mjs assembles: a toolbar icon carried
// out to open its window where it drops, the header mark dropped for the clean view, the theme
// lens, one colour swatch dropped on another, and the fullscreen toolbar at the top edge. Driven as
// tests/app/drag/iconDragGui.hpp drives the suites, so the real drag machines run.
#include "captureStrip.hpp"

#include "CanvasWidget.hpp"
#include "app/drag/iconDragGui.hpp"

#include <QCursor>
#include <QScrollArea>
#include <QToolBar>
#include <QToolButton>

using namespace stencil::gui;
using namespace stencil::guitest;

namespace {
  constexpr int CARRY_STEPS = 12;   // frames along one carry

  // Lifted past the slop, then carried from where it is to `to`, a frame per step.
  void carry(Strip& strip, QWidget* source, const QPoint& to) {
    const QPoint from = iconCentre(source);
    strip.press(from);
    iconMouse(source, QEvent::MouseButtonPress, from);
    strip.frame(3);
    for (int i = 1; i <= CARRY_STEPS; ++i) {
      const double t = double(i) / CARRY_STEPS;
      const double ease = t * t * (3 - 2 * t);
      const QPoint at = from + (to - from) * ease + QPoint(0, i == 1 ? 40 : 0);
      strip.point(at);
      iconMouse(source, QEvent::MouseMove, at);
      strip.frame();
    }
  }

  void release(Strip& strip, QWidget* source, const QPoint& at) {
    strip.point(at);
    dropIcon(source, at);
  }

  QToolButton* switchFor(const QWidget& win, const QAction* act) {
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->defaultAction() == act && b->isVisible()) return b;
    return nullptr;
  }
}  // namespace

void MainWindowGuiTest::gestureShots(MainWindow& win, const ShotSet& shots) {
  auto* canvas = win.findChild<CanvasWidget*>();
  const auto dress = [&] {
    useProject(win, "sunset", "Sunset sketch", scene());
    win.acts.showLines->setChecked(true);
    win.acts.showPoints->setChecked(false);
    canvas->setLines({line({{140, 150}, {520, 260}, {330, 520}}, "#c81e1e", 4, "solid"),
                      line({{610, 170}, {860, 540}}, "#1e63c8", 3, "dashed")});
    fit(win);
    pumpFor(300);
  };

  // The Projects icon: one a reader sees on the toolbar whatever is open.
  if (shots.has("drag-icon-window")) {
    dress();
    QWidget* icon = win.buttonForAction(win.acts.projects);
    if (!icon) std::printf("  drag-icon-window SKIPPED (no icon)\n");
    else {
      Strip strip(&win, QStringLiteral("drag-icon-window"));
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
      release(strip, icon, drop);
      waitUntil([&] { return held; }, 6000);
      pumpFor(200);
      std::printf("  drag-icon-window/ (frames)%s\n", held ? "" : " — NO WINDOW");
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
    release(strip, win.tools.logoBtn, drop);
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
        const QPoint at = inCanvas(win.scroll, 0.45 + 0.15 * t, 0.35 + 0.25 * t);
        strip.point(at);
        iconMouse(button, QEvent::MouseMove, at);
        strip.frame();
      }
      strip.frame(DWELL);
      release(strip, button, inCanvas(win.scroll, 0.6, 0.6));
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
      std::printf("  drag-color: the tint swatch is not under its own centre\n");
    Strip strip(&win, QStringLiteral("drag-color"));
    strip.frame(DWELL / 2);
    carry(strip, from, iconCentre(onto));
    strip.frame(DWELL);
    release(strip, from, iconCentre(onto));
    pumpFor(200);
    clearToasts(&win);
    strip.frame(DWELL * 2);
    std::printf("  drag-color/ (frames)\n");
  }

  if (shots.has("fullscreen-strip")) {
    dress();
    win.parts.view.toggleFullscreen();
    pumpFor(600);
    fit(win);
    pumpFor(200);
    clearToasts(&win);
    Strip strip(&win, QStringLiteral("fullscreen-strip"));
    // The pointer at the top edge brings the rows in; down over the picture, they leave again. The rows
    // slide on their height, so each move is filmed as it plays, then held once every bar is in or out.
    const auto hover = [&](double fy, bool shown) {
      const QPoint at = win.mapToGlobal(QPoint(win.width() / 2, int(win.height() * fy)));
      strip.point(at);
      QCursor::setPos(at);
      win.parts.view.fsHoverTick();
      for (int i = 0; i < 8; ++i) {
        pumpFor(60);
        clearToasts(&win);
        strip.frame();
      }
      waitUntil([&] {
        for (QToolBar* bar : win.fs.bars) if (bar->isVisible() != shown) return false;
        return true;
      }, 3000);
      pumpFor(150);
      clearToasts(&win);
    };
    hover(0.6, false);
    strip.frame(DWELL);
    hover(0.05, true);
    strip.frame(DWELL * 2);
    hover(0.6, false);
    strip.frame(DWELL);
    win.parts.view.toggleFullscreen();
    pumpFor(600);
    std::printf("  fullscreen-strip/ (frames)\n");
  }
}
