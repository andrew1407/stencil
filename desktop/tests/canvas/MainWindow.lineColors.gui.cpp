// MainWindow GUI e2e — colour chips: the Lines tab swatch picks a line's colour or, double-clicked,
// resets it to the toolbar's line colour; the toolbar chip resets to the canonical default
// (browser ui/control/dblReset.js + ui/panel/linesList.js).
#include "../MainWindow.gui.hpp"
#include "uiTimings.hpp"

namespace {
  // The picker opens later from a timer and exec()s its own loop, so a polling timer (which that loop
  // still runs) answers it; a blocking wait here would sit under the modal and never resume.
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
  constexpr int SWATCH_COL = 1;   // selectionPanelParts.hpp LCOL_SWATCH
}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void swatchPicksAndResetsTheLineColour() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    stencil::core::Line line;
    line.points = {{10, 10}, {60, 40}};
    line.color = "#ff0000";
    canvas->commitLines({line});
    auto* linesList = win.findChild<QTableWidget*>("linesList");
    QVERIFY(linesList);
    QTRY_COMPARE(linesList->rowCount(), 1);
    const auto colour = [canvas] { return QString::fromStdString(canvas->getLines()[0].color); };

    answerPicker(QColor("#123456"));
    emit linesList->cellClicked(0, SWATCH_COL);
    QVERIFY2(!QApplication::activeModalWidget(), "the picker waits out the double-click window");
    QTRY_COMPARE_WITH_TIMEOUT(colour(), QString("#123456"), 3000);
    QCOMPARE(canvas->getSelectedLineIdx(), 0);

    win.settings.defaultColor = QStringLiteral("#00ff00");
    emit linesList->cellClicked(0, SWATCH_COL);
    emit linesList->cellDoubleClicked(0, SWATCH_COL);
    QTest::qWait(stencil::support::uiTimings().doubleClickMs + 100);
    QVERIFY2(!QApplication::activeModalWidget(), "a double-click never opens the picker");
    QCOMPARE(colour(), QString("#00ff00"));
  }

  void toolbarChipDoubleClickRestoresTheDefault() {
    MainWindow win(nullptr, false);
    openLoaded(win);
    win.settings.defaultColor = QStringLiteral("#00ff00");
    win.tools.lineColorBtn->click();
    win.tools.lineColorBtn->click();
    QTest::qWait(stencil::support::uiTimings().doubleClickMs + 100);
    QVERIFY(!QApplication::activeModalWidget());
    QCOMPARE(QColor(win.settings.defaultColor), QColor(stencil::gui::defaultVisuals::table().color));
    QCOMPARE(win.tools.lineColorValue, QColor(stencil::gui::defaultVisuals::table().color));
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.lineColors.gui.moc"
