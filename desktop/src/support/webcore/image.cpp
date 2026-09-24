#include "image.hpp"

#include "rules.hpp"

#include <QColor>
#include <QPainter>

namespace stencil::support {

  QImage paintWebcoreImage() {
    const WebcoreConfig& c = webcoreConfig();
    const CellGrid grid = cellGrid();
    QImage img(c.width, c.height, QImage::Format_RGB32);
    img.fill(Qt::black);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, false);
    QVector<CellRun> runs = cellRuns(grid);
    runs += cloudBlocks();
    for (const CellRun& r : runs)
      p.fillRect(r.x * grid.cell, r.y * grid.cell, r.w * grid.cell, r.h * grid.cell, QColor(r.color));
    return img;
  }

}  // namespace stencil::support
