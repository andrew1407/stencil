#include "wallpaper.hpp"

#include "table.hpp"

#include <QColor>
#include <QPainter>

namespace stencil::support {

  QImage paintAquaImage() {
    const AquaConfig& c = aquaConfig();
    const WallGrid grid = wallGrid();
    QImage img(c.width, c.height, QImage::Format_RGB32);
    img.fill(Qt::black);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, false);
    for (const CellRun& r : wallRuns(grid))
      p.fillRect(r.x * grid.cell, r.y * grid.cell, r.w * grid.cell, r.h * grid.cell, QColor(r.color));
    return img;
  }

}  // namespace stencil::support
