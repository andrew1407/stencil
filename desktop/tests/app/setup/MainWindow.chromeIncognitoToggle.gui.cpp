// MainWindow GUI e2e — Toggling incognito moves nothing.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

#include "controlReveal.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The incognito indicator is DECOR: toggling it must not move, resize or reflow a single other
  // widget (user report: "the points panel jumps down a little", and the canvas with it).
  void incognitoToggleMovesNothing() {
    for (const QString& mode : {QStringLiteral("light"), QStringLiteral("dark")}) {
      MainWindow win(nullptr, false);
      win.resize(1200, 820);
      win.show();
      QVERIFY(QTest::qWaitForWindowExposed(&win));
      win.settings.themeMode = mode;
      win.applyTheme();
      settleLayout(&win, 150);
      QVERIFY(win.actIncognito && !win.actIncognito->isChecked());
      // With the assistant OPEN, so the dock is a real on-screen neighbour of the
      // canvas rather than a hidden widget whose geometry means nothing.
      win.actChat->setChecked(true);
      QTRY_VERIFY(win.chatDock->isVisible());
      awaitAnim(win.chatAnim);

      // Every widget the tag could possibly push around, in window coordinates.
      const auto snapshot = [&win] {
        QMap<QString, QRect> out;
        // Only what is actually ON SCREEN: a hidden widget has no geometry to
        // disturb, and Qt re-lays hidden docks whenever it likes.
        const auto add = [&](const QString& name, QWidget* w) {
          if (w && w->isVisible()) out.insert(name, QRect(w->mapTo(&win, QPoint(0, 0)), w->size()));
        };
        add(QStringLiteral("canvas viewport"), win.scroll->viewport());
        add(QStringLiteral("canvas"), win.canvas);
        add(QStringLiteral("points panel"), win.selPanel);
        add(QStringLiteral("chat dock"), win.chatDock);
        add(QStringLiteral("coord readout"), win.status);
        for (QToolBar* tb : win.findChildren<QToolBar*>())
          add(QStringLiteral("toolbar ") + tb->objectName(), tb);
        return out;
      };
      // Under load the opening layout can be a pass short when the baseline is taken, and
      // the settle that follows then reads as a move. Read it only once it holds still.
      const auto steady = [&] {
        QMap<QString, QRect> a = snapshot();
        for (int i = 0, held = 0; i < 60; ++i) {
          QTest::qWait(25);
          const QMap<QString, QRect> b = snapshot();
          held = (a == b) ? held + 1 : 0;
          a = b;
          if (held >= 4) break;   // four quiet looks: the opening passes are done
        }
        return a;
      };
      const auto same = [&](const QMap<QString, QRect>& a, const QMap<QString, QRect>& b,
                            const QString& what) {
        QCOMPARE(a.keys(), b.keys());
        for (auto it = a.cbegin(); it != a.cend(); ++it) {
          const QRect& was = it.value();
          const QRect& now = b.value(it.key());
          QVERIFY2(was == now,
                   qPrintable(QString("%1: %2 moved %3,%4 %5x%6 -> %7,%8 %9x%10")
                                  .arg(what, it.key())
                                  .arg(was.x()).arg(was.y()).arg(was.width()).arg(was.height())
                                  .arg(now.x()).arg(now.y()).arg(now.width()).arg(now.height())));
        }
      };

      for (const bool loaded : {false, true}) {
        if (loaded) {
          QImage pic(376, 501, QImage::Format_RGB32);
          pic.fill(QColor("#2a6f97"));
          win.canvas->loadFromImage(pic);
          QTRY_VERIFY(win.canvas->hasImage());
          win.updateImageSizeInfo();
        }
        const QString state = QStringLiteral("%1/%2").arg(mode, loaded ? "loaded" : "empty");
        // Baseline after ONE round: the info bar reserves the tallest box it has ever needed, so the first
        // tag it shows can still grow that reserve by a pixel. Every toggle after that must not move.
        win.actIncognito->setChecked(true);
        settleLayout(&win, 150);
        win.actIncognito->setChecked(false);
        const QMap<QString, QRect> before = steady();
        const int hintBefore = win.imageSizeInfo->sizeHint().height();

        win.actIncognito->setChecked(true);
        QTRY_VERIFY_WITH_TIMEOUT(win.incognitoTag->isVisible(), 150);
        QVERIFY2(win.incognitoTag->text().contains(QStringLiteral("Incognito")),
                 qPrintable(state + ": the tag never appeared — the check would be vacuous"));
        same(before, steady(), state + " on");
        QCOMPARE(win.imageSizeInfo->sizeHint().height(), hintBefore);

        win.actIncognito->setChecked(false);
        QTRY_VERIFY_WITH_TIMEOUT(!win.incognitoTag->isVisible(), 150);
        same(before, steady(), state + " off again");
        QCOMPARE(win.imageSizeInfo->sizeHint().height(), hintBefore);
      }
    }
    beat();
  }

  // …and the size text itself may not move a pixel, at rest OR mid-flight: the badge rode the
  // same label as rich text once, and the plain-to-rich swap lifted the line (user report).
  void incognitoBadgeNeverMovesTheSizeText() {
    auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1200, 820);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 200);
    QLabel* info = win.imageSizeInfo;
    QVERIFY(info && win.incognitoTag);
    // The label's own rows of ink at dpr 2, beside its place in the window and its text FORMAT:
    // a label that switches plain to rich re-places its baseline without moving a single rect.
    const auto row = [&] {
      QPixmap pm(info->size() * 2);
      pm.setDevicePixelRatio(2);
      pm.fill(Qt::transparent);
      info->render(&pm, QPoint(), QRegion(), QWidget::DrawChildren);
      const QImage shot = pm.toImage().convertToFormat(QImage::Format_ARGB32);
      int first = -1, last = -1;
      for (int y = 0; y < shot.height(); ++y) {
        bool ink = false;
        for (int x = 0; x < shot.width() && !ink; ++x) ink = qAlpha(shot.pixel(x, y)) > 24;
        if (ink) { if (first < 0) first = y; last = y; }
      }
      return QString("fmt=%1 y=%2 h=%3 bar=%4 dock=%5 ink=%6..%7")
          .arg(int(info->textFormat())).arg(info->mapTo(&win, QPoint(0, 0)).y())
          .arg(info->height()).arg(win.imageInfoBar->height())
          .arg(win.imageInfoDock->height()).arg(first).arg(last);
    };
    const QString rest = row();
    QVERIFY2(!rest.endsWith(QStringLiteral("ink=-1..-1")),
             qPrintable(QString("the size line inked nothing: %1").arg(rest)));
    const auto hold = [&](const QString& what, int ms) {
      QElapsedTimer t;
      t.start();
      while (t.elapsed() < ms) {
        const QString now = row();
        QVERIFY2(now == rest, qPrintable(QString("%1: %2 (was %3)").arg(what, now, rest)));
        QTest::qWait(8);
      }
    };
    win.actIncognito->setChecked(true);
    hold(QStringLiteral("badge arriving"), stencil::gui::CONTROL_REVEAL_IN_MS + 400);
    QVERIFY2(win.incognitoTag->isVisible(), "the badge never arrived — the check would be vacuous");
    win.actIncognito->setChecked(false);
    hold(QStringLiteral("badge leaving"), stencil::gui::CONTROL_REVEAL_OUT_MS + 400);
    QVERIFY(!win.incognitoTag->isVisible());
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chromeIncognitoToggle.gui.moc"
