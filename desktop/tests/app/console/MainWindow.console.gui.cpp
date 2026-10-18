// MainWindow GUI e2e — the console window (app/console/ConsoleHost, dialogs/console/ConsoleWindow;
// browser twin ui/console/modal.js): the Data section's first button and the Data menu's first row
// open a non-modal, resizable window that blocks nothing and sits beside another window; a typed
// line answers and /exit closes it. Shared ground: MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "ConsoleWindow.hpp"
#include "../../../src/support/console/TypewriterReveal.hpp"

#include <QSizeGrip>

using stencil::gui::ConsoleWindow;

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  static ConsoleWindow* consoleOf(MainWindow& win) { return win.parts.console.consoleWindow(); }
  static QString settled(ConsoleWindow* c) {
    c->revealModel().settleNow();
    return c->outputText();
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void theButtonOpensANonModalWindowThatBlocksNothing() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QMenu* dataMenu = nullptr;
    for (QMenu* m : win.menuBar()->findChildren<QMenu*>())
      if (m->actions().contains(win.acts.console)) dataMenu = m;
    QVERIFY2(dataMenu && dataMenu->actions().first() == win.acts.console, "the Data menu leads with the console");
    QVERIFY(!consoleOf(win));
    win.acts.console->trigger();
    ConsoleWindow* console = consoleOf(win);
    QVERIFY2(console && console->isVisible(), "the action opens the console window");
    QVERIFY2(console->isWindow() && console->windowModality() == Qt::NonModal, "a window of its own, non-modal");
    QVERIFY2(!QApplication::activeModalWidget(), "nothing is blocked");
    QVERIFY2(win.pop.windows.isEmpty(), "it is not one of the side-by-side windows");
    QVERIFY2(console->minimumSize().width() >= 400 && console->findChild<QSizeGrip*>(), "resizable through its own grip");
    QCOMPARE(console->objectName(), QString("stencilConsoleDialog"));
    // The editor keeps working behind it.
    const QString before = win.settings.themeMode;
    win.acts.theme->trigger();
    QVERIFY2(win.settings.themeMode != before, "the editor's own actions still run with the console up");
    win.acts.console->trigger();
    QTRY_VERIFY2_WITH_TIMEOUT(!console->isVisible(), "a second trigger closes the window, as the browser icon toggles", 3000);
    win.acts.console->trigger();
    QTRY_VERIFY2_WITH_TIMEOUT(consoleOf(win) == console && console->isVisible(), "and a third brings the same window back", 3000);
    console->submit(QStringLiteral("/help"));
    QVERIFY2(settled(console).contains("Commands"), "a typed line answers in the scrollback");
    QVERIFY2(settled(console).contains("/help"), "…the line itself echoed first");
    console->submit(QStringLiteral("/theme dark"));
    QCOMPARE(win.settings.themeMode, QString("dark"));
    QVERIFY2(settled(console).contains("theme: dark"), "a setting word answers with the setting");
    console->submit(QStringLiteral("/nope"));
    QVERIFY2(settled(console).contains("error: unknown command 'nope'"), "an unknown word is an error line");
    console->submit(QStringLiteral("/clear"));
    QVERIFY2(!settled(console).contains("Commands"), "/clear empties the scrollback");
    console->submit(QStringLiteral("/exit"));
    QTRY_VERIFY2(!console->isVisible(), "/exit closes the window");
    beat();
  }

  void itStaysUpBesideAnotherWindow() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.acts.console->trigger();
    ConsoleWindow* console = consoleOf(win);
    QVERIFY(console && console->isVisible());
    bool stillUp = false;
    QTimer::singleShot(200, &win, [&] {
      stillUp = console->isVisible();
      if (QWidget* modal = QApplication::activeModalWidget())
        if (auto* dlg = qobject_cast<QDialog*>(modal)) dlg->reject();
    });
    win.acts.info->trigger();   // the caller waits in the window's loop
    QVERIFY2(stillUp, "another window opening leaves the console where it is");
    QVERIFY2(console->isVisible(), "…and closing it leaves the console too");
    win.parts.console.closeConsole();
    QTRY_VERIFY(!console->isVisible());
    beat();
  }

  void theEditsRunThroughTheAppliers() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    win.acts.console->trigger();
    ConsoleWindow* console = consoleOf(win);
    QVERIFY(console);
    console->submit(QStringLiteral("/rotate 1"));
    QCOMPARE(canvas->getRotationQuarters(), 1);
    QVERIFY2(canvas->canUndo(), "a console edit is one undo step");
    console->submit(QStringLiteral("/undo"));
    QCOMPARE(canvas->getRotationQuarters(), 0);
    console->submit(QStringLiteral("/status"));
    // The picture is page-cropped on load, so only its height is the file's own.
    QVERIFY2(settled(console).contains("image: stencil_e2e_input.png (") && settled(console).contains("x160 px"), "the status line names the picture");
    console->submit(QStringLiteral("/filter bw"));
    QCOMPARE(win.settings.imageFilter, QString("bw"));
    console->submit(QStringLiteral("/color #ff0000"));
    QCOMPARE(win.settings.defaultColor, QString("#ff0000"));
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.console.gui.moc"
