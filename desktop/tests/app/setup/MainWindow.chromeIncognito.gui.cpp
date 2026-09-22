// MainWindow GUI e2e — The incognito frame, and the tag riding the image-size line.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

#include "controlReveal.hpp"
#include "motionPrefs.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The incognito frame DRAWS ON clockwise from the top-left and retracts last-edge-first — the
  // desktop half of the browser's four staggered .ig-edge elements. Both halves are pure, so no display.
  void incognitoFrameDrawsClockwiseFromTheTopLeft() {
    using stencil::gui::IncognitoOverlay;
    using Edges = IncognitoOverlay::EdgeLengths;
    const QRectF box(0, 0, 200, 100);
    const Edges none{0.0, 0.0, 0.0, 0.0}, all{1.0, 1.0, 1.0, 1.0};

    QVERIFY2(IncognitoOverlay::framePath(box, none).isEmpty(), "nothing is drawn at rest");
    QVERIFY2(IncognitoOverlay::framePath(QRectF(), all).isEmpty(), "an empty viewport draws nothing");
    QCOMPARE(IncognitoOverlay::framePath(box, all).boundingRect(), box);
    // The last edge CLIMBS from the bottom-left, so the loop closes at the top-left it started from.
    QCOMPARE(IncognitoOverlay::framePath(box, all).currentPosition(), QPointF(0.0, 0.0));
    QCOMPARE(IncognitoOverlay::framePath(box, Edges{1.0, 1.0, 1.0, 0.6}).currentPosition(),
             QPointF(0.0, 40.0));
    // Out-of-range lengths are clamped, never extrapolated.
    QCOMPARE(IncognitoOverlay::framePath(box, Edges{2.0, 2.0, 2.0, 2.0}).boundingRect(), box);
    QVERIFY(IncognitoOverlay::framePath(box, Edges{-1.0, -1.0, -1.0, -1.0}).isEmpty());

    // Half the top edge alone is a flat run along the top.
    const QPainterPath topHalf = IncognitoOverlay::framePath(box, Edges{0.5, 0.0, 0.0, 0.0});
    QCOMPARE(topHalf.boundingRect().width(), 100.0);
    QCOMPARE(topHalf.boundingRect().height(), 0.0);
    QCOMPARE(topHalf.boundingRect().top(), 0.0);

    // ON: the edges start one stagger apart and each is done a draw later, top first.
    for (int e = 0; e < 4; ++e) {
      const double starts = e * IncognitoOverlay::STAGGER_MS;
      QCOMPARE(IncognitoOverlay::edgeLengths(starts, true, none)[e], 0.0);
      QVERIFY2(IncognitoOverlay::edgeLengths(starts + 1, true, none)[e] > 0.0, "edge never starts");
      QCOMPARE(IncognitoOverlay::edgeLengths(starts + IncognitoOverlay::EDGE_MS, true, none)[e], 1.0);
    }
    // …and nothing is left short when the whole frame's clock runs out.
    QCOMPARE(IncognitoOverlay::edgeLengths(IncognitoOverlay::DRAW_MS, true, none), all);

    // OFF mirrors the delays: the LEFT edge (drawn last) goes first, the top last.
    const Edges early = IncognitoOverlay::edgeLengths(IncognitoOverlay::EDGE_MS, false, all);
    QCOMPARE(early[3], 0.0);
    QCOMPARE(early[0], 1.0);
    QVERIFY2(early[1] > early[2], "the bottom retracts ahead of the right");
    QCOMPARE(IncognitoOverlay::edgeLengths(IncognitoOverlay::DRAW_MS, false, all), none);

    // A flip mid-flight carries on from where each edge stands, so nothing jumps.
    const Edges part{0.4, 0.2, 0.0, 0.0};
    QCOMPARE(IncognitoOverlay::edgeLengths(0, false, part), part);
    QCOMPARE(IncognitoOverlay::edgeLengths(0, true, part), part);
  }

  // Incognito shows beside the image-size line (browser parity: an accent bold tag behind a muted
  // "|"), in both states and only while on. The glyph is the app's themed icon, never an emoji.
  void incognitoTagRidesTheImageSizeLine() {
    MainWindow win(nullptr, false);
    win.resize(1200, 820);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QLabel* info = win.imageSizeInfo;
    QLabel* badge = win.incognitoTag;
    QVERIFY(info && badge);
    const QString tag = QStringLiteral("Incognito");

    // Empty editor, incognito OFF: the plain hint, and the badge is not up.
    QVERIFY(!info->text().contains(tag));
    QCOMPARE(info->textFormat(), Qt::PlainText);
    QVERIFY(!badge->isVisible());

    // Empty editor, incognito ON: the badge stands beside "No image loaded".
    win.actIncognito->setChecked(true);
    QTRY_VERIFY2(badge->isVisible(), "no incognito badge on the empty line");
    QVERIFY2(info->text().contains(QStringLiteral("No image loaded")),
             "the empty-state text was replaced instead of extended");
    QCOMPARE(info->textFormat(), Qt::PlainText);
    QVERIFY2(!info->text().contains(tag), "the size line must carry the size alone");
    const QColor accent =
        stencil::gui::themePalette(stencil::gui::resolveDark(win.settings.themeMode),
                                   win.settings.accentColor).accent;
    QVERIFY2(badge->text().contains(accent.name()), "the tag is not accent-coloured");
    QVERIFY2(badge->text().contains(QStringLiteral("font-weight:700")), "the tag is not bold");

    // The divider + the real icon, in whichever state the line is in.
    const QString muted =
        stencil::gui::themePalette(stencil::gui::resolveDark(win.settings.themeMode),
                                   win.settings.accentColor).textMuted.name();
    const auto checkTagChrome = [&](const char* state) {
      const QString html = badge->text();
      QVERIFY2(html.contains(QStringLiteral("<span style=\"color:%1;\">|</span>").arg(muted)),
               qPrintable(QString("%1: no muted | divider before the tag").arg(state)));
      QVERIFY2(html.indexOf(QLatin1Char('|')) < html.indexOf(tag),
               qPrintable(QString("%1: the divider must sit BETWEEN the two facts").arg(state)));
      QVERIFY2(!html.contains(QString::fromUtf8("\xF0\x9F\x95\xB6")),
               qPrintable(QString("%1: the sunglasses EMOJI is still there").arg(state)));
      QVERIFY2(html.contains(QStringLiteral("<img src=\"data:image/png;base64,")),
               qPrintable(QString("%1: the tag carries no inline icon").arg(state)));
      QVERIFY2(html.contains(QStringLiteral("vertical-align:middle")),
               qPrintable(QString("%1: the glyph is not vertically centred").arg(state)));
      // …and it is a real, non-empty raster of the app's own incognito glyph.
      const QImage sent = pngOf(html);
      QVERIFY2(!sent.isNull() && sent.width() >= 12,
               qPrintable(QString("%1: the inline icon did not decode").arg(state)));
      QVERIFY2(hasInk(sent), qPrintable(QString("%1: the inline icon is blank").arg(state)));
    };
    checkTagChrome("empty");

    // …with an image loaded it sits beside the size.
    QImage pic(320, 240, QImage::Format_RGB32);
    pic.fill(Qt::darkCyan);
    win.canvas->loadFromImage(pic);
    QTRY_VERIFY(win.canvas->hasImage());
    win.updateImageSizeInfo();
    QVERIFY2(info->text().contains(QStringLiteral("Image Size:")) &&
                 info->text().contains(QString::number(win.canvas->imageWidth())),
             "the size left the line");
    QVERIFY2(badge->isVisible() && badge->text().contains(tag), "no incognito badge beside the size");
    checkTagChrome("loaded");

    // …and it goes when incognito does — divider included, so a plain line never
    // ends in a dangling separator. The "?" bubble still carries the fact.
    win.actIncognito->setChecked(false);
    QTRY_VERIFY2(!badge->isVisible(), "the badge outlived incognito");
    QCOMPARE(info->textFormat(), Qt::PlainText);
    QVERIFY2(!info->text().contains(QLatin1Char('|')), "a divider survived on the size line");
    QVERIFY2(!info->text().contains(QStringLiteral("<img")), "an icon survived on the size line");
    win.actIncognito->setChecked(true);
    QTRY_VERIFY(win.statusHint->toolTip().contains(tag));
    win.actIncognito->setChecked(false);

    // The glyph is rasterised for the SCREEN it will be shown on: at dpr 2 the same 16px element
    // carries a 32px PNG. Offscreen runs at 1x, so the ratio is passed in.
    for (const qreal dpr : {qreal(1), qreal(2)}) {
      const QString html =
          stencil::gui::inlineIconHtml(QStringLiteral("incognito"), accent, 16, QString(), dpr);
      QVERIFY(html.contains(QStringLiteral("width=\"16\"")));
      QCOMPARE(pngOf(html).width(), qRound(16 * dpr));
    }
    QVERIFY2(stencil::gui::inlineIconHtml(QStringLiteral("no-such-glyph"), accent, 16).isEmpty(),
             "an unknown glyph must degrade to nothing, not to a broken <img>");
    beat();
  }

  // The badge comes and goes in whatever motion is SELECTED (support/controlReveal.hpp
  // revealControls, browser toolbar.js): a cloud in the particle modes, the slot alone in
  // `slide`, and a plain show/hide under `none` — the browser's five modes, one for one.
  void incognitoBadgeComesAndGoesInTheSelectedMode() {
    using stencil::support::MotionMode;
    const MotionMode had = stencil::support::motionMode();
    auto restoreMode = qScopeGuard([had] { stencil::support::setMotionMode(had); });
    auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1200, 820);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QLabel* badge = win.incognitoTag;
    QVERIFY(badge && !badge->isVisible());
    settleLayout(&win, 300);
    const QString cloudName = QString::fromLatin1(stencil::gui::CONTROL_REVEAL_OBJECT_NAME);
    const auto clouds = [&] { return int(win.findChildren<QWidget*>(cloudName).size()); };
    // The widest the slot may be while it is still closed: a grown one has been handed its cap back.
    const auto slotClosed = [badge] { return badge->maximumWidth() < badge->sizeHint().width(); };

    for (const MotionMode mode : {MotionMode::PARTICLES, MotionMode::WATER, MotionMode::FIRE}) {
      stencil::support::setMotionMode(mode);
      win.actIncognito->setChecked(true);
      QVERIFY2(badge->isVisible(), "the badge takes its slot at once, whatever flies over it");
      QVERIFY2(slotClosed(), "the slot starts closed, not at a flash of full width");
      QTRY_VERIFY2(clouds() == 1 && badge->graphicsEffect() != nullptr,
                   "no cloud gathered over the arriving badge");
      QTRY_VERIFY_WITH_TIMEOUT(!slotClosed(), stencil::gui::CONTROL_REVEAL_IN_MS + 3000);
      QTRY_VERIFY_WITH_TIMEOUT(clouds() == 0 && badge->graphicsEffect() == nullptr,
                               stencil::gui::CONTROL_REVEAL_IN_MS + 3000);

      win.actIncognito->setChecked(false);
      QTRY_VERIFY2(clouds() == 1, "the leaving badge did not come apart");
      QVERIFY2(badge->isVisible(), "the badge holds its slot while the collapse runs");
      QTRY_VERIFY_WITH_TIMEOUT(!badge->isVisible(), stencil::gui::CONTROL_REVEAL_OUT_MS + 3000);
      QTRY_VERIFY_WITH_TIMEOUT(clouds() == 0, stencil::gui::CONTROL_REVEAL_OUT_MS + 3000);
      beat();
    }

    // `slide`: no particles at all, and the slot is then the whole flight.
    stencil::support::setMotionMode(MotionMode::SLIDE);
    win.actIncognito->setChecked(true);
    QVERIFY(badge->isVisible() && slotClosed());
    int peak = 0;
    QElapsedTimer t;
    t.start();
    for (; t.elapsed() < stencil::gui::CONTROL_REVEAL_IN_MS + 3000 && slotClosed(); QTest::qWait(10))
      peak = std::max(peak, clouds());
    QVERIFY2(!slotClosed(), "slide never opened the badge's slot");
    QCOMPARE(peak, 0);
    QVERIFY2(badge->graphicsEffect() == nullptr, "with no motes to wait for, nothing veils the badge");
    win.actIncognito->setChecked(false);
    QVERIFY2(badge->isVisible(), "slide closes the slot before the badge goes");
    QCOMPARE(clouds(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(!badge->isVisible(), stencil::gui::CONTROL_REVEAL_OUT_MS + 3000);

    // `none` and STENCIL_NO_ANIM: the end state, in the same frame, with nothing in the air.
    for (const bool viaEnv : {false, true}) {
      stencil::support::setMotionMode(viaEnv ? MotionMode::PARTICLES : MotionMode::NONE);
      if (viaEnv) qputenv("STENCIL_NO_ANIM", "1");
      win.actIncognito->setChecked(true);
      QVERIFY(badge->isVisible());
      QCOMPARE(clouds(), 0);
      QVERIFY2(badge->graphicsEffect() == nullptr, "no motion left a veil on the badge");
      QVERIFY2(!slotClosed(), "no motion must not leave the slot part-open");
      win.actIncognito->setChecked(false);
      QVERIFY2(!badge->isVisible(), "the badge must go in the same frame");
      QCOMPARE(clouds(), 0);
      if (viaEnv) qunsetenv("STENCIL_NO_ANIM");
    }
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chromeIncognito.gui.moc"
