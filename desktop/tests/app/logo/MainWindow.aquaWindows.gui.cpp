// MainWindow GUI e2e — The aqua skin across windows: the session Blue is worn by every open
// window and by one opened meanwhile, a write from any of them keeps the user's own accent in the
// file, each gives it back on the way out unless the user picked one; and the per-widget sheets a
// sibling built before the toggle lose their hover rules the moment it turns on, and get them back.
#include "../../MainWindow.gui.hpp"
#include "skinPrefs.hpp"
#include "../../../src/support/aqua/look.hpp"
#include "../../../src/support/skin/skinAppliers.hpp"

#include <QFile>

namespace support = stencil::support;

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private:
  static bool shown(MainWindow& win) {
    win.resize(1000, 760);
    win.show();
    return QTest::qWaitForWindowExposed(&win);
  }
  static QByteArray settingsBytes() {
    QFile f(stencil::gui::fileStore::settingsPath());
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
  }
  static void seed(MainWindow& win, const QString& accent) {
    win.settings.themeMode = QStringLiteral("light");
    win.settings.accentColor = accent;
    win.applySettings(win.settings, /*persist=*/true);
  }
  static bool hovers(const QWidget* w) {
    const QString s = w->styleSheet();
    return s.contains(QLatin1String(":hover")) || s.contains(QLatin1String("[hovered=\"true\"]"));
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }
  void cleanup() {
    if (support::isAqua()) { support::setSkin(support::Skin::DEFAULT); support::applyAquaLook(false); }
    support::clearMotionOverride();
    support::clearForcedDark();
  }

  void everyWindowWearsTheStoredBlueAndGivesItBack() {
    MainWindow a(nullptr, /*restoreLast=*/false);
    MainWindow b(nullptr, /*restoreLast=*/false);
    QVERIFY(shown(a) && shown(b));
    seed(a, QStringLiteral("violet"));
    QCOMPARE(b.settings.accentColor, QString("violet"));   // settings are the process's
    a.parts.theme.toggleAqua();
    QVERIFY(support::isAqua());
    QCOMPARE(a.settings.accentColor, QString("blue"));
    QCOMPARE(b.settings.accentColor, QString("blue"));   // the sibling wears it too
    QCOMPARE(b.painted.accent, QString("blue"));         // …painted, not only held
    {
      MainWindow c(nullptr, /*restoreLast=*/false);
      QCOMPARE(c.settings.accentColor, QString("blue"));   // a window opened meanwhile wears it
    }
    b.applySettings(b.settings, /*persist=*/true);
    QCOMPARE(a.settings.accentColor, QString("blue"));   // a sibling's write keeps Blue
    QVERIFY2(settingsBytes().contains("\"blue\""), "…which the file holds as the user's own");
    b.parts.theme.toggleAqua();   // off from the other window
    QVERIFY(!support::isAqua());
    QCOMPARE(a.settings.accentColor, QString("violet"));
    QCOMPARE(b.settings.accentColor, QString("violet"));
    QCOMPARE(a.painted.accent, QString("violet"));
  }

  void aPickInOneWindowStandsInEveryWindow() {
    MainWindow a(nullptr, /*restoreLast=*/false);
    MainWindow b(nullptr, /*restoreLast=*/false);
    QVERIFY(shown(a) && shown(b));
    seed(a, QStringLiteral("violet"));
    a.parts.theme.toggleAqua();
    b.parts.theme.commitAccent(QStringLiteral("grass"));
    QCOMPARE(a.settings.accentColor, QString("grass"));
    a.parts.theme.toggleAqua();
    QCOMPARE(a.settings.accentColor, QString("grass"));
    QCOMPARE(b.settings.accentColor, QString("grass"));
    QVERIFY(settingsBytes().contains("\"grass\""));
  }

  void aSiblingsSheetsLoseTheirHoverTheMomentTheSkinTurnsOn() {
    MainWindow a(nullptr, /*restoreLast=*/false);
    MainWindow b(nullptr, /*restoreLast=*/false);
    QVERIFY(shown(a) && shown(b));
    seed(a, QStringLiteral("violet"));
    b.acts.chat->setChecked(true);
    QTRY_VERIFY(b.chatDock->isVisible());
    b.chatDock->appendAssistant(QStringLiteral("A reply, so a row carries its \"…\"."));
    QList<QWidget*> sheets{b.tools.lineColorBtn, b.nameBar.field};
    for (const char* name : {"chatJumpBtn", "chatCardMore"})
      for (QToolButton* t : b.chatDock->findChildren<QToolButton*>(QString::fromLatin1(name))) sheets << t;
    QVERIFY(sheets.size() >= 5);
    for (QWidget* w : sheets) QVERIFY2(w && hovers(w), qPrintable(w->objectName() + " hovers before the skin"));
    a.parts.theme.toggleAqua();
    for (QWidget* w : sheets) QVERIFY2(!hovers(w), qPrintable(w->objectName() + " is still at once"));
    a.parts.theme.toggleAqua();
    for (QWidget* w : sheets) QVERIFY2(hovers(w), qPrintable(w->objectName() + " hovers again after"));
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.aquaWindows.gui.moc"
