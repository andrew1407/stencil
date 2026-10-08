// MainWindow GUI e2e — every way a line or point leaves the canvas keeps the selection panel true, as
// the browser's tests/core/drawingApp-removal.test.js pins it there: the Lines tab lists each line with
// its point count and marks the selection, the points table lists the panel's line, the bar shows the
// selected line. Shared ground is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  static constexpr int SELECTED_ROLE = Qt::UserRole + 1;   // selectionPanelParts.hpp

  static stencil::core::Line lineOf(double y, int n = 3) {
    stencil::core::Line line;
    for (int i = 0; i < n; ++i) line.points.push_back({20.0 + 40 * i, y});
    line.color = "#ff0000";
    line.thickness = 2;
    line.pointSize = 4;
    return line;
  }

  static int columnTitled(QTableWidget* t, const QString& title) {
    for (int c = 0; c < t->columnCount(); ++c)
      if (QTableWidgetItem* h = t->horizontalHeaderItem(c); h && h->text() == title) return c;
    return -1;
  }

  // The three panels agree with the canvas: the list row for row, the table with its line, the bar.
  static void checkPanels(MainWindow& win, const char* what) {
    CanvasWidget* canvas = win.canvas;
    auto* list = win.findChild<QTableWidget*>("linesList");
    auto* points = win.findChild<QTableWidget*>("pointsTable");
    QVERIFY2(list && points, what);
    const auto& lines = canvas->getLines();
    const int n = static_cast<int>(lines.size());
    if (n == 0) {
      QVERIFY2(list->rowCount() == 1 && list->columnSpan(0, 0) > 1, what);
    } else {
      QCOMPARE(list->rowCount(), n);
      const int pts = columnTitled(list, QStringLiteral("Pts"));
      QVERIFY2(pts >= 0, what);
      std::vector<int> marked;
      for (int r = 0; r < n; ++r) {
        QCOMPARE(list->item(r, pts)->text(), QString::number(lines[r].points.size()));
        if (list->item(r, 0)->data(SELECTED_ROLE).toBool()) marked.push_back(r);
      }
      QVERIFY2(marked == canvas->selectedIndices(), what);
    }
    for (const auto* note : win.selPanel->findChildren<QLabel*>())
      if (note->text().contains(QStringLiteral("lines selected")))
        QVERIFY2(note->isHidden() || canvas->selectionCount() >= 2, what);
    QVERIFY2(win.selectedLineDock->isVisible() == (canvas->selectedLine() != nullptr), what);
    const auto* panelLine = canvas->panelLine();
    const int rows = panelLine && !panelLine->points.empty() ? static_cast<int>(panelLine->points.size()) : 1;
    QCOMPARE(points->rowCount(), rows);
  }

  // Lines 1 and 3 of three, multi-selected through the list's own Ctrl+Shift route.
  static void multiSelect(CanvasWidget* canvas, std::initializer_list<int> rows) {
    for (int r : rows) canvas->toggleLineSelectionByIndex(r);
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void clearAllDropsAMultiSelection() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QVERIFY(canvas->hasImage());
    canvas->setLines({lineOf(20), lineOf(60), lineOf(100)});
    multiSelect(canvas, {0, 2});
    QCOMPARE(canvas->selectionCount(), 2);
    canvas->clearAll();
    checkPanels(win, "after the clear");
    canvas->commitLines({lineOf(20), lineOf(60), lineOf(100)});
    QCOMPARE(canvas->selectionCount(), 0);
    checkPanels(win, "after new lines arrive");
  }

  // Undo and redo keep the selection on lines that still exist; a layout replaces them all.
  void undoRedoKeepAMultiSelectionAndALayoutDropsIt() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    canvas->setLines({lineOf(20), lineOf(60), lineOf(100)});
    canvas->removeLineByIndex(2);
    multiSelect(canvas, {0, 1});
    canvas->undo();
    QVERIFY(canvas->selectedIndices() == std::vector<int>({0, 1}));
    checkPanels(win, "after the undo");
    multiSelect(canvas, {0, 2});
    canvas->redo();
    QCOMPARE(canvas->selectionCount(), 1);
    QCOMPARE(canvas->getSelectedLineIdx(), 1);
    checkPanels(win, "after the redo drops line 3");
    multiSelect(canvas, {0});
    canvas->commitLines({lineOf(30, 2)});
    canvas->commitLines({lineOf(30, 2), lineOf(70, 2)});
    QCOMPARE(canvas->selectionCount(), 0);
    checkPanels(win, "after two layouts");
  }

  // A turn either way and a flip move the picture under the lines: the selection, bar and lists stay.
  void rotateAndFlipKeepTheSelection() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    canvas->setLines({lineOf(20), lineOf(60), lineOf(100)});
    canvas->selectLineByIndex(1);
    for (int step = 0; step < 3; ++step) {
      if (step == 2) canvas->flipImage();
      else canvas->rotateImage(step == 0);
      QCOMPARE(canvas->getSelectedLineIdx(), 1);
      QVERIFY(win.selectedLineDock->isVisible());
      checkPanels(win, "a single selection after a turn or flip");
    }
    canvas->undo();
    QCOMPARE(canvas->getSelectedLineIdx(), 1);
    checkPanels(win, "after undoing the flip");
    canvas->deselect();
    multiSelect(canvas, {0, 2});
    canvas->rotateImage(true);
    canvas->rotateImage(false);
    canvas->flipImage();
    QVERIFY(canvas->selectedIndices() == std::vector<int>({0, 2}));
    checkPanels(win, "a multi-selection after turns and a flip");
    canvas->undo();
    canvas->redo();
    QVERIFY(canvas->selectedIndices() == std::vector<int>({0, 2}));
    checkPanels(win, "a multi-selection through undo and redo");
  }

  void escapeDropsAMultiSelection() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    canvas->setLines({lineOf(20), lineOf(60), lineOf(100)});
    multiSelect(canvas, {0, 2});
    canvas->deselect();
    QCOMPARE(canvas->selectionCount(), 0);
    checkPanels(win, "after the deselect");
  }

  // The points table's delete empties the panel's line: a multi-selection holding it keeps the rest.
  void emptyingALineKeepsTheSelectionOnTheRest() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    canvas->setLines({lineOf(20), lineOf(60), lineOf(100, 1)});
    multiSelect(canvas, {0, 2});
    canvas->deletePoint(0);
    QCOMPARE(static_cast<int>(canvas->getLines().size()), 2);
    QCOMPARE(canvas->getSelectedLineIdx(), 0);
    checkPanels(win, "after the last point went");
    canvas->commitLines({lineOf(20), lineOf(60), lineOf(100)});
    checkPanels(win, "after new lines arrive");
  }

  // The paths that already kept the panels true stay so: the list's bin, a key, a double-click erase.
  void theOtherRemovalsLeaveThePanelsTrue() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    canvas->setLines({lineOf(20), lineOf(60, 4), lineOf(100), lineOf(140)});
    multiSelect(canvas, {0, 2, 3});
    canvas->removeLineByIndex(2);
    checkPanels(win, "a Lines-row removal inside a multi-selection");
    canvas->selectLineByIndex(1);
    canvas->deletePoint(0);
    checkPanels(win, "a points-table delete");
    canvas->deleteSelectedLine();
    checkPanels(win, "Alt+Delete");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.selectionLists.gui.moc"
