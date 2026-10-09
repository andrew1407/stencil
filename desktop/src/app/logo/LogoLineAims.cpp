#include "LogoLineAims.hpp"
#include "CanvasWidget.hpp"
#include "fileStore.hpp"
#include "SelectionPanel.hpp"
#include "markHits.hpp"

#include <QTableWidget>

namespace stencil::gui {

  namespace {
    bool within(const QWidget* at, const QWidget* region) {
      return at && region && (at == region || region->isAncestorOf(at));
    }
    int lineCount(const LogoLineParts& w) { return static_cast<int>(w.canvas->getLines().size()); }
  }  // namespace

  void addLogoLineHooks(LogoDragHooks& drag, LogoLineParts w) {
    drag.lineAt = [w](QWidget* at, const QPoint& global) -> LogoLineAim {
      QTableWidget* table = w.panel ? w.panel->linesTable() : nullptr;
      if (table && within(at, table->viewport())) {
        const int row = table->rowAt(table->viewport()->mapFromGlobal(global).y());
        return row >= 0 && row < lineCount(w) ? LogoLineAim{row, table->viewport()} : LogoLineAim{};
      }
      if (within(at, w.bar)) {
        const int idx = w.canvas->getSelectedLineIdx();
        return idx >= 0 && idx < lineCount(w) ? LogoLineAim{idx, w.bar} : LogoLineAim{};
      }
      if (!w.canvas->hasImage() || !within(at, w.canvas)) return {};
      // Image px are widget px over the zoom; the hit reach is constant on screen, as a click's is.
      const QPoint p = w.canvas->mapFromGlobal(global);
      const double scale = w.canvas->getScale() > 0 ? w.canvas->getScale() : 1.0;
      const int idx = model::lineAt(w.canvas->getLines(), w.canvas->shownMarks(), p.x() / scale, p.y() / scale,
                                    w.canvas->hitRadius(pointerTuning::table().lineRadiusPx));
      return idx >= 0 ? LogoLineAim{idx, w.frame} : LogoLineAim{};
    };
    drag.lineHover = [w](int idx) { w.canvas->setListHoverLine(idx); };
    // One step through commitLines; the selection it drops is put back, being no part of the history.
    drag.styleLine = [w](int idx) {
      core::Lines lines = w.canvas->getLines();
      if (idx < 0 || idx >= static_cast<int>(lines.size()) || w.canvas->compareReadOnly()) return;
      core::Line& line = lines[static_cast<std::size_t>(idx)];
      const core::Line was = line;
      line.color = w.settings->defaultColor.toStdString();
      line.pointColor = w.settings->defaultPointColor.toStdString();
      line.thickness = w.settings->defaultThickness;
      line.pointSize = w.settings->defaultPointSize;
      line.style = w.settings->defaultStyle.toStdString();
      if (line.color == was.color && line.pointColor == was.pointColor && line.thickness == was.thickness &&
          line.pointSize == was.pointSize && line.style == was.style)
        return;
      const int selected = w.canvas->getSelectedLineIdx();
      w.canvas->commitLines(lines);
      if (selected >= 0) w.canvas->selectLineByIndex(selected);
    };
  }

}  // namespace stencil::gui
