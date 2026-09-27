// MainWindow GUI e2e — Fixture isolation: the picture every case opens belongs to its own binary.
// ctest runs the areas side by side, each booting through the same initTestCase, so a sibling's
// boot must never reach it. Shared ground is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"
#include <QProcess>

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  // Run as the sibling below, it boots as every area does and stops mid-rewrite of its picture:
  // the moment a loaded machine can hold another area's initTestCase in.
  void initTestCase() {
    prepareGuiTestCase();
    if (!qEnvironmentVariableIsSet("STENCIL_GUI_SIBLING")) return;
    QVERIFY(QFile(guiTestImage()).open(QIODevice::WriteOnly | QIODevice::Truncate));
    QSKIP("a sibling's boot only");
  }

  void siblingBootLeavesThePictureWhole() {
    QTemporaryDir state;
    QVERIFY(state.isValid());
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("STENCIL_GUI_SIBLING"), QStringLiteral("1"));
    env.insert(QStringLiteral("STENCIL_STATE_DIR"), state.path());
    QProcess sibling;
    sibling.setProcessEnvironment(env);
    sibling.start(QCoreApplication::applicationFilePath(), {});
    QVERIFY(sibling.waitForFinished(30000));
    QCOMPARE(sibling.exitCode(), 0);

    MainWindow win(nullptr, /*restoreLast=*/false);
    CanvasWidget* canvas = openLoaded(win);
    QVERIFY2(canvas->hasImage(), "another area's boot reached the picture this binary opens");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.fixtureIsolation.gui.moc"
