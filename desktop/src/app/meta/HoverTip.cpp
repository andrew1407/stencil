#include "MainWindow.hpp"
#include "HoverTip.hpp"
#include "CanvasTooltip.hpp"
#include "CanvasWidget.hpp"
#include "hitTest.hpp"

#include <algorithm>

// The canvas hover tooltip: its reveal debounce and the rows it shows.

namespace stencil::gui {

  // Debounced by target (browser tooltip.js scheduleShow); `immediate` skips the wait.
  void HoverTip::scheduleHoverShow(const QString& key, std::function<void()> revealFn,
                                     bool immediate) {
    if (immediate) {
      if (hoverTooltipTimer) hoverTooltipTimer->stop();
      hoverPendingKey.clear();
      hoverPendingReveal = nullptr;
      hoverShownKey = key;
      revealFn();
      return;
    }
    if (hoverShownKey == key) { revealFn(); return; }
    if (hoverPendingKey == key) { hoverPendingReveal = std::move(revealFn); return; }
    // A different target: hide only if one is actually ON SCREEN, else just drop the timer.
    if (!hoverShownKey.isEmpty()) {
      hoverShownKey.clear();
      w.overlays.tooltip->hide();
    } else if (hoverTooltipTimer) {
      hoverTooltipTimer->stop();
    }
    hoverPendingKey = key;
    hoverPendingReveal = std::move(revealFn);
    if (!hoverTooltipTimer) {
      hoverTooltipTimer = new QTimer(&w);
      hoverTooltipTimer->setSingleShot(true);
      // Same 200 ms wake-up as every other tooltip (main.cpp SH_ToolTip_WakeUpDelay).
      hoverTooltipTimer->setInterval(200);
      QObject::connect(hoverTooltipTimer, &QTimer::timeout, &w, [this] {
        hoverShownKey = hoverPendingKey;
        hoverPendingKey.clear();
        auto fn = std::move(hoverPendingReveal);
        hoverPendingReveal = nullptr;
        if (fn) fn();
      });
    }
    hoverTooltipTimer->start();
  }

  // Drops the pending reveal too — an abandoned target must not pop in late.
  void HoverTip::hideHoverTooltip() {
    if (hoverTooltipTimer) hoverTooltipTimer->stop();
    hoverPendingKey.clear();
    hoverPendingReveal = nullptr;
    hoverShownKey.clear();
    w.overlays.tooltip->hide();
  }

  // Port of tooltip.js applyHover: Alt → hide; Ctrl → cursor coords; else nearest point; else hovered line; else hide.
  void HoverTip::onHoverDetail(double imageX, double imageY,
                                 const QPoint& globalPos,
                                 Qt::KeyboardModifiers mods, bool immediate) {
    if (!w.settings.tooltipEnabled || !w.canvas->hasImage()) {
      hideHoverTooltip();
      return;
    }
    if (mods & Qt::AltModifier) {  // Alt held -> hide
      hideHoverTooltip();
      return;
    }
    // Compare view: nothing the "before" half covers can be labelled.
    const auto shown = [this](double x, double y) {
      return !w.canvas->compareReadOnly() || w.canvas->compareShowsEdited(x, y);
    };
    if (!shown(imageX, imageY)) {
      hideHoverTooltip();
      return;
    }
    const auto dims = w.currentPageDimensions();

    // By value: copies outlive this frame in the hover-delay closures.
    auto rowsForPoint = [this, dims](double px, double py) {
      const auto page = w.pageCoords(px, py);
      // Per-row visibility (contextMenu.js tooltipShowScreen/Page/Coords → tooltip.js show()).
      core::TooltipRowFlags flags;
      flags.showScreen = w.settings.tooltipShowScreen;
      flags.showPage = w.settings.tooltipShowPage;
      flags.showCoords = w.settings.tooltipShowCoords;
      const auto coreRows =
          core::buildTooltipRows({px, py}, page, dims, flags, w.unitFormat());
      std::vector<std::pair<QString, QString>> out;
      for (const auto& r : coreRows)
        out.emplace_back(QString::fromStdString(r.first),
                         QString::fromStdString(r.second));
      return out;
    };

    // Key is stable regardless of pixel, so the tooltip keeps following without re-waiting.
    if ((mods & Qt::ControlModifier) && !(mods & Qt::ShiftModifier)) {
      scheduleHoverShow(QStringLiteral("coords"), [this, globalPos, imageX, imageY, rowsForPoint] {
        w.overlays.tooltip->setRows(rowsForPoint(imageX, imageY));
        w.overlays.tooltip->showAt(globalPos);
      }, immediate);
      return;
    }

    // The browser's app.findNearestPoint then app.findLineAt (HIT radii over the zoom). Core scans
    // topmost-first; reversed, it takes hitTest.js's order: committed lines up, the in-progress last.
    const pointerTuning::Table& tune = pointerTuning::table();
    core::Lines order = w.canvas->allLines();
    std::reverse(order.begin(), order.end());
    if (const auto hit = core::findNearestPoint(order, imageX, imageY, w.canvas->hitRadius(tune.pointRadiusPx))) {
      const core::Point& nearest = order[hit->lineIdx].points[hit->ptIdx];
      // A point straddling the divider is labelled only where it is drawn.
      if (!shown(nearest.x, nearest.y)) {
        hideHoverTooltip();
        return;
      }
      const double nx = nearest.x, ny = nearest.y;
      const QString key = QStringLiteral("point:%1:%2").arg(nx).arg(ny);
      scheduleHoverShow(key, [this, globalPos, nx, ny, rowsForPoint] {
        w.overlays.tooltip->setRows(rowsForPoint(nx, ny));
        w.overlays.tooltip->showAt(globalPos);
      }, immediate);
      return;
    }

    const int hitLineIdx =
        core::findLineAt(w.canvas->getLines(), imageX, imageY, w.canvas->hitRadius(tune.lineRadiusPx));
    if (hitLineIdx == -1) {
      hideHoverTooltip();
      return;
    }

    // Start/End, or ALL points with Shift (tooltip.js showLine).
    const bool showAll = bool(mods & Qt::ShiftModifier);
    const QString key = QStringLiteral("line:%1:%2").arg(hitLineIdx).arg(showAll);
    scheduleHoverShow(key, [this, globalPos, hitLineIdx, showAll] {
      // Read the lines fresh at reveal time (browser parity) and re-check the index — the line may be gone.
      const core::Lines& fresh = w.canvas->getLines();
      if (hitLineIdx >= int(fresh.size())) return;
      const auto& hitLine = fresh[hitLineIdx];
      if (hitLine.points.empty()) return;
      std::vector<std::pair<QString, QString>> rows;
      const auto u = w.unitFormat();
      const QString ulbl = QString::fromStdString(u.label);
      auto fmt = [&](const QString& label, const core::Point& p) {
        const auto page = w.pageCoords(p.x, p.y);
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
      w.overlays.tooltip->setRows(rows);
      w.overlays.tooltip->showAt(globalPos);
    }, immediate);
  }

}  // namespace stencil::gui
