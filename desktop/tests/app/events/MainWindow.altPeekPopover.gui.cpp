// MainWindow GUI e2e — the Alt peeks around the popovers: the logo's accent peek hides whole on
// release in every motion mode, and a selector inside a peeked mini window or a modal dialog peeks
// without closing it, the mini window lingering while the pointer is in that selector's list.
#include "altPeekGui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  static QToolButton* buttonFor(MainWindow& win, QAction* act) {
    for (auto it = win.pop.buttons.cbegin(); it != win.pop.buttons.cend(); ++it)
      if (it.value() == act && static_cast<QWidget*>(it.key())->isVisible())
        return static_cast<QToolButton*>(it.key());
    return nullptr;
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }
  void cleanup() { stencil::support::setMotionMode(MotionMode::PARTICLES); }

  void theLogoAccentPeekHidesWholeOnRelease_data() { addMotionRows(); }
  void theLogoAccentPeekHidesWholeOnRelease() {
    QFETCH(int, mode);
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    bootForPeeks(win, MotionMode(mode));
    if (!cursorWarps(win)) QSKIP("the platform ignores QCursor::setPos");
    QToolButton* logo = win.logoBtn;
    if (QWidget* fw = QApplication::focusWidget()) fw->clearFocus();
    // A settled peek, a release while its open flight still runs, and one over the logo itself.
    const QList<QPair<int, bool>> rounds = {{450, true}, {60, true}, {450, false}};
    for (const QPair<int, bool>& round : rounds) {
      const int releaseAfterMs = round.first;
      const bool moveAway = round.second;   // a plain local: a lambda may not capture a binding in C++17
      QCursor::setPos(centreOf(logo));
      logo->setAttribute(Qt::WA_UnderMouse, true);
      bool opened = false, stuck = false;
      QTimer::singleShot(releaseAfterMs, &win, [&] {
        opened = win.pop.active && win.pop.active->objectName() == QLatin1String("accentPopover") &&
                 win.pop.active->isVisible();
        if (moveAway) {
          logo->setAttribute(Qt::WA_UnderMouse, false);
          QCursor::setPos(centreOf(win.canvas));
        }
        altKey(QEvent::KeyRelease);   // to the popover, which holds the focus
      });
      QTimer watchdog;
      watchdog.setSingleShot(true);
      QObject::connect(&watchdog, &QTimer::timeout, &win, [&] {
        stuck = bool(win.pop.active);
        if (win.pop.active) win.pop.active->done(QDialog::Rejected);
      });
      watchdog.start(4000);
      altKey(QEvent::KeyPress);   // blocks in the peek until its close has run
      watchdog.stop();
      const QByteArray at = QByteArray::number(releaseAfterMs) + (moveAway ? " ms, away" : " ms, on the logo");
      QVERIFY2(opened, "Alt over the logo did not peek the accent popover: " + at);
      QVERIFY2(!stuck, "the accent popover never finished closing after the Alt release: " + at);
      QVERIFY(!win.pop.active);
      QTRY_VERIFY2(win.findChildren<QWidget*>(QStringLiteral("popoverOverlay")).isEmpty(),
                   "the popover's box outlived its close");
      QTRY_VERIFY2_WITH_TIMEOUT(strayWindows(win).isEmpty(),
                                qPrintable(strayWindows(win).join(", ")), 2000);
      logo->setAttribute(Qt::WA_UnderMouse, false);
    }
  }

  void aSelectorInsideAPeekedMiniWindowKeepsIt_data() { addMotionRows(); }
  void aSelectorInsideAPeekedMiniWindowKeepsIt() {
    QFETCH(int, mode);
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    bootForPeeks(win, MotionMode(mode));
    if (!cursorWarps(win)) QSKIP("the platform ignores QCursor::setPos");
    QToolButton* btn = buttonFor(win, win.actSettings);
    QVERIFY2(btn, "no visible Visuals button");
    if (QWidget* fw = QApplication::focusWidget()) fw->clearFocus();
    QCursor::setPos(centreOf(btn));
    for (auto it = win.pop.buttons.cbegin(); it != win.pop.buttons.cend(); ++it)
      static_cast<QWidget*>(it.key())->setAttribute(Qt::WA_UnderMouse, it.key() == btn);
    QPointer<QDialog> box;
    QPointer<QComboBox> combo;
    bool opened = false, hasCombo = false, peeked = false, lingered = false;
    QTimer::singleShot(600, &win, [&] {
      box = win.pop.active.data();
      opened = box;
      btn->setAttribute(Qt::WA_UnderMouse, false);
      if (box) (void)QTest::qWaitFor([&] { return box && firstCombo(box); }, 2000);   // under load
      combo = box ? firstCombo(box) : nullptr;
      hasCombo = combo;
      if (!combo) return;
      QCursor::setPos(centreOf(combo));
      enterWidget(combo);   // Alt is still down
      QWidget* list = comboPopup(combo);
      peeked = list && win.pop.active == box;
      if (!list) return;
      // Parked beside the mini window, so only its ownership of the list can keep it open.
      const QRect r = win.popoverRectGlobal();
      list->move(r.right() + 30, r.top());
      QCursor::setPos(list->geometry().center());
      win.altHeldForTest = true;   // the icon glide poll runs, and must stand down over the list
      QTest::qWait(250);
      win.altHeldForTest = false;
      altKey(QEvent::KeyRelease);
      QTest::qWait(stencil::support::LINGER_CLOSE_MS + 400);
      lingered = win.pop.active == box && box && box->isVisible() && comboPopup(combo);
      QCursor::setPos(centreOf(win.canvas));
    });
    QTimer::singleShot(6000, &win, [&] { if (win.pop.active) win.pop.active->reject(); });
    altKey(QEvent::KeyPress);   // blocks in the Visuals peek until it closes
    QVERIFY2(opened, "Alt over the Visuals icon did not peek its mini window");
    QVERIFY2(hasCombo, "the Visuals mini window has no selector");
    QVERIFY2(peeked, "a selector inside the mini window did not peek, or closed the window");
    QVERIFY2(lingered, "released over its selector's list, the mini window must linger with it");
    QTRY_VERIFY2(!combo || !comboPopup(combo), "the selector's list outlived its window");
    QVERIFY(!win.pop.active);
  }

  void aSelectorInsideAModalDialogPeeks() {
    const auto motion = withoutMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    bootForPeeks(win, MotionMode::NONE);
    if (!cursorWarps(win)) QSKIP("the platform ignores QCursor::setPos");
    bool opened = false, closed = false, dialogKept = false;
    QTimer::singleShot(300, &win, [&] {
      auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
      QComboBox* c = dlg ? firstCombo(dlg) : nullptr;
      if (!c) { if (dlg) dlg->reject(); return; }
      const QPoint outside = dlg->mapToGlobal(QPoint(4, dlg->height() - 4));
      QCursor::setPos(outside);
      altKey(QEvent::KeyPress);
      QCursor::setPos(centreOf(c));
      enterWidget(c);
      opened = comboPopup(c) != nullptr;
      QCursor::setPos(outside);
      altKey(QEvent::KeyRelease);
      QTest::qWait(300);
      closed = !comboPopup(c);
      dialogKept = dlg->isVisible();
      dlg->reject();
    });
    win.actSettings->trigger();   // the full window, modal
    QVERIFY2(opened, "a selector inside the Visuals dialog did not peek");
    QVERIFY2(closed, "releasing Alt outside its list did not close it");
    QVERIFY2(dialogKept, "the selector's peek closed its dialog");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.altPeekPopover.gui.moc"
