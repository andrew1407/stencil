// MainWindow GUI e2e — Motion that is not tied to one control: dialog reveal flights, image arrival
// dust, hover shimmer and the app-wide control swaps.
// Shared ground (helpers, the loaded window, the motion pins) is in mainWindow.gui.hpp.
#include "mainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The open flight photographs the dialog before its scroll area has decided its
  // scrollbar, so the picture that flew was a scrollbar too wide.
  // settleLayout brings the scrollbar in before the shot.
  void revealSnapshotWaitsForTheScrollbar() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    stencil::gui::SettingsDialog dlg(win.settings_, &win);
    dlg.resize(dlg.width(), 320);   // short enough that the body must scroll
    dlg.show();
    auto* scroll = dlg.findChild<QScrollArea*>();
    QVERIFY(scroll);
    // Straight after show() the vertical bar may not be decided yet — the frame the old
    // snapshot was taken on. After the settle the viewport is the real row width.
    stencil::support::settleLayout(dlg);
    QVERIFY2(scroll->verticalScrollBar()->isVisible(), "the settled shell shows its scrollbar");
    const int settledViewport = scroll->viewport()->width();
    QTest::qWait(80);   // …and nothing moves once the event loop has had its say
    QCOMPARE(scroll->viewport()->width(), settledViewport);
    QVERIFY(scroll->viewport()->width() < scroll->width());
    dlg.reject();
  }

  // A press outside a modal dismisses it, like a press on the browser's modal overlay
  // (ui/base.js). This covers the decision only: QTest hands the widget a QMouseEvent,
  // while a real press on a window a modal blocks never becomes one — that half is
  // modalDismissMac.mm reading the NSEvent, which no offscreen test can reach.
  void clickOutsideAModalDismissesIt() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(900, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    stencil::support::installModalDismiss();   // the app installs it at startup; explicit here

    QDialog dlg(&win);
    dlg.setObjectName(QStringLiteral("probeModal"));
    dlg.resize(200, 150);
    dlg.setModal(true);
    dlg.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dlg));

    // A press INSIDE the box changes nothing…
    QTest::mouseClick(&dlg, Qt::LeftButton, {}, QPoint(60, 60));
    QTest::qWait(40);
    QVERIFY2(dlg.isVisible(), "a press inside the dialog closed it");

    // …and one on a widget the dialog RAISED (a popup keeps it as its parent) neither.
    QWidget popup(&dlg, Qt::Popup);
    popup.resize(40, 40);
    QTest::mouseClick(&popup, Qt::LeftButton, {}, QPoint(5, 5));
    QTest::qWait(40);
    QVERIFY2(dlg.isVisible(), "a press in the dialog's own popup closed it");

    // A press on the blocked main window dismisses it — posted the way the PLATFORM does,
    // not with QTest::mouseClick: that hands the widget a QMouseEvent directly and skips
    // the window-system layer, which is exactly where Qt drops a blocked window's clicks.
    QTest::mouseClick(&win, Qt::LeftButton, {}, QPoint(60, 400));
    QTRY_VERIFY_WITH_TIMEOUT(!dlg.isVisible(), 1500);
    QCOMPARE(dlg.result(), int(QDialog::Rejected));

    // …unless the dialog says it must be answered.
    QDialog must(&win);
    must.setProperty(stencil::support::NO_OUTSIDE_DISMISS_PROPERTY, true);
    must.resize(200, 150);
    must.setModal(true);
    must.show();
    QVERIFY(QTest::qWaitForWindowExposed(&must));
    QTest::mouseClick(&win, Qt::LeftButton, {}, QPoint(60, 400));
    QTest::qWait(80);
    QVERIFY2(must.isVisible(), "an opted-out dialog was clicked away");
    must.close();
  }

  void motionComboRowsWearOneGlyph() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    stencil::gui::SettingsDialog dlg(win.settings_, &win);
    dlg.show();
    settleLayout(&dlg, 30);
    auto* combo = static_cast<stencil::gui::SearchComboBox*>(   // no Q_OBJECT on the combo: found as its base
        dlg.findChild<QComboBox*>(QStringLiteral("motionModeCombo")));
    QVERIFY(combo);
    QCOMPARE(combo->count(), 5);
    QVERIFY2(combo->toolTip().isEmpty(), "the browser's dropdown carries no tooltip");
    for (int i = 0; i < combo->count(); ++i) QVERIFY(!combo->itemIcon(i).isNull());
    combo->showPopup();
    QTRY_VERIFY(QApplication::activePopupWidget());
    QListView* list = combo->popupList();
    QVERIFY(list && list->isVisible());
    // Measured in LOGICAL pixels: the grab is at the screen's ratio (2x offscreen here).
    const QPixmap shot = list->grab();
    const int dpr = qMax(1, qRound(shot.devicePixelRatio()));
    const QImage img = shot.toImage().convertToFormat(QImage::Format_ARGB32)
                           .scaled(shot.width() / dpr, shot.height() / dpr, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    // Per row: the ICON slot's lit columns (the label starts past 36px), as runs. One
    // glyph is one run no wider than a 16px icon; a second icon would be a second run.
    const int rowH = img.height() / 5;
    QVERIFY(rowH > 10);
    for (int r = 0; r < 5; ++r) {
      QString cols;
      // Ink is whatever differs from the ROW's own background (a selected row wears the
      // accent wash), sampled just inside the popup's border.
      const QColor bg = img.pixelColor(5, r * rowH + rowH / 2);
      // Alpha counts: the list's viewport grabs transparent, so black ink on a clear row
      // differs from it in alpha alone.
      const auto far = [&](const QColor& c) {
        return qAbs(c.red() - bg.red()) + qAbs(c.green() - bg.green()) + qAbs(c.blue() - bg.blue())
               + qAbs(c.alpha() - bg.alpha()) > 90;
      };
      for (int x = 4; x < qMin(36, img.width()); ++x) {   // past the popup's own border
        bool lit = false;
        for (int y = r * rowH + 4; y < (r + 1) * rowH - 4 && !lit; ++y) lit = far(img.pixelColor(x, y));
        cols += lit ? QLatin1Char('#') : QLatin1Char('.');
      }
      qDebug("row %d icon columns: %s", r, qPrintable(cols));
      const QStringList runs = cols.split(QLatin1Char('.'), Qt::SkipEmptyParts);
      QVERIFY2(runs.size() >= 1, qPrintable(QStringLiteral("row %1 shows no glyph (%2)").arg(r).arg(cols)));
      int widest = 0, gaps = 0;
      for (const QString& run : runs) widest = qMax(widest, int(run.size()));
      // Runs parted by a gap of three or more columns are separate glyphs.
      for (int i = 3; i < cols.size(); ++i)
        if (cols.mid(i - 3, 3) == QLatin1String("...") && cols[i] == QLatin1Char('#') && cols.left(i - 3).contains(QLatin1Char('#'))) ++gaps;
      QVERIFY2(gaps == 0, qPrintable(QStringLiteral("row %1: two glyphs (%2)").arg(r).arg(cols)));
      QVERIFY2(widest <= 20, qPrintable(QStringLiteral("row %1: glyph ink %2px wide — more than one icon (%3)").arg(r).arg(widest).arg(cols)));
    }
    // …and a pick from the popup reports itself as a user pick — activated(), the signal
    // the dialog applies live from — BEFORE the list leaves. setCurrentIndex alone never
    // emits it, which is why a popup pick used to apply only on OK.
    QSignalSpy picked(combo, &QComboBox::activated);
    list->setCurrentIndex(list->model()->index(2, 0));   // Fire
    emit list->clicked(list->currentIndex());
    QCOMPARE(picked.count(), 1);
    QCOMPARE(picked.at(0).at(0).toInt(), 2);
    QCOMPARE(combo->currentData().toString(), QStringLiteral("fire"));
    QVERIFY(!list->isVisible());
    combo->hidePopup();
  }

  // The dialog reveal must START at the icon that opened it: support::revealDialog dusts
  // a snapshot of the dialog across the window, every mote streaming out of that icon, so
  // the flight's target point IS the origin the user sees the window come out of.
  void dialogRevealStartsAtTheIconThatOpenedIt() {
    MainWindow win;
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 150);
    // ctest runs this suite with STENCIL_NO_ANIM=1 (dialogs are driven immediately), and
    // revealDialog is a no-op under it — this test is about the flight, so turn it back on.
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    // The cloud is already in flight by the time we can look, so read the point it aims
    // at — that is where the user sees the window come out of.
    const auto flightOrigin = [&](QWidget* anchor) {
      QDialog dlg(&win);
      dlg.resize(300, 200);
      stencil::support::revealDialog(dlg, anchor, QRect());
      dlg.show();
      QTest::qWait(50);          // past the 0-timer that builds the cloud, inside the flight
      const QPoint from = surfaceFlightTarget(&win);
      dlg.close();
      return from;
    };
    QToolButton* icon = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->isVisible() && b->property("toolSection").isValid()) { icon = b; break; }
    QVERIFY2(icon, "no visible toolbar icon to fly out of");
    const QPoint origin = flightOrigin(icon);
    QVERIFY2(origin != QPoint(-1, -1), "no reveal flight was created");
    const QPoint want = flightPointOf(icon, &win);
    QVERIFY2(origin == want, qPrintable(QString("the flight starts at %1, the icon is at %2")
                                            .arg(QDebug::toString(origin), QDebug::toString(want))));
  }

  // pickColorAnimated is the getColor drop-in behind every colour swatch: same modal
  // contract (picked colour on OK, invalid QColor on cancel) plus the revealDialog
  // flight out of the anchor icon. The suite runs with STENCIL_NO_ANIM=1, so the
  // animation is re-enabled for the origin assertion, like the reveal tests above.
  void pickColorAnimatedMatchesGetColorAndFliesFromItsAnchor() {
    MainWindow win;
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 150);
    QToolButton* icon = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->isVisible() && b->property("toolSection").isValid()) { icon = b; break; }
    QVERIFY2(icon, "no visible toolbar icon to anchor on");
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    // exec() blocks, so a 0-timer drives the modal: the watcher catches the flight's
    // origin the instant it starts (QColorDialog is comfortably past the dust size
    // ceiling, so this is the ghost — reading it late would already be mid-tween).
    RevealOriginWatcher watcher;
    qApp->installEventFilter(&watcher);
    const auto removeWatcher = qScopeGuard([&] { qApp->removeEventFilter(&watcher); });
    QTimer::singleShot(0, [&] {
      for (int i = 0; i < 200; ++i) {
        if (auto* dlg = qobject_cast<QColorDialog*>(QApplication::activeModalWidget())) {
          // revealDialog's own 0-timer (registered after this one) needs a turn to
          // fire and start the OPEN flight before accept() closes the dialog and
          // starts the close flight instead — same watcher, same object name.
          QTest::qWait(50);
          dlg->setCurrentColor(QColor("#12ab34"));
          dlg->accept();
          return;
        }
        QTest::qWait(5);
      }
    });
    const QColor picked =
        stencil::support::pickColorAnimated(QColor("#ffffff"), &win, "Test colour", icon);
    QCOMPARE(picked, QColor("#12ab34"));
    const QPoint want = flightPointOf(icon, &win);
    QVERIFY2(watcher.origin == want,
             qPrintable(QString("the picker's flight starts at %1, the anchor icon is at %2")
                            .arg(QDebug::toString(watcher.origin), QDebug::toString(want))));
    // Cancel path: reject → invalid colour, exactly the getColor contract call sites rely on.
    QTimer::singleShot(0, [] {
      for (int i = 0; i < 200; ++i) {
        if (auto* dlg = qobject_cast<QColorDialog*>(QApplication::activeModalWidget())) {
          dlg->reject();
          return;
        }
        QTest::qWait(5);
      }
    });
    const QColor cancelled =
        stencil::support::pickColorAnimated(QColor("#ffffff"), &win, "Test colour", icon);
    QVERIFY2(!cancelled.isValid(), "cancel must return an invalid QColor");
  }

  // …and the anchor that feeds it follows whatever was triggered last, including actions
  // with no icon of their own — those must CLEAR it, not leave the previous icon in place.
  void revealAnchorFollowsTheTriggeredIcon() {
    MainWindow win;
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(60, 40, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    win.loadImageWithLayout(img, QJsonObject());   // rotate needs an image to be enabled
    settleLayout(&win, 150);
    QAction* first = win.actRotateLeft_;
    QAction* second = win.actRotateRight_;
    QVERIFY(first && second);
    QWidget* b1 = win.buttonForAction(first);
    QWidget* b2 = win.buttonForAction(second);
    QVERIFY2(b1 && b2 && b1 != b2, "expected a distinct toolbar button per action");
    first->trigger();
    QCOMPARE(win.pop_.dialogAnchor.data(), b1);
    second->trigger();
    QCOMPARE(win.pop_.dialogAnchor.data(), b2);
    // An action with no toolbar icon of its own clears the anchor instead of inheriting
    // the last icon — this is the "it flew out of the wrong button" case.
    // A bare action with no toolbar icon (and no slot of its own, so nothing opens).
    auto* iconless = new QAction("Menu-only command", &win);
    win.addAction(iconless);
    win.bindRevealAnchors();     // idempotent — binds whatever is not bound yet
    win.pop_.dialogAnchor = b2;
    iconless->trigger();
    QVERIFY2(!win.pop_.dialogAnchor, "an icon-less action left the previous icon as the origin");
  }

  // End-to-end: open a dialog the way a user does and read where its flight STARTS.
  // Covers both origins — an icon-backed command flies out of its icon, and a menu-only
  // one (no icon at all) flies out of the menu row that was clicked.
  void openingADialogFliesFromWhatWasClicked() {
    MainWindow win;
    win.resize(1400, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 150);
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });

    // The watcher catches the flight's origin the instant it starts, whichever
    // mechanism plays it — a ghost's geometry is what a QPropertyAnimation is
    // actively tweening, so reading it any time after Show would already be
    // mid-flight, not the origin.
    RevealOriginWatcher watcher;
    qApp->installEventFilter(&watcher);
    const auto removeWatcher = qScopeGuard([&] { qApp->removeEventFilter(&watcher); });
    // Let the dialog and its flight fully appear, then close it so trigger() returns.
    const auto closeSoon = [&] {
      QTimer::singleShot(140, &win, [&] {
        if (QWidget* modal = QApplication::activeModalWidget()) modal->close();
      });
    };

    // ── an icon-backed dialog ──
    QWidget* icon = win.buttonForAction(win.actProjects_);
    QVERIFY2(icon && icon->isVisible(), "the Projects icon is not on the toolbar");
    watcher.reset();
    closeSoon();
    win.actProjects_->trigger();
    settle([&] { return watcher.captured; }, 50);
    {
      const QPoint want = flightPointOf(icon, &win);
      QVERIFY2(watcher.origin == want,
               qPrintable(QString("icon case: flight starts at %1, icon at %2")
                              .arg(QDebug::toString(watcher.origin), QDebug::toString(want))));
    }

    // ── a menu-only dialog: no icon, so the clicked ROW is the origin ──
    // Synthetic action: every dialog-opening action now has a toolbar icon
    // (actShortcuts_ used to be the exception this borrowed — fixed to carry the
    // browser's gear icon like its siblings), so nothing icon-less is left to
    // borrow. Built the same way a real one would be: bound, then wired to open
    // a plain dialog through the same execMaybePopover path.
    QMenu* help = nullptr;
    for (QMenu* m : win.menuBar()->findChildren<QMenu*>())
      if (m->actions().contains(win.actInfo_)) { help = m; break; }
    QVERIFY2(help, "the Help menu was not found");
    auto* act = new QAction("Test Menu-Only Dialog", &win);
    help->addAction(act);
    win.bindRevealAnchor(act);
    connect(act, &QAction::triggered, &win, [&win, act] {
      QDialog dlg(&win);
      dlg.resize(300, 200);
      win.execMaybePopover(dlg, act);
    });
    QVERIFY2(!win.buttonForAction(act), "the synthetic action must have no toolbar icon");
    help->popup(win.mapToGlobal(QPoint(60, 40)));
    QVERIFY(QTest::qWaitForWindowExposed(help));
    const QRect row = help->actionGeometry(act);
    QTest::mouseMove(help, row.center());
    QTest::qWait(30);
    help->close();
    watcher.reset();
    closeSoon();
    act->trigger();
    settle([&] { return watcher.captured; }, 50);
    const QRect rowInWin(win.mapFromGlobal(help->mapToGlobal(row.topLeft())), row.size());
    QVERIFY2(watcher.origin == rowInWin.center(),
             qPrintable(QString("menu case: flight starts at %1, row at %2")
                            .arg(QDebug::toString(watcher.origin), QDebug::toString(rowInWin))));
  }

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

  // The hover shimmer must genuinely ANIMATE: after a hover-enter, the
  // overlay's sweep progress ADVANCES between two samples inside the 325 ms
  // window and clears (-1) on completion — not a band that pops in at a fixed
  // position and sits there, on text-entry fields too. Exercised on a
  // shimmered text-entry control (toolbar spinbox) when visible, else any
  // shimmered toolbutton.
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
  // browser/js/config/iconMotion.json): the trash lid lifts, plus grows, minus shrinks.
  // Driven here on a REAL toolbar button, for the three things the app-wide contract is
  // made of — reduced motion wins, the glyph really is repainted, and NOTHING reflows
  // (only the icon's own pixels change, so a hovered control cannot shove the row).
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

  void layoutPasteDialogButtonsCarryGlyphs() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QTRY_VERIFY(canvas->width() > 0 && canvas->height() > 0);

    // An existing line is what raises the question at all.
    QAction* start = actionByText(&win, "Start Drawing");
    QVERIFY(start);
    start->trigger();
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier,
                      QPoint(canvas->width() * 0.3, canvas->height() * 0.3));
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier,
                      QPoint(canvas->width() * 0.6, canvas->height() * 0.5));
    QTRY_VERIFY(!canvas->allLines().empty());

    QApplication::clipboard()->setText(
        QStringLiteral(R"({"lines":[{"points":[{"x":5,"y":5},{"x":9,"y":9}]}]})"));

    // Inspect the modal while it blocks the trigger, then back out of it. The prompt
    // is the chrome-styled askAlt (modalChrome confirmModalChoice), not a QMessageBox.
    QMap<QString, bool> hasGlyph;
    QTimer::singleShot(0, [&hasGlyph]() {
      for (int i = 0; i < 200; ++i) {
        QWidget* m = QApplication::activeModalWidget();
        if (m && m->objectName() == QLatin1String("stencilConfirmModal")) {
          for (QPushButton* b : m->findChildren<QPushButton*>())
            hasGlyph.insert(QString(b->text()).remove('&'), !b->icon().isNull());
          for (QPushButton* b : m->findChildren<QPushButton*>())
            if (QString(b->text()).remove('&').compare("Cancel", Qt::CaseInsensitive) == 0) {
              b->click();
              return;
            }
          if (auto* d = qobject_cast<QDialog*>(m)) d->reject();
          return;
        }
        QTest::qWait(5);
      }
    });
    QAction* paste = actionByText(&win, "Paste Layout JSON");
    QVERIFY(paste);
    paste->trigger();

    QVERIFY2(hasGlyph.value("Combine", false), "Combine must show a glyph, not a bare word");
    QVERIFY2(hasGlyph.value("Replace", false), "Replace must show a glyph");
    QVERIFY2(hasGlyph.value("Cancel", false), "Cancel must show a glyph");
    // The glyph names themselves must resolve — a renamed one degrades to a null QIcon,
    // which is exactly the "button has no icon" the assertions above would then catch,
    // but this says WHICH name broke.
    QVERIFY2(stencil::gui::hasIcon("layers"), "Combine's glyph");
    QVERIFY2(stencil::gui::hasIcon("swap"), "Replace's glyph");
    beat();
  }

  // Every way a picture lands on the canvas ASSEMBLES out of dust (Sweep::Gather) instead of
  // appearing all at once, with the real canvas held back until the motes land: a fresh open
  // (the OS-open / drop path — a hand-built QDropEvent never routes through Qt's drag
  // session), a created BLANK (it used to pop into place while a dropped one animated), and
  // a REOPEN of a saved project, the everyday route that once had no arrival at all.
  void imageArrivalAssemblesOnEveryRoute() {
    const auto motion = withMotion();
    const char* DUST = stencil::gui::DisintegrateOverlay::OBJECT_NAME;
    enum Route { FreshOpen, CreatedBlank, ReopenedProject };
    for (const Route route : {FreshOpen, CreatedBlank, ReopenedProject}) {
      const char* name = route == FreshOpen ? "fresh open"
                         : route == CreatedBlank ? "created blank" : "reopened project";
      MainWindow win(nullptr, false);
      win.resize(1000, 760);
      win.show();
      QVERIFY2(QTest::qWaitForWindowExposed(&win), name);
      CanvasWidget* canvas = win.findChild<CanvasWidget*>();
      QVERIFY2(canvas, name);
      if (route == ReopenedProject) {
        // Let the OPEN's own arrival finish, so what we see next belongs to the reopen.
        win.openPathFromOS(guiTestImage());
        QTRY_VERIFY2_WITH_TIMEOUT(canvas->hasImage(), name, 5000);
        QTRY_VERIFY2_WITH_TIMEOUT(win.findChild<QWidget*>(DUST) == nullptr, name,
                                  stencil::gui::DisintegrateOverlay::DUST_MS + 2000);
      }
      QVERIFY2(!win.findChild<QWidget*>(DUST), name);   // nothing flying before the route runs
      switch (route) {
        case FreshOpen:
          win.openPathFromOS(guiTestImage());
          break;
        case CreatedBlank:
          win.createBlankImageFromDialog(QColor("#3366cc"), 320, 240);
          break;
        case ReopenedProject:
          QVERIFY2(!win.activeProjectId_.isEmpty(), "the loaded image was adopted as a project");
          QVERIFY2(win.loadProjectIntoCanvas(win.activeProjectId_), name);
          break;
      }
      QTRY_VERIFY2_WITH_TIMEOUT(canvas->hasImage(), name, 5000);
      // The dust layer exists and the canvas is behind it (opacity effect at 0), so the
      // picture is the motes, not a canvas that popped in under them.
      QTRY_VERIFY2_WITH_TIMEOUT(win.findChild<QWidget*>(DUST) != nullptr, name, 3000);
      if (auto* fx = qobject_cast<QGraphicsOpacityEffect*>(canvas->graphicsEffect()))
        QVERIFY2(fx->opacity() < 0.01, "the real canvas waits behind the motes");
      // Both are gone when it lands, leaving no effect on a canvas that repaints per stroke.
      QTRY_VERIFY2_WITH_TIMEOUT(win.findChild<QWidget*>(DUST) == nullptr, name,
                                stencil::gui::DisintegrateOverlay::DUST_MS + 2000);
      QTRY_VERIFY2_WITH_TIMEOUT(canvas->graphicsEffect() == nullptr, name, 2000);
    }
    beat();
  }

  // …and the two shapes that must NOT flourish. A REBIND is not an arrival: the same
  // picture is already on screen (a move-to-local relinks the open editor). Under reduced
  // motion the image is simply THERE — the bug pinned there is not the missing dust but the
  // opacity effect, which used to stay on at 0 and leave the canvas blank for 900 ms.
  void arrivalIsSkippedWhenNothingIsArriving() {
    const char* DUST = stencil::gui::DisintegrateOverlay::OBJECT_NAME;
    for (const bool reduced : {false, true}) {
      const char* name = reduced ? "reduced motion" : "rebind";
      const auto motion = motionPinned(!reduced);
      MainWindow win(nullptr, false);
      CanvasWidget* canvas = openLoaded(win);
      QTRY_VERIFY2_WITH_TIMEOUT(canvas->hasImage(), name, 5000);
      if (reduced) {
        QVERIFY2(!win.findChild<QWidget*>(DUST), "no dust under reduced motion");
      } else {
        // The open's own arrival has to land first, or the rebind inherits its dust.
        QTRY_VERIFY2_WITH_TIMEOUT(win.findChild<QWidget*>(DUST) == nullptr, name,
                                  stencil::gui::DisintegrateOverlay::DUST_MS + 2000);
        QVERIFY2(win.loadProjectIntoCanvas(win.activeProjectId_, /*animate=*/false), name);
        settle([&] { return win.findChild<QWidget*>(DUST) != nullptr; }, 150);
        QVERIFY2(!win.findChild<QWidget*>(DUST), "a rebind is not an image appearing");
      }
      QVERIFY2(!canvas->graphicsEffect(), "the end state, immediately: a visible canvas");
      if (!reduced) continue;

      // The clear counterpart lands on its end state too — no scatter, and the empty-canvas
      // invitation is back at once instead of waiting out an animation that never ran.
      QAction* clear = actionByText(&win, "Clear Project");
      QVERIFY(clear);
      dismissModal("OK");
      clear->trigger();
      QTRY_VERIFY_WITH_TIMEOUT(!canvas->hasImage(), 5000);
      QVERIFY2(!win.findChild<QWidget*>(DUST), "no dust on the clear either");
      QVERIFY2(!canvas->idleHintHidden(), "the invitation is not held back by a missing animation");
    }
    beat();
  }

  // The drop zones paint the SAME glyphs as the browser (upload / incognito) — they used
  // to be the text characters "↑" and "◐", i.e. whatever the system font happened to have.
  void dropZonesPaintTheBrowsersGlyphs() {
    QVERIFY2(stencil::gui::hasIcon("upload"), "the saving half's glyph");
    QVERIFY2(stencil::gui::hasIcon("incognito"), "the incognito half's glyph");

    QWidget host;
    host.resize(700, 460);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    stencil::gui::DropZonesOverlay zones(&host);
    zones.showZones();
    QImage shot(zones.size(), QImage::Format_ARGB32);
    shot.fill(Qt::transparent);
    zones.render(&shot);

    // Something is actually drawn in each zone's glyph band — a mistyped icon name would
    // leave it empty, which is exactly the "no glyph at all" failure to catch.
    const auto bandHasInk = [&shot](int left, int right) {
      const int top = shot.height() / 6, bottom = shot.height() / 2;
      int ink = 0;
      for (int y = top; y < bottom; y += 2)
        for (int x = left; x < right; x += 2)
          if (qAlpha(shot.pixel(x, y)) > 40) ink++;
      return ink;
    };
    QVERIFY2(bandHasInk(20, shot.width() / 2 - 20) > 50, "the upload zone drew its glyph");
    QVERIFY2(bandHasInk(shot.width() / 2 + 20, shot.width() - 20) > 50, "and so did incognito");
    beat();
  }

  // The checkbox particle toggle and the combo value exchange are installed ONCE, on the
  // application (support/controlSwap.hpp) — no dialog wires its own. What the real window
  // has to prove is that the hook is actually on, that the real toolbar controls got it
  // without a call site, and that both effects converge on the true state and leave the
  // toolbar's geometry exactly where it was.
  void controlSwapsAreInstalledAppWide() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1400, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY2(qApp->findChild<QObject*>(
                 QString::fromLatin1(stencil::gui::CONTROL_SWAP_FILTER_NAME),
                 Qt::FindDirectChildrenOnly),
             "MainWindow never installed the app-wide control-swap filter");
    auto* box = win.showPointsCheck_;
    auto* combo = win.units_.pageSize;
    QVERIFY(box && combo);
    QTRY_VERIFY(box->property(stencil::gui::CONTROL_SWAP_WIRED_PROPERTY).toBool());
    QVERIFY2(combo->property(stencil::gui::CONTROL_SWAP_WIRED_PROPERTY).toBool(),
             "a toolbar combo built before the window was shown went unwired");

    // This suite runs under STENCIL_NO_ANIM; lift it just here so the REAL motion runs in
    // the REAL window, then put it back for everything after.
    qunsetenv("STENCIL_NO_ANIM");
    const QRect boxGeom = box->geometry();
    const QRect comboGeom = combo->geometry();
    const bool was = box->isChecked();
    box->setChecked(!was);
    QCOMPARE(box->isChecked(), !was);
    QCOMPARE(box->geometry(), boxGeom);
    // Rapid toggling: the last state is the one that survives, with nothing stranded.
    bool last = false;
    for (int i = 0; i < 6; ++i) { last = i % 2 == 0; box->setChecked(last); QTest::qWait(20); }
    QTRY_VERIFY(win.findChildren<QWidget*>(
                       QString::fromLatin1(stencil::gui::CHECK_SWAP_OBJECT_NAME)).isEmpty());
    QCOMPARE(box->isChecked(), last);
    QCOMPARE(box->geometry(), boxGeom);

    if (combo->count() > 1) {
      const int other = combo->currentIndex() == 0 ? 1 : 0;
      combo->setCurrentIndex(other);
      QCOMPARE(combo->currentIndex(), other);
      QCOMPARE(combo->geometry(), comboGeom);
      QTRY_VERIFY(!stencil::gui::ValueSwapOverlay::running(combo));
      QCOMPARE(combo->currentIndex(), other);
      QVERIFY2(combo->styleSheet().isEmpty(),
               "the swap left its transparent-text override on the combo");
      QCOMPARE(combo->geometry(), comboGeom);
    }

    qputenv("STENCIL_NO_ANIM", "1");
    box->setChecked(was);
    QCOMPARE(box->isChecked(), was);   // reduced motion still changes the state
    QVERIFY(win.findChildren<QWidget*>(
                   QString::fromLatin1(stencil::gui::CHECK_SWAP_OBJECT_NAME)).isEmpty());
    beat();
  }

  // A dust flight that leaves the host must not be cropped to it (placeForSurface).
  void surfaceDustLayerCoversTheWholeFlightNotJustTheWindow() {
    using stencil::gui::DisintegrateOverlay;
    const QRect host(120, 122, 900, 620);
    const QRect dragged(879, 613, 620, 700);   // Projects dragged past the bottom-right
    const QPoint icon(300, 200);
    const QRect need = DisintegrateOverlay::surfaceLayerRect(dragged, icon);
    QVERIFY2(!host.contains(need), "the host cannot hold the flight — the layer must escape it");
    QVERIFY2(need.contains(dragged), "the layer must cover the window that is coming apart");
    QVERIFY2(need.contains(icon), "…and the point its motes are pouring into");
    QVERIFY2(need.bottom() > host.bottom() && need.right() > host.right(),
             "the cropped-off part is exactly what the layer has to reach");
    QVERIFY2(!host.contains(DisintegrateOverlay::surfaceLayerRect(QRect(260, 82, 620, 700), icon)),
             "a dialog taller than the window needs the escape too");
    QVERIFY2(host.contains(DisintegrateOverlay::surfaceLayerRect(QRect(400, 300, 200, 160), icon)),
             "a flight that fits must not pay for a window of its own");
  }

  // Offscreen's virtual screen is no real desktop, so the layer stays a child there.
  void surfaceDustStaysAChildWhenThereIsNoDesktop() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(dock);
    win.actChat_->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    settleLayout(&win, 300);
    win.actChat_->setChecked(false);
    QTRY_VERIFY(surfaceFlight(&win));
    auto* fx = surfaceFlight(&win);
    QVERIFY2(fx, "the floating chat's flight did not play");
    QVERIFY2(!fx->isWindow(), "offscreen has no desktop to escape onto");
    QCOMPARE(fx->geometry(), win.rect());
  }

  // ── A motion mode changed WHILE a window is up governs how that window LEAVES ──
  // The Visuals & Settings dialog live-applies its own Motion rows (support/motionPrefs.hpp),
  // so switching to "None" in it and closing it must not leave that very window still flying
  // back into its icon — and switching motion back ON must give it the closing flight its
  // open never installed. The close flight therefore asks the mode when it PLAYS, not when
  // it was hung on the dialog (support/modalReveal.cpp CloseFlight::fly).
  void dialogCloseAsksTheMotionModeAgainOnItsWayOut() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    // Any flight at all — the dust cloud or the ghost it falls back to.
    struct FlightSpy : QObject {
      bool seen = false;
      bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::Show) {
          auto* w = qobject_cast<QWidget*>(o);
          if (w && (w->objectName()
                        == QLatin1String(stencil::gui::DisintegrateOverlay::OBJECT_NAME)
                    || w->objectName() == QLatin1String("stencilModalGhost")))
            seen = true;
        }
        return false;
      }
    };

    const auto flewOnClose = [&](stencil::support::MotionMode openMode,
                                 stencil::support::MotionMode closeMode) {
      stencil::support::setMotionMode(openMode);
      QDialog dlg(&win);
      dlg.resize(260, 180);
      stencil::support::revealDialog(dlg, nullptr, QRect(40, 40, 26, 26));
      dlg.show();
      QTest::qWait(80);            // the open flight, whichever mode allowed it
      stencil::support::setMotionMode(closeMode);   // …the user moves the setting…
      FlightSpy spy;
      qApp->installEventFilter(&spy);
      dlg.hide();                  // …and closes the window
      QTest::qWait(30);
      qApp->removeEventFilter(&spy);
      return spy.seen;
    };

    QVERIFY2(!flewOnClose(stencil::support::MotionMode::Particles,
                          stencil::support::MotionMode::None),
             "motion turned OFF while the window was up: it must leave without a flight");
    QVERIFY2(flewOnClose(stencil::support::MotionMode::None,
                         stencil::support::MotionMode::Particles),
             "motion turned ON while the window was up: it must leave WITH one");
    stencil::support::setMotionMode(stencil::support::MotionMode::Particles);
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "mainWindow.motion.gui.moc"
