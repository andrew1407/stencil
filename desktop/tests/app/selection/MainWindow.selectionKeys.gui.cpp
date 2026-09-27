// MainWindow GUI e2e — Delete / Backspace in the selection panel's lists, as the browser's coordTable.js
// and linesList.js take them: the focused row's own point or line goes, whatever the modifiers, and
// the window's shortcuts never see the key. Shared ground is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  static stencil::core::Line lineOf(std::vector<stencil::core::Point> points) {
    stencil::core::Line line;
    line.points = std::move(points);
    return line;
  }

  static std::vector<int> pointCounts(const CanvasWidget* canvas) {
    std::vector<int> out;
    for (const auto& line : canvas->getLines()) out.push_back(int(line.points.size()));
    return out;
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Line 0 is the canvas selection, the list's focused row is line 2: the key removes line 2, where
  // the window's Delete Last Point / Alt+Delete would have taken the selected line 0.
  void theLinesListRemovesItsFocusedRowsLine() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QVERIFY(canvas->hasImage());
    // Active: only then do the window's shortcuts compete for the key.
    QTRY_VERIFY_WITH_TIMEOUT(QApplication::activeWindow() == &win, 5000);
    auto* list = win.findChild<QTableWidget*>("linesList");
    QVERIFY(list);
    const auto seed = [&] {
      canvas->setLines({lineOf({{10, 10}, {40, 10}}), lineOf({{10, 50}, {40, 50}, {40, 70}}),
                        lineOf({{100, 20}, {140, 20}, {140, 60}, {100, 60}})});
      canvas->selectLineByIndex(0);
      QTRY_COMPARE(list->rowCount(), 3);
      list->setFocus();
      list->setCurrentCell(2, 0);
    };
    for (const Qt::KeyboardModifiers mods : {Qt::KeyboardModifiers(Qt::NoModifier),
                                             Qt::KeyboardModifiers(Qt::AltModifier)}) {
      seed();
      QTest::keyClick(list, Qt::Key_Backspace, mods);
      QCOMPARE(pointCounts(canvas), (std::vector<int>{2, 3}));
    }
    // The row that took its place is focused, so the next press goes on down the list.
    QCOMPARE(list->currentRow(), 1);
    QTest::keyClick(list, Qt::Key_Delete);
    QCOMPARE(pointCounts(canvas), (std::vector<int>{2}));
    QCOMPARE(canvas->getSelectedLineIdx(), 0);
  }

  // The selected line's points table: the focused row's POINT goes, not the whole line, the line
  // stays up in the table and focus moves to the row that took the removed one's place.
  void thePointsTableRemovesItsFocusedRowsPoint() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QVERIFY(canvas->hasImage());
    // Active: only then do the window's shortcuts compete for the key.
    QTRY_VERIFY_WITH_TIMEOUT(QApplication::activeWindow() == &win, 5000);
    auto* table = win.findChild<QTableWidget*>("pointsTable");
    QVERIFY(table);
    canvas->setLines({lineOf({{10, 10}, {40, 10}, {40, 40}, {10, 40}})});
    canvas->selectLineByIndex(0);
    QTRY_COMPARE(table->rowCount(), 4);
    table->setFocus();
    table->setCurrentCell(1, 1);
    QTest::keyClick(table, Qt::Key_Backspace);
    QCOMPARE(pointCounts(canvas), (std::vector<int>{3}));
    QCOMPARE(canvas->getLines()[0].points[1].y, 40.0);   // (40, 10) went; (40, 40) moved up
    QCOMPARE(table->rowCount(), 3);
    QCOMPARE(table->currentRow(), 1);
    QTest::keyClick(table, Qt::Key_Delete, Qt::AltModifier);
    QCOMPARE(pointCounts(canvas), (std::vector<int>{2}));
    // A read-only compare view edits nothing: the key is left to the window, which refuses it too.
    win.parts.styleControls.setCompareModeUi(QStringLiteral("vertical"));
    QVERIFY(canvas->compareReadOnly());
    QTest::keyClick(table, Qt::Key_Backspace);
    QCOMPARE(pointCounts(canvas), (std::vector<int>{2}));
    win.parts.styleControls.setCompareModeUi(QStringLiteral("none"));
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.selectionKeys.gui.moc"
