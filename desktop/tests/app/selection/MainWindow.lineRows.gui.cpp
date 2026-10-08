// MainWindow GUI e2e — the Lines tab's rows carry each line's own options, as the browser's
// tests/ui/panel/lines suites pin them: line and point colour chips, a thickness and a point size
// typed in place and held to LIMITS, one undo step each, the selection and the bar left in step.
// Shared ground is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "uiTimings.hpp"
#include "../../../src/support/control/lineLimits.hpp"
#include "../../../src/support/drag/iconDrag.hpp"

namespace {
  // selectionPanelParts.hpp LineCol.
  enum { INDEX_COL = 0, COLOR_COL, THICK_COL, POINT_COL, SIZE_COL, PTS_COL };

  // The picker opens later from a timer and exec()s its own loop, so a polling timer answers it.
  void answerPicker(const QColor& c) {
    auto* poll = new QTimer(qApp);
    QObject::connect(poll, &QTimer::timeout, poll, [poll, c] {
      auto* dlg = qobject_cast<QColorDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      poll->stop();
      poll->deleteLater();
      QTimer::singleShot(50, dlg, [dlg, c] { dlg->setCurrentColor(c); dlg->accept(); });
    });
    poll->start(10);
  }
}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  static stencil::core::Line lineOf(double y, const char* color, double thickness, double size) {
    stencil::core::Line line;
    line.points = {{20, y}, {80, y}, {140, y + 10}};
    line.color = color;
    line.thickness = thickness;
    line.pointSize = size;
    return line;
  }

  static QTableWidget* listOf(MainWindow& win) { return win.findChild<QTableWidget*>("linesList"); }
  static QAbstractButton* chipAt(QTableWidget* list, int row, int col) {
    const char* name = col == POINT_COL ? "linesPointSwatch" : "linesSwatch";
    return list->cellWidget(row, col)->findChild<QAbstractButton*>(QLatin1String(name));
  }
  static QColor faceOf(QAbstractButton* chip) { return chip->grab().toImage().pixelColor(7, 7); }

  // Two lines, the second selected, the list on screen.
  static CanvasWidget* seed(MainWindow& win) {
    CanvasWidget* canvas = openLoaded(win);
    canvas->setLines({lineOf(20, "#ff0000", 3, 6), lineOf(60, "#0000ff", 5, 8)});
    canvas->selectLineByIndex(1);
    settle([&win] { return listOf(win)->rowCount() == 2; });
    return canvas;
  }

  // The size cell's own editor, open, set to `value` and committed with Enter.
  static void typeInto(QTableWidget* list, int row, int col, int value) {
    list->editItem(list->item(row, col));
    auto* spin = list->findChild<QSpinBox*>();
    QVERIFY(spin);
    spin->setValue(value);
    QTest::keyClick(spin, Qt::Key_Return);
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void aRowIsItsLinesOwnOptionsEachNamed() {
    MainWindow win(nullptr, false);
    seed(win);
    QTableWidget* list = listOf(win);
    QCOMPARE(list->rowCount(), 2);
    QCOMPARE(list->columnCount(), 7);
    QCOMPARE(list->horizontalHeaderItem(COLOR_COL)->text(), QString("Line"));
    QCOMPARE(list->horizontalHeaderItem(POINT_COL)->text(), QString("Point"));
    QCOMPARE(list->item(0, THICK_COL)->data(Qt::EditRole).toInt(), 3);
    QCOMPARE(list->item(0, SIZE_COL)->data(Qt::EditRole).toInt(), 6);
    QCOMPARE(list->item(0, PTS_COL)->text(), QString("3"));
    QCOMPARE(faceOf(chipAt(list, 0, COLOR_COL)), QColor("#ff0000"));
    QCOMPARE(faceOf(chipAt(list, 0, POINT_COL)), QColor("#ff0000"));   // no point colour: the line's
    for (int col : {INDEX_COL, COLOR_COL, THICK_COL, POINT_COL, SIZE_COL, PTS_COL})
      QVERIFY2(!list->item(0, col)->toolTip().isEmpty(), qPrintable(QString("column %1 is named").arg(col)));
  }

  void aTypedSizeEditsThatLineAndKeepsTheSelection() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = seed(win);
    QTableWidget* list = listOf(win);
    emit list->cellClicked(0, THICK_COL);
    QCOMPARE(canvas->getSelectedLineIdx(), 1);   // a number never selects
    typeInto(list, 0, THICK_COL, 12);
    QTRY_COMPARE(canvas->getLines()[0].thickness, 12.0);
    QCOMPARE(canvas->getSelectedLineIdx(), 1);
    QCOMPARE(list->item(0, THICK_COL)->data(Qt::EditRole).toInt(), 12);
    canvas->undo();
    QCOMPARE(canvas->getLines()[0].thickness, 3.0);   // one undo step
    // A value past LIMITS is held to them, as the bar's are.
    emit win.selPanel->lineThicknessEdited(0, 99);
    QCOMPARE(canvas->getLines()[0].thickness, double(stencil::support::lineLimits::table().thickMax));
  }

  void aSizeOnTheSelectedLineReachesTheBar() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = seed(win);
    typeInto(listOf(win), 1, SIZE_COL, 14);
    QTRY_COMPARE(canvas->getLines()[1].pointSize, 14.0);
    QVERIFY(win.selectedLineDock->isVisible());
    bool barShowsIt = false;
    for (const auto* spin : win.selectedLineBar->findChildren<QSpinBox*>()) barShowsIt |= spin->value() == 14;
    QVERIFY2(barShowsIt, "the bar follows a row edit of its line");
  }

  void thePointChipPicksAndResetsWithoutSelecting() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = seed(win);
    QTableWidget* list = listOf(win);
    answerPicker(QColor("#123456"));
    chipAt(list, 0, POINT_COL)->click();
    QTRY_COMPARE_WITH_TIMEOUT(QString::fromStdString(canvas->getLines()[0].pointColor), QString("#123456"), 3000);
    QCOMPARE(canvas->getSelectedLineIdx(), 1);
    QCOMPARE(canvas->getLines()[0].color, std::string("#ff0000"));
    chipAt(list, 0, POINT_COL)->click();
    chipAt(list, 0, POINT_COL)->click();
    QTest::qWait(stencil::support::uiTimings().doubleClickMs + 100);
    QVERIFY2(!QApplication::activeModalWidget(), "a double-click never opens the picker");
    QCOMPARE(canvas->getLines()[0].pointColor, std::string());
    QCOMPARE(faceOf(chipAt(list, 0, POINT_COL)), QColor("#ff0000"));
    QCOMPARE(canvas->getSelectedLineIdx(), 1);
  }

  // The second press dragged the chip away (a colour drag): no picker opens over the drop, and the
  // line chip selects nothing either.
  void aChipDraggedAwayOpensNoPicker() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = seed(win);
    QTableWidget* list = listOf(win);
    bool opened = false;
    QTimer guard;
    QObject::connect(&guard, &QTimer::timeout, [&opened] {
      if (QWidget* dlg = QApplication::activeModalWidget()) { opened = true; dlg->close(); }
    });
    guard.start(10);
    for (int col : {COLOR_COL, POINT_COL}) {
      chipAt(list, 0, col)->click();
      ++stencil::support::dragsStarted();   // what a drag that starts does (support/drag/iconDrag.hpp)
      QTest::qWait(stencil::support::uiTimings().doubleClickMs + 100);
    }
    QVERIFY2(!opened, "no picker over a drop");
    QCOMPARE(canvas->getSelectedLineIdx(), 1);
  }

  void aReadOnlyViewEditsNothing() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = seed(win);
    QTableWidget* list = listOf(win);
    win.parts.styleControls.setCompareModeUi(QStringLiteral("vertical"));
    QVERIFY(canvas->compareReadOnly());
    list->editItem(list->item(0, SIZE_COL));
    QVERIFY2(!list->findChild<QSpinBox*>(), "no field opens on a comparison");
    chipAt(list, 0, POINT_COL)->click();
    chipAt(list, 0, POINT_COL)->click();
    QTest::qWait(stencil::support::uiTimings().doubleClickMs + 100);
    QCOMPARE(canvas->getLines()[0].pointColor, std::string());
    win.parts.styleControls.setCompareModeUi(QStringLiteral("none"));
  }

  // A cell a double-click edits shows the I-beam, the rest of a row the hand (browser cursor: text).
  void anEditableCellShowsTheIBeam() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = seed(win);
    QTableWidget* list = listOf(win);
    auto* points = win.findChild<QTableWidget*>("pointsTable");
    const auto shapeAt = [](QTableWidget* t, int row, int col) {
      emit t->cellEntered(row, col);
      return t->viewport()->cursor().shape();
    };
    for (int col : {THICK_COL, SIZE_COL}) QCOMPARE(shapeAt(list, 0, col), Qt::IBeamCursor);
    for (int col : {INDEX_COL, PTS_COL}) QCOMPARE(shapeAt(list, 0, col), Qt::PointingHandCursor);
    QCOMPARE(shapeAt(points, 0, 1), Qt::IBeamCursor);   // px x (selectionPanelParts.hpp COL_X)
    QCOMPARE(shapeAt(points, 0, 2), Qt::IBeamCursor);   // px y
    QCOMPARE(shapeAt(points, 0, 0), Qt::PointingHandCursor);
    QCOMPARE(shapeAt(points, 0, 3), Qt::PointingHandCursor);   // the page x is read-only
    win.parts.styleControls.setCompareModeUi(QStringLiteral("vertical"));
    QVERIFY(canvas->compareReadOnly());
    QCOMPARE(shapeAt(list, 0, SIZE_COL), Qt::PointingHandCursor);
    win.parts.styleControls.setCompareModeUi(QStringLiteral("none"));
    canvas->setLines({});
    settle([list] { return list->rowCount() == 1; });
    QCOMPARE(shapeAt(list, 0, 0), Qt::ArrowCursor);   // the lone "No lines yet." row
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.lineRows.gui.moc"
