// MainWindow GUI e2e — Theme and accent: the swap wipe, the accent popover and the logo's hover fx.
// Shared ground (helpers, the loaded window, the motion pins) is in mainWindow.gui.hpp.
#include "mainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The logo's hover fx (browser animations.css logoPulse / logoRaysSpin parity):
  // hovering the mark shows a window-level overlay that owns the pixels (the button
  // icon blanks so the pulsing copy never doubles a static one), the loop genuinely
  // ADVANCES, and leaving stops every animation (no idle timers) and hands the icon
  // back. The overlay is larger than the button — glow + rays paint in a margin
  // around it, never by resizing the toolbar row.
  void logoHoverFxPulsesWhileHoveredOnly() {
    const auto motion = withMotion();   // the loop honours motionReduced(), which is on here
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QToolButton* logo = win.logoBtn_;
    QVERIFY(logo);
    QWidget* fx = win.findChild<QWidget*>("logoHoverFx");
    QVERIFY2(fx, "logo hover fx overlay not installed");
    QTest::qWait(20);   // the overlay shows its resting mark on a deferred tick after show
    auto iconBlank = [logo] {
      const QImage im = logo->icon().pixmap(logo->iconSize()).toImage();
      for (int y = 0; y < im.height(); ++y)
        for (int x = 0; x < im.width(); ++x)
          if (qAlpha(im.pixel(x, y)) != 0) return false;
      return true;
    };
    // The overlay now paints the mark at ALL times (a QToolButton draws its icon half
    // size on Retina), so at rest it is VISIBLE but NOT animating, and the button icon is
    // blanked — the overlay owns the pixels. Only the pulse/rays are hover-gated.
    QVERIFY2(fx->isVisible(), "the fx paints the resting mark");
    QVERIFY(!fx->property("fxActive").toBool());
    QVERIFY2(iconBlank(), "the overlay owns the mark at rest (button icon blanked)");

    const QPointF c(logo->rect().center());
    QEnterEvent enter(c, c, logo->mapToGlobal(logo->rect().center()));
    QApplication::sendEvent(logo, &enter);
    QVERIFY2(fx->isVisible(), "hover-enter must show the fx overlay");
    QVERIFY(fx->property("fxActive").toBool());
    QVERIFY2(iconBlank(), "the overlay owns the mark while animating (icon blanked)");
    QVERIFY2(fx->width() > logo->width() && fx->height() > logo->height(),
             "fx overlay must give the glow/rays room AROUND the button");
    const qreal b0 = fx->property("pulseBeat").toReal();
    const qreal a0 = fx->property("raysAngle").toReal();
    QTRY_VERIFY2(fx->property("pulseBeat").toReal() != b0, "pulse beat did not advance");
    QTRY_VERIFY2(fx->property("raysAngle").toReal() != a0, "ray rotation did not advance");
    QVERIFY(!fx->grab().isNull());   // painting the fx offscreen must not crash

    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(logo, &leave);
    QVERIFY2(fx->isVisible(), "leave keeps the resting mark shown (only the pulse stops)");
    QVERIFY(!fx->property("fxActive").toBool());
    for (QVariantAnimation* a : fx->findChildren<QVariantAnimation*>())
      QVERIFY2(a->state() != QAbstractAnimation::Running,
               "an fx animation kept running after hover-leave");
    QVERIFY2(iconBlank(), "the overlay keeps the mark after leave (button icon stays blanked)");
  }

  // Browser parity: the accent popover the logo opens is part of the logo's hover —
  // the shine holds while the cursor crosses the anchor gap onto it and while it rests
  // there, starts from a hover that begins on the popover, and stops only once the
  // cursor has left both.
  void logoHoverFxHoldsOverAccentPopover() {
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QToolButton* logo = win.logoBtn_;
    QWidget* fx = win.findChild<QWidget*>("logoHoverFx");
    QVERIFY(logo && fx);
    if (QWidget* fw = QApplication::focusWidget()) fw->clearFocus();   // typingFocus gate off
    const QPoint c = logo->rect().center();
    const QPoint logoGlobal = logo->mapToGlobal(c);
    const QPoint awayGlobal = win.mapToGlobal(QPoint(win.width() / 2, win.height() - 40));
    const auto enter = [](QWidget* w, const QPoint& global) {
      const QPointF local(w->mapFromGlobal(global));
      QEnterEvent e(local, local, QPointF(global));
      QApplication::sendEvent(w, &e);
    };
    const auto leave = [](QWidget* w) {
      QEvent e(QEvent::Leave);
      QApplication::sendEvent(w, &e);
    };
    QCursor::setPos(logoGlobal);
    enter(logo, logoGlobal);
    QVERIFY(fx->property("fxActive").toBool());

    bool opened = false, heldOnCrossing = false, heldOnBox = false;
    bool stoppedOffBoth = false, startedOnBox = false;
    QTimer::singleShot(600, &win, [&] {   // past the popover's open flight
      QWidget* box = win.pop_.overlay.data();
      opened = box && box->isVisible() && win.pop_.active &&
               win.pop_.active->objectName() == QLatin1String("accentPopover");
      if (!opened) { if (win.pop_.active) win.pop_.active->reject(); return; }
      // The cursor crosses the anchor gap onto the box: the logo's Leave alone must not
      // stop the loop (the browser's hover bridge), and resting on the box holds it.
      const QPoint boxGlobal = box->mapToGlobal(box->rect().center());
      QCursor::setPos(boxGlobal);
      leave(logo);
      QTest::qWait(60);   // inside the grace
      heldOnCrossing = fx->property("fxActive").toBool();
      enter(box, boxGlobal);
      QTest::qWait(300);  // well past the grace
      heldOnBox = fx->property("fxActive").toBool() && fx->isVisible();
      // Off both (onto the canvas): the loop stops once the grace runs out.
      QCursor::setPos(awayGlobal);
      leave(box);
      QTest::qWait(300);
      stoppedOffBoth = !fx->property("fxActive").toBool();
      // A hover that BEGINS on the popover lights the logo too.
      QCursor::setPos(boxGlobal);
      enter(box, boxGlobal);
      startedOnBox = fx->property("fxActive").toBool();
      QCursor::setPos(awayGlobal);
      leave(box);
      win.pop_.active->reject();
    });
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, c, logoGlobal);
    QApplication::sendEvent(logo, &ctx);   // blocks in the popover's loop until the timer acts
    QTRY_VERIFY(!win.pop_.active);
    QVERIFY2(opened, "right-click did not open the accent popover");
    QVERIFY2(heldOnCrossing, "leaving the logo for the open popover stopped the shine");
    QVERIFY2(heldOnBox, "hovering the open popover did not hold the shine");
    QVERIFY2(stoppedOffBoth, "the shine kept running with the cursor off logo and popover");
    QVERIFY2(startedOnBox, "hovering the popover did not start the shine");
    // The popover has gone and the cursor is on neither: the loop is down and idle.
    QTRY_VERIFY(!fx->property("fxActive").toBool());
    for (QVariantAnimation* a : fx->findChildren<QVariantAnimation*>())
      QVERIFY(a->state() != QAbstractAnimation::Running);
  }

  // The ✓ in an OPEN accent popover follows the accent wherever it moved from — the
  // logo's click-cycle (applySettings), not only the popover's own row picks (user
  // report). The browser's logo menu re-marks off stencil:accent-changed the same way.
  void accentPopoverTickFollowsAnOutsideAccentChange() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 30);
    QToolButton* logo = win.logoBtn_;
    QVERIFY(logo);
    const QString original = win.settings_.accentColor;
    const auto& presets = stencil::gui::accentPresets();
    QVERIFY(presets.size() >= 2);
    QString other;
    for (const auto& a : presets) if (a.key != original) { other = a.key; break; }
    bool opened = false, markedBefore = false, markedAfter = false, oneMark = true;
    QTimer::singleShot(120, &win, [&] {
      QDialog* pop = win.pop_.active.data();
      opened = pop && pop->objectName() == QLatin1String("accentPopover") && pop->isVisible();
      if (pop) {
        auto* was = pop->findChild<QPushButton*>(QStringLiteral("accentRow-") + original);
        markedBefore = was && was->property("currentAccent").toBool();
        // The accent moves OUTSIDE the popover — the logo click-cycle's own path.
        auto next = win.settings_;
        next.accentColor = other;
        win.applySettings(next, true);
        settleLayout(&win, 30);
        int marks = 0;
        for (const auto& a : presets) {
          auto* r = pop->findChild<QPushButton*>(QStringLiteral("accentRow-") + a.key);
          if (r && r->property("currentAccent").toBool()) ++marks;
        }
        auto* now = pop->findChild<QPushButton*>(QStringLiteral("accentRow-") + other);
        markedAfter = now && now->property("currentAccent").toBool();
        oneMark = marks == 1;
        pop->reject();
      }
    });
    const QPoint c = logo->rect().center();
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
    QApplication::sendEvent(logo, &ctx);   // blocks in the popover's exec until the timer acts
    QVERIFY2(opened, "right-click did not open the accent popover");
    QVERIFY2(markedBefore, "the current accent's row starts marked");
    QVERIFY2(markedAfter, "an accent applied from outside the popover must move its tick");
    QVERIFY2(oneMark, "exactly one row carries the tick");
    auto restore = win.settings_;
    restore.accentColor = original;
    win.applySettings(restore, true);
  }

  // Alt held with the cursor resting ON the open popover must not glide onto an icon the
  // box is COVERING: the glide's cursor-rect fallback is pure geometry, so it read that as
  // hovering the buttons under the box and opened their window beneath it.
  void altGlideIgnoresIconsUnderTheOpenPopover() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 30);
    QToolButton* logo = win.logoBtn_;
    QVERIFY(logo);
    if (QWidget* fw = QApplication::focusWidget()) fw->clearFocus();   // typingFocus gate off
    const QPoint cursorWas = QCursor::pos();
    bool opened = false, covered = false, stayed = false, armed = true;
    QTimer::singleShot(700, &win, [&] {   // past the popover's open flight
      QWidget* box = win.pop_.overlay.data();
      opened = box && box->isVisible() && win.pop_.active &&
               win.pop_.active->objectName() == QLatin1String("accentPopover");
      if (opened) {
        const QRect boxGlobal(box->mapToGlobal(QPoint(0, 0)), box->size());
        QPoint on;   // a point on the box AND on a popover icon it covers
        for (auto it = win.pop_.buttons.cbegin(); it != win.pop_.buttons.cend(); ++it) {
          auto* b = static_cast<QToolButton*>(it.key());
          if (b == logo || !b->isVisible() || !it.value()->isEnabled()) continue;
          const QRect hit = QRect(b->mapToGlobal(QPoint(0, 0)), b->size()).intersected(boxGlobal);
          if (hit.isEmpty()) continue;
          covered = true;
          on = hit.center();
          break;
        }
        if (covered) {
          win.altHeldForTest_ = true;   // the glide poll's stand-in for a held Alt
          QCursor::setPos(on);
          QTest::qWait(300);            // several glide ticks (80ms)
          stayed = win.pop_.active &&
                   win.pop_.active->objectName() == QLatin1String("accentPopover");
          armed = !win.pop_.peekNextAction.isNull();
          win.altHeldForTest_ = false;
          // Never leave a peek queued: it would open (and block) after this unwinds.
          win.pop_.peekNextAction.clear();
          win.pop_.peekNextButton.clear();
        }
      }
      if (win.pop_.active) win.pop_.active->reject();
    });
    const QPoint c = logo->rect().center();
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
    QApplication::sendEvent(logo, &ctx);   // blocks in the popover's loop until the timer acts
    QCursor::setPos(cursorWas);
    QVERIFY2(opened, "right-click did not open the accent popover");
    QVERIFY2(covered, "the accent box covers no popover icon — nothing to guard here");
    QVERIFY2(stayed, "resting on the box glided onto an icon underneath it");
    QVERIFY2(!armed, "resting on the box armed a covered icon's peek");
  }

  // A row under the pointer eases a couple of pixels RIGHT — the browser's
  // `.accent-dd-opt:hover { transform: translateX(2px) }` — and SURVIVES the preview's own
  // flood, which re-polishes every stylesheet and re-lays the popover out.
  void accentRowSlidesUnderThePointer() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 30);
    QToolButton* logo = win.logoBtn_;
    QVERIFY(logo);
    const auto& presets = stencil::gui::accentPresets();
    QVERIFY(presets.size() >= 2);
    const QString original = win.settings_.accentColor;
    QString other;
    for (const auto& a : presets) if (a.key != original) { other = a.key; break; }
    bool opened = false, foundRow = false;
    int restX = 0, hoverX = 0, floodedX = 0, backX = 0;
    QTimer::singleShot(120, &win, [&] {
      QDialog* pop = win.pop_.active.data();
      opened = pop && pop->objectName() == QLatin1String("accentPopover") && pop->isVisible();
      if (pop) {
        auto* row = pop->findChild<QPushButton*>(QStringLiteral("accentRow-") + presets.front().key);
        foundRow = row != nullptr;
        if (row) {
          restX = row->x();
          const QPointF p(4, 4);
          QEnterEvent enter(p, p, row->mapToGlobal(QPoint(4, 4)));
          QApplication::sendEvent(row, &enter);
          QTest::qWait(200);            // the 120ms slide, with room to spare
          hoverX = row->x();
          // The accent flood the preview plays, straight through applySettings.
          auto next = win.settings_;
          next.accentColor = other;
          win.applySettings(next, true);
          QTest::qWait(150);
          floodedX = row->x();
          QEvent leave(QEvent::Leave);
          QApplication::sendEvent(row, &leave);
          QTest::qWait(200);
          backX = row->x();
        }
        pop->reject();
      }
    });
    const QPoint c = logo->rect().center();
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
    QApplication::sendEvent(logo, &ctx);   // blocks in the popover's exec until the timer acts
    QVERIFY2(opened, "right-click did not open the accent popover");
    QVERIFY2(foundRow, "the popover must carry the preset rows");
    QCOMPARE(hoverX, restX + 2);
    QCOMPARE(floodedX, restX + 2);
    QCOMPARE(backX, restX);
    auto restore = win.settings_;
    restore.accentColor = original;
    win.applySettings(restore, true);
  }

  // A popover must close on a click OUTSIDE it — canvas, toolbar, anywhere — from BOTH
  // routes that open the accent picker (right-click sticky, hold-Alt peek), and whether
  // Qt delivers that press or not: exec() is application-modal, so the platform DROPS
  // presses aimed at the blocked main window and the app-wide filter never sees them.
  // That is why the theme-colour popup ignored outside clicks on a real desktop while
  // every test synthesising a press straight into a widget passed.
  void accentPopoverClosesOnOutsideClick() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QToolButton* logo = win.logoBtn_;
    QVERIFY(logo && win.canvas_);
    // With a picture loaded, so a canvas press is an ordinary editing press and not the
    // empty canvas's "create a blank image" invitation (another modal dialog).
    QImage pic(320, 240, QImage::Format_RGB32);
    pic.fill(Qt::darkCyan);
    win.canvas_->loadFromImage(pic);
    QTRY_VERIFY(win.canvas_->hasImage());
    if (QWidget* fw = QApplication::focusWidget()) fw->clearFocus();   // typingFocus gate off
    const QPoint c = logo->rect().center();
    // A toolbar icon that is not the logo — and NOT one the open popover's box covers: a
    // press on a covered icon is a press ON the window (the app's own onOpenBox rule), so
    // it is not the "outside press" this case is about. The SETTINGS cluster's ℹ sits at
    // the far end of the last row, clear of a box anchored to the logo.
    QToolButton* other = nullptr;
    for (auto it = win.pop_.buttons.cbegin(); it != win.pop_.buttons.cend(); ++it)
      if (it.value() == win.actInfo_ && static_cast<QWidget*>(it.key())->isVisible())
        other = static_cast<QToolButton*>(it.key());
    QVERIFY2(other, "no visible Help button to press outside on");

    enum Route { Sticky, Peek };
    // Open the picker by `route`, press outside on `target`, and report whether the
    // popover opened and then went.
    const auto outsidePressCloses = [&](Route route, QWidget* target, const char* what) {
      bool opened = false, closed = false, notModal = false;
      QTimer::singleShot(120, &win, [&] {
        opened = win.pop_.active &&
                 win.pop_.active->objectName() == QLatin1String("accentPopover") &&
                 win.pop_.active->isVisible();
        // THE mechanism: the popover must not be MODAL. An application-modal dialog
        // marks this window blockedByModalWindow, and Qt then drops every press aimed
        // at it before any filter runs — which is exactly why the outside click did
        // nothing on the user's machine. No modal widget ⇒ the press is delivered.
        notModal = !QApplication::activeModalWidget() && win.pop_.active &&
                   !win.pop_.active->isModal() && win.isEnabled();
        const QPoint local = target->rect().center();
        const QPoint at = target->mapToGlobal(local);
        // A real press, press + release, exactly as the window system delivers it now
        // that the popover no longer blocks this window.
        QMouseEvent press(QEvent::MouseButtonPress, local, at, Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &press);
        QMouseEvent rel(QEvent::MouseButtonRelease, local, at, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(target, &rel);
        closed = !win.pop_.active || win.pop_.active->isHidden();
        if (win.pop_.active && !win.pop_.active->isHidden()) win.pop_.active->reject();
      });
      if (route == Sticky) {
        QContextMenuEvent ctx(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
        QApplication::sendEvent(logo, &ctx);      // blocks in exec until the timer acts
      } else {
        logo->setAttribute(Qt::WA_UnderMouse, true);
        QTest::keyPress(&win, Qt::Key_Alt);       // ditto, via the peek
        logo->setAttribute(Qt::WA_UnderMouse, false);
        QTest::keyRelease(&win, Qt::Key_Alt);
      }
      QVERIFY2(opened, qPrintable(QString("%1: the popover never opened").arg(what)));
      QVERIFY2(notModal,
               qPrintable(QString("%1: the popover is application-modal — Qt will drop "
                                  "the outside press before any filter sees it").arg(what)));
      QVERIFY2(closed, qPrintable(QString("%1: the popover survived a press outside it")
                                      .arg(what)));
      QVERIFY(!win.pop_.active);
      // The closing click is spent: no icon may re-open what it just dismissed.
      QVERIFY2(!win.pop_.clickTimer->isActive(),
               qPrintable(QString("%1: the dismissing click armed a re-open").arg(what)));
      QVERIFY2(!win.logoClickTimer_->isActive(),
               qPrintable(QString("%1: the dismissing click armed the accent cycle").arg(what)));
      QTest::qWait(320);   // past both deferred-click delays…
      QVERIFY2(!win.pop_.active, qPrintable(QString("%1: it came back").arg(what)));
      // …and the nested loop really unwound: the OUTER loop is running our timers again.
      bool alive = false;
      QTimer::singleShot(0, &win, [&alive] { alive = true; });
      QTRY_VERIFY2(alive, qPrintable(QString("%1: the nested event loop leaked").arg(what)));
    };

    outsidePressCloses(Sticky, win.canvas_, "sticky + canvas press");
    outsidePressCloses(Sticky, other, "sticky + toolbar press");
    outsidePressCloses(Peek, win.canvas_, "peek + canvas press");
    outsidePressCloses(Peek, other, "peek + toolbar press");
    // The logo itself is NOT outside: a left click on it with a STICKY popover up keeps
    // the list open and cycles the accent under it, the ✓ following (browser parity; user
    // report). The peek's own no-op rule is checked in logoAccentPopoverPicksDirectly.
    {
      const QString accentBefore = win.settings_.accentColor;
      const auto& presets = stencil::gui::accentPresets();
      bool opened = false, stayedOpen = false, cycleArmed = false, cycled = false, stillOpen = false, ticked = false;
      QTimer::singleShot(120, &win, [&] {
        QDialog* pop = win.pop_.active.data();
        opened = pop && pop->objectName() == QLatin1String("accentPopover") && pop->isVisible();
        const QPoint local = logo->rect().center();
        const QPoint at = logo->mapToGlobal(local);
        QMouseEvent press(QEvent::MouseButtonPress, local, at, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(logo, &press);
        QMouseEvent rel(QEvent::MouseButtonRelease, local, at, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(logo, &rel);
        stayedOpen = win.pop_.active && !win.pop_.active->isHidden();
        cycleArmed = win.logoClickTimer_->isActive();
        settle([&] { return win.settings_.accentColor != accentBefore; }, 320);
        cycled = win.settings_.accentColor != accentBefore &&
                 std::any_of(presets.begin(), presets.end(),
                             [&](const auto& a) { return a.key == win.settings_.accentColor; });
        stillOpen = win.pop_.active && !win.pop_.active->isHidden();
        if (pop) {
          auto* r = pop->findChild<QPushButton*>(QStringLiteral("accentRow-") + win.settings_.accentColor);
          ticked = r && r->property("currentAccent").toBool();
        }
        if (win.pop_.active && !win.pop_.active->isHidden()) win.pop_.active->reject();
      });
      QContextMenuEvent ctx(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
      QApplication::sendEvent(logo, &ctx);      // blocks in exec until the timer acts
      QVERIFY2(opened, "sticky + logo click: the popover never opened");
      QVERIFY2(stayedOpen, "a left press on the logo must not close its sticky popover");
      QVERIFY2(cycleArmed, "the logo click must still arm the accent cycle");
      QVERIFY2(cycled, "the deferred click must cycle the accent under the open popover");
      QVERIFY2(stillOpen, "the popover must survive the accent change");
      QVERIFY2(ticked, "the popover's tick must follow the cycled accent");
      QVERIFY(!win.pop_.active);
      auto restore = win.settings_;
      restore.accentColor = accentBefore;
      win.applySettings(restore, true);
    }

    // …and the other half of the rule: a press INSIDE the popover, or in a NESTED dialog
    // the popover opened (a confirm, a colour picker), leaves it alone — and Escape
    // still closes it.
    bool insideKept = false, nestedKept = false, escapeClosed = false;
    QTimer::singleShot(120, &win, [&] {
      QDialog* pop = win.pop_.active.data();
      if (pop) {
        const auto pressAt = [](QWidget* w) {
          const QPoint local = w->rect().center();
          QMouseEvent press(QEvent::MouseButtonPress, local, w->mapToGlobal(local),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
          QApplication::sendEvent(w, &press);
        };
        pressAt(pop);
        insideKept = win.pop_.active && !win.pop_.active->isHidden();
        // A nested dialog on top of the popover: its presses belong to another window.
        auto nested = std::make_unique<QDialog>(pop);
        nested->setObjectName(QStringLiteral("nestedOverPopover"));
        nested->resize(120, 80);
        nested->show();
        QTest::qWait(30);
        pressAt(nested.get());
        nestedKept = win.pop_.active && !win.pop_.active->isHidden() &&
                     win.pop_.active.data() == pop;
        nested->close();
        QTest::qWait(30);
        QTest::keyClick(pop, Qt::Key_Escape);
        escapeClosed = !win.pop_.active || win.pop_.active->isHidden();
      }
      if (win.pop_.active && !win.pop_.active->isHidden()) win.pop_.active->reject();
    });
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
    QApplication::sendEvent(logo, &ctx);
    QVERIFY2(insideKept, "a press INSIDE the popover must not dismiss it");
    QVERIFY2(nestedKept, "a press in a NESTED dialog must not dismiss the popover under it");
    QVERIFY2(escapeClosed, "Escape must still close the popover");
    QVERIFY(!win.pop_.active);

    // Losing the KEYBOARD closes it too — the path that survives when the window system
    // The popover lives in this window, so an app-focus loss is what ends it (a click
    // elsewhere in the window is the press rule's job). The exemption: a NESTED dialog
    // the popover opened took that focus for us.
    bool deactClosedSticky = false, deactKeptWithNested = false;
    QTimer::singleShot(120, &win, [&] {
      QDialog* pop = win.pop_.active.data();
      if (pop) {
        auto nested = std::make_unique<QDialog>(pop);
        nested->resize(120, 80);
        nested->show();
        QTest::qWait(30);
        QEvent deact1(QEvent::WindowDeactivate);
        QApplication::sendEvent(&win, &deact1);   // …handing focus to OUR nested window
        deactKeptWithNested = win.pop_.active && !win.pop_.active->isHidden();
        nested->close();
        nested.reset();
        QTest::qWait(30);
        QEvent deact2(QEvent::WindowDeactivate);
        QApplication::sendEvent(&win, &deact2);   // …now the app really lost focus
        deactClosedSticky = !win.pop_.active || win.pop_.active->isHidden();
      }
      if (win.pop_.active && !win.pop_.active->isHidden()) win.pop_.active->reject();
    });
    QContextMenuEvent ctx2(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
    QApplication::sendEvent(logo, &ctx2);
    QVERIFY2(deactKeptWithNested,
             "a nested window's activation must not close the popover under it");
    QVERIFY2(deactClosedSticky, "losing focus must close even a STICKY popover");
    QVERIFY(!win.pop_.active);

    // One open, one close, both animated, nothing re-shown. The popover is a CHILD
    // widget of the main window, so its grow/shrink are ordinary in-window animations.
    // (Animations are off for the suite; this case needs them.)
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] {
      if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim);
    });
    // The lifecycle as it happens, in order: the popover's own show/hide, the particle
    // dust flights execMaybePopover plays over the hosting overlay, and any ghost the
    // reveal machinery might fly instead — there must be none.
    struct Trace : QObject {
      QStringList seq;
      QSet<QObject*> dialogs;
      QElapsedTimer clock;
      qint64 pressedAt = -1, hidAt = -1;
      // DisintegrateOverlay flights actually launched, before/after the outside press.
      int dustOpening = 0, dustClosing = 0;
      bool dismissed = false;
      bool eventFilter(QObject* o, QEvent* e) override {
        auto* w = qobject_cast<QWidget*>(o);
        if (w && w->objectName() == QLatin1String("accentPopover")) {
          if (e->type() == QEvent::Show) { seq << QStringLiteral("show"); dialogs.insert(o); }
          if (e->type() == QEvent::Hide) {
            seq << QStringLiteral("hide");
            hidAt = clock.isValid() ? clock.elapsed() : -1;
          }
        }
        if (w && w->objectName() == QLatin1String(stencil::gui::DisintegrateOverlay::kObjectName)
            && e->type() == QEvent::Show) {
          if (dismissed) ++dustClosing; else ++dustOpening;
        }
        if (w && w->objectName() == QLatin1String("stencilModalGhost") &&
            e->type() == QEvent::Show)
          seq << QStringLiteral("ghost");
        return false;
      }
    } trace;
    trace.clock.start();
    qApp->installEventFilter(&trace);
    const auto removeTrace = qScopeGuard([&] { qApp->removeEventFilter(&trace); });

    // The logo is not an outside target any more (the block above), so the lifecycle is
    // traced on the canvas and a toolbar icon.
    for (QWidget* target : {static_cast<QWidget*>(win.canvas_), static_cast<QWidget*>(other)}) {
      trace.seq.clear();
      trace.dialogs.clear();
      trace.pressedAt = trace.hidAt = -1;
      trace.dustOpening = trace.dustClosing = 0;
      trace.dismissed = false;
      bool wasTopLevel = true, hadOverlay = false;
      QTimer::singleShot(400, &win, [&] {   // …once the open animation has landed
        const QPoint local = target->rect().center();
        const QPoint at = target->mapToGlobal(local);
        trace.pressedAt = trace.clock.elapsed();
        // THE property: no window of its own. That is what made three animated closes
        // invisible, and what the in-window overlay fixes.
        if (win.pop_.active) wasTopLevel = win.pop_.active->isWindow();
        hadOverlay = win.pop_.overlay && win.pop_.overlay->isVisible();
        trace.dismissed = true;
        QMouseEvent pr(QEvent::MouseButtonPress, local, at, Qt::LeftButton, Qt::LeftButton,
                       Qt::NoModifier);
        QApplication::sendEvent(target, &pr);
        QMouseEvent rl(QEvent::MouseButtonRelease, local, at, Qt::LeftButton, Qt::NoButton,
                       Qt::NoModifier);
        QApplication::sendEvent(target, &rl);
      });
      QContextMenuEvent open(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
      QApplication::sendEvent(logo, &open);
      QTest::qWait(700);   // past both deferred-click delays and the closing flight
      const QString what = QStringLiteral("dismiss on %1").arg(target->objectName().isEmpty()
                                                                   ? target->metaObject()->className()
                                                                   : target->objectName());
      const QString seq = trace.seq.join(QLatin1Char(','));
      QCOMPARE(trace.dialogs.size(), 1);   // ONE popover instance, never a second
      QVERIFY2(trace.seq.count(QStringLiteral("show")) == 1,
               qPrintable(QString("%1: shown %2x — something re-showed it (%3)")
                              .arg(what).arg(trace.seq.count(QStringLiteral("show"))).arg(seq)));
      QVERIFY2(trace.seq.count(QStringLiteral("hide")) == 1,
               qPrintable(QString("%1: hidden %2x (%3)")
                              .arg(what).arg(trace.seq.count(QStringLiteral("hide"))).arg(seq)));
      // No ghost at all: the popover animates itself now, and a ghost is the thing that
      // could be revealed from under a window and read as a re-open.
      QVERIFY2(!seq.contains(QLatin1String("ghost")),
               qPrintable(QString("%1: a ghost was flown for an in-window popover (%2)")
                              .arg(what, seq)));
      QVERIFY2(seq == QLatin1String("show,hide"),
               qPrintable(QString("%1: unexpected lifecycle — got %2").arg(what, seq)));
      // Never a show AFTER a hide: nothing is re-shown, by construction.
      QVERIFY2(!seq.contains(QLatin1String("hide,show")),
               qPrintable(QString("%1: the popover was re-shown after closing (%2)")
                              .arg(what, seq)));
      // …and it is a CHILD WIDGET, hosted by an in-window overlay.
      QVERIFY2(!wasTopLevel, qPrintable(QString("%1: the popover is still a top-level "
                                                "window — nothing will animate it").arg(what)));
      QVERIFY2(hadOverlay, qPrintable(QString("%1: no in-window overlay hosted it").arg(what)));
      // Both edges actually flew the particle dust — the popover no longer grows/shrinks
      // its own box.
      QVERIFY2(trace.dustOpening > 0,
               qPrintable(QString("%1: no dust played on open").arg(what)));
      QVERIFY2(trace.dustClosing > 0,
               qPrintable(QString("%1: no dust played on close").arg(what)));
      QVERIFY(!win.pop_.active);
    }
    beat();
  }

  // Logo accent-preset picker as a first-class popover: right-click opens it sticky,
  // hold-Alt peeks it, Alt-glide swaps popovers one at a time, a mid-peek right-press
  // promotes the peek to sticky, plain click still cycles. The popovers are modal (exec),
  // so every mid-open interaction runs from timers scheduled before the blocking call.
  void logoAccentPopoverPicksDirectly() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settleLayout(&win, 30);   // let the toolbar's own deferred layout pass settle first
    QToolButton* logo = win.logoBtn_;
    QVERIFY(logo);
    const QString original = win.settings_.accentColor;   // persisted — restored below
    const auto& presets = stencil::gui::accentPresets();
    QVERIFY(!presets.empty());
    if (QWidget* fw = QApplication::focusWidget()) fw->clearFocus();   // typingFocus gate off
    const QPoint c = logo->rect().center();
    int pick = -1;
    for (size_t i = 0; i < presets.size(); ++i)
      if (presets[i].key != original) { pick = int(i); break; }
    QVERIFY(pick >= 0);

    // ── STICKY right-click route ──
    bool stickyOpened = false, rowsOk = false, stickySurvivedAlt = false;
    bool picksOk = true, escapeClosedAfterPicks = false;
    QString lastPick;
    QTimer::singleShot(120, &win, [&] {
      QDialog* pop = win.pop_.active.data();
      stickyOpened = pop && pop->objectName() == QLatin1String("accentPopover") && pop->isVisible();
      if (pop) {
        int currentCount = 0;
        rowsOk = true;
        for (const auto& a : presets) {
          auto* row = pop->findChild<QPushButton*>(QStringLiteral("accentRow-") + a.key);
          rowsOk = rowsOk && row && row->text() == a.label && !row->icon().isNull();
          if (row && row->property("currentAccent").toBool()) ++currentCount;
        }
        rowsOk = rowsOk && currentCount == 1;
      }
      // Sticky: Alt press/release must NOT close it (only a peek dies with the key).
      QTest::keyPress(&win, Qt::Key_Alt);
      QTest::keyRelease(&win, Qt::Key_Alt);
      stickySurvivedAlt = win.pop_.active && !win.pop_.active->isHidden();
      // Picking a colour APPLIES it and CLOSES the popover (user decision — hovering
      // already previews live, so a click is a commit). Browser twin: the logo menu
      // closes on a pick.
      if (pop) {
        const QString key = presets[size_t(pick)].key;
        auto* row = pop->findChild<QPushButton*>(QStringLiteral("accentRow-") + key);
        if (!row) { picksOk = false; }
        else {
          row->click();
          settle([&] { return win.settings_.accentColor == key; }, 50);
          picksOk = win.settings_.accentColor == key;                     // applied
          escapeClosedAfterPicks = !win.pop_.active || win.pop_.active->isHidden();  // closed
          lastPick = key;
        }
      }
      if (win.pop_.active && !win.pop_.active->isHidden()) win.pop_.active->reject();
    });
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, c, logo->mapToGlobal(c));
    QApplication::sendEvent(logo, &ctx);   // blocks in the popover's exec until the timer acts
    QVERIFY2(stickyOpened, "right-click did not open the accent popover");
    QVERIFY2(rowsOk, "popover rows must be the preset list with one ✓-marked current row");
    QVERIFY2(stickySurvivedAlt, "the sticky popover must survive an Alt press/release");
    QVERIFY2(picksOk, "a colour pick must apply the accent");
    QVERIFY2(escapeClosedAfterPicks, "a colour pick must close the popover");
    QCOMPARE(win.settings_.accentColor, lastPick);
    QVERIFY2(!win.logoClickTimer_->isActive(), "the popover routes must not arm the click-cycle");
    QVERIFY(!win.pop_.active);

    // ── Alt+click stays inert (no popover, no accent cycle armed) ──
    QTest::mouseClick(logo, Qt::LeftButton, Qt::AltModifier, c);
    QVERIFY2(!win.pop_.active, "Alt+click must not open a popover");
    QVERIFY2(!win.logoClickTimer_->isActive(), "Alt+click must not arm the click-cycle timer");

    // ── PEEK: plain Alt hold over the logo opens promptly; Alt release closes ──
    // (hover simulated via WA_UnderMouse — the state a real Enter leaves behind; the
    // Alt KeyPress loop and glide poll accept it alongside the cursor check).
    logo->setAttribute(Qt::WA_UnderMouse, true);
    bool peekOpened = false, releaseClosed = false;
    QTimer::singleShot(120, &win, [&] {
      QDialog* pop = win.pop_.active.data();
      peekOpened = pop && pop->objectName() == QLatin1String("accentPopover") && pop->isVisible();
      QTest::keyRelease(&win, Qt::Key_Alt);   // ends the peek on the spot
      releaseClosed = !win.pop_.active || win.pop_.active->isHidden();
      if (win.pop_.active && !win.pop_.active->isHidden()) win.pop_.active->reject();
    });
    QTest::keyPress(&win, Qt::Key_Alt);   // blocks in the peek's exec
    QVERIFY2(peekOpened, "holding Alt over the logo did not peek the accent popover");
    QVERIFY2(releaseClosed, "releasing Alt must close the peeked popover at once");
    QVERIFY(!win.pop_.active);

    // ── GLIDE logo → Connections: one popover at a time, swapped by the system ──
    QToolButton* connBtn = nullptr;
    for (auto it = win.pop_.buttons.cbegin(); it != win.pop_.buttons.cend(); ++it)
      if (it.value() == win.actConnect_) connBtn = static_cast<QToolButton*>(it.key());
    QVERIFY2(connBtn, "no popover button registered for the Connections action");
    win.altHeldForTest_ = true;   // the glide poll's stand-in for a physically held Alt
    bool accentFirst = false;
    QTimer::singleShot(120, &win, [&] {
      accentFirst = win.pop_.active &&
                    win.pop_.active->objectName() == QLatin1String("accentPopover");
      // The pointer glides off the logo onto the Connections icon…
      logo->setAttribute(Qt::WA_UnderMouse, false);
      connBtn->setAttribute(Qt::WA_UnderMouse, true);
      // …and the glide poll (80ms) rejects this popover, then opens the next one.
    });
    QTest::keyPress(&win, Qt::Key_Alt);   // returns once the glide rejects the accent popover
    QVERIFY2(accentFirst, "the glide phase did not start from the accent popover");
    bool swapped = false, glideClosed = false;
    QTimer::singleShot(150, &win, [&] {   // fires inside the Connections popover's exec
      QDialog* pop = win.pop_.active.data();
      swapped = pop && qobject_cast<stencil::gui::ConnectDialog*>(pop) && pop->isVisible() &&
                win.findChildren<QDialog*>("accentPopover").isEmpty();   // single instance
      connBtn->setAttribute(Qt::WA_UnderMouse, false);
      win.altHeldForTest_ = false;
      QTest::keyRelease(&win, Qt::Key_Alt);   // closes the glided-to popover too
      glideClosed = !win.pop_.active || win.pop_.active->isHidden();
      if (win.pop_.active && !win.pop_.active->isHidden()) win.pop_.active->reject();
    });
    settle([&] { return glideClosed; }, 700);   // the deferred altPeekOpen ran and closed
    QVERIFY2(swapped, "the glide did not swap to the Connections popover (single instance)");
    QVERIFY2(glideClosed, "Alt release did not close the glided-to popover");
    QVERIFY(!win.pop_.active);

    // ── Mid-peek gestures: left-click on the logo is a NO-OP; right-press PROMOTES ──
    logo->setAttribute(Qt::WA_UnderMouse, true);
    bool noopKept = false, cycleNotArmed = false, promoted = false;
    QTimer::singleShot(120, &win, [&] {
      if (win.pop_.active) {
        QTest::mousePress(logo, Qt::LeftButton, Qt::NoModifier, c);
        QTest::mouseRelease(logo, Qt::LeftButton, Qt::NoModifier, c);
        noopKept = win.pop_.active && !win.pop_.active->isHidden();
        cycleNotArmed = !win.logoClickTimer_->isActive();
        QTest::mousePress(logo, Qt::RightButton, Qt::NoModifier, c);   // promote to sticky
        QTest::mouseRelease(logo, Qt::RightButton, Qt::NoModifier, c);
        QTest::keyRelease(&win, Qt::Key_Alt);   // promoted → the release must NOT close it
        promoted = win.pop_.active && !win.pop_.active->isHidden();
      }
      if (win.pop_.active && !win.pop_.active->isHidden()) win.pop_.active->reject();
    });
    QTest::keyPress(&win, Qt::Key_Alt);
    QVERIFY2(noopKept, "a left-click on the logo must not dismiss its peeked popover");
    QVERIFY2(cycleNotArmed, "a left-click during the peek armed the accent cycle");
    QVERIFY2(promoted, "a right-press during the peek must promote it past the Alt release");
    QVERIFY(!win.pop_.active);

    // ── Losing the app's focus ends a peek (Cmd-Tab eats the keyup). The popover is a
    // child widget of this window, so it is THIS window's deactivation that says so ──
    bool deactClosed = false;
    QTimer::singleShot(120, &win, [&] {
      if (win.pop_.active) {
        QEvent deact(QEvent::WindowDeactivate);
        QApplication::sendEvent(&win, &deact);
        deactClosed = !win.pop_.active || win.pop_.active->isHidden();
      }
      if (win.pop_.active && !win.pop_.active->isHidden()) win.pop_.active->reject();
      QTest::keyRelease(&win, Qt::Key_Alt);
    });
    QTest::keyPress(&win, Qt::Key_Alt);
    QVERIFY2(deactClosed, "the peeked popover must close when the app loses focus");
    QVERIFY(!win.pop_.active);
    logo->setAttribute(Qt::WA_UnderMouse, false);

    // ── Alt with the cursor NOT over any popover icon opens nothing ──
    win.move(400, 300);   // the offscreen cursor's resting point goes cold too
    QTest::qWait(30);
    QTest::keyPress(&win, Qt::Key_Alt);
    QTest::keyRelease(&win, Qt::Key_Alt);
    QVERIFY2(!win.pop_.active, "Alt away from the icons must not open a popover");

    // ── A PLAIN click still cycles: it arms the deferred timer ──
    QTest::mouseClick(logo, Qt::LeftButton, Qt::NoModifier, c);
    QVERIFY2(win.logoClickTimer_->isActive(), "plain click no longer arms the accent cycle");
    win.logoClickTimer_->stop();

    // Leave the persisted accent as we found it — the settings are shared across tests.
    auto restore = win.settings_;
    restore.accentColor = original;
    win.applySettings(restore, true);
  }

  // A palette change gets the flood-from-the-centre wipe (support/themeSwapOverlay.hpp,
  // the desktop twin of themeSwap in browser/js/ui/motion.js). The contract worth pinning
  // is WHEN it plays: on a real theme/accent change, never on the boot pass or on the many
  // re-applies that resolve to the same palette — and it must always clean itself up.
  void themeSwapWipesOnlyOnRealChanges() {
    const auto motion = withMotion();   // the wipe is motion: reduced motion just restyles
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(900, 640);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // The overlay is deliberately MOC-free, so it is found by object name, not by type.
    auto overlays = [&win] {
      return win.findChildren<QWidget*>(QString::fromLatin1(ThemeSwapOverlay::kObjectName),
                                        Qt::FindDirectChildrenOnly).size();
    };
    // Boot already ran applyTheme(); nothing should be mid-wipe.
    QTRY_COMPARE(overlays(), 0);

    // Re-applying the SAME palette is a no-op, however many times it is asked for.
    win.applyTheme();
    win.applyTheme();
    QCOMPARE(overlays(), 0);

    // A real flip does wipe…
    Settings flipped = win.settings_;
    flipped.themeMode = win.paintedDark_ ? "light" : "dark";
    win.applySettings(flipped, /*persist=*/false);
    QCOMPARE(overlays(), 1);
    // …and reaps itself when the animation lands, leaving no lingering child.
    QTRY_VERIFY_WITH_TIMEOUT(overlays() == 0, 3000);

    // An accent change is a palette change too.
    Settings accented = win.settings_;
    accented.accentColor = win.settings_.accentColor == "grass" ? "violet" : "grass";
    win.applySettings(accented, /*persist=*/false);
    QCOMPARE(overlays(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(overlays() == 0, 3000);
    // The wipe is decoration: it must never swallow input from the live window under it.
    QCOMPARE(win.settings_.accentColor, accented.accentColor);
  }

  // …but ONE flip at a time. A second press while the wipe plays would restyle the window
  // under an overlay still holding the PREVIOUS snapshot, and the two palettes tear across
  // each other — hammering the toolbar button was visibly breaking the window. The browser
  // gets this free (a new view transition supersedes the one in flight); here the press is
  // dropped until the wipe has finished.
  void themeToggleIsIgnoredMidWipe() {
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(900, 640);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto overlays = [&win] {
      return win.findChildren<QWidget*>(QString::fromLatin1(ThemeSwapOverlay::kObjectName),
                                        Qt::FindDirectChildrenOnly).size();
    };
    const QString original = win.settings_.themeMode;
    QTRY_COMPARE(overlays(), 0);

    win.toggleTheme();
    const QString mid = win.settings_.themeMode;
    QCOMPARE(overlays(), 1);
    // Dropped, not queued: two presses mid-wipe leave the palette exactly where it was.
    win.toggleTheme();
    win.toggleTheme();
    QCOMPARE(win.settings_.themeMode, mid);
    QCOMPARE(overlays(), 1);
    // …and the toggle is live again the moment the wipe has reaped itself.
    QTRY_VERIFY_WITH_TIMEOUT(overlays() == 0, 3000);
    win.toggleTheme();
    QVERIFY2(win.settings_.themeMode != mid, "the toggle stayed blocked after the wipe ended");

    // Leave the persisted theme as we found it — the settings are shared across tests.
    QTRY_VERIFY_WITH_TIMEOUT(overlays() == 0, 3000);
    auto restore = win.settings_;
    restore.themeMode = original;
    win.applySettings(restore, true);
  }

  // Nothing the restyle touched may show its new colours before the circle gets there.
  // The colour chips (updateColorSwatch) carry a palette-coloured frame, and applySettings
  // used to re-issue them BEFORE applyTheme() grabbed its snapshot — so the pickers were
  // baked into the snapshot already light while the window around them was still dark, and
  // stayed that way until the wipe finally reached them.
  void themeSwapSnapshotStillWearsTheOldPalette() {
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1100, 720);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto overlays = [&win] {
      return win.findChildren<QWidget*>(QString::fromLatin1(ThemeSwapOverlay::kObjectName),
                                        Qt::FindDirectChildrenOnly).size();
    };
    QTRY_COMPARE(overlays(), 0);
    QVERIFY(win.lineColorBtn_ && win.lineColorBtn_->isVisible());

    const QImage before = win.grab().toImage();
    Settings flipped = win.settings_;
    flipped.themeMode = win.paintedDark_ ? "light" : "dark";
    win.applySettings(flipped, /*persist=*/false);
    QCOMPARE(overlays(), 1);
    // The wipe has not ticked yet, so the whole window is still the snapshot.
    const QImage during = win.grab().toImage();

    const qreal dpr = before.devicePixelRatio();
    auto deviceRect = [dpr](QWidget* w, QWidget* top) {
      const QRect r(w->mapTo(top, QPoint(0, 0)), w->size());
      return QRect(qRound(r.x() * dpr), qRound(r.y() * dpr),
                   qRound(r.width() * dpr), qRound(r.height() * dpr));
    };
    for (QToolButton* chip : {win.lineColorBtn_, win.pointColorBtn_}) {
      const QRect r = deviceRect(chip, &win);
      QVERIFY2(before.rect().contains(r), "the chip is off-window; nothing was compared");
      QCOMPARE(during.copy(r), before.copy(r));
    }
    QTRY_VERIFY_WITH_TIMEOUT(overlays() == 0, 3000);
  }

  // macOS reads our raw pixels as if they were already in the display's space, so an sRGB
  // hex paints over-saturated on a P3 Mac while the browser — which colour-manages — shows
  // the same token quieter. theme.cpp encodes into the display space; this pins the result
  // to the value Chrome actually puts on screen for --accent (#7c3aed → #743ee4, measured).
  void accentMatchesTheBrowsersRenderedColour() {
    // The palette IS the browser's, byte for byte, on every platform: encoding into
    // Display P3 on macOS was a second conversion on an already colour-managed surface and
    // made the whole app read duller. The values below are exactly the ones in
    // browser/css/theme.css and js/config/constants.json.
    QCOMPARE(stencil::gui::accentPrimary("violet").name(), QStringLiteral("#7c3aed"));
    QCOMPARE(stencil::gui::themePalette(true).bgPage.name(), QStringLiteral("#1a1a1a"));
    QCOMPARE(stencil::gui::themePalette(false).bgPage.name(), QStringLiteral("#f0f0f0"));
    QCOMPARE(stencil::gui::themePalette(false).danger.name(), QStringLiteral("#d6293e"));
    QCOMPARE(stencil::gui::themePalette(true).danger.name(), QStringLiteral("#f0697a"));
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "mainWindow.theme.gui.moc"
