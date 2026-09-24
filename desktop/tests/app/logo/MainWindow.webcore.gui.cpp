// MainWindow GUI e2e — The webcore word: typed into the bare window it dresses the whole app in
// the session skin, still and light, writes nothing, gives an empty editor its picture and its
// word, and typed again puts the stored look back. Shared ground is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "LogoStage.hpp"
#include "SettingsDialog.hpp"
#include "skinPrefs.hpp"
#include "theme.hpp"
#include "../../../src/support/tip/SnappyTooltipStyle.hpp"
#include "../../../src/support/webcore/look.hpp"
#include "../../../src/support/webcore/stylesheet.hpp"

#include <QComboBox>
#include <QFile>

using stencil::gui::LogoStage;
namespace support = stencil::support;

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private:
  static LogoStage* stageOf(MainWindow& win) { return win.findChild<LogoStage*>("logoStage"); }
  static bool shown(MainWindow& win) {
    win.resize(1000, 760);
    win.show();
    return QTest::qWaitForWindowExposed(&win);
  }
  // The hold is seconds long in the table; a test shrinks its timer the way the logo suite does.
  static void holdLogo(MainWindow& win) {
    auto* hold = stageOf(win)->findChild<QTimer*>("logoHold");
    const QPoint c = win.logoBtn->rect().center();
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(c), win.logoBtn->mapToGlobal(c),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(win.logoBtn, &press);
    QVERIFY(hold->isActive());
    hold->setInterval(1);
    QTest::qWait(60);
  }
  static QByteArray settingsBytes() {
    QFile f(stencil::gui::fileStore::settingsPath());
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
  }
  static void type(MainWindow& win, const QString& word) {
    QWidget* to = QApplication::focusWidget() ? QApplication::focusWidget() : &win;
    QTest::keyClicks(to, word);
  }
  // A window whose stored look is dark and watery, so the skin has something to override.
  static void seed(MainWindow& win) {
    win.settings.themeMode = QStringLiteral("dark");
    win.settings.motionMode = QStringLiteral("water");
    win.settings.accentColor = QStringLiteral("violet");
    win.applySettings(win.settings, /*persist=*/true);
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }
  void cleanup() {
    if (support::isWebcore()) {
      support::setSkin(support::Skin::DEFAULT);
      support::applyWebcoreLook(false);
    }
    support::clearMotionOverride();
    support::clearForcedDark();
  }

  void typingWebcoreOnABlankEditorDressesTheWindow() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(shown(win));
    seed(win);
    const QByteArray stored = settingsBytes();
    QVERIFY(!win.canvas->hasImage());
    type(win, QStringLiteral("webcore"));
    QVERIFY2(support::isWebcore(), "the word turns the skin on");
    QVERIFY(!stageOf(win)->isOpen());
    QCOMPARE(qApp->styleSheet(), support::buildWebcoreStylesheet(true, QStringLiteral("violet")));
    QCOMPARE(support::motionMode(), support::MotionMode::NONE);   // a still interface…
    QVERIFY(!support::drawingAnimations());                         // …and still lines
    QVERIFY2(win.paintedDark, "the theme already chosen stands");
    QCOMPARE(win.settings.themeMode, QString("dark"));
    QCOMPARE(support::installedStyleKey(), QString("Windows"));
    QCOMPARE(settingsBytes(), stored);
    // The empty editor got the picture, the word and the name.
    QCOMPARE(win.canvas->getOriginalImage().size(), QSize(1024, 768));
    QCOMPARE(int(win.canvas->getLines().size()), 7);
    QSet<std::string> fills;
    for (const auto& l : win.canvas->getLines()) { QVERIFY(l.locked); fills.insert(l.fillColor); }
    QCOMPARE(fills.size(), 7);
    QVERIFY(win.canvas->canUndo());
    const auto* pr = win.findProject(win.activeProjectId.toStdString());
    QVERIFY(pr);
    QCOMPARE(QString::fromStdString(pr->meta.name), QString("webcore"));

    // Typed again: the stored look, the base style, the picture kept.
    type(win, QStringLiteral("webcore"));
    QVERIFY(!support::isWebcore());
    QCOMPARE(qApp->styleSheet(), stencil::gui::buildStylesheet(true, QStringLiteral("violet")));
    QCOMPARE(support::motionMode(), support::MotionMode::WATER);
    QVERIFY(win.paintedDark);
    QCOMPARE(support::installedStyleKey(), QString("Fusion"));
    QCOMPARE(settingsBytes(), stored);
    QCOMPARE(int(win.canvas->getLines().size()), 7);
  }

  void anEmptyEditorReopensTheLocalWebcoreProjectInsteadOfMintingAnother() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(shown(win));
    win.toggleWebcore();
    const QString first = win.activeProjectId;
    QVERIFY(!first.isEmpty());
    win.toggleWebcore();
    win.resetToBlankEditor();
    QVERIFY(!win.canvas->hasImage());
    const size_t count = win.projectList.size();
    win.toggleWebcore();
    QCOMPARE(win.activeProjectId, first);
    QCOMPARE(win.projectList.size(), count);
    QCOMPARE(int(win.canvas->getLines().size()), 7);
  }

  void aThemeToggleInsideWebcorePersistsAndKeepsTheSkin() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    CanvasWidget* canvas = openLoaded(win);
    seed(win);
    const int lines = int(canvas->getLines().size());
    type(win, QStringLiteral("webcore"));
    QVERIFY2(win.paintedDark, "the skin keeps the theme it was handed");
    win.toggleTheme();
    QVERIFY2(!win.paintedDark, "the toggle flips relative to what is painted");
    QVERIFY(support::isWebcore());
    QCOMPARE(qApp->styleSheet(), support::buildWebcoreStylesheet(false, QStringLiteral("violet")));
    QCOMPARE(stencil::gui::fileStore::loadSettings().themeMode, QString("light"));
    QCOMPARE(int(canvas->getLines().size()), lines);   // an editor with a picture keeps it
    type(win, QStringLiteral("webcore"));
    QCOMPARE(qApp->styleSheet(), stencil::gui::buildStylesheet(false, QStringLiteral("violet")));
  }

  // The skin stills the session (browser toggle.js): the rows show what is in force, the store
  // keeps what was chosen, and moving a row hands the user's choice back.
  void theSettingsRowsShowTheStillnessUnderTheSkin() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(shown(win));
    seed(win);   // stored: water, drawing on, backdrop on
    const QByteArray stored = settingsBytes();
    type(win, QStringLiteral("webcore"));
    QVERIFY(support::motionOverridden());
    QCOMPARE(support::motionMode(), support::MotionMode::NONE);
    {
      stencil::gui::SettingsDialog dlg(win.settings, &win);
      QCOMPARE(dlg.findChild<QComboBox*>("motionModeCombo")->currentData().toString(), QString("none"));
      const stencil::gui::Settings out = dlg.result();
      QCOMPARE(out.motionMode, QString("water"));   // untouched rows write back what is stored
      QVERIFY(out.drawingAnimations && out.modalBackdrop);
    }
    QCOMPARE(settingsBytes(), stored);
    type(win, QStringLiteral("webcore"));
    QCOMPARE(support::motionMode(), support::MotionMode::WATER);
  }

  // Grey is the one colour two rows share, and the interface animation alone tells them apart:
  // dust flying opens the dust show, a still interface the skin — whatever the pen is set to.
  void aHoldOnGreyPicksTheSkinOrTheDustShowByTheInterfaceAnimation() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(shown(win));
    win.settings.accentColor = QStringLiteral("grey");
    for (bool pen : {true, false}) {
      win.settings.motionMode = QStringLiteral("particles");
      win.settings.drawingAnimations = pen;
      win.applySettings(win.settings, /*persist=*/false);
      holdLogo(win);
      QVERIFY2(stageOf(win)->isOpen(), "dust flying: the dust show, whatever the pen does");
      QVERIFY(!support::isWebcore());
      stageOf(win)->dismiss();
      QTest::qWait(60);

      win.settings.motionMode = QStringLiteral("none");
      win.applySettings(win.settings, /*persist=*/false);
      holdLogo(win);
      QVERIFY2(support::isWebcore(), "interface still: the skin, and never a dead hold");
      QVERIFY(!stageOf(win)->isOpen());
      win.toggleWebcore();
      QTest::qWait(60);
    }
  }

  void fromIncognitoItLeavesIncognitoFirst() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(shown(win));
    win.actIncognito->setChecked(true);
    QVERIFY(win.incognito);
    type(win, QStringLiteral("webcore"));
    QVERIFY(!win.incognito);
    QVERIFY(!win.activeProjectId.isEmpty());
    type(win, QStringLiteral("webcore"));
  }

  void anotherWordStillOpensItsShowWhileOn() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(shown(win));
    type(win, QStringLiteral("webcore"));
    LogoStage* stage = stageOf(win);
    type(win, QStringLiteral("neonon"));
    QVERIFY2(stage->isOpen(), "the other eggs keep working under the skin");
    type(win, QStringLiteral("webcore"));
    QVERIFY2(!support::isWebcore() && stage->isOpen(), "the skin toggles off under a running show");
    type(win, QStringLiteral("webcore"));
    QVERIFY2(support::isWebcore() && stage->isOpen(), "…and back on, the show still up");
    stage->dismiss();
    QTest::qWait(100);
    type(win, QStringLiteral("webcore"));
    QVERIFY(!support::isWebcore());
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.webcore.gui.moc"
