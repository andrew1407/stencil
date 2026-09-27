// A scene's render copy (CanvasScene::renderCopy), the co-edit result's off-thread render: on a
// pool thread it paints exactly the pixels the GUI thread's renderToImage does — filter, lines,
// fills, points, the theme's point outline — and later edits to the canvas never reach it.
#include "CanvasWidget.hpp"
#include "../../../src/support/rowWork.hpp"

#include <QApplication>
#include <QEventLoop>
#include <QImage>
#include <QThread>
#include <QTimer>

#include "../../support/check.hpp"

using stencil::gui::CanvasScene;
using stencil::gui::CanvasWidget;

namespace {
  struct PoolRender {
    QImage image;
    bool offGuiThread = false;
  };

  PoolRender renderOnPool(const std::shared_ptr<CanvasScene>& scene) {
    PoolRender out;
    QThread* gui = QThread::currentThread();
    QEventLoop loop;
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    stencil::support::runOnPool<PoolRender>(
        &loop,
        [scene, gui] { return PoolRender{scene->renderToImage(true), QThread::currentThread() != gui}; },
        [&out, &loop](PoolRender r) {
          out = r;
          loop.quit();
        });
    loop.exec();
    return out;
  }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  CanvasWidget canvas;
  QImage page(320, 240, QImage::Format_RGB32);
  for (int y = 0; y < page.height(); ++y)
    for (int x = 0; x < page.width(); ++x) page.setPixel(x, y, qRgb(x % 256, y % 256, (x * y) % 256));
  canvas.loadFromImage(page, stencil::core::CropRect{0, 0, 320, 240}, 0);
  stencil::core::Line open;
  open.points = {{20, 30}, {150, 60}, {300, 40}};
  open.color = "#ff00aa";
  open.thickness = 4;
  open.style = "dashed";
  stencil::core::Line shape;
  shape.points = {{60, 120}, {200, 110}, {170, 220}};
  shape.locked = true;
  shape.fillColor = "rgba(0, 200, 120, 0.4)";
  shape.pointColor = "#00ffff";
  shape.pointSize = 6;
  canvas.setLines({open, shape});
  canvas.setDark(true);
  canvas.setImageFilter(QStringLiteral("sepia"), QColor("#7c3aed"));

  const QImage onGui = canvas.renderToImage(true);
  const std::shared_ptr<CanvasScene> copy = canvas.renderCopy();
  canvas.setLines({});
  canvas.setImageFilter(QStringLiteral("none"), QColor("#7c3aed"));
  canvas.setDark(false);
  const PoolRender pooled = renderOnPool(copy);
  check(pooled.offGuiThread, "the copy renders on a pool thread");
  check(!onGui.isNull() && pooled.image == onGui, "…pixel for pixel what the GUI thread renders");
  check(canvas.renderToImage(true) != onGui, "edits after the copy stay on the canvas");

  // A filter changed just before the copy is still stale in the canvas's cache: the copy rebuilds it.
  canvas.setLines({open, shape});
  canvas.setImageFilter(QStringLiteral("contour"), QColor("#7c3aed"));
  const std::shared_ptr<CanvasScene> stale = canvas.renderCopy();
  const PoolRender rebuilt = renderOnPool(stale);
  check(rebuilt.offGuiThread && rebuilt.image == canvas.renderToImage(true),
        "a stale filter is rebuilt on the pool, to the same pixels");

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
