// MainWindow GUI e2e — The live canvas: loading, drawing, selecting, undo, compare and the scrollbars.
// Shared ground (helpers, the loaded window, the motion pins) is in mainWindow.gui.hpp.
#include "mainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void clearingImageBringsBackTheIdleAffordance() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1200, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    CanvasWidget* canvas = win.canvas_;
    QVERIFY(canvas);
    const QSize idle = canvas->size();

    QImage big(1600, 2200, QImage::Format_ARGB32);
    big.fill(Qt::darkCyan);
    canvas->loadFromImage(big);
    QTRY_VERIFY(canvas->hasImage());
    QVERIFY2(canvas->height() > win.scroll_->viewport()->height(),
             "the fixture must be taller than the viewport, or this proves nothing");

    canvas->clearImage();
    QTRY_VERIFY(!canvas->hasImage());
    QCOMPARE(canvas->size(), idle);
    QVERIFY2(canvas->width() <= win.scroll_->viewport()->width()
                 && canvas->height() <= win.scroll_->viewport()->height(),
             "the cleared canvas must fit its viewport, or the idle hint is scrolled away");
  }

  void loadsImageAndEnablesActions() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    CanvasWidget* canvas = openLoaded(win);
    QVERIFY(canvas);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);  // load settles on the event loop
    QVERIFY(canvas->imageWidth() > 0);
    QVERIFY(canvas->imageHeight() > 0);

    QAction* rotate = actionByText(&win, "Rotate Right");
    QVERIFY(rotate);
    QVERIFY(rotate->isEnabled());            // an image makes the transform actions live
    QVERIFY(!win.windowTitle().isEmpty());
  }

  void rotateActionsRoundTrip() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    const int w0 = canvas->imageWidth(), h0 = canvas->imageHeight();
    const int r0 = canvas->rotationQuarters();
    // The page crop of our landscape test image is non-square, so a quarter turn
    // produces an observable W↔H swap below (guards the swap assertion's premise).
    QVERIFY2(w0 != h0, "the page crop should be non-square so the rotation swap is observable");

    QAction* right = actionByText(&win, "Rotate Right");
    QAction* left = actionByText(&win, "Rotate Left");
    QVERIFY(right && left);

    beat();
    right->trigger();
    QCOMPARE(canvas->rotationQuarters(), (r0 + 1) % 4);
    // A quarter turn swaps the visible (cropped) dimensions — proof the rotation
    // actually transformed the image, not merely bumped the quarter-turn counter.
    QCOMPARE(canvas->imageWidth(), h0);
    QCOMPARE(canvas->imageHeight(), w0);
    beat();

    left->trigger();                                       // undo the quarter turn
    QCOMPARE(canvas->rotationQuarters(), r0);
    beat();
    QCOMPARE(canvas->imageWidth(), w0);                    // exact state restored
    QCOMPARE(canvas->imageHeight(), h0);
  }

  void drawWithMouseThenUndo() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QTRY_VERIFY(canvas->width() > 0 && canvas->height() > 0);

    // Enter drawing mode via the real "Start Drawing" action, then click three
    // well-separated points on the canvas — the same left-click path the app uses.
    QAction* start = actionByText(&win, "Start Drawing");
    QVERIFY(start && start->isEnabled());
    start->trigger();

    const int W = canvas->width(), H = canvas->height();
    for (const QPoint& p : { QPoint(W * 0.35, H * 0.35), QPoint(W * 0.6, H * 0.45), QPoint(W * 0.45, H * 0.65) }) {
      QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, p);
      beat();
    }
    // Each left-click press adds exactly one point: the canvas widget is fixed to
    // the scaled-image size with a zero-offset widget→image mapping, so all three
    // clicks land inside the image (no letterboxing to miss) and none are deduped.
    QCOMPARE(totalPoints(canvas), 3);

    // Commit the line via the "New Line" action — this is what pushes an undo snapshot.
    QAction* newLine = actionByText(&win, "New Line");
    QVERIFY(newLine);
    newLine->trigger();
    QCOMPARE(static_cast<int>(canvas->lines().size()), 1);   // committed line landed
    QVERIFY(canvas->canUndo());
    QVERIFY(!canvas->canRedo());
    beat();

    // The Undo action steps back the history stack, dropping the committed line.
    QAction* undo = actionByText(&win, "Undo");
    QVERIFY(undo);
    undo->trigger();
    QCOMPARE(static_cast<int>(canvas->lines().size()), 0);
    QVERIFY(canvas->canRedo());          // undo made a redo available
    beat();

    // Redo re-applies it via the real action: the committed line comes back,
    // exactly as the toolbar / Ctrl+Shift+Z would restore it.
    QAction* redo = actionByText(&win, "Redo");
    QVERIFY(redo && redo->isEnabled());
    redo->trigger();
    QCOMPARE(static_cast<int>(canvas->lines().size()), 1);
    QVERIFY(!canvas->canRedo());
    beat();
  }

  // A bare Delete/Backspace inside the selection panel's lists removes the current row —
  // the points table already did this; the Lines list is the parity addition (the browser's
  // focused .lines-row / coordinates row take the same key). Scoped by widget focus, so the
  // global Alt+Delete on the canvas selection is untouched.
  void deleteKeyRemovesRowInSelectionLists() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QTRY_VERIFY(canvas->width() > 0 && canvas->height() > 0);

    // Commit two lines through the real drawing path.
    QAction* start = actionByText(&win, "Start Drawing");
    QAction* newLine = actionByText(&win, "New Line");
    QVERIFY(start && newLine);
    const int W = canvas->width(), H = canvas->height();
    const QList<QList<QPoint>> shapes{
        {QPoint(W * 0.25, H * 0.25), QPoint(W * 0.45, H * 0.35)},
        {QPoint(W * 0.55, H * 0.55), QPoint(W * 0.75, H * 0.65), QPoint(W * 0.65, H * 0.8)},
    };
    for (const QList<QPoint>& shape : shapes) {
      start->trigger();
      for (const QPoint& p : shape) { QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, p); beat(); }
      newLine->trigger();
    }
    QCOMPARE(static_cast<int>(canvas->lines().size()), 2);

    // --- Lines tab: Delete on the current row removes that line ---
    auto* linesList = win.findChild<QListWidget*>("linesList");
    QVERIFY(linesList);
    QTRY_COMPARE(linesList->count(), 2);
    QVERIFY2(linesList->focusPolicy() != Qt::NoFocus, "the list must accept focus for a scoped Delete");
    linesList->setCurrentRow(0);
    QTest::keyClick(linesList, Qt::Key_Delete);
    QTRY_COMPARE(static_cast<int>(canvas->lines().size()), 1);
    // The current row survives the repopulate, so a second press deletes again.
    QCOMPARE(linesList->currentRow(), 0);
    QTest::keyClick(linesList, Qt::Key_Backspace);
    QTRY_COMPARE(static_cast<int>(canvas->lines().size()), 0);

    // --- Points table: same key, unchanged behaviour ---
    start->trigger();
    for (const QPoint& p : { QPoint(W * 0.3, H * 0.3), QPoint(W * 0.5, H * 0.4), QPoint(W * 0.4, H * 0.6) }) {
      QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, p);
      beat();
    }
    QCOMPARE(totalPoints(canvas), 3);
    auto* points = win.findChild<QTableWidget*>();
    QVERIFY(points);
    QTRY_COMPARE(points->rowCount(), 3);
    points->setCurrentCell(1, 0);
    QTest::keyClick(points, Qt::Key_Delete);
    QTRY_COMPARE(totalPoints(canvas), 2);
    beat();
  }

  // Alt+Shift dragging a line must move EVERY point — including when Shift lifts a beat
  // before the mouse button, which is how the gesture naturally ends. The regression left
  // all but the grabbed segment's endpoints snapped back to their pre-drag spots.
  void wholeLineDragMovesEveryPoint() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QTRY_VERIFY(canvas->width() > 0 && canvas->height() > 0);

    const double s = canvas->scale();
    stencil::core::Line line;
    line.points = {{20, 20}, {60, 20}, {100, 40}, {140, 60}};
    canvas->setLines({line});
    const auto orig = canvas->lines()[0].points;

    auto sendMouse = [&](QEvent::Type t, const QPointF& pos, Qt::MouseButton btn,
                         Qt::MouseButtons btns, Qt::KeyboardModifiers mods) {
      QMouseEvent ev(t, pos, canvas->mapToGlobal(pos.toPoint()), btn, btns, mods);
      QCoreApplication::sendEvent(canvas, &ev);
    };

    // Grab the FIRST segment's midpoint (widget space = image * scale), drag by a known
    // delta with Shift ALREADY RELEASED on the move and the release, then let go.
    const QPointF grab(40 * s, 20 * s);
    const QPointF drop((40 + 30) * s, (20 + 25) * s);
    sendMouse(QEvent::MouseButtonPress, grab, Qt::LeftButton, Qt::LeftButton,
              Qt::AltModifier | Qt::ShiftModifier);
    sendMouse(QEvent::MouseMove, drop, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, drop, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    beat();

    const auto& moved = canvas->lines()[0].points;
    QCOMPARE(static_cast<int>(moved.size()), 4);
    for (std::size_t i = 0; i < moved.size(); ++i) {
      QVERIFY2(std::abs(moved[i].x - (orig[i].x + 30)) < 0.5 &&
                   std::abs(moved[i].y - (orig[i].y + 25)) < 0.5,
               qPrintable(QString("point %1 carries the full drag delta").arg(i)));
    }
  }

  // Alt+Delete routes by selection: a focused POINT narrows it to that point (the line
  // survives); with no focused point it deletes the selected line — and the shared
  // hotkeysConfig label says so.
  void altDeleteRoutesBySelection() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    stencil::core::Line line;
    line.points = {{30, 30}, {80, 30}, {80, 80}};
    canvas->setLines({line});

    QAction* del = actionByText(&win, "Delete Selected Line (Point if focused)");
    QVERIFY2(del, "the routed delete action is discoverable under its new label");

    // Click-select a POINT → the chord deletes just that point.
    canvas->selectLineAt(30, 30);
    QCOMPARE(canvas->selectedPoint(), 0);
    del->trigger();
    QTRY_COMPARE(static_cast<int>(canvas->lines().size()), 1);
    QCOMPARE(static_cast<int>(canvas->lines()[0].points.size()), 2);

    // Select the LINE via a segment (no focused point) → the chord deletes the line.
    canvas->selectLineAt(80, 55);
    QVERIFY(canvas->selectedLineIdx() == 0 && canvas->selectedPoint() == -1);
    del->trigger();
    QTRY_COMPARE(static_cast<int>(canvas->lines().size()), 0);
  }

  // Hover cross-highlight plumbing: moving the mouse over a point emits
  // canvasHoverChanged (panel row tints), and the panel-driven setListHover* calls are
  // safe to drive directly (canvas ring/glow — the reverse direction).
  void hoverCrossHighlightSignals() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    const double s = canvas->scale();
    stencil::core::Line line;
    line.points = {{40, 40}, {90, 40}};
    canvas->setLines({line});

    QSignalSpy spy(canvas, &CanvasWidget::canvasHoverChanged);
    QMouseEvent over(QEvent::MouseMove, QPointF(40 * s, 40 * s),
                     canvas->mapToGlobal(QPoint(40 * s, 40 * s)), Qt::NoButton,
                     Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(canvas, &over);
    QTRY_VERIFY(spy.count() >= 1);
    const QList<QVariant> args = spy.last();
    QCOMPARE(args.at(0).toInt(), 0);   // line 0
    QCOMPARE(args.at(1).toInt(), 0);   // point 0
    QCOMPARE(args.at(2).toInt(), 0);   // over line 0 (tints the Lines-list row)

    // Leaving the canvas clears the hover (all -1) so panel tints drop too.
    spy.clear();
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(canvas, &leave);
    QTRY_VERIFY(spy.count() >= 1);
    QCOMPARE(spy.last().at(0).toInt(), -1);

    // Reverse direction: the panel rows drive the canvas ring/glow without incident.
    canvas->setListHoverLine(0);
    canvas->setListHoverPoint(1);
    canvas->setListHoverLine(-1);
    canvas->setListHoverPoint(-1);
  }

  void filterActionAppliesToCanvas() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    // The context-menu filter options are hosted QRadioButtons (an exclusive QButtonGroup) so
    // picking one keeps the menu open; find them by their "filterValue" property and drive them.
    // The radios live inside QWidgetActions' default widgets (setDefaultWidget reparents them out
    // of the window until a menu shows them), so reach them via the actions, not win's children.
    auto filterRadio = [&](const QString& value) -> QRadioButton* {
      for (QWidgetAction* a : win.findChildren<QWidgetAction*>())
        if (QWidget* dw = a->defaultWidget())
          for (QRadioButton* r : dw->findChildren<QRadioButton*>())
            if (r->property("filterValue").toString() == value) return r;
      return nullptr;
    };

    // Normalize to a known baseline via the real "None" radio: applyImageFilter PERSISTS the
    // chosen mode to settings, so a prior run/test can start this canvas non-"none". Drive it
    // rather than assuming the default (order-safe).
    QRadioButton* none = filterRadio("none");
    QVERIFY(none);
    none->setChecked(true);
    QCOMPARE(canvas->imageFilter(), QString("none"));

    // Check the SHARED filter path (toggling the radio runs the real applyImageFilter, which also
    // syncs the toolbar combo) and lands the mode on the live canvas.
    QRadioButton* bw = filterRadio("bw");
    QVERIFY(bw && bw->isEnabled());
    bw->setChecked(true);
    QCOMPARE(canvas->imageFilter(), QString("bw"));      // menu/toolbar wiring reached the canvas
    beat();

    // Switching filters is live and mutually exclusive (one button group).
    QRadioButton* sepia = filterRadio("sepia");
    QVERIFY(sepia);
    sepia->setChecked(true);
    QCOMPARE(canvas->imageFilter(), QString("sepia"));
    QVERIFY(!bw->isChecked());                            // exclusive group cleared the old mode
    beat();

    none->setChecked(true);   // leave the persisted filter clean for other tests/runs
  }

  void clearAllActionEmptiesCanvas() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QTRY_VERIFY(canvas->width() > 0 && canvas->height() > 0);

    // Draw and commit a line so there is something to clear.
    QAction* start = actionByText(&win, "Start Drawing");
    QVERIFY(start && start->isEnabled());
    start->trigger();
    const int W = canvas->width(), H = canvas->height();
    for (const QPoint& p : { QPoint(W * 0.4, H * 0.4), QPoint(W * 0.6, H * 0.55) }) {
      QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, p);
      beat();
    }
    QAction* newLine = actionByText(&win, "New Line");
    QVERIFY(newLine);
    newLine->trigger();
    QCOMPARE(static_cast<int>(canvas->lines().size()), 1);

    // The destructive "Clear All Lines" action (canvas context menu + Edit menu
    // reuse it) asks first — the browser's styled confirm (drawingApp.js
    // clearAllLines) — and on Confirm wipes every committed and in-progress point.
    QAction* clear = actionByText(&win, "Clear All Lines");
    QVERIFY(clear && clear->isEnabled());
    dismissModal("OK");
    clear->trigger();
    QCOMPARE(static_cast<int>(canvas->lines().size()), 0);
    QCOMPARE(totalPoints(canvas), 0);   // nothing committed or in-progress remains
    beat();
  }

  // The blank-image card gets the browser's glass sweep on hover (layout.css ui-shimmer):
  // a light band crossing it once. Asserts the band genuinely MOVES, not just appears.
  void blankImageCardShimmersOnHover() {
    MainWindow win;
    win.resize(900, 640);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // A pixel test owns its palette: this suite shares the real settings file, and on the
    // LIGHT theme the card's dashed border is paler than its accent fill, so the
    // brightest-pixel search below locks onto the border and never sees the band move.
    // Not persisted (applySettings' `persist` is false) — the app's own theme is left be.
    {
      Settings s = win.settings_;
      s.themeMode = QStringLiteral("dark");
      win.applySettings(s, /*persist=*/false);
      settleLayout(&win, 60);
    }
    win.canvas_->clearImage();
    win.refreshActions();
    QVERIFY(waitForIdleCard(win));   // past the clear-dust hold that hides the card
    // Brightest column of the card's mid row — where the band is right now.
    const auto bandX = [&] {
      const QImage im = win.canvas_->grab().toImage();
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
    const QPoint c(win.canvas_->width() / 2, win.canvas_->height() / 2);
    QMouseEvent move(QEvent::MouseMove, QPointF(c), win.canvas_->mapToGlobal(c),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(win.canvas_, &move);
    QTest::qWait(150);
    const int first = bandX();
    QTest::qWait(220);
    const int second = bandX();
    QVERIFY2(second > first,
             qPrintable(QString("the sweep does not travel: %1 then %2").arg(first).arg(second)));
  }

  // "＋ Blank image" is a BUTTON, not the whole empty page. A left-click anywhere on the
  // empty canvas used to create a blank image — the card was only the drawing that
  // advertised it, and the hand cursor covered the whole area too. Only
  // the card's own rect clicks, and only over it is the cursor a hand.
  void blankImageCardIsTheOnlyClickTarget() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    CanvasWidget* canvas = win.canvas_;
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

  // The bottom-bar coordinate readout must follow the cursor over the image —
  // in every state the user can be in. A frozen readout reads as a frozen app.
  void coordReadoutFollowsTheCursor() {
    MainWindow win(nullptr, false);
    win.resize(1200, 850);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    settleLayout(&win, 200);
    QVERIFY(win.status_);

    // Synthesize a real hover over the canvas and read the status bar.
    const auto hoverAt = [&](const QPoint& p) {
      QMouseEvent move(QEvent::MouseMove, QPointF(p), canvas->mapToGlobal(p), Qt::NoButton,
                       Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(canvas, &move);
      QTest::qWait(20);
      return win.status_->text();
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

    win.actIncognito_->setChecked(true);   // the state the report came from
    settleLayout(&win, 80);
    readoutMoves("incognito");
    win.actIncognito_->setChecked(false);

    win.actChat_->setChecked(true);        // …with the chat open over the layout
    QTRY_VERIFY(win.chatDock_->isVisible());
    awaitAnim(win.chatAnim_);              // …on the slide's own end
    readoutMoves("chat open");
    win.actChat_->setChecked(false);
    awaitAnim(win.chatAnim_);

    // COMPARE: the canvas is read-only there, but the readout is information,
    // not editing — it must keep following the cursor (it used to stop dead,
    // which is exactly what "the app is frozen" looked like).
    for (const char* mode : {"vertical", "horizontal"}) {
      win.setCompareModeUi(QString::fromLatin1(mode));
      QTRY_VERIFY2(win.canvas_->compareReadOnly(), "compare did not engage");
      readoutMoves(mode);
    }
    win.setCompareModeUi(QStringLiteral("none"));
    beat();
  }

  // The way back OUT of a closed area. "Unchain" sits in the bar's area-only group, so it
  // is offered exactly when a line is an area, and clicking it puts the line back to an
  // open polyline (canvas/chainEdit.hpp; Alt+Ctrl+drag is the gesture route).
  void unchainButtonIsOfferedOnlyForAreas() {
    MainWindow win(nullptr, false);
    win.resize(1200, 850);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);

    auto* unchain = win.selectedLineBar_->findChild<QPushButton*>("selectedLineUnchain");
    QVERIFY2(unchain, "the bar has no Unchain button");

    // An OPEN line: the area controls, Unchain among them, stay away.
    stencil::core::Line open;
    open.points = {{20, 20}, {80, 80}, {40, 90}};
    canvas->setLines({open});
    canvas->selectLineByIndex(0);
    QTRY_VERIFY_WITH_TIMEOUT(win.selectedLineDock_->isVisible(), 2000);
    // The group SLIDES away now (controlReveal), so visibility settles on the event loop
    // rather than on the same tick — wait it out instead of reading it mid-flight.
    QTRY_VERIFY2(!unchain->isVisible(), "an open line was offered Unchain");

    // REGRESSION: the fill swatch showed a washed-out salmon for a translucent green — a
    // CSS `#rrggbbaa` handed to QColor, whose 8-digit form is #AARRGGBB, alpha first. It
    // must go through cssColor(), like every other stored colour.
    {
      stencil::core::Line tinted;
      tinted.points = {{10, 10}, {90, 10}, {90, 70}, {10, 70}};
      tinted.locked = true;
      tinted.fillColor = "#00aa4440";        // green at alpha 0x40
      canvas->setLines({tinted});
      canvas->selectLineByIndex(0);
      beat();
      auto* swatch = win.selectedLineBar_->findChild<QPushButton*>("selectedLineFillSwatch");
      QVERIFY2(swatch, "the bar has no fill swatch");
      // The colour lives in the well's CHIP now (a 32x16 pixmap inside the input frame),
      // so read it there: its channels must be the CSS ones, alpha included.
      const QImage chip = swatch->icon().pixmap(32, 16).toImage();
      const QColor mid = chip.pixelColor(chip.width() / 2, chip.height() / 2);
      // ±2 per channel: the chip is drawn into a premultiplied pixmap, so the readback
      // rounds by a unit. What matters is that it is THIS green at THIS alpha, and not
      // the washed-out salmon a #AARRGGBB misread produced (170, 68, 64).
      const auto near8 = [](int got, int want) { return std::abs(got - want) <= 2; };
      QVERIFY2(near8(mid.red(), 0) && near8(mid.green(), 170) && near8(mid.blue(), 68) &&
                   near8(mid.alpha(), 64),
               qPrintable("fill chip reads " + mid.name(QColor::HexArgb)));

      // REGRESSION: the bar's colours must be the browser's own hex, painted flat.
      // Encoding into Display P3 on macOS was a second conversion on an already
      // colour-managed surface and made the whole app read duller.
      win.resize(1900, 900);
      settleLayout(&win, 300);
      const QImage bar = win.selectedLineBar_->grab().toImage();
      auto* ds = win.selectedLineBar_->findChild<QWidget*>("selectedLineDeselect");
      QVERIFY(ds);
      const QColor got = bar.pixelColor(ds->mapTo(win.selectedLineBar_, QPoint(5, ds->height() / 2)));
      // Deselect wears the bar's own amber, the same token its siblings use — one
      // palette, no orange outlier (browser .deselect-btn -> var(--bg-sel-btn)). For the
      // theme the window is actually in: another case may have left the app in light.
      const stencil::gui::Palette live = stencil::gui::themePalette(
          stencil::gui::resolveDark(win.settings_.themeMode), win.settings_.accentColor);
      QCOMPARE(got.name(), live.bgSelBtn.name());
      QCOMPARE(stencil::gui::themePalette(true, "violet").danger.name(), QStringLiteral("#f0697a"));
    }

    // The two glyphs in this group are sized like the browser's: the clear-fill cross is
    // small (11px box, not the style's 16 scaling an 11px pixmap up), and Unchain carries
    // the same icon-plus-label pairing #sel-unchain has.
    {
      auto* clear = win.selectedLineBar_->findChild<QPushButton*>("selectedLineFillClear");
      QVERIFY2(clear, "no clear-fill button");

      // …and the same boxes the browser's controls have: a 23x19 cross, 28px-tall buttons
      // and 46x34 colour wells beside 34px fields. Clear-fill is a full-height control,
      // not a small cross sitting low in the row.
      QVERIFY2(clear->height() >= 26, qPrintable(QString("clear is %1px tall").arg(clear->height())));
      QCOMPARE(clear->iconSize(), QSize(13, 13));
      // ONE colour well everywhere: 46x24, the size the browser and extension now use too.
      auto* swatch2 = win.selectedLineBar_->findChild<QPushButton*>("selectedLineFillSwatch");
      QVERIFY(swatch2);
      QCOMPARE(swatch2->size(), QSize(46, 26));
      QVERIFY2(!swatch2->icon().isNull(),
               "the well should draw a colour CHIP inside its frame, like the toolbar's");
      // …in the theme's own input chrome, exactly as the toolbar's wells are. Read from
      // the widget's palette instead, the frame resolved to the LIGHT theme's #dddddd and
      // the wells sat in the dark bar ringed in near-white.
      const stencil::gui::Palette chrome = stencil::gui::themePalette(
          stencil::gui::resolveDark(win.settings_.themeMode), win.settings_.accentColor);
      QVERIFY2(swatch2->styleSheet().contains(chrome.borderMain.name()),
               qPrintable("well frame reads: " + swatch2->styleSheet()));
      QVERIFY2(swatch2->styleSheet().contains(chrome.inputBg.name()),
               "the well should sit on the theme's input ground");
      // …and four hairlines part the bar, as the browser's do: header | colours |
      // geometry | fill | actions. The fill's own comes and goes WITH the group, or
      // unchaining leaves two side by side with nothing between. Measured while the group
      // is still there, at a width narrow enough that it costs a second row.
      win.resize(1100, 900);
      settleLayout(&win, 300);
      const int barHeightWithFill = win.selectedLineBar_->height();
      const auto visibleSeps = [&] {
        int n = 0;
        for (QFrame* f : win.selectedLineBar_->findChildren<QFrame*>("selectedLineSep"))
          if (f->isVisible()) ++n;
        return n;
      };
      QCOMPARE(visibleSeps(), 4);
      canvas->unchainSelectedLine();
      beat();
      QTRY_COMPARE(visibleSeps(), 3);
      QTRY_VERIFY2(!swatch2->isVisible(), "the fill group should be gone with it");
      // …and the bar SHRINKS with it. Losing the fill group can cost the flow layout a
      // whole row, and nothing re-asked for the height. refitHeight() runs on every
      // content change now.
      const int tallWithFill = barHeightWithFill;
      QTRY_VERIFY2(win.selectedLineBar_->height() < tallWithFill,
                   qPrintable(QString("bar stayed %1px tall after the fill group left (was %2)")
                                  .arg(win.selectedLineBar_->height()).arg(tallWithFill)));
      for (QComboBox* cb : win.selectedLineBar_->findChildren<QComboBox*>()) {
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

    // …and pressing it opens the area, keeping every corner. click() rather than a
    // synthetic press at coordinates: the group is mid-slide when it first becomes visible
    // (controlReveal), so a positional click can land beside a still-growing button.
    unchain->click();
    beat();
    QVERIFY2(!canvas->lines()[0].locked, "the click did not unchain the area");
    QCOMPARE(canvas->lines()[0].points.size(), std::size_t(4));
    QTRY_VERIFY2(!unchain->isVisible(), "Unchain is still offered on a line that is now open");
  }

  // The "Selected Line:" bar appears/disappears through dustSelectedLineBarIn/Out
  // (mainWindow.cpp) rather than a plain instant show/hide. Like the other docked
  // surface flights, it declines under the offscreen QPA platform this suite runs
  // under, so this asserts the end state rather than a live flight.
  void selectedLineBarAppearsAndDisappearsWithDust() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1200, 850);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QVERIFY2(!win.selectedLineDock_->isVisible(), "nothing selected yet");

    stencil::core::Line line;
    line.points = {{20, 20}, {80, 80}};
    canvas->setLines({line});

    // Select it: the bar comes up (and, off this platform, dust would gather into it).
    canvas->selectLineByIndex(0);
    QTRY_VERIFY_WITH_TIMEOUT(win.selectedLineDock_->isVisible(), 2000);
    beat();

    // selectedLineBarDustPoint() is plain geometry, so it runs fine offscreen even
    // though the flight it feeds does not.
    QVERIFY2(win.imageInfoBar_ && win.imageInfoBar_->isVisible(), "no image-info row to anchor to");
    const QRect barPicture(win.selectedLineBar_->mapTo(&win, QPoint(0, 0)),
                           win.selectedLineBar_->size());
    const QRect infoRectNow(win.imageInfoBar_->mapTo(&win, QPoint(0, 0)), win.imageInfoBar_->size());
    const int dockTop = win.selectedLineDock_->mapTo(&win, QPoint(0, 0)).y();

    // The x is the BAR's own centre — both bars span the full window width (each its own
    // Qt::TopDockWidgetArea dock), so this already IS the window's centre.
    QVERIFY2(std::abs(barPicture.center().x() - win.width() / 2) < 4,
             "the bar itself is not spanning the full window width — the premise of this test");
    const QPoint openPt = win.selectedLineBarDustPoint(barPicture, /*closing=*/false);
    QCOMPARE(openPt.x(), barPicture.center().x());
    QCOMPARE(openPt.y(), infoRectNow.bottom());   // reflow already ran — read it as-is

    // Closing predicts the row's post-close position (the dock's current top + the row's
    // height) rather than using its live, still-stale bottom — which would overshoot.
    const QPoint closePt = win.selectedLineBarDustPoint(barPicture, /*closing=*/true);
    QCOMPARE(closePt.x(), barPicture.center().x());
    QCOMPARE(closePt.y(), dockTop + infoRectNow.height());
    QVERIFY2(closePt.y() <= barPicture.top() + infoRectNow.height(),
             "the dock's own top must be at or above the bar's content-widget top");
    QVERIFY2(closePt.y() < infoRectNow.bottom(),
             "the closing point used the stale (pre-close) position instead of predicting it");

    // Reselecting a DIFFERENT line while the bar is already open must not disturb it —
    // it just repopulates in place (same as the browser's wasHidden gate).
    stencil::core::Line line2;
    line2.points = {{100, 20}, {160, 80}};
    canvas->setLines({line, line2});
    canvas->selectLineByIndex(1);
    QTRY_VERIFY2(win.selectedLineDock_->isVisible(), "the bar stays up across a re-selection");
    beat();

    // Deselect: the bar goes away (and, off this platform, dust would scatter out of it).
    canvas->deselect();
    QTRY_VERIFY_WITH_TIMEOUT(!win.selectedLineDock_->isVisible(), 2000);
    beat();

    // Re-selecting after a full hide brings it straight back — nothing latched stuck.
    canvas->selectLineByIndex(0);
    QTRY_VERIFY_WITH_TIMEOUT(win.selectedLineDock_->isVisible(), 2000);
    beat();
  }

  // Clearing the image scatters it as dust — and the empty-canvas invitation must NOT
  // appear underneath the falling particles, which reads as the clear happening twice.
  // It is held back for the length of the animation, and cannot be clicked while hidden.
  void clearHoldsTheIdleHintUntilTheDustLands() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QVERIFY(!canvas->idleHintHidden());

    QAction* clear = actionByText(&win, "Clear Project");
    QVERIFY(clear);
    dismissModal("OK");
    clear->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!canvas->hasImage(), 5000);

    // Image gone, dust falling, invitation still off screen.
    QVERIFY2(canvas->idleHintHidden(), "the blank-image affordance must wait for the dust");
    QSignalSpy asked(canvas, &CanvasWidget::blankImageRequested);
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
    QCOMPARE(asked.count(), 0);   // nothing visible to click, so nothing opens

    // …and it comes back once the animation is over, so the affordance is reachable again.
    // NOT clicked here: accepting it opens the blank-image creator, whose modal loop would
    // hold this test until it timed out (which is exactly what it did).
    QTRY_VERIFY_WITH_TIMEOUT(!canvas->idleHintHidden(),
                             stencil::gui::DisintegrateOverlay::kMs + 1500);
    QCOMPARE(asked.count(), 0);
    beat();
  }

  // A REAL click through the compare combo's own themed popup, not setCompareModeUi():
  // the connect() has to live in buildDrawViewToolbar, after compareCombo_ is built —
  // wired from buildStyleToolbar it was a connect() on a null sender, silently dropped.
  void compareComboClickActuallyChangesTheCanvas() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(64, 48, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    win.loadImageWithLayout(img, QJsonObject());

    QVERIFY(win.compareCombo_);
    QCOMPARE(win.compareCombo_->currentData().toString(), QStringLiteral("none"));
    win.compareCombo_->showPopup();
    QTRY_VERIFY(QApplication::activePopupWidget());
    QWidget* popup = nullptr;
    for (QWidget* w : QApplication::topLevelWidgets())
      if (w->isVisible() && w->findChild<QWidget*>("searchComboPopup")) popup = w;
    QVERIFY2(popup, "the compare combo's themed popup never appeared");
    auto* list = popup->findChild<QListView*>("searchComboList");
    QVERIFY(list);
    // Row 2 = "Split ↔" (vertical) — see the addItem() order in buildDrawViewToolbar.
    const QModelIndex idx = list->model()->index(2, 0);
    QCOMPARE(idx.data(Qt::DisplayRole).toString(), QString::fromUtf8("Split ↔"));
    QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, list->visualRect(idx).center());
    QTRY_COMPARE(win.compareCombo_->currentData().toString(), QStringLiteral("vertical"));
    QCOMPARE(win.canvas_->compareMode(), QStringLiteral("vertical"));
  }

  // A split compare of a BLANK page shows the blank's own fill on BOTH halves:
  // the original side is the untouched red page, the edit side the red page
  // with the lines — never a gray/neutral placeholder.
  void compareSplitOfBlankKeepsItsFill() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.applyImageFilter("none");   // the filter persists across sessions/tests
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"done\",\"actions\":["
                      "{\"op\":\"blank\",\"color\":\"red\",\"format\":\"a4\"},"
                      "{\"op\":\"layout\",\"lines\":[{\"points\":[{\"x\":200,\"y\":300},"
                      "{\"x\":600,\"y\":300},{\"x\":600,\"y\":800},{\"x\":200,\"y\":800},"
                      "{\"x\":200,\"y\":300}],\"color\":\"#000000\"}]},"
                      "{\"op\":\"compare\",\"mode\":\"vertical\",\"split\":0.5}]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    win.onChatSend("red page with a centred rectangle, compared side by side");
    QTRY_COMPARE(win.canvas_->compareMode(), QStringLiteral("vertical"));
    // The arrival effect hides the canvas briefly; grab only once it is gone.
    QTRY_VERIFY(win.canvas_->graphicsEffect() == nullptr);
    const QImage shot = win.canvas_->grab().toImage();
    // Sample well inside each half, away from the rectangle and the divider.
    const QColor left = shot.pixelColor(
        QPoint(int(shot.width() * 0.10), int(shot.height() * 0.5)));
    const QColor right = shot.pixelColor(
        QPoint(int(shot.width() * 0.90), int(shot.height() * 0.5)));
    const auto isRed = [](const QColor& c) {
      return c.red() > 200 && c.green() < 80 && c.blue() < 80;
    };
    QVERIFY2(isRed(right), qPrintable(QStringLiteral("edit half is %1, not the blank fill")
                                          .arg(right.name())));
    QVERIFY2(isRed(left), qPrintable(QStringLiteral("original half is %1, not the blank fill")
                                         .arg(left.name())));
    // With a filter riding (how a model often colours a page: blank + tint/filter),
    // a BLANK's compare still shows the SAME page colour on both halves — its
    // colour IS the page, so only the lines may differ. Before the fix the
    // original half dropped the filter and went red-vs-gray.
    win.applyImageFilter("bw");
    QTest::qWait(30);
    const QImage shotF = win.canvas_->grab().toImage();
    const QColor leftF = shotF.pixelColor(
        QPoint(int(shotF.width() * 0.10), int(shotF.height() * 0.5)));
    const QColor rightF = shotF.pixelColor(
        QPoint(int(shotF.width() * 0.90), int(shotF.height() * 0.5)));
    QCOMPARE(leftF.name(), rightF.name());
    QVERIFY2(!isRed(leftF), "the filtered blank's original half ignored the filter");
    win.applyImageFilter("none");
    beat();
  }

  // A filter left over from the previous image/session must not repaint a FRESH
  // blank: "make a red page" under a riding 'bw' filter rendered flat gray
  // (Rec. 709 luma of pure red = 54) with no red anywhere. Creation resets the
  // filter to none; a filter applied AFTER creation still works (test above).
  void blankCreationResetsRidingFilter() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.applyImageFilter("bw");
    win.createBlankImage(QColor("#ff0000"), 400, 300);
    QCOMPARE(win.settings_.imageFilter, QStringLiteral("none"));
    QTRY_VERIFY(win.canvas_->graphicsEffect() == nullptr);
    const QImage shot = win.canvas_->grab().toImage();
    const QColor mid = shot.pixelColor(shot.width() / 2, shot.height() / 2);
    QVERIFY2(mid.red() > 200 && mid.green() < 80 && mid.blue() < 80,
             qPrintable(QStringLiteral("blank is %1, not red").arg(mid.name())));
    beat();
  }

  // The bug this locks down: recoloring a blank project's background regenerates the
  // SAME dimensions in place (applyBlankColor → loadFromImage(img, keepZoom=true)) —
  // there is nothing to refit, so the zoom the user had set must survive it (browser
  // parity: drawingApp.js loadImageFromFile's opts.keepZoom / drawingApp-launch tests).
  void recoloringABlankKeepsTheZoom() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.createBlankImage(QColor("#ffffff"), 400, 300);
    win.canvas_->setScale(2.5);
    QCOMPARE(win.canvas_->scale(), 2.5);
    win.applyBlankColor(QColor("#0000ff"));
    QCOMPARE(win.canvas_->scale(), 2.5);
    beat();
  }

  // Canvas scrollbars are invisible at rest, revealed only by an actual pan or zoom — never
  // just from hovering the canvas — and fade back out once the view settles. Direct opacity
  // checks: a real fade plays only off the offscreen platform, but the opacity value itself
  // is plain state either way.
  void canvasScrollbarsHideUntilPanOrZoom() {
    MainWindow win;
    win.resize(600, 500);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // TALL, not square: the last leg needs a zoom where the width fits and the height
    // still does not, and a square image left that to a viewport size the layout picks.
    QImage img(800, 4000, QImage::Format_RGB32);   // bigger than the viewport at 100%
    img.fill(Qt::white);
    win.loadImageWithLayout(img, QJsonObject());
    win.refreshActions();
    QVERIFY(win.vScrollOpacity_ && win.hScrollOpacity_);

    win.setZoom(1.0);
    QCOMPARE(win.vScrollOpacity_->opacity(), 1.0);
    QCOMPARE(win.hScrollOpacity_->opacity(), 1.0);
    // Overlay bars, browser-style: they float INSIDE the viewport (which spans the whole
    // area — no gutter reserved beside/below it) and are only there while there's overflow.
    QScrollBar* vbar = win.canvasScrollBar(Qt::Vertical);
    QScrollBar* hbar = win.canvasScrollBar(Qt::Horizontal);
    QVERIFY(vbar->isVisible() && hbar->isVisible());
    QVERIFY(win.scroll_->viewport()->geometry().contains(vbar->geometry()));
    QVERIFY(win.scroll_->viewport()->geometry().contains(hbar->geometry()));
    QCOMPARE(win.scroll_->viewport()->geometry(), win.scroll_->contentsRect());
    QVERIFY(!vbar->testAttribute(Qt::WA_TransparentForMouseEvents));
    // The thumb is a painted pill in the browser's thumb grey (support/pillScrollBars.hpp,
    // like every bar in the app; QSS cannot round a handle on macOS): its top edge's
    // midpoint carries the thumb colour while the slot's corner beside it does not.
    {
      QStyleOptionSlider opt;
      opt.initFrom(vbar);
      opt.orientation = Qt::Vertical;
      opt.minimum = vbar->minimum(); opt.maximum = vbar->maximum();
      opt.sliderPosition = vbar->sliderPosition(); opt.sliderValue = vbar->value();
      opt.pageStep = vbar->pageStep(); opt.singleStep = vbar->singleStep();
      opt.upsideDown = vbar->invertedAppearance();
      const QRect slider = vbar->style()->subControlRect(QStyle::CC_ScrollBar, &opt, QStyle::SC_ScrollBarSlider, vbar);
      QVERIFY(slider.isValid());
      const QImage shot = vbar->grab().toImage();
      const qreal dpr = shot.devicePixelRatio();
      const QColor thumb = stencil::gui::canvasScrollThumb(stencil::gui::resolveDark(win.settings_.themeMode));
      const auto near = [](const QColor& a, const QColor& b) {
        return qAbs(a.red() - b.red()) < 24 && qAbs(a.green() - b.green()) < 24 && qAbs(a.blue() - b.blue()) < 24;
      };
      const QColor mid = shot.pixelColor(QPoint(slider.center().x(), slider.top() + 1) * dpr);
      const QColor corner = shot.pixelColor(QPoint(slider.left(), slider.top()) * dpr);
      QVERIFY2(near(mid, thumb), qPrintable("the thumb's top-edge midpoint is not the thumb grey: " + mid.name()));
      QVERIFY2(!near(corner, thumb), "the thumb's corner is filled — the thumb is not rounded");
      // Thin at rest, a little thicker under the pointer (browser parity). FOUR px off
      // the centre line, not three: the pill is centred on the slot's half-pixel centre,
      // so centre−3 is an antialiased blend and centre−4 the first column truly outside.
      const QPoint side(slider.center().x() - 4, slider.top() + 6);
      const QColor slot = corner;
      QVERIFY2(near(shot.pixelColor(side * dpr), slot), "the resting thumb is already wide");
      QEnterEvent enter(QPointF(slider.center()), QPointF(vbar->mapTo(&win, slider.center())),
                        QPointF(vbar->mapToGlobal(slider.center())));
      QCoreApplication::sendEvent(vbar, &enter);
      const auto swollen = [&] { return !near(vbar->grab().toImage().pixelColor(side * dpr), slot); };
      settle(swollen, 300);   // the 150ms swell
      QVERIFY2(swollen(), "the thumb did not swell under the pointer");
      QEvent leave0(QEvent::Leave);
      QCoreApplication::sendEvent(vbar, &leave0);
      settle([&] { return !swollen(); }, 300);
      QVERIFY2(near(vbar->grab().toImage().pixelColor(side * dpr), slot), "the thumb did not settle back after the pointer left");
      win.scrollbarHovered_ = false;
      win.revealCanvasScrollbars();   // re-arm the reveal our synthetic Leave just cancelled
    }
    // Out on the idle timer's own timeout(), not on an opacity that may never have been
    // 1: a QTRY_ on the value alone goes green on a fade that never ran.
    const auto hidesOut = [&win] {
      QSignalSpy fired(win.scrollbarHideTimer_, &QTimer::timeout);
      return win.scrollbarHideTimer_->isActive() && fired.wait(1500)
             && win.vScrollOpacity_->opacity() == 0.0 && win.hScrollOpacity_->opacity() == 0.0;
    };
    QVERIFY(hidesOut());

    // A pan (here: the vertical scrollbar's own value, exactly what a drag-pan/wheel-scroll
    // drives — see MainWindow::scrollTo) reveals it again, and it fades back out the same way.
    win.scroll_->verticalScrollBar()->setValue(50);
    QCOMPARE(win.vScrollOpacity_->opacity(), 1.0);
    QVERIFY(hidesOut());

    // Hovering the bar itself (to grab it) must never let it fade out from under the cursor.
    QEvent enter(QEvent::Enter);
    QCoreApplication::sendEvent(win.canvasScrollBar(Qt::Vertical), &enter);
    QVERIFY(win.scrollbarHovered_);
    QCOMPARE(win.vScrollOpacity_->opacity(), 1.0);
    QTest::qWait(1200);   // would have hidden by now if hovering didn't suppress it
    QCOMPARE(win.vScrollOpacity_->opacity(), 1.0);
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(win.canvasScrollBar(Qt::Vertical), &leave);
    QVERIFY(!win.scrollbarHovered_);
    QVERIFY(hidesOut());
    // Dragging the floating bar drives the real scroll model, and vice versa.
    vbar->setValue(120);
    QCOMPARE(win.scroll_->verticalScrollBar()->value(), 120);
    win.scroll_->verticalScrollBar()->setValue(60);
    QCOMPARE(vbar->value(), 60);
    // Once an axis fits, its bar goes away entirely rather than lingering as a gutter, and
    // the survivor then runs the viewport's full length. The zoom is read off the LIVE
    // viewport: the image is five times as tall as it is wide, so a width that just fits
    // leaves the height overflowing whatever size the layout gave the canvas.
    win.setZoom(double(win.scroll_->viewport()->width() - 40) / 800.0);
    QTRY_VERIFY(!hbar->isVisible());
    QVERIFY(vbar->isVisible());
    QCOMPARE(vbar->height(), win.scroll_->viewport()->height());
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "mainWindow.canvas.gui.moc"
