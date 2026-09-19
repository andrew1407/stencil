// MainWindow GUI e2e — A dialog flying from a card, the hover shimmer, and icon motion without reflow.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Clicking the "＋ Blank image" card opens the dialog out of THE CARD, not the toolbar
  // icon the same command flies from when picked there.
  void blankImageDialogFliesFromTheCard() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    MainWindow win;
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.canvas_->clearImage();
    win.refreshActions();
    QVERIFY2(waitForIdleCard(win), "the blank-image card never came back after the clear");
    const QRect card = win.canvas_->idleCardGlobalRect();
    QVERIFY2(card.isValid(), "the blank-image card is not on screen");

    QPoint start(-1, -1);
    QTimer::singleShot(140, &win, [&] {
      start = surfaceFlightTarget(&win);
      if (QWidget* modal = QApplication::activeModalWidget()) modal->close();
    });
    // The CLOSE flight is captured separately: its motes must pour back into the CARD.
    // It used to ignore the anchor rect and shrink into the box above instead.
    QPoint closeEnd(-1, -1);
    bool closeScatters = false;
    QTimer::singleShot(300, &win, [&] {
      if (auto* fx = surfaceFlight(&win)) {
        closeEnd = fx->surfaceTarget();
        closeScatters = !fx->gathering();
      }
    });
    emit win.canvas_->blankImageRequested();
    settle([&] { return closeEnd != QPoint(-1, -1); }, 500);
    const QPoint want = win.mapFromGlobal(card.center());
    QCOMPARE(start, want);
    QVERIFY2(closeScatters, "the close must come APART into the card, not form out of it");
    QVERIFY2(closeEnd == want, qPrintable(QString("the close pours into %1, the card is at %2")
                                              .arg(QDebug::toString(closeEnd), QDebug::toString(want))));
  }

  // The hover shimmer must genuinely ANIMATE: after a hover-enter the overlay's sweep progress
  // ADVANCES between two samples inside the 325 ms window and clears (-1) on completion.
  void hoverShimmerAnimates() {
    const auto motion = withMotion();   // the sweep honours motionReduced(), which is on here
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    QWidget* target = nullptr;
    QWidget* overlay = nullptr;
    for (QAbstractSpinBox* s : win.findChildren<QAbstractSpinBox*>()) {
      if (!s->isVisible() || !s->isEnabled()) continue;
      if (QWidget* o = s->findChild<QWidget*>("shimmerOverlay")) {
        target = s;
        overlay = o;
        break;
      }
    }
    if (!target) {
      for (QToolButton* b : win.findChildren<QToolButton*>()) {
        if (!b->isVisible() || !b->isEnabled()) continue;
        if (QWidget* o = b->findChild<QWidget*>("shimmerOverlay")) {
          target = b;
          overlay = o;
          break;
        }
      }
    }
    QVERIFY2(target && overlay, "no visible shimmered control found");
    QCOMPARE(overlay->geometry(), target->rect());  // the band covers the control

    // Two-sample advance, re-triggering the sweep if a slow run let it finish
    // between the samples (the sweep is only 325 ms long).
    bool advanced = false;
    qreal p1 = -1.0, p2 = -1.0;
    for (int attempt = 0; attempt < 3 && !advanced; ++attempt) {
      // Synthesized hover-enter (offscreen QPA has no real cursor motion).
      QEnterEvent enter(QPointF(5, 5), QPointF(5, 5),
                        target->mapToGlobal(QPoint(5, 5)));
      QApplication::sendEvent(target, &enter);
      QTRY_VERIFY(overlay->property("sweepProgress").toReal() >= 0.0);
      p1 = overlay->property("sweepProgress").toReal();
      QTest::qWait(75);
      p2 = overlay->property("sweepProgress").toReal();
      advanced = p2 > p1;
    }
    QVERIFY2(advanced, qPrintable(QString("shimmer sweep did not advance (%1 -> %2)")
                                      .arg(p1)
                                      .arg(p2)));
    // The sweep completes and CLEARS — no lingering band on the control.
    QTRY_COMPARE(overlay->property("sweepProgress").toReal(), -1.0);
    beat();
  }

  // Every icon button mimes its OWN action on hover (support/iconMotion.hpp, the port of
  // iconMotion.json): reduced motion wins, the glyph really is repainted, and NOTHING reflows.
  void iconMotionRunsOnToolbarButtonsWithoutReflow() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 30);   // let the toolbar's own deferred layout pass settle first

    // A shown, enabled toolbar button carrying a SETTLE design — it comes back to rest on
    // its own, so convergence can be asserted without a leave.
    QToolButton* btn = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>()) {
      if (!b->isVisible() || !b->isEnabled()
          || b->property(stencil::gui::NO_ICON_MOTION_PROPERTY).toBool())
        continue;
      stencil::gui::IconRequest r;
      if (!stencil::gui::iconRequestForKey(b->icon().cacheKey(), &r)) continue;
      const stencil::gui::IconMotionSpec* spec = stencil::gui::iconMotionFor(r.name);
      if (!spec || spec->hold) continue;
      btn = b;
      break;
    }
    QVERIFY2(btn, "no shown toolbar button with a settle icon motion");

    const auto enter = [](QWidget* w) {
      QEnterEvent e(QPointF(3, 3), QPointF(3, 3), w->mapToGlobal(QPoint(3, 3)));
      QApplication::sendEvent(w, &e);
    };
    const auto leave = [](QWidget* w) {
      QEvent e(QEvent::Leave);
      QApplication::sendEvent(w, &e);
    };
    // Every sibling's box, so a reflow anywhere in the row is caught, not just the
    // hovered button's own.
    QWidget* row = btn->parentWidget();
    QList<QRect> before;
    for (QWidget* w : row->findChildren<QWidget*>()) before << w->geometry();

    // Reduced motion is this suite's default, and the preference wins outright.
    const qint64 rest = btn->icon().cacheKey();
    enter(btn);
    QTest::qWait(80);
    QCOMPARE(btn->icon().cacheKey(), rest);
    leave(btn);

    // …and with motion allowed, the hover repaints the glyph and the play lands back on it.
    const auto motion = withMotion();
    enter(btn);
    QTRY_VERIFY2(btn->icon().cacheKey() != rest, "a hover did not move the glyph");
    QList<QRect> during;
    for (QWidget* w : row->findChildren<QWidget*>()) during << w->geometry();
    QCOMPARE(during, before);   // no layout shift, anywhere in the row
    QTRY_COMPARE(btn->icon().cacheKey(), rest);
    leave(btn);
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.motionDialogs.gui.moc"
