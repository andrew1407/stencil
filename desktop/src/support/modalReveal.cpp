#include "modalReveal.hpp"
#include "disintegrateOverlay.hpp"
#include <QEvent>

#include <QAbstractAnimation>
#include <QAbstractScrollArea>
#include <QLayout>
#include <QResizeEvent>
#include <QColorDialog>
#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QGraphicsOpacityEffect>
#include <QDialog>
#include <QEasingCurve>
#include <QApplication>
#include <QFileDialog>
#include <QLabel>
#include <QParallelAnimationGroup>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QRect>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <memory>

namespace stencil::support {

  // One surface ceiling across the app: a dialog's cloud is a surface like any other.
  static_assert(kDialogDustMaxCells == gui::DisintegrateOverlay::kSurfaceMaxCells,
                "a dialog's mote budget is the shared surface ceiling");

  // Set on a dialog whose flight is already owned — by an explicit revealDialog() call,
  // or by the watcher having taken it once.
  static constexpr const char* kRevealedProperty = "stencilDialogRevealed";

  namespace {
    // A MODAL WINDOW takes its time: every clock below runs 1.5x the shared surface clock
    // (user decision — dialogs opened and closed too fast; every other surface keeps its
    // own pace). Ease-OUT on the way in. The ghost fallback's pair (slide mode)…
    constexpr int kOpenMs = 450;
    // …and CLOSING is slower again by the same 1.5x (user decision): a window leaving is
    // the beat you actually watch, and at the open's pace it was gone before it read.
    constexpr int kCloseMs = 360 * 3 / 2;
    // …and the dust's (particle modes): the shared surface clock, slowed the same way,
    // the out-clock taking the closing 1.5x on top. The fade-up rides the in-clock so the
    // window lands with its dust.
    constexpr int kDialogDustInMs = gui::DisintegrateOverlay::kSurfaceInMs * 3 / 2;
    constexpr int kDialogDustOutMs = gui::DisintegrateOverlay::kSurfaceOutMs * 9 / 4;

    // Where the motion starts/ends, in GLOBAL coords: the icon, else a small box above
    // the dialog (hidden widgets map to 0x0, which is the same "not on screen" case).
    QRect originRect(QWidget* anchor, const QRect& target, const QRect& anchorRect = QRect()) {
      if (anchor && anchor->isVisible() && anchor->width() > 0 && anchor->height() > 0)
        return QRect(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
      // The icon is hidden (toolbars collapsed) but the caller knew WHERE the command
      // came from — the menu row that was clicked. Growing out of that beats the
      // generic box above the dialog, which reads as "it fell from the top".
      if (anchorRect.isValid() && anchorRect.width() > 0 && anchorRect.height() > 0)
        return anchorRect;
      const QSize small(qMax(target.width() / 4, 40), qMax(target.height() / 4, 32));
      const int above = target.top() - qMax(48, target.height() / 3) - small.height();
      return QRect(QPoint(target.center().x() - small.width() / 2, above), small);
    }

    // Is there still a control for the flight to come from / go back to?
    bool anchorOnScreen(QWidget* anchor, const QRect& anchorRect) {
      if (anchor && anchor->isVisible() && anchor->width() > 0 && anchor->height() > 0) return true;
      return anchorRect.isValid() && anchorRect.width() > 0 && anchorRect.height() > 0;
    }

    // Where a dialog collapses to when there is NOT one any more: the canvas, which is what
    // the user is looking at. The box above the dialog is the ENTRANCE for a window nobody
    // asked for; as an exit it leaves towards nothing, and the openers this happens to are
    // the ones the dialog's own work hides (the toolbar's Open icon goes the moment an
    // image exists). Browser twin: ui/base.js canvasHomeRect.
    QRect canvasHomeRect(QWidget* host) {
      QWidget* canvas =
          host ? host->findChild<QWidget*>(QStringLiteral("canvasViewport")) : nullptr;
      if (!canvas || !canvas->isVisible() || canvas->width() < 1 || canvas->height() < 1)
        return QRect();
      constexpr int kHomePx = 40;   // a small box, so the shrink reads as collapsing INTO it
      const QRect g(canvas->mapToGlobal(QPoint(0, 0)), canvas->size());
      return QRect(g.center() - QPoint(kHomePx / 2, kHomePx / 2), QSize(kHomePx, kHomePx));
    }

    // The flight is played by a CHILD widget of the main window, never by a window of
    // its own. Two reasons, both learned the hard way: moving/resizing a real top-level
    // window per frame goes through the window server, which stutters and lets the
    // compositor place it where it likes; and a child is just a repaint inside a window
    // that is already on screen. It holds a snapshot, so it also has no layout to fight
    // (a dialog's layout re-asserts its minimumSizeHint on every resize).
    QLabel* makeGhost(QWidget* host, const QPixmap& shot, const QRect& globalAt) {
      auto* ghost = new QLabel(host);
      ghost->setObjectName(QStringLiteral("stencilModalGhost"));  // findable by the GUI tests
      ghost->setAttribute(Qt::WA_TransparentForMouseEvents);
      ghost->setScaledContents(true);
      ghost->setPixmap(shot);
      ghost->setGeometry(QRect(host->mapFromGlobal(globalAt.topLeft()), globalAt.size()));
      ghost->raise();
      ghost->show();
      return ghost;
    }

    // geometry (host-local) + a windowOpacity-equivalent fade. `hold` is the opacity stop
    // that keeps the box solid while it is still small, so the eye follows a travelling
    // window instead of watching one fade (the browser keyframes do the same).
    void flyGhost(QLabel* ghost, QWidget* host, const QRect& fromGlobal, const QRect& toGlobal,
                  int ms, double fromOpacity, double toOpacity, double hold,
                  QEasingCurve::Type easing, std::function<void()> done) {
      const QRect from(host->mapFromGlobal(fromGlobal.topLeft()), fromGlobal.size());
      const QRect to(host->mapFromGlobal(toGlobal.topLeft()), toGlobal.size());
      auto* fx = new QGraphicsOpacityEffect(ghost);
      fx->setOpacity(fromOpacity);
      ghost->setGraphicsEffect(fx);

      auto* geo = new QPropertyAnimation(ghost, "geometry", ghost);
      geo->setDuration(ms);
      geo->setStartValue(from);
      geo->setEndValue(to);
      geo->setEasingCurve(easing);
      auto* fade = new QPropertyAnimation(fx, "opacity", ghost);
      fade->setDuration(ms);
      fade->setKeyValueAt(0.0, fromOpacity);
      fade->setKeyValueAt(hold, qMax(fromOpacity, toOpacity));
      fade->setKeyValueAt(1.0, toOpacity);

      auto* group = new QParallelAnimationGroup(ghost);
      group->addAnimation(geo);
      group->addAnimation(fade);
      QObject::connect(group, &QParallelAnimationGroup::finished, ghost, [ghost, done] {
        if (done) done();
        ghost->deleteLater();
      });
      group->start(QAbstractAnimation::DeleteWhenStopped);
    }

    // ── The dust flight (browser js/ui/motion.js surfaceIn / surfaceOut) ──────
    // A window does not SCALE out of its icon any more: it forms from motes streaming
    // out of that icon, and comes apart into motes pouring back into it. Same origin,
    // same direction, same clock family as the ghost it replaces — what changed is that
    // the flight is drawn as particles rather than as a moving rectangle. The ghost
    // stays as the fallback for anything the dust declines (an unmeasurable box, a
    // snapshot that failed), so a window never simply blinks.
    //
    // Unlike the browser (which never dusts its big `.app-modal` windows, only
    // small popups), desktop dusts big dialogs too, on a raised-but-still-modest
    // mote budget — coarser sand than a popup's, since kSurfaceMaxCells is kept
    // low on purpose (a window-sized cloud gets laggy past a few thousand cells).
    bool flySurfaceDust(QWidget* host, const QPixmap& shot, const QRect& windowGlobal,
                        const QRect& iconGlobal, bool opening, const QColor& ink) {
      if (!host || shot.isNull() || !windowGlobal.isValid()) return false;
      const QRect box(host->mapFromGlobal(windowGlobal.topLeft()), windowGlobal.size());
      const QPoint point = host->mapFromGlobal(iconGlobal.center());
      // escapeHost: a dialog can be dragged off the app, so its cloud must not be
      // cropped to the host's rect.
      auto* fx = gui::DisintegrateOverlay::overSurface(shot, box, host, point, opening,
                                                       opening ? kDialogDustInMs : kDialogDustOutMs, ink,
                                                       kDialogDustMaxCells,
                                                       /*escapeHost=*/true);
      // Painted NOW on a close, not on the next posted frame — the same synchronous
      // first paint the ghost fallback does (CloseFlight's ghost->repaint()). The dialog
      // window unmaps in this very turn; one deferred frame here is exactly the gap in
      // which it blinked out bare before the cloud appeared (user report). At t=0 the
      // overlay draws the full snapshot in place, so the hand-off is seamless.
      if (fx && !opening) fx->repaint();
      return fx != nullptr;
    }

    // The window's own text colour — what its motes are lifted towards, so a dark window
    // dusts light and a light one dusts dark (DisintegrateOverlay::kSurfaceInkMix).
    QColor inkOf(const QWidget& w) { return w.palette().color(QPalette::WindowText); }

    // The window waits behind its own dust and fades up as the last motes land — the
    // shared surfaceForm ramp (disintegrateOverlay.hpp), on the dialog clock.
    void fadeUpBehindDust(QWidget* w) { gui::fadeUpBehindDust(w, kDialogDustInMs); }

    // The window the flight is measured against — the dialog's own top-level parent. Null
    // only for an unparented dialog, which has nothing to fly out of. Deliberately NOT
    // also requiring the target to fit inside the host: that test rejected ordinary
    // centred dialogs and silently turned the whole effect off. Nor does it clip any
    // more — the dust layer leaves the host when it has to (placeForSurface); only the
    // ghost fallback below, still a child, can crop at the window edge.
    QWidget* hostFor(const QDialog& dlg) {
      QWidget* parent = dlg.parentWidget();
      if (!parent) return nullptr;
      QWidget* host = parent->window();
      return (host && host->isVisible()) ? host : nullptr;
    }
  }  // namespace

  void revealDialog(QDialog& dlg, QWidget* anchor) { revealDialog(dlg, anchor, QRect()); }

  // Bring `w`'s layout to what it will show as, so a snapshot of it is the window that
  // lands: a scroll area decides its scrollbar on a posted layout pass that grab() ran
  // ahead of, so the flight flew a picture a scrollbar too wide (user report).
  void settleLayout(QWidget& w) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    if (QLayout* l = w.layout()) l->activate();
    for (QAbstractScrollArea* area : w.findChildren<QAbstractScrollArea*>()) {
      QResizeEvent ev(area->size(), area->size());
      QCoreApplication::sendEvent(area, &ev);
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
  }

  namespace {
    // Shared body of the two window flights: photograph `w`, fly the ghost between the
    // icon and the window's box inside the ANCHOR's window (a floating dock has no host
    // of its own to draw in), then run `after`.
    void flyWindow(QWidget& w, QWidget* anchor, bool opening, std::function<void()> after) {
      QPointer<QWidget> guard(&w);
      QWidget* host = anchor ? anchor->window() : nullptr;
      const QRect target(w.mapToGlobal(QPoint(0, 0)), w.size());
      settleLayout(w);
      const QPixmap shot = w.grab();
      if (!host || !target.isValid() || shot.isNull()) { if (after) after(); return; }
      const QRect icon = originRect(anchor, target);
      if (icon == target) { if (after) after(); return; }
      const QRect from = opening ? icon : target;
      const QRect to = opening ? target : icon;
      if (opening) w.setWindowOpacity(0.0);
      if (flySurfaceDust(host, shot, target, icon, opening, inkOf(w))) {
        if (opening && guard) fadeUpBehindDust(guard);
        if (after) after();
        return;
      }
      QLabel* ghost = makeGhost(host, shot, from);
      flyGhost(ghost, host, from, to, opening ? kOpenMs : kCloseMs,
               opening ? 0.0 : 1.0, opening ? 1.0 : 0.0, opening ? 0.18 : 0.7,
               opening ? QEasingCurve::OutCubic : QEasingCurve::InCubic,
               [guard, opening, after] {
                 if (guard && opening) guard->setWindowOpacity(1.0);
                 if (after) after();
               });
    }

    // Installed on a revealed dialog: plays the shrink the instant the dialog hides. Owned
    // by the dialog (parented to it), and fires once — a dialog can be hidden again on its
    // way to destruction, and a second flight would fly an already-landed ghost.
    class CloseFlight : public QObject {
     public:
      CloseFlight(QDialog* dlg, QPointer<QWidget> anchor, QRect anchorRect,
                  std::shared_ptr<QPixmap> shot, QRect closeRect = QRect())
          : QObject(dlg), dlg_(dlg), anchor_(std::move(anchor)),
            anchorRect_(anchorRect), closeRect_(closeRect), shot_(std::move(shot)) {}

     protected:
      bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Hide && watched == dlg_ && !flown_) {
          flown_ = true;
          fly();
        }
        return QObject::eventFilter(watched, event);
      }

     private:
      void fly() {
        if (!dlg_) return;
        // Asked HERE, not when the flight was installed: the Visuals & Settings dialog
        // live-applies its own Motion rows, so turning motion off in it must govern the
        // way that very window leaves (and turning it back on must give it a flight the
        // open never installed).
        if (motionReduced()) return;
        // geometry() survives the hide (and tracks a window the user dragged).
        const QRect target = dlg_->geometry();
        QWidget* host = hostFor(*dlg_);
        // Re-photograph NOW — grab() still renders a hidden widget, and the open-time
        // snapshot goes stale the moment the dialog's content changes (closing the
        // projects dialog after a removal flew the deleted rows back into view). The
        // stored open-time shot is only the fallback for a render that yields nothing.
        QPixmap shot = dlg_->grab();
        if (shot.isNull() && shot_) shot = *shot_;
        if (!host || !target.isValid() || shot.isNull()) return;
        // An explicit close target wins over the way in: the menu row this grew out of is
        // gone, so the anchor widget is deliberately dropped with it (see revealDialog).
        QRect to = closeRect_.isValid() ? closeRect_
                                        : originRect(anchor_.data(), target, anchorRect_);
        // Nothing left to fly back into: land on the canvas rather than above the top edge.
        if (!closeRect_.isValid() && !anchorOnScreen(anchor_.data(), anchorRect_)) {
          const QRect home = canvasHomeRect(host);
          if (home.isValid()) to = home;
        }
        if (to == target) return;
        if (flySurfaceDust(host, shot, target, to, false, inkOf(*dlg_))) return;
        QLabel* ghost = makeGhost(host, shot, target);
        // Painted NOW rather than on the next posted update: one deferred frame here is
        // exactly the gap the dialog's disappearance shows through.
        ghost->repaint();
        flyGhost(ghost, host, target, to, kCloseMs, 1.0, 0.0, 0.7,
                 QEasingCurve::InCubic, nullptr);
      }

      QPointer<QDialog> dlg_;
      QPointer<QWidget> anchor_;
      QRect anchorRect_;
      QRect closeRect_;
      std::shared_ptr<QPixmap> shot_;
      bool flown_ = false;
    };
  }  // namespace

  QColor pickColorAnimated(const QColor& initial, QWidget* parent, const QString& title,
                           QWidget* anchor, const QRect& anchorRect,
                           const std::function<void(const QColor&)>& preview, bool withAlpha,
                           const QRect& closeRect) {
    // Non-native for the same reasons as everywhere else in the app (the macOS shared
    // panel misbehaves under our event filters) — and only a Qt dialog can be flown.
    QColorDialog dlg(parent);
    dlg.setOption(QColorDialog::DontUseNativeDialog);
    // Alpha only where the caller can actually store it (see the header): a line, its
    // points and an area fill are CSS `#rrggbbaa` (support/cssColor.hpp, as in the
    // browser); a tint or accent is a plain #rrggbb and must not offer a byte it drops.
    if (withAlpha) dlg.setOption(QColorDialog::ShowAlphaChannel);
    dlg.setWindowTitle(title);
    dlg.setCurrentColor(initial);
    // No explicit move: exec() centres an unpositioned QDialog over its parent, where
    // getColor's lands, and revealDialog reads the geometry only after it is laid out.
    // Live: the thing being recoloured follows the picker, so the choice is made against
    // the real picture. Cancel puts the original back (below).
    if (preview) {
      QObject::connect(&dlg, &QColorDialog::currentColorChanged, &dlg,
                       [&preview](const QColor& c) { if (c.isValid()) preview(c); });
    }
    revealDialog(dlg, anchor, anchorRect, closeRect);
    const bool accepted = dlg.exec() == QDialog::Accepted;
    if (!accepted && preview) preview(initial);
    return accepted ? dlg.selectedColor() : QColor();
  }

  void revealWindow(QWidget& w, QWidget* anchor) {
    if (motionReduced()) return;
    flyWindow(w, anchor, true, nullptr);
  }

  void dismissWindow(QWidget& w, QWidget* anchor) {
    if (motionReduced()) { w.hide(); return; }
    QPointer<QWidget> guard(&w);
    // Photograph it while it is still up, hide it at once, and let the ghost travel.
    flyWindow(w, anchor, false, nullptr);
    w.hide();
  }

  // The flight itself. Split from the public entry point so the application-wide watcher
  // below can play it WITHOUT claiming the dialog: a claim is what says "a call site owns
  // this one", and a dialog the watcher flew must still fly the next time it is shown.
  static void flyDialog(QDialog& dlg, QWidget* anchor, const QRect& anchorRect,
                        const QRect& closeRect = QRect()) {
    QPointer<QDialog> guard(&dlg);
    QPointer<QWidget> anchorGuard(anchor);
    // A snapshot taken while the dialog is definitely on screen. The close flight
    // re-photographs the CURRENT content as the dialog hides (grab() still renders a
    // hidden widget); this open-time shot is only its fallback.
    auto shotWhileOpen = std::make_shared<QPixmap>();

    // Only the OPEN flight is decided now — the close one asks again when it plays, so a
    // dialog whose own rows changed the motion mode leaves the way the NEW mode says.
    if (!motionReduced()) {
      // Transparent BEFORE exec() shows it: the dialog is mapped at its final size and
      // position the instant exec() runs, so anything deferred to a timer lets the real
      // window flash at full size first — the "it appears, then jumps" part.
      dlg.setWindowOpacity(0.0);
      const auto restore = [guard] { if (guard) guard->setWindowOpacity(1.0); };

      // A 0-timer so this runs once exec() has laid the dialog out — geometry() is not the
      // final box before that.
      QTimer::singleShot(0, &dlg, [guard, anchorGuard, anchorRect, restore, shotWhileOpen] {
        if (!guard || !guard->isVisible()) { restore(); return; }
        const QRect target(guard->mapToGlobal(QPoint(0, 0)), guard->size());
        QWidget* host = hostFor(*guard);
        settleLayout(*guard);   // the scrollbar in, before the photograph
        const QPixmap shot = guard->grab();
        *shotWhileOpen = shot;
        if (!host || !target.isValid() || shot.isNull()) { restore(); return; }
        const QRect from = originRect(anchorGuard.data(), target, anchorRect);
        if (from == target) { restore(); return; }
        if (flySurfaceDust(host, shot, target, from, true, inkOf(*guard))) { fadeUpBehindDust(guard); return; }
        QLabel* ghost = makeGhost(host, shot, from);
        flyGhost(ghost, host, from, target, kOpenMs, 0.0, 1.0, 0.18,
                 QEasingCurve::OutCubic, restore);
      });
    }

    // Closing: the dialog hides on exec()'s own terms — what shrinks into the icon is a
    // snapshot, a child ghost that deletes itself on landing.
    // anchorRect travels with the CLOSE too: a dialog opened from a menu row or from the
    // blank-image card has no anchor widget, so without it the shrink fell back to the box
    // above — it grew out of what you clicked and then vanished upwards.
    // Driven off the dialog's own Hide, NOT QDialog::finished: done() hides first and emits
    // a beat later, and in that gap the window server had already unmapped the dialog and
    // repainted the page under it — so the dialog blinked out, a full-size ghost popped back
    // in where it had been, and only then did the flight start. Starting on Hide (and
    // painting the ghost synchronously) puts the ghost up in the same turn as the unmap.
    dlg.installEventFilter(new CloseFlight(&dlg, anchorGuard, anchorRect, shotWhileOpen, closeRect));
  }

  void revealDialog(QDialog& dlg, QWidget* anchor, const QRect& anchorRect) {
    revealDialog(dlg, anchor, anchorRect, QRect());
  }

  void revealDialog(QDialog& dlg, QWidget* anchor, const QRect& anchorRect,
                    const QRect& closeRect) {
    // Claimed: this dialog has a call site that knows where it came from, so the
    // application-wide watcher below leaves it alone.
    dlg.setProperty(kRevealedProperty, true);
    flyDialog(dlg, anchor, anchorRect, closeRect);
  }

  namespace {
    // A box this big around the point the user last pressed — the browser's
    // GESTURE_ANCHOR_PX, so a question forms out of the gesture that raised it on both
    // surfaces. It only has to be big enough for originRect() to accept it as real.
    constexpr int kGestureAnchorPx = 26;
    constexpr const char* kDialogRevealFilterName = "stencilDialogRevealFilter";

    // The one application-wide watcher. Show is a once-per-dialog event, so this costs
    // nothing at rest and needs no per-call-site installation — which is the whole point:
    // `QMessageBox::question(this, …)` is built and exec'd in one expression and there is
    // nowhere to hang a reveal off.
    class DialogRevealFilter : public QObject {
     public:
      explicit DialogRevealFilter(QObject* parent) : QObject(parent) {
        setObjectName(QString::fromLatin1(kDialogRevealFilterName));
      }

     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() != QEvent::Show) return QObject::eventFilter(o, e);
        auto* dlg = qobject_cast<QDialog*>(o);
        auto* fileDlg = qobject_cast<QFileDialog*>(dlg);
        if (!dlg || dlg->property(kRevealedProperty).toBool()
            || dlg->property(kNoDialogRevealProperty).toBool()
            // A NATIVE save/open panel is positioned by the OS, not us — flying a
            // ghost of its hidden Qt fallback UI just duplicated it off to one side.
            // One built with DontUseNativeDialog opted OUT of that: it is real
            // Qt-rendered content like any other dialog (dataExportController.cpp
            // saveImageFile, stencilFileSync.cpp saveProjectFileAs), so it gets the
            // same flight and the same parent-centered placement.
            || (fileDlg && !fileDlg->testOption(QFileDialog::DontUseNativeDialog)))
          return QObject::eventFilter(o, e);
        // The press that provoked the question. A keyboard-raised one has no fresh point
        // to use, and revealDialog's own fallback (from above the box) covers it — an
        // anchor at a stale cursor would be a gesture that never happened.
        flyDialog(*dlg, nullptr, gestureAnchorRect());
        return QObject::eventFilter(o, e);
      }
    };
  }  // namespace

  QRect gestureAnchorRect() {
    const QPoint p = QCursor::pos();
    return QRect(p.x() - kGestureAnchorPx / 2, p.y() - kGestureAnchorPx / 2,
                 kGestureAnchorPx, kGestureAnchorPx);
  }

  namespace {
    constexpr const char* kModalDismissFilterName = "stencilModalDismissFilter";

    // A press anywhere outside the top modal dismisses it, exactly as a press on the
    // browser's overlay does. Application-wide, because the press we care about is
    // delivered to a BLOCKED window — QApplication drops it a moment later, but an
    // application filter still sees it first.
    class ModalDismissFilter : public QObject {
     public:
      explicit ModalDismissFilter(QObject* parent) : QObject(parent) {
        setObjectName(QString::fromLatin1(kModalDismissFilterName));
      }

     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() != QEvent::MouseButtonPress) return QObject::eventFilter(o, e);
        auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* w = qobject_cast<QWidget*>(o);
        // A NATIVE panel is the OS's window, not ours to close; an opted-out dialog is a
        // question that has to be answered.
        if (!dlg || !w || !dlg->isVisible() || qobject_cast<QFileDialog*>(dlg)
            || dlg->property(kNoOutsideDismissProperty).toBool())
          return QObject::eventFilter(o, e);
        // Inside the dialog, or inside anything it raised — a combo popup, a colour
        // picker, a nested question — is not an outside press. Popups keep the widget
        // they were built from as their parent, so the walk reaches the dialog.
        for (const QWidget* p = w; p; p = p->parentWidget())
          if (p == dlg) return QObject::eventFilter(o, e);
        dlg->reject();
        return true;   // swallowed, like the overlay eating the click in the browser
      }
    };
  }  // namespace

  void installModalDismiss() {
    QCoreApplication* app = QCoreApplication::instance();
    if (!app) return;
    if (app->findChild<QObject*>(QString::fromLatin1(kModalDismissFilterName),
                                 Qt::FindDirectChildrenOnly))
      return;
    app->installEventFilter(new ModalDismissFilter(app));
    installModalDismissNative();
  }

  // The real one lives in modalDismissMac.mm, which only the targets that open windows
  // compile. WEAK so the headless binaries — they link this TU for revealDialog alone —
  // still link; the strong Objective-C++ definition wins wherever it is present.
  __attribute__((weak)) void installModalDismissNative() {}

  void installDialogReveal() {
    QCoreApplication* app = QCoreApplication::instance();
    // Offscreen has no compositor for windowOpacity and the tests answer dialogs the
    // instant they land — both want the plain, immediate box (revealMenu does the same).
    // Reduced motion is NOT checked here: revealDialog re-reads it per flight, so the
    // preference can be turned on and off while the app runs.
    if (!app || QGuiApplication::platformName() == QLatin1String("offscreen")) return;
    if (app->findChild<QObject*>(QString::fromLatin1(kDialogRevealFilterName),
                                 Qt::FindDirectChildrenOnly))
      return;
    app->installEventFilter(new DialogRevealFilter(app));
  }

}  // namespace stencil::support
