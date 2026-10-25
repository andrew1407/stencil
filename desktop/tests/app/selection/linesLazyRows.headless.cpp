// A long list or line in the selection panel (SelectionPanelLineRow.cpp, SelectionPanelRows.cpp): every
// row's cells at once, its widgets only for the rows on screen and those a scroll brings in, or a
// picker anchors on; a short one makes them all, as it always did.
#include "SelectionPanel.hpp"

#include <QApplication>
#include <QGraphicsEffect>
#include <QScrollBar>
#include <QTabWidget>
#include <QTableWidget>

#include "../../support/check.hpp"

using namespace stencil;

namespace {
  constexpr int COL_COLOR = 2, COL_DEL = 8;   // selectionPanelParts.hpp LineCol
  constexpr int POINT_DEL = 5;                 // selectionPanelParts.hpp PointCol

  core::Lines linesOf(int n) {
    core::Lines lines(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
      lines[i].points = {{1.0 * i, 2}, {3, 4.0 * i}};
      lines[i].color = "#ff0000";
      lines[i].thickness = 2;
      lines[i].hidden = i % 7 == 3;
    }
    return lines;
  }

  int withWidgets(const QTableWidget* t) {
    int made = 0;
    for (int i = 0; i < t->rowCount(); ++i) made += t->cellWidget(i, COL_DEL) != nullptr;
    return made;
  }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  gui::SelectionPanel panel;
  panel.resize(400, 600);
  panel.show();
  QApplication::processEvents();
  auto* table = panel.findChild<QTableWidget*>("linesList");
  check(table != nullptr, "the Lines tab's table is there");

  panel.setLines(linesOf(150), {});
  QApplication::processEvents();
  check(withWidgets(table) == 150, "a short list makes every row's widgets");

  core::Lines lines = linesOf(3000);
  panel.setLines(lines, {});
  QApplication::processEvents();
  const int shown = withWidgets(table);
  check(table->rowCount() == 3000 && table->item(2999, 0) && table->item(2999, 0)->text() == "3000",
        "a long list holds every row's cells");
  check(shown > 0 && shown < 300, "…and the widgets of the rows on screen only");
  auto* tabs = panel.findChild<QTabWidget*>();
  tabs->setCurrentIndex(1);   // the Lines tab: its viewport gets its height
  QApplication::processEvents();
  const int onTab = withWidgets(table);
  check(table->viewport()->height() > 0 && onTab >= shown && onTab < 300
            && table->cellWidget(table->rowAt(table->viewport()->height() - 1), COL_DEL),
        "showing the tab makes the rows it shows, and only those");

  table->verticalScrollBar()->setValue(table->verticalScrollBar()->maximum());
  QApplication::processEvents();
  check(table->cellWidget(2999, COL_DEL) != nullptr, "a scroll to the end makes the rows it shows");
  const QWidget* hiddenChip = table->cellWidget(2999, COL_COLOR);   // 2999 % 7 == 3: a hidden line
  check(hiddenChip && hiddenChip->graphicsEffect(), "a hidden line's row fades as its widgets are made");

  check(panel.lineSwatchCell(1500) != nullptr && panel.pointSwatchCell(1600) != nullptr,
        "a picker's anchor row is made on demand");

  const int before = withWidgets(table);
  lines[2999].thickness = 9;
  panel.setLines(lines, {});
  QApplication::processEvents();
  check(withWidgets(table) == before, "an edit rebuilds its row and makes no others");
  lines.resize(150);   // rows kept from the long list, scrolled past and never shown, are short-list rows now
  panel.setLines(lines, {});
  QApplication::processEvents();
  check(withWidgets(table) == 150, "a list cut back to short has every row's widgets");

  // The points tab of a long line: every row's cells, a bin only on the rows shown.
  tabs->setCurrentIndex(0);
  auto* points = panel.findChild<QTableWidget*>("pointsTable");
  core::Line line;
  for (int i = 0; i < 20000; ++i) line.points.push_back({1.0 * i, 2.0 * i});
  panel.showLine(&line, -1, std::vector<gui::SelectionPanel::PageRow>(line.points.size()));
  QApplication::processEvents();
  int bins = 0;
  for (int r = 0; r < points->rowCount(); ++r) bins += points->cellWidget(r, POINT_DEL) != nullptr;
  check(points->rowCount() == 20000 && points->item(19999, 0)->text() == "20000", "a long line lists every point");
  check(bins > 0 && bins < 300, "…with a bin on the rows shown only");
  points->verticalScrollBar()->setValue(points->verticalScrollBar()->maximum());
  QApplication::processEvents();
  check(points->cellWidget(19999, POINT_DEL) != nullptr, "a scroll to the end makes its bins");
  core::Line shortLine;
  for (int i = 0; i < 50; ++i) shortLine.points.push_back({1.0 * i, 1});
  panel.showLine(&shortLine, -1, std::vector<gui::SelectionPanel::PageRow>(50));
  bins = 0;
  for (int r = 0; r < points->rowCount(); ++r) bins += points->cellWidget(r, POINT_DEL) != nullptr;
  check(bins == 50, "a short line has every bin, as it always did");

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}
