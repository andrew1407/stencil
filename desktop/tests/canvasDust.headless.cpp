// The CANVAS image's arrival and clear clouds (app/MainWindowDnd playImageArrival,
// MainWindowProjectClose resetToBlankEditor). They run on CANVAS_DUST_MS, their own clock —
// 1.5x faster than the DUST_MS every list row, chip and dialog shares.
#include "CanvasWidget.hpp"
#include "MainWindow.hpp"
#include "../src/support/DisintegrateOverlay.hpp"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QTest>
#include <cstdio>

#include "support/check.hpp"

using stencil::gui::CanvasWidget;
using stencil::gui::DisintegrateOverlay;
using stencil::gui::MainWindow;

namespace {

  QString writeImage() {
    QImage img(120, 90, QImage::Format_ARGB32);
    img.fill(Qt::darkCyan);
    const QString path = QDir::temp().filePath(QStringLiteral("stencil_canvas_dust.png"));
    img.save(path, "PNG");
    return path;
  }

  // The newest cloud over the canvas, whatever it is hosted on.
  int lastCloudMs(const MainWindow& win) {
    // The overlay carries no Q_OBJECT, so it is found by its own object name and cast back.
    const QList<QWidget*> clouds =
        win.findChildren<QWidget*>(QString::fromLatin1(DisintegrateOverlay::OBJECT_NAME));
    return clouds.isEmpty() ? -1
                            : static_cast<DisintegrateOverlay*>(clouds.last())->durationMs();
  }

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);   // offscreen via QT_QPA_PLATFORM, with motion left ON

  std::printf("the canvas clock is the shared one divided by 1.5:\n");
  {
    check(DisintegrateOverlay::DUST_MS == 1100, "the shared dust clock is browser/1.5");
    // Pinned to the browser's GHOST_MS: the two canvases must land and clear alike.
    check(stencil::gui::CANVAS_DUST_MS == 760, "the canvas clock matches the browser's");
    check(stencil::gui::CANVAS_DUST_MS < DisintegrateOverlay::DUST_MS,
          "…and is faster than the shared list-row clock");
  }

  std::printf("an image landing on the canvas assembles on the canvas clock:\n");
  {
    MainWindow win(nullptr, false);
    win.resize(800, 600);
    win.show();
    check(QTest::qWaitForWindowExposed(&win), "the window is up");
    win.openPathFromOS(writeImage());
    auto* canvas = win.findChild<CanvasWidget*>();
    for (int i = 0; i < 200 && !(canvas && canvas->hasImage()); ++i) QTest::qWait(10);
    check(canvas && canvas->hasImage(), "the image loaded");
    QTest::qWait(30);
    const int arrival = lastCloudMs(win);
    check(arrival == stencil::gui::CANVAS_DUST_MS,
          "the arrival cloud runs on CANVAS_DUST_MS, not the shared DUST_MS");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
