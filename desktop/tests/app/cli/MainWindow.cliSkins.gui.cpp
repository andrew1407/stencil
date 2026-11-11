// MainWindow GUI e2e — the CLI stage under a skin (app/cli/CliStage; browser twin ui/cli/mode.js,
// which lifts data-skin while the stage is up): the plain theme, aqua and webcore paint the same
// terminal, its banner pixel for pixel. Shared ground: MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "CliStage.hpp"

using stencil::gui::CliStage;

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  // The banner rows: aqua opens its own picture, which the status row below them names.
  static QImage stageHead(MainWindow& win) {
    win.parts.console.enterCli();
    if (!QTest::qWaitFor([&] { return win.parts.console.cliModeOn(); }, 3000)) return {};
    QTest::qWait(1500);   // the toast's flight and the hint's reveal settle
    CliStage* stage = win.findChild<CliStage*>("cliStage");
    const QImage head = stage ? stage->grab(QRect(0, 0, stage->width(), 165)).toImage() : QImage();
    win.parts.console.leaveCli();
    QTest::qWait(100);
    return head;
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void theStageLooksTheSameUnderEverySkin() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    stencil::gui::ChatPlanTarget(win).setAccent(QStringLiteral("blue"));   // the Blue aqua wears for its session
    const QImage plain = stageHead(win);
    QVERIFY(!plain.isNull());

    QVERIFY(win.parts.theme.toggleAqua());
    const QImage aqua = stageHead(win);
    QVERIFY(win.parts.theme.toggleAqua() == false);
    QVERIFY2(aqua == plain, "aqua leaves the terminal as the plain theme paints it");

    stencil::gui::ChatPlanTarget(win).setAccent(QStringLiteral("grey"));   // the Gray webcore's word sets
    const QImage plainGrey = stageHead(win);
    QVERIFY(win.parts.theme.toggleWebcore());
    const QImage webcore = stageHead(win);
    QVERIFY(!win.parts.theme.toggleWebcore());
    QVERIFY2(webcore == plainGrey, "…and so does webcore");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.cliSkins.gui.moc"
