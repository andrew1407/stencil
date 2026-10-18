// MainWindow GUI e2e — CLI mode (app/cli/CliStage, app/console/ConsoleHost; browser twin
// ui/cli/mode.js): the typed word or a logo dropped on the console icon takes the window over with
// the stage, closing what was open; its commands edit the real picture and the edits stay after
// /clioff; the name peeks the picture, the secret words dress it, the hold leaves. Shared ground:
// MainWindow.gui.hpp, app/drag/iconDragGui.hpp.
#include "../../MainWindow.gui.hpp"
#include "../drag/iconDragGui.hpp"
#include "CliPeek.hpp"
#include "CliStage.hpp"
#include "../../../src/support/console/TypewriterReveal.hpp"

using stencil::gui::CliPeek;
using stencil::gui::CliStage;
using stencil::support::ConsoleKind;

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  static CliStage* stageOf(MainWindow& win) { return win.findChild<CliStage*>("cliStage"); }
  static MainWindow* shown(bool withPicture = false) {
    auto* win = new MainWindow(nullptr, /*restoreLast=*/false);
    if (withPicture) {
      openLoaded(*win);
    } else {
      win->resize(1000, 760);
      win->show();
    }
    QTest::qWaitForWindowExposed(win);
    return win;
  }
  static int secretLines(CliStage* stage) {
    int n = 0;
    for (int i = 0; i < stage->scrollback().count(); ++i) n += stage->scrollback().at(i).kind == ConsoleKind::SECRET;
    return n;
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void theTypedWordOpensTheStageOverEverything() {
    std::unique_ptr<MainWindow> win(shown());
    win->acts.chat->setChecked(true);
    QVERIFY(win->acts.chat->isChecked());
    win->acts.console->trigger();
    QVERIFY(win->parts.console.consoleOpen());
    // The word is typed into the editor window, not the console's own prompt.
    win->activateWindow();
    win->setFocus(Qt::OtherFocusReason);
    QTest::keyClicks(win.get(), QStringLiteral("cliclicli"));
    QTRY_VERIFY2_WITH_TIMEOUT(win->parts.console.cliModeOn(), "the typed word enters CLI mode", 3000);
    CliStage* stage = stageOf(*win);
    QVERIFY(stage && stage->isVisible() && stage->isOpen());
    QCOMPARE(stage->size(), win->rect().size());
    QVERIFY2(!win->acts.chat->isChecked(), "the chat closed first");
    QVERIFY2(!win->parts.console.consoleOpen(), "…and the console window");
    QVERIFY2(!QApplication::activeModalWidget(), "nothing modal under it");
    QString note;
    QVERIFY(stencil::gui::ChatPlanTarget(*win).openDialog("projects", &note));
    QVERIFY2(note.contains("CLI mode is on"), "a plan that would open a window is refused while it is on");
    stage->submit(QStringLiteral("/clioff"));
    QTRY_VERIFY2(!win->parts.console.cliModeOn() && !stage->isVisible(), "/clioff leaves");
    QVERIFY(stencil::gui::ChatPlanTarget(*win).openDialog("", &note));
    QVERIFY2(!note.contains("CLI mode"), "…and the gate lifts");
    beat();
  }

  void theLogoDroppedOnTheConsoleIconEntersAndTheHoldLeaves() {
    std::unique_ptr<MainWindow> win(shown());
    QWidget* icon = win->buttonForAction(win->acts.console);
    QVERIFY2(icon && icon->isVisible(), "the console icon is on the toolbar");
    liftIcon(win->tools.logoBtn);
    iconMouse(win->tools.logoBtn, QEvent::MouseMove, iconCentre(icon));
    dropIcon(win->tools.logoBtn, iconCentre(icon));
    QTRY_VERIFY2_WITH_TIMEOUT(win->parts.console.cliModeOn(), "the mark dropped on the console icon enters CLI mode", 3000);
    CliStage* stage = stageOf(*win);
    QVERIFY(stage);
    auto* hold = stage->findChild<QTimer*>("cliHold");
    QVERIFY(hold);
    stage->repaint();   // the banner's box is measured by the paint
    QVERIFY(!stage->bannerRect().isEmpty());
    const QPoint c = stage->bannerRect().center();
    QTest::mousePress(stage, Qt::LeftButton, Qt::NoModifier, c);
    QVERIFY2(hold->isActive(), "a press on the banner arms the hold");
    QTest::qWait(150);   // past the press frame's own 95 ms
    QVERIFY2(stage->pressFrameShown(), "the press frame stays as long as the button is down");
    hold->setInterval(1);
    QTRY_VERIFY2(!win->parts.console.cliModeOn(), "the hold leaves CLI mode");
    QTest::mouseRelease(stage, Qt::LeftButton, Qt::NoModifier, c);
    QVERIFY(!stage->pressFrameShown());
    beat();
  }

  void anEditMadeInsideStaysAndTheNamePeeks() {
    std::unique_ptr<MainWindow> win(shown(true));
    QTRY_VERIFY_WITH_TIMEOUT(win->canvas->hasImage(), 5000);
    win->parts.console.enterCli();
    QTRY_VERIFY_WITH_TIMEOUT(win->parts.console.cliModeOn(), 3000);
    CliStage* stage = stageOf(*win);
    QVERIFY(stage);
    stage->submit(QStringLiteral("/rotate 1"));
    QCOMPARE(win->canvas->getRotationQuarters(), 1);
    stage->submit(QStringLiteral("/status"));
    stage->repaint();
    QVERIFY2(!stage->nameRect().isEmpty(), "the status line carries the picture's name");
    const QPoint name = stage->nameRect().center();
    QTest::mouseClick(stage, Qt::LeftButton, Qt::NoModifier, name);
    QTRY_VERIFY2(stage->peek()->shown() == CliPeek::Kind::EDITING && stage->peek()->isVisible(), "a click on the name shows the editing picture");
    stage->repaint();
    QVERIFY2(stage->peek()->height() >= stage->height() / 5, "a tiny picture is shown at a fifth of the stage at least");
    QVERIFY2(stage->bodyTop() > stage->peek()->geometry().bottom(), "the text starts under the picture, not behind it");
    const QSize editingSize = stage->peek()->size();
    QTest::mouseClick(stage, Qt::LeftButton, Qt::NoModifier, name);
    QTRY_VERIFY2(!stage->peek()->shown().has_value(), "a second click puts it away");
    stage->repaint();
    QVERIFY2(stage->bodyTop() < stage->nameRect().bottom() + 3 * stage->height() / 20, "…and the text moves back up");
    QTest::mouseDClick(stage, Qt::LeftButton, Qt::NoModifier, name);
    QTRY_VERIFY2(stage->peek()->shown() == CliPeek::Kind::ORIGINAL, "a double-click shows the original");
    QCOMPARE(stage->peek()->size(), editingSize);   // the original keeps the rotation: the same box, the lines and filter gone
    QTest::mouseDClick(stage, Qt::LeftButton, Qt::NoModifier, name);
    QTRY_VERIFY2(stage->peek()->shown() == CliPeek::Kind::EDITING, "…and again the editing picture");
    stage->peek()->hideNow();
    stage->peek()->hold(CliPeek::Kind::EDITING, /*magnified=*/true);
    QVERIFY2(stage->peek()->isVisible() && stage->peek()->isMagnified(), "Alt+Shift holds it magnified");
    stage->peek()->release();
    QVERIFY(!stage->peek()->isVisible());
    stage->submit(QStringLiteral("/theme dark"));
    QCOMPARE(win->settings.themeMode, QString("dark"));
    stage->submit(QStringLiteral("/clioff"));
    QTRY_VERIFY(!win->parts.console.cliModeOn());
    QCOMPARE(win->canvas->getRotationQuarters(), 1);
    QCOMPARE(win->settings.themeMode, QString("dark"));
    beat();
  }

  void theBodyTextSelectsWithTheMouseAndCopies() {
    std::unique_ptr<MainWindow> win(shown());
    win->parts.console.enterCli();
    QTRY_VERIFY_WITH_TIMEOUT(win->parts.console.cliModeOn(), 3000);
    CliStage* stage = stageOf(*win);
    QVERIFY(stage);
    stage->submit(QStringLiteral("/help"));
    stage->repaint();
    const QPoint from(14, stage->bodyTop() + 2), to(stage->width() - 14, stage->bodyTop() + stage->height() / 4);
    QTest::mousePress(stage, Qt::LeftButton, Qt::NoModifier, from);
    QMouseEvent drag(QEvent::MouseMove, to, stage->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(stage, &drag);
    QTest::mouseRelease(stage, Qt::LeftButton, Qt::NoModifier, to);
    QVERIFY2(stage->hasSelection() && stage->selectedText().contains(QLatin1Char('\n')), "a drag over the body selects its rows");
    QString all;
    for (int i = 0; i < stage->scrollback().count(); ++i) all += stage->scrollback().at(i).text + QLatin1Char('\n');
    QVERIFY2(all.contains(stage->selectedText()), "…the scrollback's own text");
    QTest::keyClick(stage, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QGuiApplication::clipboard()->text(), stage->selectedText());
    QTest::mouseClick(stage, Qt::LeftButton, Qt::NoModifier, from);
    QVERIFY2(!stage->hasSelection(), "a click puts the selection away");
    beat();
  }

  void noWindowChordReachesThePageUnderTheStage() {
    std::unique_ptr<MainWindow> win(shown());
    win->activateWindow();
    win->setFocus(Qt::OtherFocusReason);
    QTest::keyClicks(win.get(), QStringLiteral("cliclicli"));
    QTRY_VERIFY_WITH_TIMEOUT(win->parts.console.cliModeOn(), 3000);
    CliStage* stage = stageOf(*win);
    QVERIFY(stage);
    QTest::keyClick(stage, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);   // openProjects
    QTest::keyClick(stage, Qt::Key_V, Qt::AltModifier);                           // openVisuals
    QTest::keyClick(stage, Qt::Key_G, Qt::AltModifier);                           // toggleChat
    QTest::qWait(50);
    QVERIFY2(!QApplication::activeModalWidget() && !win->pop.active, "no dialog opened under the stage");
    for (const QPointer<QDialog>& d : win->pop.windows) QVERIFY2(!d || !d->isVisible(), "no side window opened under the stage");
    QVERIFY2(!win->acts.chat || !win->acts.chat->isChecked(), "the chat stayed closed");
    // Alt held over the page peeks a toolbar icon's window; under the stage it must not.
    if (QWidget* icon = win->buttonForAction(win->acts.description)) {
      QTest::mouseMove(icon, icon->rect().center());
      QTest::keyPress(stage, Qt::Key_Alt);
      QTest::qWait(400);
      QVERIFY2(!QApplication::activeModalWidget() && !win->pop.active, "Alt under the stage peeks nothing");
      QTest::keyRelease(stage, Qt::Key_Alt);
    }
    QVERIFY(win->parts.console.cliModeOn());
  }

  void theSecretWordsDressTheStage() {
    std::unique_ptr<MainWindow> win(shown());
    win->parts.console.enterCli();
    QTRY_VERIFY_WITH_TIMEOUT(win->parts.console.cliModeOn(), 3000);
    CliStage* stage = stageOf(*win);
    QVERIFY(stage);
    stage->submit(QStringLiteral("/eastereggs"));
    QCOMPARE(secretLines(stage), 9);   // the gold echo of the secret word, then the eight words
    stage->submit(QStringLiteral("/mranderson"));
    QVERIFY2(stage->skin() && stage->skin()->key == "matrix", "a skin word puts its skin on");
    QCOMPARE(secretLines(stage), 11);   // its echo and its phrase
    stage->submit(QStringLiteral("mranderson"));
    QVERIFY2(stage->skin() && stage->skin()->key == "matrix", "the word answers only with a slash");
    stage->submit(QStringLiteral("/mranderson"));
    QVERIFY2(!stage->skin(), "the same word takes it off");
    stage->submit(QStringLiteral("/bifrost"));
    QVERIFY2(stage->skin() && stage->skin()->key == "bifrost", "another skin");
    for (const auto& [word, key] : {std::pair{"/meow", "meow"}, std::pair{"/theyareinthetrees", "fruit"}, std::pair{"/pieday", "pie"}}) {
      stage->submit(QString::fromLatin1(word));
      QVERIFY2(stage->skin() && stage->skin()->key == QLatin1String(key), word);
      stage->repaint();   // the picture skins paint their pictures
    }
    const QString accent = win->settings.accentColor;
    stage->submit(QStringLiteral("/fairylight"));
    QVERIFY2(stage->skin() && stage->skin()->key == "fairylight", "the fairy lights replace the bifrost");
    win->parts.console.leaveCli();
    QVERIFY2(!stage->skin() && win->settings.accentColor == accent, "leaving takes the skin off and the accent comes back");
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.cliMode.gui.moc"
