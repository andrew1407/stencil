// MainWindow GUI e2e — a real double-click on a colour chip puts its default back and never opens
// the picker (support/control/dblReset.hpp wireColorChip; browser dblReset.js): the toolbar's line,
// point and tint chips, and the selected line's own line and point wells. Shared ground:
// MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "uiTimings.hpp"
#include "defaultVisuals.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  // A double-click, then the double-click window run out: a picker would be up by now.
  static bool dblClickOpensNothing(QAbstractButton* chip) {
    bool opened = false;
    QTimer guard;
    QObject::connect(&guard, &QTimer::timeout, [&opened] {
      if (QWidget* dlg = QApplication::activeModalWidget()) { opened = true; dlg->close(); }
    });
    guard.start(10);
    // What a real double-click delivers to a widget: press, release, press, double-click, release
    // (QTest::mouseDClick sends the double-click event alone).
    const QPoint at = chip->rect().center();
    QTest::mouseClick(chip, Qt::LeftButton, {}, at);
    QTest::mousePress(chip, Qt::LeftButton, {}, at);
    QMouseEvent dbl(QEvent::MouseButtonDblClick, at, chip->mapToGlobal(at), Qt::LeftButton, Qt::LeftButton, {});
    QApplication::sendEvent(chip, &dbl);
    QTest::mouseRelease(chip, Qt::LeftButton, {}, at);
    QTest::qWait(stencil::support::uiTimings().doubleClickMs + 150);
    return !opened;
  }

  static CanvasWidget* withSelectedLine(MainWindow& win) {
    CanvasWidget* canvas = openLoaded(win);
    stencil::core::Line line;
    line.points = {{20, 20}, {80, 20}};
    line.color = "#ff0000";
    line.pointColor = "#00ff00";
    canvas->setLines({line});
    canvas->selectLineByIndex(0);
    settle([&win] { return win.selectedLineDock->isVisible(); });
    return canvas;
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void theToolbarChipsResetOnADoubleClick() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(openLoaded(win)->hasImage());
    win.applyTintColor(QColor("#123456"));
    win.settings.defaultPointColor = QStringLiteral("#00ff00");
    QVERIFY(dblClickOpensNothing(win.tools.pointColorBtn));
    QVERIFY2(QColor(win.settings.defaultPointColor) == win.tools.lineColorValue,
             "the toolbar's points take the line colour, as their own");
    QVERIFY(dblClickOpensNothing(win.tools.filterColorBtn));
    QCOMPARE(win.tools.filterColorValue, QColor(Settings().filterColor));
    win.tools.lineColorValue = QColor("#0000ff");
    QVERIFY(dblClickOpensNothing(win.tools.lineColorBtn));
    QCOMPARE(win.tools.lineColorValue, QColor(stencil::gui::defaultVisuals::table().color));
  }

  void theSelectedLineWellsResetOnADoubleClick() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    CanvasWidget* canvas = withSelectedLine(win);
    auto* point = win.selectedLineBar->findChild<QPushButton*>("selectedLinePointSwatch");
    auto* line = win.selectedLineBar->findChild<QPushButton*>("selectedLineColorSwatch");
    QVERIFY(point && line);
    win.settings.defaultPointColor = QStringLiteral("#8000ff");   // the toolbar's purple points
    QVERIFY(dblClickOpensNothing(point));
    QCOMPARE(QColor(QString::fromStdString(canvas->getLines()[0].pointColor)), QColor("#8000ff"));
    QVERIFY(dblClickOpensNothing(line));
    QCOMPARE(QColor(QString::fromStdString(canvas->getLines()[0].color)), QColor(win.settings.defaultColor));
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.colorReset.gui.moc"
