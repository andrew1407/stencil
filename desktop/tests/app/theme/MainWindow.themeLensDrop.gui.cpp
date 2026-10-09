// MainWindow GUI e2e — The theme lens's release (app/theme/ThemePainterLens.cpp; browser twin
// ui/drag/themeLens.js): let go anywhere, the lens shrinks away and the theme stays, nothing wiped
// and nothing stored; back on the switch, nothing happens; a plain click still toggles. Shared
// ground is in themeLensGui.hpp.
#include "themeLensGui.hpp"
#include "ThemeLens.hpp"

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

  // With motion on: the circle shrinks away over more than the open's time, and the theme holds.
  void aReleaseClosesTheLensAndTheThemeStays() {
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    CanvasWidget* canvas = openLoaded(win);
    QVERIFY(canvas->hasImage());
    QTRY_COMPARE(wipesOn(win), 0);
    QToolButton* button = themeSwitch(win);
    QVERIFY(button);
    const QString mode = win.settings.themeMode;
    const QPoint spots[] = {iconCentre(canvas), iconCentre(win.tools.headerToolbar),
                            win.mapToGlobal(QPoint(-60, -60))};
    for (const QPoint& at : spots) {
      const bool dark = win.painted.dark;
      const QByteArray stored = storedSettings();
      liftIcon(button);
      QTest::qWait(stencil::gui::ThemeLens::GROW_MS);
      QVERIFY(lensOf(win));
      dropIcon(button, at);
      QTest::qWait(stencil::gui::ThemeLens::GROW_MS);   // past the open's whole time: the close outlasts it
      QVERIFY2(lensOf(win) && win.painted.dark == dark, "the circle is still shrinking, the theme held");
      QTRY_VERIFY2_WITH_TIMEOUT(!lensOf(win), "…then the lens is gone", 3000);
      QVERIFY2(win.painted.dark == dark && storedSettings() == stored && wipesOn(win) == 0,
               "and the theme stays, nothing stored, nothing wiped");
    }
    QCOMPARE(win.settings.themeMode, mode);
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
