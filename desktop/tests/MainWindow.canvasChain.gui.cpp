// MainWindow GUI e2e — The coordinate read-out following the cursor, and the unchain button's own gate.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The bottom-bar coordinate readout must follow the cursor over the image —
  // in every state the user can be in. A frozen readout reads as a frozen app.
  void coordReadoutFollowsTheCursor() {
    MainWindow win(nullptr, false);
    win.resize(1200, 850);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    settleLayout(&win, 200);
    QVERIFY(win.status);

    // Synthesize a real hover over the canvas and read the status bar.
    const auto hoverAt = [&](const QPoint& p) {
      QMouseEvent move(QEvent::MouseMove, QPointF(p), canvas->mapToGlobal(p), Qt::NoButton,
                       Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(canvas, &move);
      QTest::qWait(20);
      return win.status->text();
    };
    const auto readoutMoves = [&](const char* state) {
      const QString a = hoverAt(QPoint(canvas->width() / 3, canvas->height() / 3));
      const QString b = hoverAt(QPoint(canvas->width() * 2 / 3, canvas->height() * 2 / 3));
      QVERIFY2(a.contains(QLatin1String("Pixel (")),
               qPrintable(QString("%1: the readout is not showing coordinates (%2)")
                              .arg(QLatin1String(state), a)));
      QVERIFY2(a != b, qPrintable(QString("%1: the readout did not follow the cursor (%2)")
                                      .arg(QLatin1String(state), a)));
    };

    readoutMoves("plain");

    win.actIncognito->setChecked(true);   // the state the report came from
    settleLayout(&win, 80);
    readoutMoves("incognito");
    win.actIncognito->setChecked(false);

    win.actChat->setChecked(true);        // …with the chat open over the layout
    QTRY_VERIFY(win.chatDock->isVisible());
    awaitAnim(win.chatAnim);              // …on the slide's own end
    readoutMoves("chat open");
    win.actChat->setChecked(false);
    awaitAnim(win.chatAnim);

    // COMPARE: the canvas is read-only there, but the readout is information, not editing —
    // it must keep following the cursor.
    for (const char* mode : {"vertical", "horizontal"}) {
      win.setCompareModeUi(QString::fromLatin1(mode));
      QTRY_VERIFY2(win.canvas->compareReadOnly(), "compare did not engage");
      readoutMoves(mode);
    }
    win.setCompareModeUi(QStringLiteral("none"));
    beat();
  }

  // "Unchain" sits in the bar's area-only group, so it is offered exactly when a line is an
  // area, and clicking it puts the line back to an open polyline (canvas/chainEdit.hpp).
  void unchainButtonIsOfferedOnlyForAreas() {
    MainWindow win(nullptr, false);
    win.resize(1200, 850);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    auto* unchain = win.selectedLineBar->findChild<QPushButton*>("selectedLineUnchain");
    QVERIFY2(unchain, "the bar has no Unchain button");

    // An OPEN line: the area controls, Unchain among them, stay away.
    stencil::core::Line open;
    open.points = {{20, 20}, {80, 80}, {40, 90}};
    canvas->setLines({open});
    canvas->selectLineByIndex(0);
    QTRY_VERIFY_WITH_TIMEOUT(win.selectedLineDock->isVisible(), 2000);
    // The group SLIDES away now (controlReveal), so visibility settles on the event loop
    // rather than on the same tick — wait it out instead of reading it mid-flight.
    QTRY_VERIFY2(!unchain->isVisible(), "an open line was offered Unchain");

    // The fill swatch must go through cssColor() like every other stored colour: a CSS
    // `#rrggbbaa` handed to QColor reads as #AARRGGBB, alpha first, and washes out.
    {
      stencil::core::Line tinted;
      tinted.points = {{10, 10}, {90, 10}, {90, 70}, {10, 70}};
      tinted.locked = true;
      tinted.fillColor = "#00aa4440";        // green at alpha 0x40
      canvas->setLines({tinted});
      canvas->selectLineByIndex(0);
      beat();
      auto* swatch = win.selectedLineBar->findChild<QPushButton*>("selectedLineFillSwatch");
      QVERIFY2(swatch, "the bar has no fill swatch");
      // The colour lives in the well's CHIP now (a 32x16 pixmap inside the input frame),
      // so read it there: its channels must be the CSS ones, alpha included.
      const QImage chip = swatch->icon().pixmap(32, 16).toImage();
      const QColor mid = chip.pixelColor(chip.width() / 2, chip.height() / 2);
      // ±2 per channel: the chip is drawn into a premultiplied pixmap, so the readback rounds by a
      // unit. What matters is THIS green at THIS alpha, not a #AARRGGBB misread's (170, 68, 64).
      const auto near8 = [](int got, int want) { return std::abs(got - want) <= 2; };
      QVERIFY2(near8(mid.red(), 0) && near8(mid.green(), 170) && near8(mid.blue(), 68) &&
                   near8(mid.alpha(), 64),
               qPrintable("fill chip reads " + mid.name(QColor::HexArgb)));

      // The bar's colours must be the browser's own hex, painted flat: encoding into Display P3 on
      // macOS is a second conversion on an already colour-managed surface.
      win.resize(1900, 900);
      settleLayout(&win, 300);
      const QImage bar = win.selectedLineBar->grab().toImage();
      auto* ds = win.selectedLineBar->findChild<QWidget*>("selectedLineDeselect");
      QVERIFY(ds);
      const QColor got = bar.pixelColor(ds->mapTo(win.selectedLineBar, QPoint(5, ds->height() / 2)));
      // Deselect wears the bar's own amber, the token its siblings use (browser .deselect-btn ->
      // var(--bg-sel-btn)), for the theme the window is actually in.
      const stencil::gui::Palette live = stencil::gui::themePalette(
          stencil::gui::resolveDark(win.settings.themeMode), win.settings.accentColor);
      QCOMPARE(got.name(), live.bgSelBtn.name());
      QCOMPARE(stencil::gui::themePalette(true, "violet").danger.name(), QStringLiteral("#f0697a"));
    }

    // The two glyphs in this group are sized like the browser's: an 11px clear-fill cross (not
    // the style's 16 scaling it up), and Unchain's icon-plus-label pairing from #sel-unchain.
    {
      auto* clear = win.selectedLineBar->findChild<QPushButton*>("selectedLineFillClear");
      QVERIFY2(clear, "no clear-fill button");

      // …and the browser's own boxes: a 23x19 cross, 28px-tall buttons and 46x34 colour wells
      // beside 34px fields. Clear-fill is a full-height control, not a small low cross.
      QVERIFY2(clear->height() >= 26, qPrintable(QString("clear is %1px tall").arg(clear->height())));
      QCOMPARE(clear->iconSize(), QSize(13, 13));
      // ONE colour well everywhere: 46x24, the size the browser and extension now use too.
      auto* swatch2 = win.selectedLineBar->findChild<QPushButton*>("selectedLineFillSwatch");
      QVERIFY(swatch2);
      QCOMPARE(swatch2->size(), QSize(46, 26));
      QVERIFY2(!swatch2->icon().isNull(),
               "the well should draw a colour CHIP inside its frame, like the toolbar's");
      // …in the theme's own input chrome, as the toolbar's wells are. The widget's own palette
      // resolves to the LIGHT theme's #dddddd and rings the wells in near-white.
      const stencil::gui::Palette chrome = stencil::gui::themePalette(
          stencil::gui::resolveDark(win.settings.themeMode), win.settings.accentColor);
      QVERIFY2(swatch2->styleSheet().contains(chrome.borderMain.name()),
               qPrintable("well frame reads: " + swatch2->styleSheet()));
      QVERIFY2(swatch2->styleSheet().contains(chrome.inputBg.name()),
               "the well should sit on the theme's input ground");
      // Four hairlines part the bar as the browser's do: header | colours | geometry | fill |
      // actions. The fill's own comes and goes WITH the group; measured at a two-row width.
      win.resize(1100, 900);
      settleLayout(&win, 300);
      const int barHeightWithFill = win.selectedLineBar->height();
      const auto visibleSeps = [&] {
        int n = 0;
        for (QFrame* f : win.selectedLineBar->findChildren<QFrame*>("selectedLineSep"))
          if (f->isVisible()) ++n;
        return n;
      };
      QCOMPARE(visibleSeps(), 4);
      canvas->unchainSelectedLine();
      beat();
      QTRY_COMPARE(visibleSeps(), 3);
      QTRY_VERIFY2(!swatch2->isVisible(), "the fill group should be gone with it");
      // …and the bar SHRINKS with it: losing the fill group can cost the flow layout a whole row,
      // so refitHeight() runs on every content change.
      const int tallWithFill = barHeightWithFill;
      QTRY_VERIFY2(win.selectedLineBar->height() < tallWithFill,
                   qPrintable(QString("bar stayed %1px tall after the fill group left (was %2)")
                                  .arg(win.selectedLineBar->height()).arg(tallWithFill)));
      for (QComboBox* cb : win.selectedLineBar->findChildren<QComboBox*>()) {
        QVERIFY2(cb->height() >= 32 && cb->height() <= 36,
                 qPrintable(QString("style combo is %1px tall, the browser's is 34").arg(cb->height())));
        break;
      }
      QVERIFY2(!clear->toolTip().isEmpty(), "the clear-fill button has no tooltip");
      QVERIFY2(!unchain->icon().isNull(), "Unchain has no icon — the browser's has one");
      QCOMPARE(unchain->iconSize(), QSize(13, 13));
      QVERIFY2(!unchain->text().isEmpty(), "…and it keeps its label beside it");
    }

    // A rect (locked, four corners, no closing duplicate) — the button appears…
    stencil::core::Line rect;
    rect.points = {{10, 10}, {90, 10}, {90, 70}, {10, 70}};
    rect.locked = true;
    canvas->setLines({rect});
    canvas->selectLineByIndex(0);
    beat();
    QTRY_VERIFY2(unchain->isVisible(), "an area was not offered Unchain");

    // …and pressing it opens the area, keeping every corner. click() rather than a positional
    // press: the group is mid-slide when it first becomes visible (controlReveal).
    unchain->click();
    beat();
    QVERIFY2(!canvas->getLines()[0].locked, "the click did not unchain the area");
    QCOMPARE(canvas->getLines()[0].points.size(), std::size_t(4));
    QTRY_VERIFY2(!unchain->isVisible(), "Unchain is still offered on a line that is now open");
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.canvasChain.gui.moc"
