// MainWindow GUI e2e — A skin survives a relaunch: the skin on at the quit is worn again by the next
// process's first window over the stored settings, which hold the word's look (still and Gray, or
// Sliding and Blue) until the user picks others; no picture. Browser twin: ui/webcore/toggle.test.js restoreSkin.
#include "../../MainWindow.gui.hpp"
#include "skinPrefs.hpp"
#include "../../../src/support/aqua/look.hpp"
#include "../../../src/support/motionPrefs.hpp"
#include "../../../src/support/skin/skinAppliers.hpp"

#include <QFile>

namespace support = stencil::support;
namespace fileStore = stencil::gui::fileStore;

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private:
  static bool shown(MainWindow& win) {
    win.resize(1000, 760);
    win.show();
    return QTest::qWaitForWindowExposed(&win);
  }
  static QByteArray settingsBytes() {
    QFile f(fileStore::settingsPath());
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
  }
  // What a quit drops: the process-wide skin state, as a fresh process starts without it.
  static void forgetProcess() {
    if (support::isAqua()) support::applyAquaLook(false);
    if (support::isWebcore()) support::applyWebcoreLook(false);
    support::setSkin(support::Skin::DEFAULT);
    support::clearMotionOverride();
    support::clearForcedDark();
    support::skinRestored() = false;
  }
  static void seed(MainWindow& win) {
    win.settings.motionMode = QStringLiteral("water");
    win.settings.accentColor = QStringLiteral("violet");
    win.applySettings(win.settings, /*persist=*/true);
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }
  void cleanup() { forgetProcess(); fileStore::saveSkin({}); }

  void webcoreComesBackStillAndGrayWithoutItsPicture() {
    QByteArray stored;
    {
      MainWindow win(nullptr, /*restoreLast=*/false);
      QVERIFY(shown(win));
      seed(win);
      stored = settingsBytes();
      QVERIFY(win.parts.theme.toggleWebcore());
      QCOMPARE(support::motionMode(), support::MotionMode::NONE);
      QCOMPARE(fileStore::loadSkin(), QString("webcore"));
      stored = settingsBytes();   // the word's look, stored as the user's
      win.projectHost.flows.resetToBlankEditor();
    }
    forgetProcess();
    MainWindow next(nullptr, /*restoreLast=*/false);
    QVERIFY(shown(next));
    QVERIFY2(support::isWebcore(), "the relaunched window wears the skin");
    QCOMPARE(support::motionMode(), support::MotionMode::NONE);
    QVERIFY(!support::drawingAnimations());
    QCOMPARE(next.settings.accentColor, QString("grey"));
    QVERIFY2(!next.canvas->hasImage(), "a relaunch opens no picture");
    QCOMPARE(settingsBytes(), stored);
    QVERIFY(!next.parts.theme.toggleWebcore());
    QCOMPARE(fileStore::loadSkin(), QString());
    QCOMPARE(support::motionMode(), support::MotionMode::NONE);   // they are the user's now
  }

  void aquaComesBackSlidingInBlueAndAPickWins() {
    {
      MainWindow win(nullptr, /*restoreLast=*/false);
      QVERIFY(shown(win));
      seed(win);
      QVERIFY(win.parts.theme.toggleAqua(/*entered=*/true));
      QCOMPARE(win.settings.accentColor, QString::fromLatin1(support::AQUA_ACCENT));
    }
    forgetProcess();
    MainWindow next(nullptr, /*restoreLast=*/false);
    QVERIFY(shown(next));
    QVERIFY(support::isAqua());
    QCOMPARE(next.settings.accentColor, QString("blue"));
    QCOMPARE(support::motionMode(), support::MotionMode::SLIDE);
    next.parts.theme.commitAccent(QStringLiteral("grass"));
    QVERIFY(support::isAqua());
    QCOMPARE(fileStore::loadSettings().accentColor, QString("grass"));   // the user's pick wins and is stored
    QVERIFY(!next.parts.theme.toggleAqua());
    QCOMPARE(next.settings.accentColor, QString("grass"));
  }

  void aSkinTurnedOffStaysOffAndJunkRestoresNothing() {
    {
      MainWindow win(nullptr, /*restoreLast=*/false);
      QVERIFY(shown(win));
      win.parts.theme.toggleWebcore(/*entered=*/false);
      win.parts.theme.toggleWebcore();
    }
    forgetProcess();
    {
      MainWindow next(nullptr, /*restoreLast=*/false);
      QVERIFY(shown(next));
      QCOMPARE(support::skin(), support::Skin::DEFAULT);
    }
    forgetProcess();
    fileStore::saveSkin(QStringLiteral("bogus"));
    QCOMPARE(fileStore::loadSkin(), QString());
    MainWindow junk(nullptr, /*restoreLast=*/false);
    QVERIFY(shown(junk));
    QCOMPARE(support::skin(), support::Skin::DEFAULT);
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.skinRelaunch.gui.moc"
