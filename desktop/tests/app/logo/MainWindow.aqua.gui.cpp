// MainWindow GUI e2e — The aqua word: typed into the bare window it dresses the app in the gel
// session skin, slides and wears Blue for the session without touching the store, gives the
// user's accent back unless they picked one, paints its gel in any accent picked, gives an empty
// editor its wallpaper and word, swaps with webcore, and the hold opens it only blue, sliding, light.
#include "../../MainWindow.gui.hpp"
#include "LogoStage.hpp"
#include "skinPrefs.hpp"
#include "theme.hpp"
#include "../../../src/support/tip/SnappyTooltipStyle.hpp"
#include "../../../src/support/aqua/look.hpp"
#include "../../../src/support/skin/skinAppliers.hpp"
#include "../../../src/support/aqua/table.hpp"
#include "../../../src/support/aqua/sheet.hpp"

#include "ExpirationDialog.hpp"

#include <QDateTime>
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
  static void holdLogo(MainWindow& win) {
    auto* hold = stageOf(win)->findChild<QTimer*>("logoHold");
    const QPoint c = win.tools.logoBtn->rect().center();
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(c), win.tools.logoBtn->mapToGlobal(c),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(win.tools.logoBtn, &press);
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
  static void seed(MainWindow& win, const QString& theme, const QString& accent = QStringLiteral("blue")) {
    win.settings.themeMode = theme;
    win.settings.motionMode = QStringLiteral("slide");
    win.settings.accentColor = accent;
    win.applySettings(win.settings, /*persist=*/true);
  }
  static void forgetAquaProjects() {
    const QString name = support::aquaConfig().projectName.trimmed().toLower();
    std::vector<stencil::gui::Project> kept;
    for (const stencil::gui::Project& p : stencil::gui::fileStore::loadProjects()) {
      if (QString::fromStdString(p.meta.name).trimmed().toLower() != name) kept.push_back(p);
      else QFile::remove(p.imagePath);
    }
    stencil::gui::fileStore::saveProjects(kept);
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); forgetAquaProjects(); }
  void cleanup() {
    if (support::isAqua()) { support::setSkin(support::Skin::DEFAULT); support::applyAquaLook(false); }
    if (support::isWebcore()) { support::setSkin(support::Skin::DEFAULT); support::applyWebcoreLook(false); }
    support::clearMotionOverride();
    support::clearForcedDark();
  }

  void typingTheWordOnABlankEditorDressesTheWindowAndStoresSlidingBlue() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(shown(win));
    win.settings.themeMode = QStringLiteral("light");
    win.settings.motionMode = QStringLiteral("water");
    win.settings.accentColor = QStringLiteral("violet");
    win.applySettings(win.settings, /*persist=*/true);
    const QByteArray stored = settingsBytes();
    QVERIFY(!win.canvas->hasImage());
    type(win, QStringLiteral("somefruitcompany"));
    QVERIFY2(support::isAqua(), "the word turns the skin on");
    QVERIFY(!stageOf(win)->isOpen());
    QCOMPARE(win.settings.accentColor, QString("blue"));   // the word sets Blue…
    QCOMPARE(qApp->styleSheet(), support::buildAquaStylesheet(false, QStringLiteral("blue")));
    QCOMPARE(stencil::gui::themePalette(false).accent.name(), QString("#2563eb"));
    QCOMPARE(support::motionMode(), support::MotionMode::SLIDE);   // …and Sliding,
    QVERIFY2(settingsBytes().contains("\"slide\""), "stored as the user's own, so a relaunch keeps them");
    QVERIFY2(support::installedStyleKey() != QString("Windows"), "the gel rides the app's own style");
    QVERIFY(settingsBytes().contains("\"blue\""));
    QCOMPARE(win.canvas->getOriginalImage().size(), QSize(1024, 768));
    QCOMPARE(int(win.canvas->getLines().size()), 7);
    const auto* pr = win.projectHost.findProject(win.projectHost.activeProjectId.toStdString());
    QVERIFY(pr);
    QCOMPARE(QString::fromStdString(pr->meta.name), QString("somefruitcompany"));

    type(win, QStringLiteral("somefruitcompany"));
    QVERIFY(!support::isAqua());
    QCOMPARE(win.settings.accentColor, QString("violet"));   // an off in the same process gives it back
    QCOMPARE(qApp->styleSheet(), stencil::gui::buildStylesheet(false, QStringLiteral("violet")));
    QCOMPARE(support::motionMode(), support::MotionMode::WATER);
    QCOMPARE(settingsBytes(), stored);   // written back exactly as the word found it
  }

  void aBlueUserKeepsBlueAndTheWordOpensBlueWhateverIsStored() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(shown(win));
    seed(win, QStringLiteral("light"));
    const QByteArray stored = settingsBytes();
    win.parts.theme.toggleAqua();
    QCOMPARE(win.settings.accentColor, QString("blue"));
    win.parts.theme.toggleAqua();
    QCOMPARE(win.settings.accentColor, QString("blue"));
    QCOMPARE(settingsBytes(), stored);
    for (const QString& own : {QStringLiteral("grass"), QStringLiteral("#123456")}) {
      seed(win, QStringLiteral("light"), own);
      const QByteArray mine = settingsBytes();
      type(win, QStringLiteral("somefruitcompany"));
      QCOMPARE(win.settings.accentColor, QString("blue"));
      type(win, QStringLiteral("somefruitcompany"));
      QCOMPARE(win.settings.accentColor, own);
      QCOMPARE(settingsBytes(), mine);
    }
  }

  void aPickDuringTheSessionRepaintsTheGelSurvivesOffAndThePopoverOpens() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(shown(win));
    seed(win, QStringLiteral("light"), QStringLiteral("grass"));
    type(win, QStringLiteral("somefruitcompany"));
    QVERIFY(support::isAqua());
    QCOMPARE(qApp->styleSheet(), support::buildAquaStylesheet(false, QStringLiteral("blue")));
    win.parts.theme.commitAccent(QStringLiteral("violet"));
    QCOMPARE(win.settings.accentColor, QString("violet"));
    QVERIFY2(settingsBytes().contains("\"violet\""), "the pick is written as any pick is");
    QCOMPARE(qApp->styleSheet(), support::buildAquaStylesheet(false, QStringLiteral("violet")));
    QVERIFY(qApp->styleSheet().contains("#aa7ff3"));   // violet's gel top
    QCOMPARE(support::skinDropGlow().ring.name(), QString("#7c3aed"));
    bool opened = false, marked = false;
    QTimer poll;
    poll.setInterval(30);
    QObject::connect(&poll, &QTimer::timeout, &win, [&] {
      QDialog* pop = win.pop.active.data();
      if (!pop || pop->objectName() != QLatin1String("accentPopover") || !pop->isVisible()) return;
      opened = true;
      auto* row = pop->findChild<QPushButton*>(QStringLiteral("accentRow-violet"));
      marked = row && row->property("currentAccent").toBool();
      poll.stop();
      pop->reject();
    });
    poll.start();
    QTimer::singleShot(3000, &win, [&win] { if (win.pop.active) win.pop.active->reject(); });
    if (QWidget* fw = QApplication::focusWidget()) fw->clearFocus();
    emit win.tools.logoBtn->customContextMenuRequested(win.tools.logoBtn->rect().center());
    QTRY_VERIFY2_WITH_TIMEOUT(opened, "the accent popover opens under aqua", 4000);
    QVERIFY2(marked, "its current preset carries the mark");
    QVERIFY(support::isAqua());
    win.parts.theme.toggleAqua();
    QCOMPARE(win.settings.accentColor, QString("violet"));   // the user's pick stands
    QCOMPARE(qApp->styleSheet(), stencil::gui::buildStylesheet(false, QStringLiteral("violet")));
  }

  void theTwoSkinsSwapAndWebcoreGivesMotionBack() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    CanvasWidget* canvas = openLoaded(win);
    QVERIFY(canvas);
    seed(win, QStringLiteral("light"));
    type(win, QStringLiteral("webcore"));
    QVERIFY(support::isWebcore());
    QCOMPARE(support::motionMode(), support::MotionMode::NONE);
    type(win, QStringLiteral("somefruitcompany"));
    QVERIFY2(support::isAqua() && !support::isWebcore(), "aqua took the window");
    QCOMPARE(support::motionMode(), support::MotionMode::SLIDE);
    QCOMPARE(support::installedStyleKey(), QString("Fusion"));
    type(win, QStringLiteral("webcore"));
    QVERIFY2(support::isWebcore() && !support::isAqua(), "and webcore took it back");
    type(win, QStringLiteral("webcore"));
    QVERIFY(!support::isWebcore() && !support::isAqua());
  }

  void theHoldOpensItOnlyBlueSlidingOnLight() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    CanvasWidget* canvas = openLoaded(win);
    QVERIFY(canvas);
    seed(win, QStringLiteral("dark"));
    holdLogo(win);
    QVERIFY2(!support::isAqua(), "graphite needs the word");
    stageOf(win)->dismiss();
    QTest::qWait(60);
    seed(win, QStringLiteral("light"));
    holdLogo(win);
    QVERIFY2(support::isAqua(), "blue, sliding, light: the skin");
    QVERIFY(!stageOf(win)->isOpen());
    win.parts.theme.toggleAqua();
    QVERIFY(!support::isAqua());
  }

  // Opt-in pictures for comparing the skin with the browser's (STENCIL_GUI_SHOTS=<dir>).
  void picturesForTheBrowserComparison() {
    const QString dir = QString::fromLocal8Bit(qgetenv("STENCIL_GUI_SHOTS"));
    if (dir.isEmpty()) QSKIP("STENCIL_GUI_SHOTS unset");
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(shown(win));
    seed(win, QStringLiteral("light"));
    type(win, QStringLiteral("somefruitcompany"));
    QTest::qWait(900);
    win.grab().save(dir + "/aqua-light.png");
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    stencil::gui::ExpirationDialog dlg(QStringLiteral("somefruitcompany"), now + 7LL * 86400000, QString(), false, now, &win);
    dlg.show();
    QTest::qWait(900);
    dlg.grab().save(dir + "/aqua-expiry.png");
    dlg.close();
    win.parts.theme.toggleTheme();
    QTest::qWait(900);
    win.grab().save(dir + "/aqua-dark.png");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.aqua.gui.moc"
