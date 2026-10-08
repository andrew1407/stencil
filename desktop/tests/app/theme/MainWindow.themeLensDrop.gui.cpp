// MainWindow GUI e2e — The theme lens's release (app/theme/ThemePainterLens.cpp; browser twin
// ui/drag/themeLens.js): the lens only previews, so a release anywhere — over the canvas, the header,
// off the window, back on the switch — takes it down, switches nothing, plays no wipe and stores
// nothing; a plain click on the switch still toggles. Shared ground is in themeLensGui.hpp.
#include "themeLensGui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private:
  static QToolButton* themeSwitch(MainWindow& win) { return switchFor(win, win.acts.theme); }
  static void storeTheme(MainWindow& win, const QString& mode) {
    Settings s = win.settings;
    s.themeMode = mode;
    win.applySettings(s, /*persist=*/true);
  }
  static bool shown(MainWindow& win) {
    win.resize(1000, 760);
    win.show();
    return QTest::qWaitForWindowExposed(&win);
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // With motion on, a flip would play its wipe: none plays wherever the lens is let go.
  void aReleaseAnywhereOnlyTakesTheLensDown() {
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    CanvasWidget* canvas = openLoaded(win);
    QVERIFY(canvas->hasImage());
    QTRY_COMPARE(wipesOn(win), 0);
    QToolButton* button = themeSwitch(win);
    QVERIFY(button);
    const bool dark = win.painted.dark;
    const QByteArray stored = storedSettings();
    const QPoint spots[] = {iconCentre(canvas), iconCentre(win.tools.headerToolbar),
                            win.mapToGlobal(QPoint(-60, -60))};
    for (const QPoint& at : spots) {
      liftIcon(button);
      QVERIFY(lensOf(win));
      dropIcon(button, at);
      QVERIFY2(!lensOf(win), "the release takes the lens down");
      QVERIFY2(win.painted.dark == dark && wipesOn(win) == 0 && storedSettings() == stored,
               "…and switches nothing, plays no wipe and stores nothing");
    }
  }

  void releasedBackOnTheSwitchNothingHappens() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(shown(win));
    QToolButton* button = themeSwitch(win);
    QVERIFY(button);
    const bool dark = win.painted.dark;
    const QByteArray stored = storedSettings();
    dragIcon(button, iconCentre(button) + QPoint(-200, 240), iconCentre(button));
    QVERIFY2(!lensOf(win) && win.painted.dark == dark && storedSettings() == stored,
             "released back on the switch, the drag cancels and the release clicks nothing");
  }

  void aPlainClickStillToggles() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(shown(win));
    QToolButton* button = themeSwitch(win);
    QVERIFY(button);
    const QString mode = win.settings.themeMode;
    const bool dark = win.painted.dark;
    iconMouse(button, QEvent::MouseButtonPress, iconCentre(button));
    iconMouse(button, QEvent::MouseButtonRelease, iconCentre(button));
    QCoreApplication::processEvents();
    QVERIFY2(!lensOf(win) && win.painted.dark != dark, "a click flips the theme, no lens in sight");
    storeTheme(win, mode);
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.themeLensDrop.gui.moc"
