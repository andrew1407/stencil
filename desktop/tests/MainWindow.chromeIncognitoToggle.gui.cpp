// MainWindow GUI e2e — Toggling incognito moves nothing.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The incognito indicator is DECOR: toggling it must not move, resize or reflow a
  // single other widget. It did — the inline glyph made the info line's box 2 px taller,
  // the info toolbar follows its only widget, and everything below it (canvas viewport,
  // points panel, the rows under them) dropped by those 2 px on every toggle (user
  // report: "the points panel jumps down a little", and the canvas with it).
  void incognitoToggleMovesNothing() {
    for (const QString& mode : {QStringLiteral("light"), QStringLiteral("dark")}) {
      MainWindow win(nullptr, false);
      win.resize(1200, 820);
      win.show();
      QVERIFY(QTest::qWaitForWindowExposed(&win));
      win.settings_.themeMode = mode;
      win.applyTheme();
      settleLayout(&win, 150);
      QVERIFY(win.actIncognito_ && !win.actIncognito_->isChecked());
      // With the assistant OPEN, so the dock is a real on-screen neighbour of the
      // canvas rather than a hidden widget whose geometry means nothing.
      win.actChat_->setChecked(true);
      QTRY_VERIFY(win.chatDock_->isVisible());
      awaitAnim(win.chatAnim_);

      // Every widget the tag could possibly push around, in window coordinates.
      const auto snapshot = [&win] {
        QMap<QString, QRect> out;
        // Only what is actually ON SCREEN: a hidden widget has no geometry to
        // disturb, and Qt re-lays hidden docks whenever it likes.
        const auto add = [&](const QString& name, QWidget* w) {
          if (w && w->isVisible()) out.insert(name, QRect(w->mapTo(&win, QPoint(0, 0)), w->size()));
        };
        add(QStringLiteral("canvas viewport"), win.scroll_->viewport());
        add(QStringLiteral("canvas"), win.canvas_);
        add(QStringLiteral("points panel"), win.selPanel_);
        add(QStringLiteral("chat dock"), win.chatDock_);
        add(QStringLiteral("coord readout"), win.status_);
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
          win.canvas_->loadFromImage(pic);
          QTRY_VERIFY(win.canvas_->hasImage());
          win.updateImageSizeInfo();
        }
        const QString state = QStringLiteral("%1/%2").arg(mode, loaded ? "loaded" : "empty");
        // Baseline after ONE round: the info bar reserves the tallest box it has ever
        // needed, so the first tag it is ever shown can still grow that reserve by a
        // pixel. What must never move is every toggle after that.
        win.actIncognito_->setChecked(true);
        settleLayout(&win, 150);
        win.actIncognito_->setChecked(false);
        const QMap<QString, QRect> before = steady();
        const int hintBefore = win.imageSizeInfo_->sizeHint().height();

        win.actIncognito_->setChecked(true);
        QTRY_VERIFY_WITH_TIMEOUT(win.imageSizeInfo_->text().contains(QStringLiteral("Incognito")), 150);
        QVERIFY2(win.imageSizeInfo_->text().contains(QStringLiteral("Incognito")),
                 qPrintable(state + ": the tag never appeared — the check would be vacuous"));
        same(before, steady(), state + " on");
        QCOMPARE(win.imageSizeInfo_->sizeHint().height(), hintBefore);

        win.actIncognito_->setChecked(false);
        QTRY_VERIFY_WITH_TIMEOUT(!win.imageSizeInfo_->text().contains(QStringLiteral("Incognito")), 150);
        same(before, steady(), state + " off again");
        QCOMPARE(win.imageSizeInfo_->sizeHint().height(), hintBefore);
      }
    }
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chromeIncognitoToggle.gui.moc"
