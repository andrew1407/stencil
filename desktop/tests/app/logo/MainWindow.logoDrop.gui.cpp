// MainWindow GUI e2e — the header mark's other targets (app/logo/LogoDrag + LogoLineAims, browser
// twin ui/drag/logoTargets.js): dropped on a toolbar control it goes back to its default; on a line
// on the canvas, a Lines-tab row or the selected-line bar, that line takes the toolbar's style as one
// undo step, and over a line the clean view is neither previewed nor set.
// Shared ground: MainWindow.gui.hpp, app/drag/iconDragGui.hpp.
#include "../../MainWindow.gui.hpp"
#include "../drag/iconDragGui.hpp"
#include "SelectedLineBar.hpp"
#include "SelectionPanel.hpp"
#include "dblReset.hpp"
#include "iconDrag.hpp"

#include <QTabWidget>
#include <QTableWidget>

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private:
  static void logoTo(MainWindow& win, const QPoint& to) {
    liftIcon(win.tools.logoBtn);
    iconMouse(win.tools.logoBtn, QEvent::MouseMove, to);
  }
  static void logoDrop(MainWindow& win, const QPoint& at) { dropIcon(win.tools.logoBtn, at); }
  static QPoint centre(QWidget* w) { return iconCentre(w); }
  // One red dashed line across the picture's top-left, its middle at image (110, 40).
  static CanvasWidget* withLine(MainWindow& win) {
    CanvasWidget* canvas = openLoaded(win);
    if (!canvas->hasImage()) return canvas;
    stencil::core::Line line;
    line.points = {{20, 40}, {200, 40}};
    line.color = "#ff0000";
    line.pointColor = "#00ff00";
    line.thickness = 9;
    line.pointSize = 11;
    line.style = "dashed";
    canvas->setLines({line});
    win.applyImageFilter(QStringLiteral("sepia"));
    return canvas;
  }
  static bool wearsToolbar(MainWindow& win, int idx) {
    const stencil::core::Line& l = win.canvas->getLines()[static_cast<std::size_t>(idx)];
    return QString::fromStdString(l.color) == win.settings.defaultColor &&
           QString::fromStdString(l.pointColor) == win.settings.defaultPointColor &&
           l.thickness == win.settings.defaultThickness && l.pointSize == win.settings.defaultPointSize &&
           QString::fromStdString(l.style) == win.settings.defaultStyle;
  }
  static QPoint onLine(MainWindow& win) {
    const double s = win.canvas->getScale();
    return win.canvas->mapToGlobal(QPoint(int(110 * s), int(40 * s)));
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void droppedOnAToolbarControlItGoesBackToItsDefault() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(openLoaded(win)->hasImage());
    const Settings d;
    win.tools.lineThickness->setValue(9);
    win.tools.lineStyle->setCurrentIndex(win.tools.lineStyle->findData(QStringLiteral("dotted")));
    win.units.pageSize->setCurrentIndex(win.units.pageSize->findData(QStringLiteral("A4")));
    logoTo(win, centre(win.tools.lineThickness));
    QVERIFY2(dropGlows(win, stencil::support::DROP_GLOW_NAME) >= 1, "the control under the mark glows");
    logoDrop(win, centre(win.tools.lineThickness));
    QCOMPARE(win.tools.lineThickness->value(), int(d.defaultThickness));
    QCOMPARE(win.settings.defaultThickness, d.defaultThickness);
    logoTo(win, centre(win.tools.lineStyle));
    logoDrop(win, centre(win.tools.lineStyle));
    QCOMPARE(win.tools.lineStyle->currentData().toString(), d.defaultStyle);
    QVERIFY2(stencil::support::resetToDefault(win.units.pageSize), "the page row shares the table");
    QCOMPARE(win.units.pageSize->currentData().toString(), QStringLiteral("A3"));
    win.tools.lineColorValue = QColor("#123456");
    win.settings.defaultColor = QStringLiteral("#123456");
    logoTo(win, centre(win.tools.lineColorBtn));
    logoDrop(win, centre(win.tools.lineColorBtn));
    QCOMPARE(win.settings.defaultColor.toLower(), QStringLiteral("#ffff00"));
    QVERIFY(dropGlows(win, stencil::support::DROP_GLOW_NAME) == 0);
  }

  void theFormulaFieldsEmptyAndTheirToggleTurnsOff() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(openLoaded(win)->hasImage());
    win.tools.allowFormulas->setChecked(true);
    QCoreApplication::processEvents();
    win.tools.formulaX->setText(QStringLiteral("x*2"));
    logoTo(win, centre(win.tools.formulaX));
    logoDrop(win, centre(win.tools.formulaX));
    QVERIFY(win.tools.formulaX->text().isEmpty());
    logoTo(win, centre(win.tools.allowFormulas));
    logoDrop(win, centre(win.tools.allowFormulas));
    QVERIFY2(!win.tools.allowFormulas->isChecked(), "formulas off, the default");
  }

  void overALineOnTheCanvasThatLineTakesTheToolbarStyleNotTheCleanView() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    CanvasWidget* canvas = withLine(win);
    QVERIFY(canvas->hasImage());
    logoTo(win, onLine(win));
    QVERIFY2(!canvas->getCleanPreview(), "over a line no clean view is previewed");
    logoDrop(win, onLine(win));
    QVERIFY(wearsToolbar(win, 0));
    QCOMPARE(canvas->getImageFilter(), QString("sepia"));
    win.acts.undo->trigger();
    QCOMPARE(QString::fromStdString(canvas->getLines()[0].style), QString("dashed"));
    QVERIFY2(QString::fromStdString(canvas->getLines()[0].color) == QLatin1String("#ff0000"), "one undo step");
  }

  void aLinesTabRowOrTheSelectedLineBarRestylesItsLine() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    CanvasWidget* canvas = withLine(win);
    QVERIFY(canvas->hasImage());
    canvas->selectLineByIndex(0);
    QTableWidget* table = win.selPanel->linesTable();
    if (auto* tabs = win.selPanel->findChild<QTabWidget*>()) tabs->setCurrentIndex(tabs->count() - 1);
    settle([table] { return table->isVisible() && table->rowCount() == 1; });
    QTest::qWait(100);   // the card re-hugs the tab it now shows
    const QPoint row = table->viewport()->mapToGlobal(table->visualRect(table->model()->index(0, 0)).center());
    if (!table->viewport()->isAncestorOf(QApplication::widgetAt(row)) && QApplication::widgetAt(row) != table->viewport())
      QSKIP("the Lines tab is not laid out here");
    logoTo(win, row);
    logoDrop(win, row);
    QVERIFY(wearsToolbar(win, 0));
    QCOMPARE(canvas->getSelectedLineIdx(), 0);
    win.acts.undo->trigger();
    canvas->selectLineByIndex(0);
    settle([&win] { return win.selectedLineBar->isVisible(); });
    if (!win.selectedLineBar->isVisible()) QSKIP("the selected-line bar is not shown here");
    QVERIFY(!wearsToolbar(win, 0));
    logoTo(win, centre(win.selectedLineBar));
    logoDrop(win, centre(win.selectedLineBar));
    QVERIFY2(wearsToolbar(win, 0), "the bar restyles the line it shows");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.logoDrop.gui.moc"
