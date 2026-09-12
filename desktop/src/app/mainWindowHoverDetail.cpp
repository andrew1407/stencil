#include "mainWindow.hpp"
#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "chatPlanTarget.hpp"
#include "planExecutor.hpp"
#include "canvasTooltip.hpp"
#include "canvasWidget.hpp"
#include "pointMath.hpp"
#include "selectionPanel.hpp"
#include "selectedLineBar.hpp"

#include <cmath>

// The rows the canvas hover tooltip shows.

namespace stencil::gui {

  // Port of tooltip.js applyHover: Alt → hide; Ctrl → cursor coords; else nearest point; else hovered line; else hide.
  void MainWindow::onHoverDetail(double imageX, double imageY,
                                 const QPoint& globalPos,
                                 Qt::KeyboardModifiers mods, bool immediate) {
    if (!settings_.tooltipEnabled || !canvas_->hasImage()) {
      hideHoverTooltip();
      return;
    }
    if (mods & Qt::AltModifier) {  // Alt held -> hide
      hideHoverTooltip();
      return;
    }
    // Compare view: nothing the "before" half covers can be labelled.
    const auto shown = [this](double x, double y) {
      return !canvas_->compareReadOnly() || canvas_->compareShowsEdited(x, y);
    };
    if (!shown(imageX, imageY)) {
      hideHoverTooltip();
      return;
    }
    const double scale = canvas_->scale();
    const auto dims = currentPageDimensions();

    // By value: copies outlive this frame in the hover-delay closures.
    auto rowsForPoint = [this, dims](double px, double py) {
      const auto page = pageCoords(px, py);
      // Per-row visibility (contextMenu.js tooltipShowScreen/Page/Coords → tooltip.js show()).
      core::TooltipRowFlags flags;
      flags.showScreen = settings_.tooltipShowScreen;
      flags.showPage = settings_.tooltipShowPage;
      flags.showCoords = settings_.tooltipShowCoords;
      const auto coreRows =
          core::buildTooltipRows({px, py}, page, dims, flags, unitFormat());
      std::vector<std::pair<QString, QString>> out;
      for (const auto& r : coreRows)
        out.emplace_back(QString::fromStdString(r.first),
                         QString::fromStdString(r.second));
      return out;
    };

    // Key is stable regardless of pixel, so the tooltip keeps following without re-waiting.
    if ((mods & Qt::ControlModifier) && !(mods & Qt::ShiftModifier)) {
      scheduleHoverShow(QStringLiteral("coords"), [this, globalPos, imageX, imageY, rowsForPoint] {
        tooltip_->setRows(rowsForPoint(imageX, imageY));
        tooltip_->showAt(globalPos);
      }, immediate);
      return;
    }

    const core::Lines all = canvas_->allLines();

    // Nearest point within (pointSize + 6)/scale image px.
    const core::Point* nearest = nullptr;
    double bestD = 1e18;
    for (const auto& line : all) {
      const double thresh = (line.pointSize + 6.0) / scale;
      for (const auto& p : line.points) {
        const double d = std::hypot(imageX - p.x, imageY - p.y);
        if (d <= thresh && d < bestD) {
          bestD = d;
          nearest = &p;
        }
      }
    }
    if (nearest) {
      // A point straddling the divider is labelled only where it is drawn.
      if (!shown(nearest->x, nearest->y)) {
        hideHoverTooltip();
        return;
      }
      const double nx = nearest->x, ny = nearest->y;
      const QString key = QStringLiteral("point:%1:%2").arg(nx).arg(ny);
      scheduleHoverShow(key, [this, globalPos, nx, ny, rowsForPoint] {
        tooltip_->setRows(rowsForPoint(nx, ny));
        tooltip_->showAt(globalPos);
      }, immediate);
      return;
    }

    // Hovered line within (thickness/2 + 5)/scale image px.
    int hitLineIdx = -1;
    for (std::size_t li = 0; li < all.size(); ++li) {
      const auto& line = all[li];
      const double thresh = (line.thickness / 2.0 + 5.0) / scale;
      bool hit = false;
      for (std::size_t i = 0; i + 1 < line.points.size(); ++i) {
        const double d = core::distToSegment(imageX, imageY, line.points[i],
                                             line.points[i + 1]);
        if (d <= thresh) { hit = true; break; }
      }
      if (hit) { hitLineIdx = int(li); break; }
    }
    if (hitLineIdx == -1 || all[hitLineIdx].points.empty()) {
      hideHoverTooltip();
      return;
    }

    // Start/End, or ALL points with Shift (tooltip.js showLine).
    const bool showAll = bool(mods & Qt::ShiftModifier);
    const QString key = QStringLiteral("line:%1:%2").arg(hitLineIdx).arg(showAll);
    scheduleHoverShow(key, [this, globalPos, hitLineIdx, showAll] {
      // Read the lines fresh at reveal time (browser parity) and re-check the index — the line may be gone.
      const core::Lines fresh = canvas_->allLines();
      if (hitLineIdx < 0 || hitLineIdx >= int(fresh.size())) return;
      const auto& hitLine = fresh[hitLineIdx];
      if (hitLine.points.empty()) return;
      std::vector<std::pair<QString, QString>> rows;
      const auto u = unitFormat();
      const QString ulbl = QString::fromStdString(u.label);
      auto fmt = [&](const QString& label, const core::Point& p) {
        const auto page = pageCoords(p.x, p.y);
        rows.emplace_back(
            label, QString("%1, %2 px   %3, %4 %5")
                       .arg(qRound(p.x)).arg(qRound(p.y))
                       .arg(page.x * u.factor, 0, 'f', 2)
                       .arg(page.y * u.factor, 0, 'f', 2)
                       .arg(ulbl));
      };
      const auto& pts = hitLine.points;
      if (showAll || pts.size() <= 2) {
        for (std::size_t i = 0; i < pts.size(); ++i)
          fmt(QString::number(i + 1), pts[i]);
      } else {
        fmt("Start", pts.front());
        fmt("End", pts.back());
      }
      tooltip_->setRows(rows);
      tooltip_->showAt(globalPos);
    }, immediate);
  }

}  // namespace stencil::gui
