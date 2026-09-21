// MainWindow GUI e2e — The blank image card: its hover shimmer, and that it is the only click target.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The blank-image card gets the browser's glass sweep on hover (layout.css ui-shimmer):
  // a light band crossing it once. Asserts the band genuinely MOVES, not just appears.
  void blankImageCardShimmersOnHover() {
    MainWindow win;
    win.resize(900, 640);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // A pixel test owns its palette: on the LIGHT theme the card's dashed border is paler than
    // its accent fill, so the brightest-pixel search locks onto the border. Not persisted.
    {
      Settings s = win.settings;
      s.themeMode = QStringLiteral("dark");
      win.applySettings(s, /*persist=*/false);
      settleLayout(&win, 60);
    }
    win.canvas->clearImage();
    win.refreshActions();
    QVERIFY(waitForIdleCard(win));   // past the clear-dust hold that hides the card
    // Brightest column of the card's mid row — where the band is right now.
    const auto bandX = [&] {
      const QImage im = win.canvas->grab().toImage();
      const QColor ground = im.pixelColor(0, im.height() / 2);   // canvas backdrop
      const auto offCard = [&](const QColor& c) {
        return qAbs(c.red() - ground.red()) + qAbs(c.green() - ground.green())
             + qAbs(c.blue() - ground.blue()) < 40;
      };
      // Sample just under the card's top edge: the glyph and label sit lower and their
      // white ink would win the brightest-pixel search every time.
      int top = -1, bottom = -1;
      for (int yy = 0; yy < im.height(); ++yy)
        if (!offCard(im.pixelColor(im.width() / 2, yy))) { if (top < 0) top = yy; bottom = yy; }
      if (top < 0) return -1;
      const int y = top + (bottom - top) / 8;
      // The card's horizontal span on that row, inset past the dashed border — which is
      // lighter than the fill and would win the search at a fixed position every time.
      int left = -1, right = -1;
      for (int x = 0; x < im.width(); ++x)
        if (!offCard(im.pixelColor(x, y))) { if (left < 0) left = x; right = x; }
      if (left < 0 || right - left < 24) return -1;
      int best = -1;
      double brightest = -1;
      for (int x = left + 6; x <= right - 6; ++x) {
        const QColor c = im.pixelColor(x, y);
        const double lum = c.redF() + c.greenF() + c.blueF();
        if (lum > brightest) { brightest = lum; best = x; }   // the band is the palest part
      }
      return best;
    };
    const QPoint c(win.canvas->width() / 2, win.canvas->height() / 2);
    QMouseEvent move(QEvent::MouseMove, QPointF(c), win.canvas->mapToGlobal(c),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(win.canvas, &move);
    QTest::qWait(150);
    const int first = bandX();
    QTest::qWait(220);
    const int second = bandX();
    QVERIFY2(second > first,
             qPrintable(QString("the sweep does not travel: %1 then %2").arg(first).arg(second)));
  }

  // "＋ Blank image" is a BUTTON, not the whole empty page: only the card's own rect clicks,
  // and only over it is the cursor a hand.
  void blankImageCardIsTheOnlyClickTarget() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    CanvasWidget* canvas = win.canvas;
    QVERIFY(canvas && !canvas->hasImage());
    canvas->grab();   // painting the card is what computes its rect
    const QRect cardGlobal = canvas->idleCardGlobalRect();
    QVERIFY2(cardGlobal.isValid(), "the idle card was never painted");
    const QRect card(canvas->mapFromGlobal(cardGlobal.topLeft()), cardGlobal.size());
    QVERIFY2(canvas->rect().contains(card), "the card must sit inside the canvas");

    // Any creator dialog that opens is closed at once (it would block on exec), and
    // counted — a dialog appearing IS the observable "it created a blank image" step.
    int dialogs = 0;
    QTimer watchdog;
    connect(&watchdog, &QTimer::timeout, &win, [&] {
      if (QWidget* modal = QApplication::activeModalWidget()) {
        ++dialogs;
        modal->close();
      }
    });
    watchdog.start(20);

    QSignalSpy asked(canvas, &CanvasWidget::blankImageRequested);
    const auto pressAt = [&](const QPoint& p) {
      QMouseEvent press(QEvent::MouseButtonPress, QPointF(p), canvas->mapToGlobal(p),
                        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(canvas, &press);
      QTest::qWait(60);
    };
    const auto moveTo = [&](const QPoint& p) {
      QMouseEvent move(QEvent::MouseMove, QPointF(p), canvas->mapToGlobal(p), Qt::NoButton,
                       Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(canvas, &move);
    };

    // ── OUTSIDE the card: bare page, in every direction that exists ──
    QList<QPoint> outside;
    for (const QPoint& p : {QPoint(4, 4),
                            QPoint(canvas->width() - 4, 4),
                            QPoint(4, canvas->height() - 4),
                            QPoint(canvas->width() - 4, canvas->height() - 4),
                            QPoint(canvas->width() / 2, card.top() - 12),
                            QPoint(card.left() - 12, card.center().y()),
                            QPoint(card.right() + 12, card.center().y()),
                            QPoint(canvas->width() / 2, card.bottom() + 12)})
      if (canvas->rect().contains(p) && !card.contains(p)) outside << p;
    QVERIFY2(outside.size() >= 4, "not enough bare-canvas points to test");
    for (const QPoint& p : outside) {
      moveTo(p);
      QVERIFY2(canvas->cursor().shape() != Qt::PointingHandCursor,
               qPrintable(QString("hand cursor on bare canvas at %1,%2").arg(p.x()).arg(p.y())));
      pressAt(p);
      QVERIFY2(asked.isEmpty(),
               qPrintable(QString("a click on bare canvas at %1,%2 asked for a blank image")
                              .arg(p.x()).arg(p.y())));
      QVERIFY2(dialogs == 0, "a click on bare canvas opened the blank-image creator");
      QVERIFY2(!canvas->hasImage(), "a click on bare canvas created an image");
    }

    // ── ON the card: the button works, cursor and all ──
    moveTo(card.center());
    QCOMPARE(canvas->cursor().shape(), Qt::PointingHandCursor);
    pressAt(card.center());
    QCOMPARE(asked.size(), 1);
    QTRY_VERIFY2(dialogs >= 1, "clicking the card did not open the blank-image creator");
    // …and its edges belong to it too (one pixel inside each corner).
    asked.clear();
    pressAt(card.topLeft() + QPoint(2, 2));
    QCOMPARE(asked.size(), 1);
    watchdog.stop();
    QTest::qWait(50);
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.canvasBlank.gui.moc"
