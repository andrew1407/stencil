// MainWindow GUI e2e — The incognito frame, and the tag riding the image-size line.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The incognito frame DRAWS ON clockwise from the top-left and retracts the same way — the desktop
  // half of the browser's four staggered .ig-edge elements. framePath is pure, so no display.
  void incognitoFrameDrawsClockwiseFromTheTopLeft() {
    using stencil::gui::IncognitoOverlay;
    const QRectF box(0, 0, 200, 100);

    QVERIFY2(IncognitoOverlay::framePath(box, 0.0).isEmpty(), "nothing is drawn at rest");

    // An eighth in: half the TOP edge, and nothing else has started.
    const QPainterPath eighth = IncognitoOverlay::framePath(box, 0.125);
    QCOMPARE(eighth.boundingRect().width(), 100.0);
    QCOMPARE(eighth.boundingRect().height(), 0.0);
    QCOMPARE(eighth.boundingRect().top(), 0.0);

    // Past the first quarter the right edge is running, still along the top-right.
    const QPainterPath half = IncognitoOverlay::framePath(box, 0.5);
    QCOMPARE(half.boundingRect().width(), 200.0);
    QCOMPARE(half.boundingRect().height(), 100.0);   // right edge fully down
    QVERIFY2(half.boundingRect().left() == 0.0, "the bottom edge has not started");

    // Closed: the full perimeter, and it only closes at the very end.
    const QPainterPath done = IncognitoOverlay::framePath(box, 1.0);
    QCOMPARE(done.boundingRect(), box);
    // The last edge CLIMBS from the bottom-left, so the loop closes at the top-left it
    // started from. currentPosition is where that edge has reached: 60% up at t=0.9.
    QCOMPARE(IncognitoOverlay::framePath(box, 0.9).currentPosition(), QPointF(0.0, 40.0));
    QCOMPARE(done.currentPosition(), QPointF(0.0, 0.0));

    // Out-of-range input is clamped, never extrapolated.
    QCOMPARE(IncognitoOverlay::framePath(box, 2.0).boundingRect(), box);
    QVERIFY(IncognitoOverlay::framePath(box, -1.0).isEmpty());
    QVERIFY2(IncognitoOverlay::framePath(QRectF(), 1.0).isEmpty(), "an empty viewport draws nothing");
  }

  // Incognito shows INLINE on the image-size line (browser parity: an accent bold tag behind a muted
  // "|"), in both states and only while on. The glyph is the app's themed icon, never an emoji.
  void incognitoTagRidesTheImageSizeLine() {
    MainWindow win(nullptr, false);
    win.resize(1200, 820);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QLabel* info = win.imageSizeInfo_;
    QVERIFY(info);
    const QString tag = QStringLiteral("Incognito");

    // Empty editor, incognito OFF: the plain hint, no tag.
    QVERIFY(!info->text().contains(tag));
    QCOMPARE(info->textFormat(), Qt::PlainText);

    // Empty editor, incognito ON: the tag rides beside "No image loaded".
    win.actIncognito_->setChecked(true);
    QTRY_VERIFY2(info->text().contains(tag), "no incognito tag on the empty line");
    QVERIFY2(info->text().contains(QStringLiteral("No image loaded")),
             "the empty-state text was replaced instead of extended");
    QCOMPARE(info->textFormat(), Qt::RichText);
    const QColor accent =
        stencil::gui::themePalette(stencil::gui::resolveDark(win.settings_.themeMode),
                                   win.settings_.accentColor).accent;
    QVERIFY2(info->text().contains(accent.name()), "the tag is not accent-coloured");
    QVERIFY2(info->text().contains(QStringLiteral("font-weight:700")), "the tag is not bold");

    // The divider + the real icon, in whichever state the line is in.
    const QString muted =
        stencil::gui::themePalette(stencil::gui::resolveDark(win.settings_.themeMode),
                                   win.settings_.accentColor).textMuted.name();
    const auto checkTagChrome = [&](const char* state) {
      const QString html = info->text();
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
    win.canvas_->loadFromImage(pic);
    QTRY_VERIFY(win.canvas_->hasImage());
    win.updateImageSizeInfo();
    QVERIFY2(info->text().contains(QStringLiteral("Image Size:")) &&
                 info->text().contains(QString::number(win.canvas_->imageWidth())),
             "the size left the line");
    QVERIFY2(info->text().contains(tag), "no incognito tag beside the size");
    checkTagChrome("loaded");

    // …and it goes when incognito does — divider included, so a plain line never
    // ends in a dangling separator. The "?" bubble still carries the fact.
    win.actIncognito_->setChecked(false);
    QTRY_VERIFY2(!info->text().contains(tag), "the tag outlived incognito");
    QCOMPARE(info->textFormat(), Qt::PlainText);
    QVERIFY2(!info->text().contains(QLatin1Char('|')), "a divider survived the tag");
    QVERIFY2(!info->text().contains(QStringLiteral("<img")), "an icon survived the tag");
    win.actIncognito_->setChecked(true);
    QTRY_VERIFY(win.statusHint_->toolTip().contains(tag));
    win.actIncognito_->setChecked(false);

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

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chromeIncognito.gui.moc"
