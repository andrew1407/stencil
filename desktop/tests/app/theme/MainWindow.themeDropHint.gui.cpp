// MainWindow GUI e2e — The drop hint's keycaps per theme and platform, and the accent's rendered colour.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The drop hint's keycaps are PAINTED pictures holding literal colours, so the line is rebuilt on a
  // theme change; and Qt binds a configured "Ctrl" to Command on a Mac, so the cap must say Command.
  void dropHintKeycapsFollowTheThemeAndThePlatform() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1100, 720);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(win.dropHintText);

    const QString dark = win.dropHintText->text();
    QVERIFY2(dark.contains(QLatin1String(stencil::gui::KEYCAP_CLASS)),
             "the paste combo must be drawn as keycaps, not spelled out");
#ifdef Q_OS_MACOS
    QVERIFY2(dark.contains(QStringLiteral("alt=\"⌘\"")),
             "a Mac's paste cap is Command, never the Control caret");
#endif

    Settings flipped = win.settings;
    flipped.themeMode = win.paintedDark ? "light" : "dark";
    win.applySettings(flipped, /*persist=*/false);
    QVERIFY2(win.dropHintText->text() != dark,
             "the caps carry their colours in the picture — a theme change must repaint them");

    // …and the SENTENCE sits centred beside them: a keycap is taller than the type and an inline image
    // inflates the line box downwards, so the line is one middle-aligned table row.
    QLabel* hint = win.dropHintText;
    QImage ink(hint->size() * 2, QImage::Format_ARGB32_Premultiplied);
    ink.setDevicePixelRatio(2);
    ink.fill(Qt::transparent);
    hint->render(&ink, QPoint(), QRegion(), QWidget::DrawChildren);
    int top = -1, bot = -1;   // the prose only: the caps sit far to the right
    for (int y = 0; y < ink.height(); ++y)
      for (int x = 0; x < qMin(900, ink.width()); ++x)
        if (qAlpha(ink.pixel(x, y)) > 40) { if (top < 0) top = y; bot = y; break; }
    QVERIFY2(top >= 0, "the hint painted nothing to measure");
    const double off = (top + bot) / 2.0 - ink.height() / 2.0;
    QVERIFY2(qAbs(off) <= 2.0,
             qPrintable(QString("the prose sits %1 device px off centre").arg(off)));
  }

  // macOS reads our raw pixels as already in the display's space, so theme.cpp encodes into it; this
  // pins the result to what Chrome puts on screen for --accent (#7c3aed → #743ee4, measured).
  void accentMatchesTheBrowsersRenderedColour() {
    // The palette IS the browser's, byte for byte, on every platform: encoding into Display P3 on macOS
    // was a second conversion. The values below are those in theme.css and js/config/constants.json.
    QCOMPARE(stencil::gui::accentPrimary("violet").name(), QStringLiteral("#7c3aed"));
    QCOMPARE(stencil::gui::themePalette(true).bgPage.name(), QStringLiteral("#1a1a1a"));
    QCOMPARE(stencil::gui::themePalette(false).bgPage.name(), QStringLiteral("#f0f0f0"));
    QCOMPARE(stencil::gui::themePalette(false).danger.name(), QStringLiteral("#d6293e"));
    QCOMPARE(stencil::gui::themePalette(true).danger.name(), QStringLiteral("#f0697a"));
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.themeDropHint.gui.moc"
