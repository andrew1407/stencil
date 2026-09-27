// MainWindow GUI e2e — the logo's accent peek under a held Alt: it opens once from either entry
// order and never blinks, a rested row's preview (and its swap) holds while Alt is down, releasing
// on a row picks it and lingers, elsewhere closes or lingers as any peek does — every motion mode.
#include "altPeekGui.hpp"

namespace {
  // Every Show / Hide the accent popover takes: a blink is a Hide before the peek is done.
  struct Blinks : QObject {
    int shows = 0, hides = 0;
    bool eventFilter(QObject* o, QEvent* e) override {
      if (o->objectName() == QLatin1String("accentPopover")) {
        if (e->type() == QEvent::Show) ++shows;
        if (e->type() == QEvent::Hide) ++hides;
      }
      return false;
    }
  };
}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  // Alt-peeks the accent popover by `enterFirst` (Alt down, then the pointer arrives) or by Alt
  // pressed while resting, and runs `inside` in the peek's own loop; returns once it closed.
  static void peekLogo(MainWindow& win, bool enterFirst, const std::function<void()>& inside) {
    QToolButton* logo = win.tools.logoBtn;
    bool ran = false;
    QTimer::singleShot(450, &win, [&win, &ran, inside] {
      if (win.pop.active) inside();
      ran = true;
      QTimer::singleShot(6000, win.pop.active.data(), [&win] { if (win.pop.active) win.pop.active->reject(); });
    });
    if (enterFirst) {
      QCursor::setPos(centreOf(win.canvas));
      altKey(QEvent::KeyPress);
      QCursor::setPos(centreOf(logo));   // Qt's own Enter; the peek opens once it is dispatched
      QVERIFY(QTest::qWaitFor([&] { return ran && !win.pop.active; }, 9000));
    } else {
      QCursor::setPos(centreOf(logo));
      logo->setAttribute(Qt::WA_UnderMouse, true);
      altKey(QEvent::KeyPress);   // blocks in the peek
    }
    logo->setAttribute(Qt::WA_UnderMouse, false);
  }

  static QPushButton* otherRow(MainWindow& win) {
    for (const auto& a : stencil::gui::accentPresets())
      if (a.key != win.settings.accentColor && win.pop.active)
        return win.pop.active->findChild<QPushButton*>(QStringLiteral("accentRow-") + a.key);
    return nullptr;
  }

  // The pointer leaves the logo for `row` with Alt still down.
  static void onto(MainWindow& win, QWidget* row) {
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(win.tools.logoBtn, &leave);
    win.tools.logoBtn->setAttribute(Qt::WA_UnderMouse, false);
    QCursor::setPos(centreOf(row));
    enterWidget(row);
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void cleanup() { stencil::support::setMotionMode(MotionMode::PARTICLES); }

  void releasingAltOnARowPicksIt_data() {
    QTest::addColumn<int>("mode");
    QTest::addColumn<bool>("enterFirst");
    QTest::addColumn<bool>("rested");   // false: let go before the preview, so the pick's swap plays mid-linger
    for (const auto& [name, m] : {std::pair{"particles", MotionMode::PARTICLES},
                                  std::pair{"slide", MotionMode::SLIDE}, std::pair{"none", MotionMode::NONE}}) {
      QTest::newRow(qPrintable(QStringLiteral("%1, Alt then pointer").arg(name))) << int(m) << true << true;
      QTest::newRow(qPrintable(QStringLiteral("%1, pointer then Alt").arg(name))) << int(m) << false << true;
      QTest::newRow(qPrintable(QStringLiteral("%1, released at once").arg(name))) << int(m) << false << false;
    }
  }
  void releasingAltOnARowPicksIt() {
    QFETCH(int, mode);
    QFETCH(bool, enterFirst);
    QFETCH(bool, rested);
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    bootForPeeks(win, MotionMode(mode));
    if (!cursorWarps(win)) QSKIP("the platform ignores QCursor::setPos");
    win.canvas->setFocus(Qt::OtherFocusReason);   // an editing session's usual focus
    Blinks blinks;
    qApp->installEventFilter(&blinks);
    QString previewed;
    int showsOnLogo = -1, hidesHeld = -1, hidesPicked = -1;
    bool heldPreview = false, logoHovered = false, keptOpen = false, marked = false;
    peekLogo(win, enterFirst, [&] {
      QTest::qWait(300);   // the logo's own hover and the open flight settle under the pointer
      showsOnLogo = blinks.shows;
      logoHovered = win.tools.logoBtn->underMouse();
      QPushButton* row = otherRow(win);
      if (!row) return;
      previewed = row->property("accentKey").toString();
      onto(win, row);
      QTest::qWait(rested ? 280 + 900 : 30);   // the rested-intent delay, then the swap and its wake
      heldPreview = !rested || win.settings.accentColor == previewed;
      hidesHeld = blinks.hides;
      altKey(QEvent::KeyRelease);   // over the row: it is the pick, and the pointer is still inside
      QTest::qWait(600);
      keptOpen = win.pop.active && win.pop.active->isVisible() && win.settings.accentColor == previewed;
      marked = row->property("currentAccent").toBool();   // the ✓ moved onto the pick
      hidesPicked = blinks.hides;
      QCursor::setPos(centreOf(win.canvas));
      QEvent leave(QEvent::Leave);
      QApplication::sendEvent(row, &leave);
    });
    QTest::qWait(300);
    qApp->removeEventFilter(&blinks);
    QVERIFY2(!previewed.isEmpty(), "the accent peek never opened, or has no other colour");
    QCOMPARE(showsOnLogo, 1);   // opened exactly once
    QVERIFY2(logoHovered, "Qt's hover record went stale in the peek: it opened inside the Enter");
    QVERIFY2(hidesHeld == 0, qPrintable(QStringLiteral("the peek blinked %1 time(s) with Alt held").arg(hidesHeld)));
    QVERIFY2(heldPreview, "the rested colour's preview did not hold while Alt was down");
    QVERIFY2(keptOpen, "released on a row, the pick must apply and the popover stay open");
    QVERIFY2(marked, "the ✓ did not move to the picked row");
    QCOMPARE(hidesPicked, 0);
    QVERIFY(!win.pop.active);
    QCOMPARE(blinks.hides, 1);   // closed once, when the pointer left; never re-shown
    QCOMPARE(blinks.shows, 1);
    QCOMPARE(win.settings.accentColor, previewed);   // leaving did not revert the pick
    QTRY_COMPARE(stencil::gui::fileStore::loadSettings().accentColor, previewed);   // persisted
  }

  void releasingAltOffTheRowsKeepsTheRules_data() {
    QTest::addColumn<int>("mode");
    QTest::addColumn<bool>("outside");
    for (const auto& [name, m] : {std::pair{"particles", MotionMode::PARTICLES},
                                  std::pair{"slide", MotionMode::SLIDE}, std::pair{"none", MotionMode::NONE}}) {
      QTest::newRow(qPrintable(QStringLiteral("%1, outside").arg(name))) << int(m) << true;
      QTest::newRow(qPrintable(QStringLiteral("%1, on its padding").arg(name))) << int(m) << false;
    }
  }
  void releasingAltOffTheRowsKeepsTheRules() {
    QFETCH(int, mode);
    QFETCH(bool, outside);
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    bootForPeeks(win, MotionMode(mode));
    if (!cursorWarps(win)) QSKIP("the platform ignores QCursor::setPos");
    win.canvas->setFocus(Qt::OtherFocusReason);
    const QString committed = win.settings.accentColor;
    Blinks blinks;
    qApp->installEventFilter(&blinks);
    bool ran = false, lingered = false;
    peekLogo(win, /*enterFirst=*/false, [&] {
      QPushButton* row = otherRow(win);
      if (!row) return;
      ran = true;
      onto(win, row);
      QTest::qWait(280 + 900);
      QDialog* box = win.pop.active.data();
      // The popover's own margin, level with the row: inside it, on no colour.
      const QPoint padding = box->mapToGlobal(QPoint(2, row->geometry().center().y()));
      QCursor::setPos(outside ? centreOf(win.canvas) : padding);
      QEvent leave(QEvent::Leave);
      QApplication::sendEvent(row, &leave);
      altKey(QEvent::KeyRelease);
      QTest::qWait(600);
      lingered = win.pop.active == box && box->isVisible();   // a close hides it at once
      QCursor::setPos(centreOf(win.canvas));
    });
    QTest::qWait(300);
    qApp->removeEventFilter(&blinks);
    QVERIFY2(ran, "the accent peek never opened, or has no other colour");
    if (outside) QVERIFY2(!lingered, "released outside, the peek must close");
    else QVERIFY2(lingered, "released on the popover's padding, the peek must linger");
    QVERIFY(!win.pop.active);
    QCOMPARE(blinks.hides, 1);
    QCOMPARE(win.settings.accentColor, committed);   // no pick: the preview went back
  }

  void aPickInsideTheHeldPeekCommits() {
    const auto motion = withoutMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    bootForPeeks(win, MotionMode::NONE);
    if (!cursorWarps(win)) QSKIP("the platform ignores QCursor::setPos");
    const QString committed = win.settings.accentColor;
    QString picked;
    peekLogo(win, /*enterFirst=*/false, [&] {
      QPushButton* row = otherRow(win);
      if (!row) return;
      picked = row->property("accentKey").toString();
      onto(win, row);
      row->click();
      altKey(QEvent::KeyRelease);
    });
    QVERIFY(!picked.isEmpty() && picked != committed);
    QVERIFY(!win.pop.active);
    QCOMPARE(win.settings.accentColor, picked);
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.altPeekAccent.gui.moc"
