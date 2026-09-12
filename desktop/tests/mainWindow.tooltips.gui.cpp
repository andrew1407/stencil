// MainWindow GUI e2e — Rich control tooltips: what they say, how they fade, and where they stay away.
// Shared ground (helpers, the loaded window, the motion pins) is in mainWindow.gui.hpp.
#include "mainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Hover text is a shared contract: a control that exists in BOTH apps says the same
  // thing on hover, so the browser's toolbar.js is the source of truth for it (data-title,
  // or plain title where a control carries no rich tooltip). Menu LABELS are free to
  // differ — a menu row and a tooltip are different jobs — and so are desktop-only
  // affordances (Cycle Filter/Compare, the page/unit fields, the Settings dialog).
  // This is what drifted: the live-sync button described itself in its own words and,
  // having been built with no shortcut, showed no keycap at all.
  void toolbarTooltipsMatchTheBrowser() {
    // The browser's toolbar markup + the shared copy canon it interpolates from.
    stencil::test::BrowserMarkup browser;
    QVERIFY2(browser.load("browser/js/ui/toolbar.js"), "cannot read the browser's toolbar");
    const auto browserTag = [&browser](const QString& id) { return browser.tag(id); };
    const auto attrOf = [&browser](const QString& t, const QString& a) {
      return browser.attr(t, a);
    };
    const auto browserTip = [&browser](const QString& id) { return browser.tip(id); };
    // A desktop tooltip is "<text> (<shortcut>)" plus a "— reason" line while the control
    // is disabled (browser composeControlTitle) — the shortcut is drawn as a keycap and the
    // reason is checked on its own below, so only the text takes part in the comparison.
    auto textOf = [](QString tip) {
      tip.remove(QRegularExpression("\\n\u2014 [^\\n]*$"));
      const QRegularExpressionMatch m = QRegularExpression("\\s*\\(([^()]*)\\)\\s*$").match(tip);
      if (m.hasMatch() && stencil::gui::isKeyCombo(m.captured(1))) tip = tip.left(m.capturedStart()).trimmed();
      return tip;
    };

    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1400, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    struct Pair { QAction* act; QWidget* widget; const char* browserId; };
    const QList<Pair> pairs{
        {win.actOpen_, nullptr, "load-image-btn"},
        {win.actOpenAnother_, nullptr, "open-image-btn"},
        {win.actSaveImage_, nullptr, "save-image"},
        {win.actCopyImage_, nullptr, "copy-image"},
        {win.actOpenIn_, nullptr, "open-in-btn"},
        {win.actProjects_, nullptr, "projects-btn"},
        {win.actOpenProjectFile_, nullptr, "open-project-btn"},
        {win.actStencilLiveSync_, nullptr, "live-sync-btn"},
        {win.actDeleteProjectFile_, nullptr, "delete-project-btn"},
        {win.actConnect_, nullptr, "connect-btn"},
        {win.actLinks_, nullptr, "links-btn"},
        {win.actDescription_, nullptr, "description-btn"},
        {win.actKeywords_, nullptr, "keywords-btn"},
        {win.actChat_, nullptr, "chat-btn"},
        {win.actCrop_, nullptr, "crop-image"},
        {win.actRotateLeft_, nullptr, "rotate-left"},
        {win.actRotateRight_, nullptr, "rotate-right"},
        {win.actUndo_, nullptr, "undo"},
        {win.actRedo_, nullptr, "redo"},
        {win.actFit_, nullptr, "zoom-fit"},
        {win.actDownloadJson_, nullptr, "download-json"},
        {win.actUploadJson_, nullptr, "upload-json-btn"},
        {win.actCopyLayout_, nullptr, "copy-json-btn"},
        {win.actClearProject_, nullptr, "clear-storage"},
        {win.actTheme_, nullptr, "theme-toggle"},
        {win.actIncognito_, nullptr, "incognito-toggle"},
        {win.actInfo_, nullptr, "info-btn"},
        {nullptr, win.imageFilter_, "image-filter"},
        {nullptr, win.compareCombo_, "compare-mode"},
        {nullptr, win.filterColorBtn_, "filter-color"},
        {nullptr, win.nameBar_.blankColorBtn, "blank-color-btn"},
        {nullptr, win.zoom_, "zoom-input"},
        {nullptr, win.units_.pageSize, "page-size"},
        {nullptr, win.units_.unitCombo, "unit-select"},
        {nullptr, win.lineColorBtn_, "line-color"},
        {nullptr, win.lineThickness_, "line-thickness"},
        {nullptr, win.lineStyle_, "line-style"},
        {nullptr, win.pointColorBtn_, "point-color"},
        {nullptr, win.pointSize_, "point-size"},
        {nullptr, win.drawModeBtn_, "draw-mode-toggle"},
        {nullptr, win.nameBar_.edit, "project-name-edit"},
        {nullptr, win.nameBar_.colorBtn, "project-color-btn"},
        {nullptr, win.nameBar_.accept, "project-name-accept"},
        {nullptr, win.nameBar_.cancel, "project-name-cancel"},
    };
    for (const Pair& p : pairs) {
      // Both sides go through textOf: the browser writes its key into the data-title
      // ("Save name (Enter)"), the desktop hands the same key to the rich tooltip as a
      // keycap — the WORDS are what has to match.
      const QString want = textOf(browserTip(QString::fromLatin1(p.browserId)));
      QVERIFY2(!want.isEmpty(), qPrintable(QString("no browser control #%1").arg(p.browserId)));
      QVERIFY2(p.act || p.widget, p.browserId);
      // The plain text a widget's tooltip was composed from (the app renders it rich in
      // place; this suite's plain QApplication does not).
      QObject* target = p.act ? static_cast<QObject*>(p.act) : p.widget;
      const QString plain = p.act ? p.act->toolTip()
                            : target->property(stencil::gui::kPlainTipProperty).isValid()
                                ? target->property(stencil::gui::kPlainTipProperty).toString()
                                : p.widget->toolTip();
      const QString got = textOf(plain);
      QVERIFY2(got == want,
               qPrintable(QString("#%1: desktop says \"%2\", the browser says \"%3\"")
                              .arg(p.browserId, got, want)));
      // …and why it is greyed out, verbatim (data-disabled-reason): carried by every control
      // the browser gives one to, and on the tooltip exactly while the control is disabled.
      const QString tag = browserTag(QString::fromLatin1(p.browserId));
      const QString wantReason = attrOf(tag, "data-disabled-reason");
      const QString gotReason = target->property(stencil::gui::kTipReasonProperty).toString();
      QVERIFY2(gotReason == wantReason,
               qPrintable(QString("#%1: desktop reason \"%2\", the browser's \"%3\"")
                              .arg(p.browserId, gotReason, wantReason)));
      if (!wantReason.isEmpty()) {
        const bool disabled = p.act ? !p.act->isEnabled() : !p.widget->isEnabled();
        QVERIFY2(plain.contains("\n\u2014 " + wantReason) == disabled,
                 qPrintable(QString("#%1 (%2): tooltip \"%3\"")
                                .arg(p.browserId, disabled ? "disabled" : "enabled", plain)));
      }
      // A widget with a data-hk-title wears that binding's chord (an action wears its own).
      const QString hk = attrOf(tag, "data-hk-title");
      if (p.widget && !hk.isEmpty()) {
        auto* bound = qobject_cast<QAction*>(
            p.widget->property(stencil::gui::kTipHotkeyProperty).value<QObject*>());
        QVERIFY2(bound, qPrintable(QString("#%1 wears no chord for %2").arg(p.browserId, hk)));
        QCOMPARE(bound->shortcut(), QKeySequence(win.hotkey(hk, QString())));
        QVERIFY2(plain.contains("(" + bound->shortcut().toString(QKeySequence::NativeText) + ")"),
                 qPrintable(QString("#%1: no chord on \"%2\"").arg(p.browserId, plain)));
      }
    }
    // …and the shortcut the shared registry defines for a control really is on it, or the
    // tooltip has no keycap to draw and the chord does nothing.
    QCOMPARE(win.actStencilLiveSync_->shortcut(), QKeySequence(win.hotkey("toggleLiveSync", "Ctrl+Shift+Y")));
    QCOMPARE(win.actDeleteProjectFile_->shortcut(), QKeySequence(win.hotkey("deleteProject", "Ctrl+Shift+Backspace")));
  }

  // ── the fading control tooltip (support/appTooltip.hpp) ──
  // A shown, enabled, tooltip-carrying TOOLBAR button. Searched per toolbar, not over the
  // whole window: a panel chevron retires itself moments after the window opens, and a
  // tooltip whose control went away is retired by the panel's anti-stranding heartbeat.
  QToolButton* tipCarrier(MainWindow& win) {
    for (QToolBar* bar : win.findChildren<QToolBar*>())
      for (QToolButton* b : bar->findChildren<QToolButton*>())
        if (b->isVisible() && b->isEnabled() && !b->toolTip().isEmpty()) return b;
    return nullptr;
  }
  static void sendToolTipTo(QWidget* w) {
    const QPoint local(4, 4);
    QHelpEvent ev(QEvent::ToolTip, local, w->mapToGlobal(local));
    QApplication::sendEvent(w, &ev);
  }

  // Tooltips FADE in and out (browser #app-tooltip: 90 ms) instead of snapping. Qt's own
  // QTipLabel cannot be animated, so QEvent::ToolTip is taken over — and the wake-up delay,
  // the content and the placement all have to survive that swap.
  void tooltipFadesInAndOut() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    win.raise();
    win.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QToolButton* btn = tipCarrier(win);
    QVERIFY2(btn, "no shown toolbar button with a tooltip");
    stencil::gui::AppTooltip* tip = stencil::gui::appTooltip();
    QVERIFY2(tip, "the app tooltip was never installed");
    QVERIFY(!tip->isVisible());

    // Park the pointer ON the control: the panel's anti-stranding heartbeat retires a
    // tooltip whose control the pointer has left, and here it must not.
    QCursor::setPos(btn->mapToGlobal(btn->rect().center()));
    sendToolTipTo(btn);
    QVERIFY2(tip->isVisible(), "the tooltip did not take over QEvent::ToolTip");
    QCOMPARE(tip->owner(), static_cast<QWidget*>(btn));
    QVERIFY2(tip->windowOpacity() < 0.99, "it snapped in at full opacity");
    QTRY_COMPARE_WITH_TIMEOUT(tip->windowOpacity(), 1.0, 1500);   // …and rose to solid
    QTest::qWait(300);
    QVERIFY2(tip->isVisible(), "it retired while the pointer was still on its control");
    // Placed off the cursor and kept on screen.
    const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
    QVERIFY2(screen.intersects(tip->geometry()), "the tooltip was placed off screen");
    QLabel* body = tip->findChild<QLabel*>();
    QVERIFY(body && !body->text().isEmpty());

    // Leaving the control fades it OUT — still visible while it goes, gone at the end.
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(btn, &leave);
    QVERIFY2(tip->fadingOut(), "it vanished instead of fading");
    QTRY_VERIFY_WITH_TIMEOUT(!tip->isVisible(), 1500);

    // Reduced motion: shown solid at once, hidden at once — the same end states.
    qputenv("STENCIL_NO_ANIM", "1");
    sendToolTipTo(btn);
    QVERIFY(tip->isVisible());
    QCOMPARE(tip->windowOpacity(), 1.0);
    QApplication::sendEvent(btn, &leave);
    QVERIFY2(!tip->isVisible(), "reduced motion still played the fade-out");
    qunsetenv("STENCIL_NO_ANIM");
    beat();
  }

  // A fast pointer sweep must never STRAND a tooltip: whatever happened to the control it
  // described — hidden, disabled, or simply left behind without a Leave we saw — the panel
  // goes on its own.
  void fastPointerSweepStrandsNoTooltip() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    win.raise();
    win.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QToolButton* btn = tipCarrier(win);
    QVERIFY(btn);
    stencil::gui::AppTooltip* tip = stencil::gui::appTooltip();
    QVERIFY(tip);

    // Shown for a control the pointer is nowhere near — the sweep already moved on, and no
    // Leave was ever delivered for it.
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 5, win.height() - 5)));
    sendToolTipTo(btn);
    QVERIFY(tip->isVisible());
    // The cursor is nowhere near it (offscreen QPA parks it at the origin, and no Leave is
    // synthesised) — the heartbeat is the only thing that can clear this.
    QTRY_VERIFY_WITH_TIMEOUT(!tip->isVisible(), 3000);
    QVERIFY(!tip->owner());
    beat();
  }

  // A shown, enabled toolbar button whose rendered tooltip does (`want`) or does not carry
  // keycaps — the shake fires on the content, not on the control.
  QToolButton* capCarrier(MainWindow& win, bool want) {
    for (QToolBar* bar : win.findChildren<QToolBar*>())
      for (QToolButton* b : bar->findChildren<QToolButton*>()) {
        if (!b->isVisible() || !b->isEnabled() || b->toolTip().isEmpty()) continue;
        const QString rich = b->toolTip().trimmed().startsWith('<')
                                 ? b->toolTip()
                                 : stencil::gui::enrichedToolTip(b->toolTip());
        if (stencil::gui::hasKeycaps(rich) == want) return b;
      }
    return nullptr;
  }

  // The KEYCAPS shake as their tooltip appears — a brief, non-repeating flick that says
  // "and here is the shortcut" while you read the tip (browser/extension:
  // .tip-key.key-shake). No key press is involved: showing the tooltip is the trigger, only
  // a tooltip that actually draws caps reacts, and the PANEL never moves — the caps painted
  // inside it do, and they settle back on exactly the picture they started from.
  void showingATooltipShakesItsKeycaps() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    win.raise();
    win.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QToolButton* btn = capCarrier(win, true);
    QVERIFY2(btn, "no shown toolbar button whose tooltip carries keycaps");
    stencil::gui::AppTooltip* tip = stencil::gui::appTooltip();
    QVERIFY(tip);

    // Park the pointer ON the control so the anti-stranding heartbeat leaves it up. The
    // offscreen screen is smaller than this window, so a control out past its edge can
    // never take the cursor at all: walk the WINDOW towards wherever the cursor actually
    // landed until the two agree. It matters more than it used to — the keycap shake now
    // waits out the tip's own arrival, well past the heartbeat's first look.
    const auto onControl = [&] {
      return btn->rect().contains(btn->mapFromGlobal(QCursor::pos()));
    };
    for (int i = 0; i < 4 && !onControl(); ++i) {
      QCursor::setPos(btn->mapToGlobal(btn->rect().center()));
      if (onControl()) break;
      win.move(win.pos() + (QCursor::pos() - btn->mapToGlobal(btn->rect().center())));
      QTest::qWait(40);
    }
    sendToolTipTo(btn);
    QVERIFY2(tip->isVisible(), "the tooltip did not appear");
    // The caps HOLD STILL while the tip is still assembling out of its own motes — a
    // nudge nobody can see is the one thing this must never be — and are queued for the
    // moment it lands. Without dust (an unmeasurable host) there is nothing to wait for
    // and the shake is immediate instead.
    QVERIFY2(tip->shaking() || tip->shakePending(),
             "the keycaps were neither shaken nor queued to shake");
    if (tip->shakePending())
      QVERIFY2(!tip->shaking(), "the caps moved while the tip was still forming");
    QTRY_VERIFY_WITH_TIMEOUT(tip->shaking(), stencil::gui::AppTooltip::kDustInMs + 2000);
    QVERIFY2(tip->keycapsShown() > 0, "the shake found no caps to move");
    QLabel* body = tip->findChild<QLabel*>();
    QVERIFY(body);
    const QPoint home = tip->pos();
    // The CAPS really move — pixels, not a counter — while the panel around them holds
    // still, and it is one pass: they settle back on the picture they started from.
    // (No "starts at 0" check here any more: the wait above is what lets the queued
    // shake begin, so by this line it is already a frame or two in.)
    QImage midShake;
    for (int i = 0; i < 40 && midShake.isNull(); ++i) {
      QTest::qWait(8);
      QCOMPARE(tip->pos(), home);                     // the panel itself must not swing
      if (tip->shakeOffset() != 0) midShake = body->grab().toImage();
    }
    QVERIFY2(!midShake.isNull(), "the caps never left their resting slot");
    QTRY_VERIFY_WITH_TIMEOUT(!tip->shaking(), stencil::gui::AppTooltip::kShakeMs + 2000);
    QCOMPARE(tip->pos(), home);
    QCOMPARE(tip->shakeOffset(), 0);
    const QImage settled = body->grab().toImage();
    QVERIFY2(midShake != settled, "the shake redrew the tooltip exactly as it sits at rest");
    QVERIFY2(tip->isVisible(), "the shake must not retire the tooltip");
    // A re-sent ToolTip for the SAME control is not a new appearance (Qt keeps re-arming
    // its wake-up while the pointer wanders inside one control): no second shake.
    sendToolTipTo(btn);
    QVERIFY2(!tip->shaking() && !tip->shakePending(),
             "the same tooltip shook again under a wandering pointer");

    // ANY key retires it — Escape included. The shake announces the shortcut; it is not a
    // way to pin the tooltip open.
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(win.canvas_, &esc);
    QTRY_VERIFY_WITH_TIMEOUT(!tip->isVisible(), 1500);

    // A tooltip with NO shortcut draws no caps, so it has nothing to announce.
    QToolButton* plain = capCarrier(win, false);
    if (!plain) {  // every live control happens to carry a chord — give one a bare tooltip
      for (QToolButton* b : win.findChildren<QToolButton*>())
        if (b->isVisible() && b->isEnabled() && b != btn) { plain = b; break; }
      QVERIFY(plain);
      plain->setToolTip(QStringLiteral("Bare hover text, nothing bound"));
    }
    QCursor::setPos(plain->mapToGlobal(plain->rect().center()));
    sendToolTipTo(plain);
    QVERIFY(tip->isVisible());
    QVERIFY2(!stencil::gui::hasKeycaps(body->text()), "the control drew keycaps after all");
    QVERIFY2(!tip->shaking() && !tip->shakePending(), "a tooltip with no keycaps still shook");
    QCOMPARE(tip->shakeOffset(), 0);
    QCOMPARE(tip->keycapsShown(), 0);
    const QPoint bareHome = tip->pos();
    const QImage bare = body->grab().toImage();
    QTest::qWait(120);
    QCOMPARE(tip->pos(), bareHome);                    // …and nothing moved while we watched
    QCOMPARE(body->grab().toImage(), bare);

    // A fast sweep re-points the tooltip mid-shake, over and over: the shakes must not
    // stack, and every cap must end up back on its own slot.
    for (int i = 0; i < 8; ++i) {
      sendToolTipTo(i % 2 ? plain : btn);
      QTest::qWait(20);
    }
    QCursor::setPos(btn->mapToGlobal(btn->rect().center()));
    sendToolTipTo(btn);
    QTRY_VERIFY_WITH_TIMEOUT(!tip->shaking(), stencil::gui::AppTooltip::kShakeMs + 2000);
    QCOMPARE(tip->shakeOffset(), 0);
    // Settled back EXACTLY: the same picture as the first appearance left behind.
    QCOMPARE(body->grab().toImage(), settled);
    // …and nothing is stranded: the pointer is on neither control any more.
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 5, win.height() - 5)));
    QTRY_VERIFY_WITH_TIMEOUT(!tip->isVisible(), 3000);
    QCOMPARE(tip->shakeOffset(), 0);

    // Reduced motion: shown, correct, and not a pixel of shake — panel or caps.
    qputenv("STENCIL_NO_ANIM", "1");
    QCursor::setPos(btn->mapToGlobal(btn->rect().center()));
    sendToolTipTo(btn);
    QVERIFY(tip->isVisible());
    QVERIFY(stencil::gui::hasKeycaps(body->text()));
    const QPoint restingPos = tip->pos();
    QVERIFY2(!tip->shaking(), "reduced motion still shook the keycaps");
    QCOMPARE(tip->shakeOffset(), 0);
    QTest::qWait(120);
    QCOMPARE(tip->pos(), restingPos);
    QCOMPARE(tip->shakeOffset(), 0);
    QCOMPARE(body->grab().toImage(), settled);         // the caps drawn exactly where they live
    qunsetenv("STENCIL_NO_ANIM");
    beat();
  }

  // A dead icon says so under the pointer — the browser's `cursor: not-allowed` on a
  // disabled control. Qt applies no cursor to a DISABLED widget (it gets no mouse events),
  // so the row it sits in carries one for it; without that the toolbar answered a dead
  // Save/live-sync icon with the plain arrow, as if it were clickable.
  void disabledToolIconShowsTheBlockedCursor() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1400, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTest::qWait(30);   // let the toolbar's own deferred layout pass settle before measuring it
    // A fresh window has no .stencil link, so live-sync is dead while Projects beside it
    // is live: one row, both states.
    QVERIFY(!win.actStencilLiveSync_->isEnabled());
    QToolButton* dead = nullptr;
    QToolButton* live = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>()) {
      if (!b->isVisible() || !b->parentWidget()) continue;
      if (!b->parentWidget()->property("toolRow").toBool()) continue;
      if (b->defaultAction() == win.actStencilLiveSync_) dead = b;
      if (b->defaultAction() == win.actProjects_ && !live) live = b;
    }
    QVERIFY2(dead, "no toolbar button for live sync");
    QWidget* row = dead->parentWidget();
    QVERIFY(row->hasMouseTracking());

    // The move Qt actually delivers: a disabled widget gets no mouse events, so the one
    // over a dead icon reaches the application filters with the BUTTON as its object and is
    // then thrown away. That is the path the cursor has to hang off — and what it sets is
    // an OVERRIDE cursor, the only one Qt applies without waiting to re-enter a widget.
    auto blocked = [] {
      const QCursor* c = QApplication::overrideCursor();
      return c && c->shape() == Qt::ForbiddenCursor;
    };
    auto moveOver = [&](QWidget* target) {
      const QPoint local(target->width() / 2, target->height() / 2);
      QMouseEvent me(QEvent::MouseMove, QPointF(local), target->mapToGlobal(local),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
      qApp->sendEvent(target, &me);
    };
    QVERIFY(!blocked());
    moveOver(dead);
    QVERIFY2(blocked(), "a dead icon left the pointer unchanged");
    // Over a LIVE icon in the same row, and it goes back at once.
    QVERIFY2(live && live->parentWidget() == row, "no live icon beside it to move onto");
    moveOver(live);
    QVERIFY2(!blocked(), "the blocked cursor stuck over the live icon next door");
    moveOver(dead);
    QVERIFY(blocked());
    // The same again through the PLATFORM path (window → childAt → notify), which is what a
    // real pointer does — the synthetic sends above only prove the filter's arithmetic.
    moveOver(live);
    QTest::mouseMove(&win, win.mapFromGlobal(dead->mapToGlobal(dead->rect().center())));
    QTest::qWait(30);
    QVERIFY2(blocked(), "a real move over the dead icon left the plain cursor");
    // …and leaving the window clears it even with no move to land anywhere else.
    QEvent leave(QEvent::Leave);
    qApp->sendEvent(row, &leave);
    QVERIFY2(!blocked(), "leaving must put the cursor back");
    // A LIVE icon still carries its own hand cursor — nothing here touches that.
    if (live) QCOMPARE(live->cursor().shape(), Qt::PointingHandCursor);
  }

  // COMPARE + hover tooltip: hovering a point (or a line) still labels its coordinates
  // while comparing — but ONLY over the half showing the EDITED image, the only place
  // the layout is drawn. Behind the "before" half, or in "original" (no layout at all),
  // there is nothing on screen to point at, so no tooltip.
  void compareTooltipOnlyOverTheEditedHalf() {
    MainWindow win(nullptr, false);
    win.resize(1200, 850);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QTest::qWait(150);
    win.settings_.tooltipEnabled = true;    // independent of the machine's saved settings
    win.settings_.tooltipShowScreen = true;

    // One horizontal line across the 240x160 picture: a point in each half of a
    // centred vertical split, the segment between them crossing the divider.
    stencil::core::Line line;
    line.points = {{60, 100}, {180, 100}};
    canvas->setLines({line});
    const double s = canvas->scale();
    QLabel* body = win.tooltip_->findChild<QLabel*>();
    QVERIFY(body);

    // Hover an image-space spot; report what the tooltip says (empty = hidden). The
    // reveal now waits out the same delay as the toolbar tooltip (mainWindow.cpp
    // scheduleHoverShow, 200 ms) before it actually shows, so this outwaits it.
    const auto hoverText = [&](double ix, double iy) {
      const QPoint p(qRound(ix * s), qRound(iy * s));
      QMouseEvent move(QEvent::MouseMove, QPointF(p), canvas->mapToGlobal(p), Qt::NoButton,
                       Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(canvas, &move);
      settle([&win] { return win.tooltip_->isVisible(); }, 240);
      return win.tooltip_->isVisible() ? body->text() : QString();
    };
    const auto compareAt = [&](const char* mode, double split) {
      win.setCompareModeUi(QString::fromLatin1(mode));
      canvas->setCompareSplit(split);
      QTest::qWait(60);
    };

    // Baseline (compare off): BOTH points and the segment between them are labelled —
    // without this the "hidden" assertions below would pass on a tooltip that never shows.
    const QString leftOff = hoverText(60, 100), rightOff = hoverText(180, 100);
    QVERIFY2(leftOff.contains("Pixel") && leftOff.contains("60, 100"), qPrintable(leftOff));
    QVERIFY2(rightOff.contains("Pixel") && rightOff.contains("180, 100"), qPrintable(rightOff));
    const QString lineOff = hoverText(150, 100);   // mid-segment, off both points
    QVERIFY2(!lineOff.contains("Pixel") && lineOff.contains("60, 100 px") &&
                 lineOff.contains("180, 100 px"),
             qPrintable("line hover should list its endpoints: " + lineOff));

    // Vertical split at the middle (divider = image x 120): the right point and the
    // right stretch of the line keep their tooltip, the left ones lose it.
    compareAt("vertical", 0.5);
    QVERIFY(canvas->compareReadOnly());
    QVERIFY2(hoverText(180, 100).contains("180, 100"), "the visible point lost its tooltip");
    QVERIFY2(hoverText(60, 100).isEmpty(), "a point behind the original half was labelled");
    QVERIFY2(hoverText(150, 100).contains("px"), "the visible line lost its tooltip");
    QVERIFY2(hoverText(90, 100).isEmpty(), "a line behind the original half was labelled");

    // Slide the divider past the right point (x 216) — the same point is now hidden…
    canvas->setCompareSplit(0.9);
    QVERIFY2(hoverText(180, 100).isEmpty(), "the divider move did not hide the point");
    // …and back before the left one (x 24), which reveals it.
    canvas->setCompareSplit(0.1);
    QVERIFY2(hoverText(60, 100).contains("60, 100"), "the divider move did not reveal the point");

    // Horizontal split: the original is the TOP, so y decides. Divider y 80 leaves the
    // line (y 100) below it; y 144 puts it above.
    compareAt("horizontal", 0.5);
    QVERIFY2(hoverText(180, 100).contains("180, 100"), "the point below the divider lost its tooltip");
    canvas->setCompareSplit(0.9);
    QVERIFY2(hoverText(180, 100).isEmpty(), "a point above the divider was labelled");
    QVERIFY2(hoverText(150, 100).isEmpty(), "a line above the divider was labelled");

    // "original" shows no layout anywhere — nothing is ever labelled.
    compareAt("original", 0.5);
    QVERIFY2(hoverText(180, 100).isEmpty() && hoverText(150, 100).isEmpty(),
             "the original-only view still labelled the layout");
    // …and the Alt+Shift+O peek is the same view, so it gates the same way.
    win.setCompareModeUi(QStringLiteral("none"));
    canvas->setCompareHoldOriginal(true);
    QVERIFY2(hoverText(180, 100).isEmpty(), "the held peek still labelled the layout");
    canvas->setCompareHoldOriginal(false);

    // Back to normal: the tooltip returns everywhere.
    QVERIFY2(hoverText(60, 100).contains("60, 100"), "the tooltip did not come back");
    beat();
  }

  // The tooltip must not show — or stay stuck showing — while the mouse is down doing
  // something else (Alt-dragging a point, drag-creating a rect/zoom box, panning);
  // hoverLeft() retracts one already up when the drag starts.
  void noTooltipWhileTheMouseIsDownDrawingOrDragging() {
    MainWindow win(nullptr, false);
    win.resize(1200, 850);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QTest::qWait(150);
    win.settings_.tooltipEnabled = true;    // independent of the machine's saved settings
    win.settings_.tooltipShowScreen = true;

    stencil::core::Line line;
    line.points = {{40, 40}, {160, 120}};
    canvas->setLines({line});
    const double s = canvas->scale();

    auto sendMouse = [&](QEvent::Type t, const QPointF& pos, Qt::MouseButton btn,
                         Qt::MouseButtons btns, Qt::KeyboardModifiers mods) {
      QMouseEvent ev(t, pos, canvas->mapToGlobal(pos.toPoint()), btn, btns, mods);
      QCoreApplication::sendEvent(canvas, &ev);
    };
    const auto moveTo = [&](double ix, double iy) {
      sendMouse(QEvent::MouseMove, QPointF(ix * s, iy * s), Qt::NoButton, Qt::NoButton,
               Qt::NoModifier);
    };

    // Baseline: hovering the first point (no modifiers, nothing else going on) shows the
    // tooltip after the reveal delay — proves the setup actually can show one at all.
    moveTo(40, 40);
    QTRY_VERIFY_WITH_TIMEOUT(win.tooltip_->isVisible(), 1000);

    // Alt-press ON that same point starts a point drag without moving first — the
    // stranding case: the very next move must retract the tooltip that was already up,
    // not just skip showing a new one. (The drag itself relocates the point to wherever
    // it's released — setLines() below puts it back for the next section.)
    sendMouse(QEvent::MouseButtonPress, QPointF(40 * s, 40 * s), Qt::LeftButton,
             Qt::LeftButton, Qt::AltModifier);
    sendMouse(QEvent::MouseMove, QPointF(70 * s, 60 * s), Qt::NoButton, Qt::LeftButton,
             Qt::AltModifier);
    QTRY_VERIFY_WITH_TIMEOUT(!win.tooltip_->isVisible(), 1000);
    // Dragging further — even back over the SECOND point — never re-shows it either.
    sendMouse(QEvent::MouseMove, QPointF(160 * s, 120 * s), Qt::NoButton, Qt::LeftButton,
             Qt::AltModifier);
    QTest::qWait(260);   // outwait the reveal delay — it must still be hidden
    QVERIFY2(!win.tooltip_->isVisible(), "a point drag popped a tooltip mid-drag");
    sendMouse(QEvent::MouseButtonRelease, QPointF(160 * s, 120 * s), Qt::LeftButton,
             Qt::NoButton, Qt::AltModifier);
    beat();

    // Rect-draw mode, dragging out a box over the first point: no tooltip either.
    canvas->setLines({line});   // undo the point drag above — point 1 back at (40, 40)
    canvas->setDrawMode(CanvasWidget::DrawMode::Rect);
    moveTo(40, 40);
    QTRY_VERIFY_WITH_TIMEOUT(win.tooltip_->isVisible(), 1000);
    sendMouse(QEvent::MouseButtonPress, QPointF(40 * s, 40 * s), Qt::LeftButton,
             Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, QPointF(90 * s, 90 * s), Qt::NoButton, Qt::LeftButton,
             Qt::NoModifier);
    QTRY_VERIFY_WITH_TIMEOUT(!win.tooltip_->isVisible(), 1000);
    QTest::qWait(260);
    QVERIFY2(!win.tooltip_->isVisible(), "a rect-draw drag popped a tooltip mid-drag");
    sendMouse(QEvent::MouseButtonRelease, QPointF(90 * s, 90 * s), Qt::LeftButton,
             Qt::NoButton, Qt::NoModifier);
    canvas->setDrawMode(CanvasWidget::DrawMode::Line);
    beat();

    // Shift-drag (zoom rect) over a point: still nothing.
    canvas->setLines({line});   // drop the rect-draw commit above, back to the plain line
    moveTo(40, 40);
    QTRY_VERIFY_WITH_TIMEOUT(win.tooltip_->isVisible(), 1000);
    sendMouse(QEvent::MouseButtonPress, QPointF(40 * s, 40 * s), Qt::LeftButton,
             Qt::LeftButton, Qt::ShiftModifier);
    // Kept under the 4-image-px commit threshold (mouseReleaseEvent) so releasing does
    // NOT actually zoom — this section only cares about the tooltip during the drag.
    sendMouse(QEvent::MouseMove, QPointF(42 * s, 41 * s), Qt::NoButton, Qt::LeftButton,
             Qt::ShiftModifier);
    QTRY_VERIFY_WITH_TIMEOUT(!win.tooltip_->isVisible(), 1000);
    QTest::qWait(260);
    QVERIFY2(!win.tooltip_->isVisible(), "a zoom-rect drag popped a tooltip mid-drag");
    sendMouse(QEvent::MouseButtonRelease, QPointF(42 * s, 41 * s), Qt::LeftButton,
             Qt::NoButton, Qt::ShiftModifier);
    beat();

    // Back to a plain hover afterwards: the tooltip is not stuck off either.
    moveTo(40, 40);
    QTRY_VERIFY_WITH_TIMEOUT(win.tooltip_->isVisible(), 1000);
    beat();
  }

  // The idle canvas card says "＋ Blank image" on its face; a hover tooltip repeating that
  // is noise, so neither surface carries one any more.
  void blankImageCardHasNoTooltip() {
    MainWindow win;
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.canvas_->clearImage();
    win.refreshActions();
    // Painting the card is what used to install the tooltip.
    QVERIFY(waitForIdleCard(win));
    QVERIFY2(win.canvas_->toolTip().isEmpty(),
             qPrintable("the empty canvas still has a tooltip: " + win.canvas_->toolTip()));
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "mainWindow.tooltips.gui.moc"
