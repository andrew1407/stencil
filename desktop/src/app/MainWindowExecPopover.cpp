// Running a dialog as an anchored popover rather than a window: the reveal, the chord forwarding
// while its own event loop runs, and the close flight back to whatever opened it.
#include "mainWindowShellParts.hpp"

namespace stencil::gui {

  int MainWindow::execMaybePopover(QDialog& dlg, QAction* opener) {
    wireWindowSwitching(dlg, pop_.dialogActions, opener);
    // Park the originals while the dialog owns those chords.
    QList<QPair<QAction*, Qt::ShortcutContext>> parked;
    for (QAction* a : pop_.dialogActions) {
      if (!a || a->shortcut().isEmpty()) continue;
      parked.append({a, a->shortcutContext()});
      a->setShortcutContext(Qt::WidgetShortcut);
    }
    const QScopeGuard restore([&] {
      for (const auto& [a, ctx] : parked) a->setShortcutContext(ctx);
    });
    QWidget* anchor = pop_.anchor.data();
    pop_.anchor.clear();
    if (!anchor) {
      support::revealDialog(dlg, pop_.dialogAnchor.data(), pop_.dialogAnchorRect);
      return dlg.exec();
    }
    // A CHILD WIDGET, never its own window: a small frameless top-level does not animate on macOS.
    // This branch flies itself — opt out of the app-wide DialogRevealFilter or its flight piles on.
    dlg.setProperty(support::NO_DIALOG_REVEAL_PROPERTY, true);
    const QSize cap(470, 590);
    dlg.setMinimumSize(0, 0);
    dlg.setMaximumSize(cap);
    const QSize want(qMin(dlg.sizeHint().width(), cap.width()),
                     qMin(dlg.sizeHint().height(), cap.height()));

    auto* overlay = new QWidget(this);
    overlay->setObjectName(QStringLiteral("popoverOverlay"));   // themed + found by tests
    // The popover extends the logo's hover (browser: the menu lives inside .app-logo-wrap).
    if (anchor == logoBtn_ && logoFx_) asLogoFx(logoFx_)->holdWhile(overlay);
    overlay->setAutoFillBackground(true);
    dlg.setParent(overlay);
    dlg.setWindowFlags(Qt::Widget);   // a plain child now: no frame, no title, no window
    dlg.setGeometry(QRect(QPoint(0, 0), want));
    dlg.show();

    // WINDOW coordinates, kept inside the window.
    const QRect anchorGlobal(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
    const QRect windowGlobal(mapToGlobal(QPoint(0, 0)), size());
    const QRect box(mapFromGlobal(support::popoverRect(anchorGlobal, want, windowGlobal)
                                      .topLeft()),
                    want);
    const QRect fromBox(mapFromGlobal(anchorGlobal.topLeft()), anchorGlobal.size());
    // Final geometry first — grab() below needs the landed size.
    overlay->setGeometry(box);
    overlay->raise();
    overlay->show();
    dlg.setFocus(Qt::PopupFocusReason);   // Escape and typing go to the popover
    // Falls back to a plain grow+fade when the flight declines.
    if (!support::motionReduced()) {
      const QPixmap shot = overlay->grab();
      gui::DisintegrateOverlay* dust =
          shot.isNull() ? nullptr
                        : gui::DisintegrateOverlay::overSurface(
                              shot, box, this, fromBox.center(), /*gather=*/true,
                              POPOVER_OPEN_MS, overlay->palette().color(QPalette::WindowText),
                              support::DIALOG_DUST_MAX_CELLS);
      auto* fx = new QGraphicsOpacityEffect(overlay);
      overlay->setGraphicsEffect(fx);
      fx->setOpacity(0.0);
      if (dust) {
        auto* fade = new QPropertyAnimation(fx, "opacity", overlay);
        fade->setDuration(POPOVER_OPEN_MS);
        fade->setKeyValueAt(0.0, 0.0);
        fade->setKeyValueAt(0.55, 0.0);
        fade->setKeyValueAt(1.0, 1.0);
        fade->start(QAbstractAnimation::DeleteWhenStopped);
      } else {
        overlay->setGeometry(fromBox);
        auto* grow = new QPropertyAnimation(overlay, "geometry", overlay);
        grow->setDuration(POPOVER_OPEN_MS);
        grow->setStartValue(fromBox);
        grow->setEndValue(box);
        grow->setEasingCurve(QEasingCurve::OutCubic);
        auto* fade = new QPropertyAnimation(fx, "opacity", overlay);
        fade->setDuration(POPOVER_OPEN_MS);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        grow->start(QAbstractAnimation::DeleteWhenStopped);
        fade->start(QAbstractAnimation::DeleteWhenStopped);
      }
    }
    pop_.active = &dlg;
    pop_.overlay = overlay;
    // Alt-GLIDE: the modal loop blocks Enter/hover events, so a poll watches the cursor.
    QTimer glide;
    glide.setInterval(80);
    connect(&glide, &QTimer::timeout, this, [this, anchor] {
      if (!pop_.active) return;
      // altHeldForTest_: the offscreen GUI test's stand-in for a held Alt (QTest never sets platform modifier state).
      if (!(QGuiApplication::queryKeyboardModifiers() & Qt::AltModifier) && !altHeldForTest_)
        return;
      const bool onBox = popoverRectGlobal().contains(QCursor::pos());
      for (auto it = pop_.buttons.cbegin(); it != pop_.buttons.cend(); ++it) {
        auto* b = static_cast<QToolButton*>(it.key());
        if (b == anchor || !b->isVisible() || !it.value()->isEnabled()) continue;
        // The cursor-rect half is blind to what COVERS the icon; underMouse() sees the overlay, so only the fallback is guarded.
        const bool hovering = b->underMouse() ||
                              (!onBox && b->rect().contains(b->mapFromGlobal(QCursor::pos())));
        if (!hovering) continue;
        pop_.peekNextButton = b;
        pop_.peekNextAction = it.value();
        dismissPopover();
        break;
      }
    });
    glide.start();
    // A nested loop, not exec(): the caller still blocks and reads a DialogCode, but there is no modal window at all.
    QPointer<QDialog> alive(&dlg);
    QPointer<QWidget> overlayAlive(overlay);
    QEventLoop loop;
    bool ended = false, closing = false;
    const auto end = [&ended, &loop] { ended = true; loop.quit(); };
    // ONE close path for every ending: freeze the picture into the overlay, then collapse it back into the icon.
    connect(&dlg, &QDialog::finished, &loop, [&] {
      if (closing) return;   // a second reject during the collapse is a no-op
      closing = true;
      if (!overlayAlive || support::motionReduced()) return end();
      const QPixmap shot = alive ? alive->grab() : QPixmap();
      if (alive) {
        auto* frozen = new QLabel(overlayAlive);
        frozen->setPixmap(shot);
        frozen->setGeometry(alive->geometry());
        frozen->show();
      }
      if (!shot.isNull() && gui::DisintegrateOverlay::overSurface(
                                shot, overlayAlive->geometry(), this, fromBox.center(),
                                /*gather=*/false, POPOVER_CLOSE_MS,
                                overlayAlive->palette().color(QPalette::WindowText),
                                support::DIALOG_DUST_MAX_CELLS)) {
        overlayAlive->hide();
        return end();
      }
      auto* fx = qobject_cast<QGraphicsOpacityEffect*>(overlayAlive->graphicsEffect());
      if (!fx) {
        fx = new QGraphicsOpacityEffect(overlayAlive);
        overlayAlive->setGraphicsEffect(fx);
      }
      fx->setOpacity(1.0);
      auto* shrink = new QPropertyAnimation(overlayAlive, "geometry", overlayAlive);
      shrink->setDuration(POPOVER_CLOSE_MS);
      shrink->setStartValue(overlayAlive->geometry());
      shrink->setEndValue(fromBox);
      shrink->setEasingCurve(QEasingCurve::InCubic);
      auto* fade = new QPropertyAnimation(fx, "opacity", overlayAlive);
      fade->setDuration(POPOVER_CLOSE_MS);
      fade->setStartValue(1.0);
      fade->setEndValue(0.0);
      connect(shrink, &QAbstractAnimation::finished, &loop, end);
      shrink->start(QAbstractAnimation::DeleteWhenStopped);
      fade->start(QAbstractAnimation::DeleteWhenStopped);
    });
    connect(&dlg, &QObject::destroyed, &loop, end);
    connect(qApp, &QCoreApplication::aboutToQuit, &loop, end);
    if (!ended) loop.exec();
    glide.stop();
    pop_.active.clear();
    pop_.overlay.clear();
    const int result = alive ? alive->result() : int(QDialog::Rejected);
    // The dialog is a stack object — never leave it parented to the overlay about to be deleted.
    if (alive) {
      alive->hide();
      alive->setParent(nullptr);
    }
    if (overlayAlive) overlayAlive->deleteLater();
    if (pop_.peekNextAction) {
      QTimer::singleShot(0, this, [this] {
        QToolButton* b = pop_.peekNextButton.data();
        QAction* a = pop_.peekNextAction.data();
        pop_.peekNextButton.clear();
        pop_.peekNextAction.clear();
        altPeekOpen(b, a);
      });
    }
    return result;
  }
}  // namespace stencil::gui
