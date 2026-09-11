// MainWindow GUI e2e — The window's own chrome: the name bar, formula fields, status hints,
// fullscreen, incognito and the toast stack.
// Shared ground (helpers, the loaded window, the motion pins) is in mainWindow.gui.hpp.
#include "mainWindow.gui.hpp"

// Comfortably past the f(x,y) idle-commit delay (mainWindow.cpp kFormulaCommitMs), so a
// "stopped typing" wait can't race the timer on a loaded machine.
constexpr int kFormulaSettleMs = 1600;
// The shared box of the panel's two collapse chevrons (mainWindow kPanelToggleBox /
// selectionPanel kToggleBox) — asserted equal so the pair can't drift apart.
constexpr int kPanelChevronBox = 24;

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Regression: enabling the f(x,y) pill must reveal the x/y formula inputs, and they must
  // stay visible across an image load and window resizes (the state the user drives).
  // Hovering the project name REVEALS its ✎/🎨 affordances. At their natural size
  // (51×47 — QToolButton padding plus a default-sized icon) they were far taller than the
  // 28px name field, so the header grew the moment the cursor arrived and the whole window
  // jumped under it. Every control in that group is sized to the row instead.
  void nameHoverDoesNotResizeTheHeader() {
    MainWindow win(nullptr, false);
    win.resize(1400, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTest::qWait(300);
    win.actToolbars_->setChecked(false);   // collapsed: the header is all there is
    QTest::qWait(400);
    QToolBar* hdr = win.headerToolbar_;
    QVERIFY(hdr);
    const int before = hdr->height();
    win.nameBar_.hover = true;
    win.refreshProjectNameButtons();
    QTest::qWait(200);
    QCOMPARE(hdr->height(), before);
    // …and the same going into edit mode, where ✓/✗ take their place.
    win.nameBar_.field->setEnabled(true);
    QTest::mouseClick(win.nameBar_.edit, Qt::LeftButton);
    QTest::qWait(200);
    QCOMPARE(hdr->height(), before);
  }

  // Every selector opens the app's OWN popup, never the platform one: macOS draws a
  // native combo popup itself — centred over the control, in its own palette — so a
  // toolbar of themed controls answered a click with a system menu. The
  // browser makes the same swap (js/ui/customSelect.js, e2e custom-selects.spec.js).
  // The ONE exception on both sides is zoom: a number field with a preset list attached,
  // which works as it is.
  void everySelectorUsesTheAppsOwnPopup() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1400, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    int checked = 0;
    for (QComboBox* c : win.findChildren<QComboBox*>()) {
      if (c == win.zoom_) { QVERIFY2(c->isEditable(), "zoom is the editable exception"); continue; }
      // dynamic_cast, not qobject_cast: SearchComboBox is deliberately MOC-free.
      QVERIFY2(dynamic_cast<stencil::gui::SearchComboBox*>(c),
               qPrintable(QString("%1 still opens the platform popup").arg(c->objectName())));
      QCOMPARE(c->cursor().shape(), Qt::PointingHandCursor);
      ++checked;
    }
    QVERIFY2(checked >= 4, "no toolbar selectors were found to check");

    // …and opening one really puts OUR popup on screen, with the rows in it.
    QVERIFY(win.lineStyle_);
    win.lineStyle_->showPopup();
    QTest::qWait(60);
    QWidget* popup = nullptr;
    for (QWidget* w : QApplication::topLevelWidgets())
      if (w->isVisible() && w->findChild<QWidget*>("searchComboPopup")) popup = w;
    QVERIFY2(popup, "the themed popup never appeared");
    auto* list = popup->findChild<QListView*>("searchComboList");
    QVERIFY(list);
    QCOMPARE(list->model()->rowCount(), win.lineStyle_->count());
    // A short list carries no search box — three rows need no filter.
    QVERIFY2(!popup->findChild<QLineEdit*>("searchComboSearch"), "a 3-row list grew a search box");
    win.lineStyle_->hidePopup();
    // The long ISO page list keeps its search box, which is what it was built for.
    win.units_.pageSize->showPopup();
    QTest::qWait(60);
    QWidget* pagePopup = nullptr;
    for (QWidget* w : QApplication::topLevelWidgets())
      if (w->isVisible() && w->findChild<QWidget*>("searchComboPopup")) pagePopup = w;
    QVERIFY(pagePopup);
    QVERIFY2(pagePopup->findChild<QLineEdit*>("searchComboSearch"), "page formats lost their search");
    win.units_.pageSize->hidePopup();
  }

  // The window opens at the size it asked for. The wrapping tool run (support/wrapRow.hpp)
  // hints its WRAPPED height from the very first pass — it used to hint the STACKED one, a
  // control per line, which QToolBarLayout took as the bar's minimum: the window was sized
  // to fit ~840px of toolbar, opened 1145px tall whatever it asked for, and never gave the
  // height back once the row settled a beat later.
  void theWindowOpensAtTheHeightItAsksFor() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1400, 500);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTRY_COMPARE_WITH_TIMEOUT(win.width(), 1400, 500);
    QVERIFY2(win.height() < 700,
             qPrintable(QString("opened %1px tall, asked for 500").arg(win.height())));
    // The run really does wrap, and the minimum tracks it: a narrower window needs more
    // lines and honestly asks for the height they take.
    auto* row = win.findChild<QWidget*>("toolWrapRow");
    QVERIFY(row);
    win.resize(1500, 500);
    settle([&] { return win.width() == 1500; }, 500);
    const int wideRow = row->height(), wideMin = win.minimumSizeHint().height();
    win.resize(900, 500);
    settle([&] { return row->height() > wideRow; }, 500);
    QVERIFY2(row->height() > wideRow, "the run did not wrap onto more lines when narrowed");
    QVERIFY2(win.minimumSizeHint().height() > wideMin, "…and the minimum did not follow it");
  }

  // The f(x,y) pair is as wide as the browser's (#formula-x / #formula-y, 180px inline) —
  // they were pinned to a 72px stub, which fits no real formula — and keep that width on a
  // narrow window, where the tool run WRAPS (support/wrapRow.hpp) rather than squeezing its
  // fields or hiding them behind QToolBar's "»".
  void formulaFieldsTakeTheBrowsersWidth() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1400, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* pill = win.findChild<QCheckBox*>("formulaPill");
    QVERIFY(pill && win.formulaX_ && win.formulaY_);
    if (!pill->isChecked()) pill->setChecked(true);
    QTRY_VERIFY(win.formulaX_->isVisible());
    // The reveal is ANIMATED, so every width here is settled with a QTRY — a plain
    // compare beside the first one reads the pair mid-slide on a slow machine.
    QTRY_COMPARE(win.formulaX_->width(), 180);
    QTRY_COMPARE(win.formulaY_->width(), 180);
    // …side by side on the browser's 6px gap, not spread out over the row's leftover width.
    QTRY_COMPARE(win.formulaY_->x() - (win.formulaX_->x() + win.formulaX_->width()), 6);
    // A row with no slack wraps around them: the pair keeps the browser's width and stays
    // on the toolbar, down to the narrowest window the layout allows.
    for (const int w : {1100, 920, 850}) {
      win.resize(w, 800);
      QTRY_COMPARE(win.formulaX_->width(), 180);
      QCOMPARE(win.formulaY_->width(), 180);
      QVERIFY2(win.formulaX_->isVisible(),
               qPrintable(QString("the formula fields hid at %1px").arg(w)));
    }
  }

  void formulaToggleRevealsInputs() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* pill = win.findChild<QCheckBox*>("formulaPill");
    QVERIFY(pill);
    QLineEdit* fx = nullptr;
    for (auto* e : win.findChildren<QLineEdit*>())
      if (e->placeholderText().startsWith("x(x)")) fx = e;
    QVERIFY(fx);
    // The pill starts wherever the LAST run left it (settings.json persists
    // allowFormulas), and its reveal/hide is animated — so every wait here is a
    // QTRY: a fixed one passed only when the pill happened to start unchecked.
    if (pill->isChecked()) pill->setChecked(false);
    QTRY_VERIFY(!fx->isVisible());
    QTest::mouseClick(pill, Qt::LeftButton, Qt::NoModifier, pill->rect().center());
    QTRY_VERIFY2(fx->isVisible(), "formula inputs should appear when f(x,y) is enabled");
    win.openPathFromOS(guiTestImage());
    QTest::qWait(120);
    QVERIFY2(fx->isVisible(), "formula inputs should survive an image load");
    win.resize(720, 800); QTest::qWait(80);
    win.resize(1200, 800); QTest::qWait(80);
    QVERIFY2(fx->isVisible(), "formula inputs should survive window resizes");
  }

  // Regression: the f(x,y) pair commits when typing SETTLES, not per keystroke. A formula
  // is typed one character at a time, so an expression still being written must not be
  // judged (no "⚠ invalid" flashing mid-word) nor applied — it is only flagged once the
  // user stops. Mirrors browser settingsController.wireFormulaInputs.
  void formulaCommitsOnIdlePauseNotPerKeystroke() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    // Wide enough that the inline formula inputs stay on the row rather than folding
    // into the toolbar's ≫ extension — this test is about the debounce, not the layout.
    win.resize(1600, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* pill = win.findChild<QCheckBox*>("formulaPill");
    QVERIFY(pill);
    if (!pill->isChecked()) {
      QTest::mouseClick(pill, Qt::LeftButton, Qt::NoModifier, pill->rect().center());
      QTest::qWait(60);
    }
    QLineEdit* fx = nullptr;
    for (auto* e : win.findChildren<QLineEdit*>())
      if (e->placeholderText().startsWith("x(x)")) fx = e;
    QVERIFY(fx);
    // By object name, not by its text: the label is icon-only (the words live on its
    // tooltip, as in the browser), so matching on "invalid" found nothing.
    QLabel* err = win.findChild<QLabel*>("formulaError");
    QVERIFY(err);
    // A window restores the persisted formulas, so start from a known-empty field and let
    // that clear settle (leaving no pending commit to race the assertions below).
    fx->clear();
    QTest::qWait(kFormulaSettleMs);
    QVERIFY(!err->isVisible());

    fx->setFocus();
    QTest::keyClicks(fx, "(x+", Qt::NoModifier, 40);   // half-written, a key at a time
    QVERIFY2(!err->isVisible(), "an expression still being typed must not be flagged");
    QTest::qWait(kFormulaSettleMs);                    // stop typing → the pair commits
    QVERIFY2(err->isVisible(), "a settled, unparseable expression IS flagged");

    QTest::keyClicks(fx, "1)", Qt::NoModifier, 40);    // finish it: valid again
    QCOMPARE(fx->text(), QStringLiteral("(x+1)"));
    QVERIFY2(!err->isVisible(), "fixing the expression clears the error indicator on the spot");
    QTest::qWait(kFormulaSettleMs);
    QVERIFY(!err->isVisible());

    // Leave the persisted formula as we found it — the settings are shared across tests.
    fx->clear();
    QTest::qWait(kFormulaSettleMs);
  }

  // Fullscreen edge-hover: prove the top toolbars and the right points panel REVEAL WITH AN
  // ANIMATION (they pass through intermediate sizes, not an instant pop) and do so MONOTONICALLY
  // (no size oscillation = no flicker), then hide + fully restore on exit with no lingering effect.
  void fullscreenRevealAnimatesSmoothly() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    auto maxBarHeight = [&win] {
      int m = 0;
      for (QToolBar* b : win.findChildren<QToolBar*>())
        if (b->isVisible()) m = std::max(m, b->height());
      return m;
    };
    auto anyBarVisible = [&win] {
      for (QToolBar* b : win.findChildren<QToolBar*>()) if (b->isVisible()) return true;
      return false;
    };
    // Sample a size getter every ~16ms across the ~200ms animation; return the series.
    auto sample = [](auto getter) {
      QList<int> s;
      for (int i = 0; i < 20; ++i) { s.append(getter()); QTest::qWait(16); }
      return s;
    };
    auto hasIntermediate = [](const QList<int>& s, int full) {   // some value strictly inside (0, full)
      for (int v : s) if (v > 2 && v < full - 2) return true;
      return false;
    };
    auto nonDecreasing = [](const QList<int>& s) {
      for (int i = 1; i < s.size(); ++i) if (s[i] < s[i - 1] - 1) return false;   // 1px slack
      return true;
    };
    auto nonIncreasing = [](const QList<int>& s) {
      for (int i = 1; i < s.size(); ++i) if (s[i] > s[i - 1] + 1) return false;
      return true;
    };

    QAction* fs = actionByText(&win, "Enter Fullscreen");
    QVERIFY(fs);
    fs->trigger();                                   // ENTER fullscreen (bars + panel hidden)
    QTest::qWait(120);
    QVERIFY(!anyBarVisible());                        // nothing shown until the cursor hits an edge

    // --- Top toolbars: cursor to the top band → animated slide-in ---
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() / 2, 40)));
    const QList<int> up = sample(maxBarHeight);
    const int full = up.isEmpty() ? 0 : up.last();
    QVERIFY2(full > 10, "toolbars should have revealed to a real height");
    QVERIFY2(hasIntermediate(up, full), "toolbar reveal popped instantly (no intermediate heights)");
    QVERIFY2(nonDecreasing(up), "toolbar reveal height oscillated (flicker)");

    // Cursor well below the keep-zone → animated slide-out.
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() / 2, win.height() - 40)));
    const QList<int> down = sample(maxBarHeight);
    QVERIFY2(nonIncreasing(down), "toolbar hide height oscillated (flicker)");
    QTest::qWait(120);

    // --- Right points panel: cursor to the right edge → animated slide-in reveal ---
    QWidget* panel = nullptr;
    for (QWidget* dw : win.findChildren<QWidget*>())
      if (QString(dw->metaObject()->className()).contains("SelectionPanel")) { panel = dw; break; }
    if (panel) {
      QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 2, win.height() / 2)));
      auto panelW = [panel] { return panel->isVisible() ? panel->width() : 0; };
      const QList<int> pin = sample(panelW);
      const int pfull = pin.isEmpty() ? 0 : pin.last();
      if (pfull > 10) {   // reveal fired (setPos is a soft no-op on some offscreen builds)
        QVERIFY2(hasIntermediate(pin, pfull), "panel reveal popped instantly (no intermediate widths)");
        QVERIFY2(nonDecreasing(pin), "panel reveal width oscillated (flicker)");
      } else {
        qWarning("panel reveal did not fire (cursor setPos likely a no-op offscreen)");
      }
    }

    // --- Exit: everything restored, no lingering graphics effect ---
    fs->trigger();
    settle([&] { return win.isVisible() && anyBarVisible(); }, 500);
    QVERIFY(win.isVisible());
    QVERIFY(anyBarVisible());
    for (QToolBar* b : win.findChildren<QToolBar*>()) QVERIFY(b->graphicsEffect() == nullptr);
  }

  // Entering/leaving fullscreen plays the canvas STRETCHING out of (and minimising
  // back into) its old viewport box — MainWindow::beginFullscreenZoom, the desktop
  // twin of the FLIP in browser/js/ui/motion.js. The contract that must hold whatever
  // the window manager does with the geometry is that the ramp only ever ENDS on the
  // zoom the user picked: the motion is decoration, never a zoom change. Offscreen the
  // resize may not land at all, in which case it correctly plays nothing.
  void fullscreenStretchPreservesZoom() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    CanvasWidget* canvas = openLoaded(win);
    QVERIFY(canvas);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    win.setZoom(0.5);
    const double chosen = canvas->scale();
    QCOMPARE(chosen, 0.5);

    QAction* fs = actionByText(&win, "Enter Fullscreen");
    QVERIFY(fs);
    fs->trigger();
    // Outlast the ramp itself plus the bounded wait for the window manager's resize.
    QTRY_VERIFY_WITH_TIMEOUT(!win.fs_.zoomAnim, 3000);
    QCOMPARE(canvas->scale(), chosen);  // entering never changed the user's zoom

    fs->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!win.fs_.zoomAnim, 3000);
    QCOMPARE(canvas->scale(), chosen);  // …and neither did leaving
  }

  // The incognito frame DRAWS ON clockwise from the top-left rather than blinking into
  // place, and retracts the same way — the desktop half of the browser's four staggered
  // .ig-edge elements. framePath is pure, so the order is checkable without a display.
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

  // The "?" beside the project name is the ONLY readout left once the tool rows
  // are collapsed, so it must survive that collapse — and its bubble carries the
  // A window shortcut pressed while that window is up CLOSES it (it used to re-open the same
  // dialog, so nothing appeared to happen), and another window's shortcut SWAPS to it rather
  // than stacking a second window on top. The dialog carries its own copies of those chords
  // because a modal event loop never lets the main window's actions fire.
  void windowShortcutsToggleAndSwap() {
    MainWindow win(nullptr, false);
    openLoaded(win);
    QVERIFY2(!win.actInfo_->shortcut().isEmpty(), "help carries a shortcut");
    QVERIFY2(!win.actShortcuts_->shortcut().isEmpty(),
             "the shortcuts window has one of its own now");
    QVERIFY2(!win.actSettings_->shortcut().isEmpty(), "…and so does Settings");
    QVERIFY(win.hotkeyActions_.contains(QStringLiteral("openHotkeys")));
    QVERIFY(win.hotkeyActions_.contains(QStringLiteral("openVisuals")));
    QVERIFY(win.hotkeyActions_.contains(QStringLiteral("openAssistantSettings")));
    QVERIFY2(!win.actInfo_->shortcut().toString().contains(QStringLiteral("F1")),
             "help left the lone F1 for the Alt+letter family");

    int settingsAsked = 0;
    connect(win.actSettings_, &QAction::triggered, &win, [&] { settingsAsked++; });

    bool sawOwn = false, sawOther = false, parked = false;
    QTimer::singleShot(0, &win, [&] {
      QDialog* shown = nullptr;
      for (QDialog* d : win.findChildren<QDialog*>())
        if (d->isVisible()) shown = d;
      QVERIFY(shown);
      // While it is up, the main window's own copies of these chords are parked, so the two
      // cannot fire ambiguously at each other.
      parked = win.actInfo_->shortcutContext() == Qt::WidgetShortcut;
      // Its own chord…
      for (QShortcut* sc : shown->findChildren<QShortcut*>()) {
        if (sc->key() == win.actInfo_->shortcut()) { sawOwn = true; emit sc->activated(); }
      }
      QVERIFY2(!shown->isVisible(), "its own shortcut closed the window");
      // …and another window's chord is wired too, queued to open after this one unwinds.
      for (QShortcut* sc : shown->findChildren<QShortcut*>())
        if (sc->key() == win.actSettings_->shortcut()) sawOther = true;
    });
    win.openInfo();

    QVERIFY2(sawOwn, "the dialog carried its own opener's chord");
    QVERIFY2(sawOther, "…and the other windows' chords, for swapping");
    QVERIFY2(parked, "the main window's duplicate was parked while the dialog owned it");
    QVERIFY2(win.actInfo_->shortcutContext() != Qt::WidgetShortcut,
             "…and handed back when the dialog closed");
    QCOMPARE(settingsAsked, 0);   // nothing was swapped to in this pass
    beat();
  }

  // image size plus, while incognito, the "not saved" line. Those two facts and
  // nothing else (the canvas pill that used to say it is gone).
  void statusHintCarriesSizeAndIncognitoOnly() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QLabel* hint = win.statusHint_;
    QVERIFY(hint);
    QCOMPARE(hint->text(), QStringLiteral("?"));
    // It is the COLLAPSED state's readout: with the tool rows up, the size line under them
    // already says this, so the "?" would only repeat it.
    QVERIFY2(!hint->isVisible(), "the hint stays out of the way while the tool rows are up");
    const QString size =
        QString("%1 × %2 px").arg(canvas->imageWidth()).arg(canvas->imageHeight());
    QVERIFY2(hint->toolTip().contains(size), "the bubble names the image size");
    QCOMPARE(hint->toolTip().split('\n').size(), 1);   // size only, nothing else

    // Collapsed tool rows: the header row stays, and NOW the hint appears — with the size
    // line hidden alongside the rows, its bubble is the only place these facts are left.
    win.actToolbars_->setChecked(false);
    QTRY_VERIFY(!win.actToolbars_->isChecked());
    // The invariant is that exactly ONE of the two readouts is up — polled for, not timed:
    // the size line only reads as gone once the fold's finish step hides the rows
    // (QToolBarLayout re-shows its action widgets on every relayout, and the slide is one
    // per frame), and the fold's duration is not this test's business.
    QTRY_VERIFY_WITH_TIMEOUT(hint->isVisible() && !win.imageSizeInfo_->isVisible(), 3000);
    QVERIFY2(hint->toolTip().contains(size), "…still carrying the size");

    // Incognito adds its line — and only its line.
    win.actIncognito_->setChecked(true);
    QTRY_VERIFY(win.incognito_);
    const QStringList lines = hint->toolTip().split('\n');
    QCOMPARE(lines.size(), 2);
    QVERIFY2(lines[0].contains(size), "the size stays first");
    QCOMPARE(lines[1], QStringLiteral("Incognito — not saved"));

    // …and it leaves again when incognito does.
    win.actIncognito_->setChecked(false);
    QTRY_VERIFY(!win.incognito_);
    QVERIFY2(!hint->toolTip().contains("Incognito"),
             "the incognito line goes with the mode");
    QCOMPARE(hint->toolTip().split('\n').size(), 1);
    win.actToolbars_->setChecked(true);
    beat();
  }

  // The desktop stays as quiet as the browser: no toast for a routine success the user can
  // already see (an image appearing, settings applying, the session restoring).
  void routineActionsDoNotToast() {
    // MainWindow's definitions are split across several TUs — scan them all.
    const QString appDir = QStringLiteral(__FILE__).section('/', 0, -3) + "/src/app";
    QString src;
    for (const QString& name : QDir(appDir).entryList({"mainWindow*.cpp", "stencilFileSync.cpp"})) {
      QFile f(appDir + '/' + name);
      QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable("cannot read " + f.fileName()));
      src += QString::fromUtf8(f.readAll());
    }
    QVERIFY2(!src.isEmpty(), "could not read the mainWindow sources");
    const QStringList banned{"Image loaded", "Image opened", "Opened from Stencil", "Opening…",
                             "Restored last session", "Session saved", "Settings saved",
                             "Assistant settings saved", "Shortcuts updated", "Blank recoloured",
                             "Crop canceled", "Blank image canceled"};
    for (const QString& msg : banned)
      QVERIFY2(!src.contains("notify_->success(\"" + msg) && !src.contains("notify_->info(\"" + msg),
               qPrintable(QString("\"%1\" has no twin in the browser — it should not toast").arg(msg)));
    // …while the ones the browser DOES show are still there.
    QVERIFY2(src.contains("Project saved"), "Project saved has a browser twin and must stay");
    QVERIFY2(src.contains("Image cropped"), "Image cropped has a browser twin and must stay");
  }

  // Incognito shows INLINE on the image-size line (browser parity: an accent, bold
  // "<icon> Incognito — not saved" tag beside the size, behind a muted "|" divider),
  // in both the loaded and the empty state, and only while incognito is on. The glyph
  // is the app's own themed incognito icon, never an emoji. The "?" hint keeps its own
  // bubble — this is an addition, not a replacement.
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

    // The glyph is rasterised for the SCREEN it will be shown on: at dpr 2 the same
    // 16 px element carries a 32 px PNG (offscreen runs at 1x, so pass the ratio in —
    // the Retina path would otherwise never be exercised here).
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
      QTest::qWait(150);
      QVERIFY(win.actIncognito_ && !win.actIncognito_->isChecked());
      // With the assistant OPEN, so the dock is a real on-screen neighbour of the
      // canvas rather than a hidden widget whose geometry means nothing.
      win.actChat_->setChecked(true);
      QTRY_VERIFY(win.chatDock_->isVisible());
      settle([&] { return !win.chatAnim_ || win.chatAnim_->state() != QAbstractAnimation::Running; }, 600);

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
        QTest::qWait(150);
        win.actIncognito_->setChecked(false);
        const QMap<QString, QRect> before = steady();
        const int hintBefore = win.imageSizeInfo_->sizeHint().height();

        win.actIncognito_->setChecked(true);
        QTest::qWait(150);
        QVERIFY2(win.imageSizeInfo_->text().contains(QStringLiteral("Incognito")),
                 qPrintable(state + ": the tag never appeared — the check would be vacuous"));
        same(before, steady(), state + " on");
        QCOMPARE(win.imageSizeInfo_->sizeHint().height(), hintBefore);

        win.actIncognito_->setChecked(false);
        QTest::qWait(150);
        QVERIFY(!win.imageSizeInfo_->text().contains(QStringLiteral("Incognito")));
        same(before, steady(), state + " off again");
        QCOMPARE(win.imageSizeInfo_->sizeHint().height(), hintBefore);
      }
    }
    beat();
  }

  // imageSizeInfo_ needs real top/bottom breathing room via contentsMargins, not
  // stylesheet `padding` — QSS padding on this QLabel had no effect on paint or sizeHint().
  void imageSizeInfoHasRealVerticalPadding() {
    MainWindow win(nullptr, false);
    openLoaded(win);
    QVERIFY(win.imageSizeInfo_);
    // 10px left/right (browser parity: css/layout.css .info padding: 10px), 11px top/bottom
    // so the readout reads as its own band between the toolbars and the canvas.
    QCOMPARE(win.imageSizeInfo_->contentsMargins(), QMargins(10, 11, 10, 11));
    // Not just set — actually taken into account: the reserved fixed height must exceed
    // the bare font height by at least the vertical margins.
    win.reserveImageInfoHeight();
    const int fontH = QFontMetrics(win.imageSizeInfo_->font()).height();
    QVERIFY2(win.imageSizeInfo_->height() >= fontH + 22,
             "the reserved height leaves no room for 11px top + 11px bottom");
  }

  // The panel toggles are mouse affordances: taking focus draws the platform's halo
  // around the chevron, which reads as a second, taller pill sitting over the canvas.
  void panelToggleChevronsTakeNoFocusHalo() {
    MainWindow win;
    win.resize(900, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actPanel_->setChecked(false);
    settle([&] { return win.panelReopenBtn_ != nullptr; }, 600);
    QVERIFY(win.panelReopenBtn_);
    QCOMPARE(win.panelReopenBtn_->focusPolicy(), Qt::NoFocus);
    QCOMPARE(win.panelReopenBtn_->size(), QSize(kPanelChevronBox, kPanelChevronBox));
    // …and its twin in the panel header, so the pair stays consistent.
    QWidget* bar = nullptr;
    for (QDockWidget* d : win.findChildren<QDockWidget*>())
      if (d->objectName() != QLatin1String("llmChatDock") &&
          d->objectName() != QLatin1String("selectedLineDock") &&
          d->objectName() != QLatin1String("imageInfoDock") && d->titleBarWidget())
        bar = d->titleBarWidget();
    QVERIFY2(bar, "no selection-panel title bar");
    // By NAME, not "every QToolButton in the header": the header also carries the
    // Points | Lines strip, and a QTabBar owns two internal scroll arrows that are
    // QToolButtons of its own sizing.
    const auto chevrons = bar->findChildren<QToolButton*>(QStringLiteral("panelCollapseBtn"));
    QVERIFY2(!chevrons.isEmpty(), "the panel header has no collapse chevron");
    for (QToolButton* b : chevrons) {
      QCOMPARE(b->focusPolicy(), Qt::NoFocus);
      QCOMPARE(b->size(), QSize(kPanelChevronBox, kPanelChevronBox));
    }
  }

  // The ✎/🎨 are hover-revealed over the name group, and a pointer that lands anywhere else
  // has left it — even when the group's own Leave never arrives (crossing straight onto
  // another row's icon left the pair lit three clusters away).
  void nameAffordancesGoWhenThePointerLeavesTheGroup() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1400, 860);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    openLoaded(win);
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    const QString id = win.addImageProjectEntry(img, "hover-out");
    QVERIFY(!id.isEmpty());
    QVERIFY(win.loadProjectIntoCanvas(id, false));
    QTest::qWait(300);
    const auto paintedOut = [](QWidget* w) { return w->property("stencilPaintedOut").toBool(); };

    QCursor::setPos(win.nameBar_.group->mapToGlobal(win.nameBar_.group->rect().center()));
    win.updateNameHover();
    QTRY_VERIFY(!paintedOut(win.nameBar_.edit));
    QVERIFY(!paintedOut(win.nameBar_.colorBtn));

    // Onto another control, and the pair goes — driven by the same recompute the app runs
    // when a pointer enters anything else (here: called directly, as the poll would).
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 60, 200)));
    win.updateNameHover();
    QTRY_VERIFY_WITH_TIMEOUT(paintedOut(win.nameBar_.edit), 2000);
    QVERIFY(paintedOut(win.nameBar_.colorBtn));
    // …and they keep their slots either way: painting out must never move the row.
    QVERIFY(win.nameBar_.edit->isVisible() && win.nameBar_.colorBtn->isVisible());
    beat();
  }

  // Edit mode SWAPS the name affordances in place: ✎/🎨 out, ✓/✗ in, and back again. The
  // pair returning while the marks were still flying out put all four in the row at once —
  // it widened, and the ✎/🎨 appeared BESIDE the leaving marks instead of in their place
  //. Never more than two hold a slot at any moment.
  void nameChipsSwapInPlaceWithoutWideningTheRow() {
    const auto motion = withMotion();   // the flights below ARE the thing under test
    MainWindow win(nullptr, false);
    win.resize(1400, 860);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    openLoaded(win);
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    const QString id = win.addImageProjectEntry(img, "swap-row");
    QVERIFY(!id.isEmpty());
    QVERIFY(win.loadProjectIntoCanvas(id, false));
    QTest::qWait(300);
    win.nameBar_.hover = true;   // ✎/🎨 are hover-revealed; pin them on for the swap
    const auto held = [&win] {
      int n = 0;
      for (QToolButton* b : { win.nameBar_.edit, win.nameBar_.colorBtn,
                              win.nameBar_.accept, win.nameBar_.cancel })
        if (b && b->isVisible()) ++n;
      return n;
    };

    win.enterNameEdit();
    for (int i = 0; i < 10; ++i) {   // through the whole in-flight
      QTest::qWait(50);
      QVERIFY2(held() <= 2, qPrintable(QString("entering: %1 chips held a slot").arg(held())));
    }
    QVERIFY(win.nameBar_.accept->isVisible() && win.nameBar_.cancel->isVisible());

    win.cancelProjectName();
    for (int i = 0; i < 10; ++i) {   // …and the whole way back
      QTest::qWait(50);
      QVERIFY2(held() <= 2, qPrintable(QString("leaving: %1 chips held a slot").arg(held())));
    }
    QTRY_VERIFY(win.nameBar_.edit->isVisible() && win.nameBar_.colorBtn->isVisible());
    QVERIFY(!win.nameBar_.accept->isVisible() && !win.nameBar_.cancel->isVisible());
    beat();
  }

  // The project name at the top is a TITLE at rest — no box — that RINGS in the accent
  // under the pointer, as the browser's read-only #project-name-input does. The ring has
  // to live in that field's OWN stylesheet (applyProjectNameStyle): a per-widget sheet
  // outranks the themed one for every property it names, so the rule in theme.cpp was
  // simply ignored and the title stayed inert. Watched in
  // PIXELS for that reason — a stylesheet that exists is not a ring that paints.
  void projectNameTitleRingsOnHoverOnly() {
    MainWindow win(nullptr, false);
    win.resize(1400, 860);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    openLoaded(win);
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    const QString id = win.addImageProjectEntry(img, "name-row");
    QVERIFY(!id.isEmpty());
    QVERIFY(win.loadProjectIntoCanvas(id, false));
    QTest::qWait(300);
    QVERIFY(win.nameBar_.field && win.nameBar_.edit && win.nameBar_.colorBtn);

    // The chips: the browser's box, and its 4px gaps either side.
    QCOMPARE(win.nameBar_.edit->size(), QSize(stencil::gui::kNameChipBox,
                                                 stencil::gui::kNameChipBox));
    QCOMPARE(win.nameBar_.colorBtn->size(), win.nameBar_.edit->size());
    const QRect f(win.nameBar_.field->mapTo(&win, QPoint(0, 0)), win.nameBar_.field->size());
    const QRect e(win.nameBar_.edit->mapTo(&win, QPoint(0, 0)), win.nameBar_.edit->size());
    const QRect c(win.nameBar_.colorBtn->mapTo(&win, QPoint(0, 0)), win.nameBar_.colorBtn->size());
    // 8px of air either side — at 4 the chips sat right against the field's edge (user
    // report, with a picture). Browser twin: .project-name-field's `gap`.
    QCOMPARE(e.left() - f.right() - 1, 8);
    QCOMPARE(c.left() - e.right() - 1, 8);

    // …and the ring itself, top edge of the field: nothing at rest, the accent on hover.
    const auto edge = [&] {
      const QImage im = win.grab(f).toImage();
      return im.pixelColor(im.width() / 2, 1);
    };
    const QColor accent = stencil::gui::accentPrimary(win.settings_.accentColor);
    const QColor rest = edge();
    // The ring is the accent at the shared 45% (the browser's two stacked layers come to
    // the same on screen), so the edge lands between the ground and the accent — never the
    // flat accent, which read far brighter than the browser's.
    const auto near = [](const QColor& a, const QColor& b, int tol) {
      return qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) + qAbs(a.blue() - b.blue()) < tol;
    };
    const QColor blend(qRound(0.45 * accent.red() + 0.55 * rest.red()),
                       qRound(0.45 * accent.green() + 0.55 * rest.green()),
                       qRound(0.45 * accent.blue() + 0.55 * rest.blue()));
    QVERIFY2(!near(rest, accent, 60), "the title wears the ring at rest");
    win.nameBar_.field->setAttribute(Qt::WA_UnderMouse, true);
    QEnterEvent enter(QPointF(5, 5), QPointF(5, 5), win.nameBar_.field->mapToGlobal(QPointF(5, 5)));
    QApplication::sendEvent(win.nameBar_.field, &enter);
    win.nameBar_.field->update();
    QTest::qWait(150);
    const QColor hovered = edge();
    QVERIFY2(!near(hovered, rest, 24), "no ring appeared under the pointer");
    QVERIFY2(near(hovered, blend, 40),
             qPrintable(QString("the ring is not the shared 45%% accent: %1 (wanted ~%2)")
                            .arg(hovered.name(), blend.name())));
    win.nameBar_.field->setAttribute(Qt::WA_UnderMouse, false);
    beat();
  }

  // Fullscreen pulls every toolbar out from under whatever they had in the air, and takes
  // the logo's own overlay with it: a cloud started by a toolbar control was left flying
  // over the bare canvas, and the logo's resting mark sat on over the label that took its
  // place.
  void fullscreenLeavesNothingBehindIt() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("fullscreen gestures need the offscreen platform");
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    openLoaded(win);
    settle([&] { return win.canvas_->hasImage(); }, 500);
    QVERIFY(win.logoBtn_);
    QWidget* fx = win.logoFx_;
    QVERIFY(fx);
    // The mark is up (the overlay paints the logo, blanked on the button itself).
    QTRY_VERIFY_WITH_TIMEOUT(fx->isVisible(), 2000);

    // Something is in the air when the switch happens — a control's own cloud.
    stencil::gui::DisintegrateOverlay::over(win.logoBtn_, &win,
                                           stencil::gui::DisintegrateOverlay::Sweep::Fall);
    const auto cloudsUp = [&win] {
      int n = 0;
      for (const char* name : {stencil::gui::DisintegrateOverlay::kObjectName,
                               "stencilControlReveal", "stencilFilterDust"})
        for (QWidget* w : win.findChildren<QWidget*>(QString::fromLatin1(name)))
          if (w->isVisible()) ++n;
      return n;
    };
    QVERIFY2(cloudsUp() > 0, "the test's own cloud never started");

    win.toggleFullscreen();
    QTest::qWait(120);
    QVERIFY2(cloudsUp() == 0, "a cloud was left flying over the fullscreen canvas");
    QVERIFY2(!win.logoBtn_->isVisible(), "fullscreen kept the header row");
    QVERIFY2(!fx->isVisible(), "the logo's mark stayed up with its button gone");

    win.toggleFullscreen();   // …and back, with the header row and its mark restored
    QTRY_VERIFY_WITH_TIMEOUT(win.logoBtn_->isVisible(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(fx->isVisible(), 2000);
    beat();
  }

  // A select popup's rows hover like every other item in the app: the glass sweep, and the
  // 2px ease right the browser's `.accent-dd-opt:hover { transform: translateX(2px) }`
  // plays. The desktop's popups had NEITHER — a page-size row lit up and that was all
  //. The slide WRAPS whatever delegate the popup already has, so a list with
  // its own painter (the motion modes' animated glyphs) keeps it.
  void selectPopupRowsSweepAndSlideOnHover() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("popup gestures need the offscreen platform");
    const auto motion = withMotion();   // the slide below IS the thing under test
    MainWindow win(nullptr, false);
    win.resize(1500, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    openLoaded(win);
    settle([&] { return win.canvas_->hasImage(); }, 500);
    stencil::gui::SearchComboBox* combo = nullptr;
    for (QComboBox* c : win.findChildren<QComboBox*>())
      if (c->isVisible() && c->count() > 2) {
        combo = dynamic_cast<stencil::gui::SearchComboBox*>(c);
        if (combo) break;
      }
    QVERIFY2(combo, "no themed select on the toolbar");
    combo->showPopup();
    settle([&] { return combo->popupList() != nullptr; }, 500);
    QListView* list = combo->popupList();
    QVERIFY(list);
    QCOMPARE(list->itemDelegate()->objectName(), QStringLiteral("stencilSlidingRows"));
    // The sweep lives on the viewport, as the projects list's does.
    QVERIFY2(!list->viewport()->findChildren<QWidget*>().isEmpty(),
             "no shimmer overlay over the popup's rows");

    const QRect row = list->visualRect(list->model()->index(1, 0));
    const auto pointAt = [&](const QPoint& at) {
      QMouseEvent mv(QEvent::MouseMove, QPointF(at),
                     list->viewport()->mapToGlobal(QPointF(at)),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(list->viewport(), &mv);
    };
    QCOMPARE(list->property("rowSlidePx").toInt(), 0);   // nothing hovered yet
    pointAt(row.center());
    QTRY_COMPARE_WITH_TIMEOUT(list->property("rowSlidePx").toInt(), 2, 1500);

    // …and it settles back the moment the pointer is off the rows.
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(list->viewport(), &leave);
    QTRY_COMPARE_WITH_TIMEOUT(list->property("rowSlidePx").toInt(), 0, 1500);
    combo->hidePopup();
    beat();
  }

  // Closing is IMMEDIATE on every path — no "Quit Stencil?" confirmation
  // modal for close() (the ✕ / ⌘Q / app-menu / Dock-Quit / Alt+F4
  // equivalents); the window simply closes.
  void closeHasNoConfirmation() {
    MainWindow win(nullptr, false);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    bool sawDialog = false;
    QTimer::singleShot(300, &win, [&win, &sawDialog] {
      if (auto* box = win.findChild<QMessageBox*>()) {
        sawDialog = true;
        box->reject();  // unblock if a dialog wrongly appeared
      }
    });
    win.close();
    QTRY_VERIFY(!win.isVisible());
    QVERIFY(!sawDialog);
    beat();
  }

  // Toasts stack, so a repeated action (flipping the theme a few times) used to build a
  // column of identical messages up the side of the canvas. The stack is capped: a new
  // arrival retires the oldest instead of piling on.
  void toastStackIsCappedAtThree() {
    QWidget host;
    host.resize(600, 420);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    stencil::gui::Notifications toasts(&host);

    // Read the stack TOP-DOWN by geometry. findChildren order is not creation order here:
    // reflow() raise()s each toast, and raise() moves a widget to the end of its parent's
    // child list — the very trap the cap itself had to be written around.
    const auto stackTopDown = [&host] {
      QList<QLabel*> live = host.findChildren<QLabel*>("toast", Qt::FindDirectChildrenOnly);
      std::sort(live.begin(), live.end(),
                [](QLabel* a, QLabel* b) { return a->y() < b->y(); });
      QStringList out;
      // text() is markup wrapping the level glyph; the plain message rides alongside it.
      for (QLabel* l : live) out << l->property("stencilToastText").toString();
      return out;
    };

    for (int i = 1; i <= 6; ++i) toasts.info(QString("Toast %1").arg(i));
    // The retired ones play their exit first, so wait for the stack to settle rather
    // than asserting on the frame the sixth arrived in.
    QTRY_COMPARE(stackTopDown().size(), stencil::gui::Notifications::kMaxVisible);
    // The OLDEST three went; the newest is lowest, where the next one will appear.
    QCOMPARE(stackTopDown(), QStringList({"Toast 4", "Toast 5", "Toast 6"}));

    // Stacked bottom-left, and none of them ran off the top of the host — which is what
    // an uncapped stack eventually does.
    for (QLabel* l : host.findChildren<QLabel*>("toast", Qt::FindDirectChildrenOnly)) {
      QCOMPARE(l->x(), 6);   // notifications.cpp kLeftMargin
      QVERIFY(l->y() >= 8);
      QVERIFY(l->geometry().bottom() <= host.height());
    }
    beat();
  }

  void repeatedToastReplacesAStillLeavingOne() {
    QWidget host;
    host.resize(600, 420);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    stencil::gui::Notifications toasts(&host);
    toasts.show("Saved", stencil::gui::Notifications::Level::Success, /*msec=*/50);
    QTest::qWait(70);   // its life timer fires -> dismiss() -> mid-way through the 160ms fadeOut
    toasts.show("Saved", stencil::gui::Notifications::Level::Success, /*msec=*/3000);
    QTest::qWait(10);
    const auto ts = host.findChildren<QLabel*>("toast", Qt::FindDirectChildrenOnly);
    QCOMPARE(ts.size(), 1);
    QCOMPARE(ts.first()->property("stencilToastText").toString(), QString("Saved"));
    QVERIFY2(!ts.first()->property("stencilToastLeaving").toBool(),
             "the survivor is the stale leaving one, not the fresh arrival");
  }

  // The rename ✓/✗ slide their slots open by animating maximumWidth, so the layout's own
  // cap is parked while that runs (controlReveal parkMaxWidth). Read back off the live
  // value instead, it ratcheted down on every interrupted swap until the pair was slivers.
  void nameChipsSurviveRenamesCutShortMidSlide() {
    MainWindow win(nullptr, false);
    win.resize(1400, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    settle([&] { return win.canvas_->hasImage(); }, 500);
    const auto motion = withMotion();   // the slide is the whole point here
    const int box = win.nameBar_.accept->maximumWidth();
    QVERIFY(box > 20);
    for (int i = 0; i < 6; ++i) {   // in and straight back out, mid-slide every time
      win.enterNameEdit();
      QTest::qWait(60);
      win.cancelProjectName();
      QTest::qWait(60);
    }
    win.enterNameEdit();
    QTRY_COMPARE(win.nameBar_.accept->width(), box);
    QCOMPARE(win.nameBar_.cancel->width(), box);
    QVERIFY2(!win.nameBar_.accept->icon().isNull(), "the tick lost its glyph");
    win.cancelProjectName();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "mainWindow.chrome.gui.moc"
