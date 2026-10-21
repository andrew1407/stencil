// The standard edits, filmed step by step for the GIFs desktop.mjs assembles: two lines clicked
// point by point over the dusk scene, then a sepia filter; and a rectangle dragged out in Rect
// mode, then a triangle closed on its first point into an area and filled. Clicks go through
// QTest the way tests/canvas/MainWindow.canvas.gui.cpp draws, so the real drawing path runs.
#include "captureStrip.hpp"

#include "CanvasWidget.hpp"
#include "app/drag/iconDragGui.hpp"

#include <QMouseEvent>
#include <QSpinBox>
#include <QTest>
#include <QToolButton>

using namespace stencil::gui;

namespace {
  // A point inside the canvas, as a fraction of the picture at its fitted size.
  QPoint at(QWidget* canvas, const QPointF& f) {
    return QPoint(int(canvas->width() * f.x()), int(canvas->height() * f.y()));
  }

  void mouse(QWidget* canvas, QEvent::Type type, const QPoint& local, Qt::MouseButtons held) {
    QMouseEvent ev(type, QPointF(local), QPointF(canvas->mapToGlobal(local)),
                   type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, held, Qt::NoModifier);
    QApplication::sendEvent(canvas, &ev);
  }
}  // namespace

void MainWindowGuiTest::editingShots(MainWindow& win, const ShotSet& shots) {
  if (!shots.hasAny({QStringLiteral("edit-draw"), QStringLiteral("draw-shapes")})) return;
  auto* canvas = win.findChild<CanvasWidget*>();
  // New lines in a strong red, 4 px, their points following them: as picked through the Line swatch.
  win.settings.defaultPointColor.clear();
  win.tools.lineColorValue = QColor("#dc2626");
  win.updateColorSwatch(win.tools.lineColorBtn, win.tools.lineColorValue);
  win.settings.defaultColor = win.tools.lineColorValue.name(QColor::HexRgb);
  win.updateColorSwatch(win.tools.pointColorBtn, win.parts.styleControls.effectiveDefaultPointColor());
  win.tools.lineThickness->setValue(4);
  win.parts.styleControls.onLineStyleControlChanged();
  const auto prepare = [&](const char* id, const char* name, const QImage& picture) {
    useProject(win, id, name, picture);
    canvas->setLines({});
    win.acts.showLines->setChecked(true);
    win.acts.showPoints->setChecked(true);
    fit(win);
    pumpFor(300);
    clearToasts(&win);
  };

  if (shots.has("edit-draw")) {
    prepare("sunset", "Sunset sketch", scene());
    Strip strip(&win, QStringLiteral("edit-draw"));
    const auto settle = [&](int frames) {
      pumpFor(120);
      clearToasts(&win);
      strip.frame(frames);
    };
    const auto clickAll = [&](std::initializer_list<QPointF> fs) {
      for (const QPointF& f : fs) {
        strip.press(canvas->mapToGlobal(at(canvas, f)));
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, at(canvas, f));
        settle(4);
      }
    };
    strip.frame(DWELL);
    win.acts.startDraw->trigger();
    settle(DWELL / 2);
    clickAll({{0.15, 0.25}, {0.55, 0.42}, {0.35, 0.82}});
    win.acts.newLine->trigger();
    settle(DWELL / 2);
    clickAll({{0.64, 0.26}, {0.9, 0.86}});
    win.acts.stopDraw->trigger();
    settle(DWELL);
    win.applyImageFilter(QStringLiteral("sepia"));
    settle(DWELL * 2);
    std::printf("  edit-draw/ (frames)\n");
  }

  if (shots.has("draw-shapes")) {
    QImage survey(960, 640, QImage::Format_RGB32);
    survey.fill(QColor("#dde3ea"));
    prepare("site", "Site sketch", survey);
    Strip strip(&win, QStringLiteral("draw-shapes"));
    const auto settle = [&](int frames) {
      pumpFor(150);
      clearToasts(&win);
      strip.frame(frames);
    };
    // The toolbar's Line / Rect toggle, pressed where a reader would press it.
    const auto toggleMode = [&] {
      strip.press(stencil::guitest::iconCentre(win.tools.drawModeBtn));
      win.tools.drawModeBtn->click();
      settle(DWELL / 2);
    };
    strip.frame(DWELL / 2);
    toggleMode();
    // Rect mode: a press, a drag, a release is one rectangle.
    const QPoint from = at(canvas, {0.12, 0.18}), to = at(canvas, {0.46, 0.78});
    strip.press(canvas->mapToGlobal(from));
    mouse(canvas, QEvent::MouseButtonPress, from, Qt::LeftButton);
    for (int i = 1; i <= 10; ++i) {
      const QPoint step = from + (to - from) * (i / 10.0);
      strip.point(canvas->mapToGlobal(step));
      mouse(canvas, QEvent::MouseMove, step, Qt::LeftButton);
      settle(1);
    }
    mouse(canvas, QEvent::MouseButtonRelease, to, Qt::NoButton);
    settle(DWELL / 2);
    // Back to Line mode: a click on the stroke's first point closes it into an area.
    toggleMode();
    win.acts.startDraw->trigger();
    for (const QPointF& f : {QPointF(0.62, 0.22), QPointF(0.9, 0.5), QPointF(0.6, 0.8), QPointF(0.62, 0.22)}) {
      strip.press(canvas->mapToGlobal(at(canvas, f)));
      QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, at(canvas, f));
      settle(4);
    }
    settle(DWELL / 2);
    canvas->selectLineByIndex(1);
    canvas->setSelectedLineFill(QStringLiteral("#f59e0b66"));
    settle(DWELL * 2);
    std::printf("  draw-shapes/ (frames)\n");
  }
}
