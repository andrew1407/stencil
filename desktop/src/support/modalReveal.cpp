#include "modalReveal.hpp"
#include "disintegrateOverlay.hpp"
#include <QEvent>

#include <QAbstractAnimation>
#include <QColorDialog>
#include <QGraphicsOpacityEffect>
#include <QDialog>
#include <QEasingCurve>
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

  namespace {
    // Brisk, but not so brisk the flight from the icon is over before it registers.
    // Ease-OUT on the way in, so the growth visibly slows as it settles into place.
    // These belong to the GHOST fallback below; the dust has its own pair
    // (DisintegrateOverlay::kSurfaceInMs / kSurfaceOutMs, the browser's numbers).
    constexpr int kOpenMs = 300;
    constexpr int kCloseMs = 240;

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
    bool flySurfaceDust(QWidget* host, const QPixmap& shot, const QRect& windowGlobal,
                        const QRect& iconGlobal, bool opening, const QColor& ink) {
      if (!host || shot.isNull() || !windowGlobal.isValid()) return false;
      const QRect box(host->mapFromGlobal(windowGlobal.topLeft()), windowGlobal.size());
      const QPoint point = host->mapFromGlobal(iconGlobal.center());
      return gui::DisintegrateOverlay::overSurface(shot, box, host, point, opening, 0, ink)
             != nullptr;
    }

    // The window's own text colour — what its motes are lifted towards, so a dark window
    // dusts light and a light one dusts dark (DisintegrateOverlay::kSurfaceInkMix).
    QColor inkOf(const QWidget& w) { return w.palette().color(QPalette::WindowText); }

    // The window waits behind its own dust and fades up as the last motes land — the
    // browser's `@keyframes surfaceForm`, which holds it invisible for the first 55% of
    // the flight. Parented to the window, so it dies with it.
    void fadeUpBehindDust(QWidget* w) {
      auto* fade = new QPropertyAnimation(w, "windowOpacity", w);
      fade->setDuration(gui::DisintegrateOverlay::kSurfaceInMs);
      fade->setKeyValueAt(0.0, 0.0);
      fade->setKeyValueAt(0.55, 0.0);
      fade->setKeyValueAt(1.0, 1.0);
      QPointer<QWidget> guard(w);
      QObject::connect(fade, &QPropertyAnimation::finished, w, [guard] {
        if (guard) guard->setWindowOpacity(1.0);   // however it ended, never left dimmed
      });
      fade->start(QAbstractAnimation::DeleteWhenStopped);
    }

    // The window the ghost lives in — the dialog's own top-level parent. Null only for an
    // unparented dialog, which has nothing to fly inside of. Deliberately NOT also
    // requiring the target to fit inside the host: that test rejected ordinary centred
    // dialogs and silently turned the whole effect off. A dialog larger than its parent
    // clips against the host edge in the last frames, which is a far smaller problem
    // than no animation at all.
    QWidget* hostFor(const QDialog& dlg) {
      QWidget* parent = dlg.parentWidget();
      if (!parent) return nullptr;
      QWidget* host = parent->window();
      return (host && host->isVisible()) ? host : nullptr;
    }
  }  // namespace

  // Qt has no portable reduce-motion hint; this env var is the opt-out (the browser
  // side uses prefers-reduced-motion).
  bool motionReduced() { return !qEnvironmentVariableIsEmpty("STENCIL_NO_ANIM"); }

  void revealDialog(QDialog& dlg, QWidget* anchor) { revealDialog(dlg, anchor, QRect()); }

  namespace {
    // Shared body of the two window flights: photograph `w`, fly the ghost between the
    // icon and the window's box inside the ANCHOR's window (a floating dock has no host
    // of its own to draw in), then run `after`.
    void flyWindow(QWidget& w, QWidget* anchor, bool opening, std::function<void()> after) {
      QPointer<QWidget> guard(&w);
      QWidget* host = anchor ? anchor->window() : nullptr;
      const QRect target(w.mapToGlobal(QPoint(0, 0)), w.size());
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
                  std::shared_ptr<QPixmap> shot)
          : QObject(dlg), dlg_(dlg), anchor_(std::move(anchor)),
            anchorRect_(anchorRect), shot_(std::move(shot)) {}

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
        const QRect to = originRect(anchor_.data(), target, anchorRect_);
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
      std::shared_ptr<QPixmap> shot_;
      bool flown_ = false;
    };
  }  // namespace

  QColor pickColorAnimated(const QColor& initial, QWidget* parent, const QString& title,
                           QWidget* anchor, const QRect& anchorRect) {
    // Non-native for the same reasons as everywhere else in the app (the macOS shared
    // panel misbehaves under our event filters) — and only a Qt dialog can be flown.
    QColorDialog dlg(parent);
    dlg.setOption(QColorDialog::DontUseNativeDialog);
    dlg.setWindowTitle(title);
    dlg.setCurrentColor(initial);
    // No explicit move: exec() centres an unpositioned QDialog over its parent — the
    // exact spot getColor's dialog lands — and revealDialog reads the geometry only
    // after exec() has laid it out.
    revealDialog(dlg, anchor, anchorRect);
    return dlg.exec() == QDialog::Accepted ? dlg.selectedColor() : QColor();
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

  void revealDialog(QDialog& dlg, QWidget* anchor, const QRect& anchorRect) {
    if (motionReduced()) return;
    QPointer<QDialog> guard(&dlg);
    QPointer<QWidget> anchorGuard(anchor);

    // Transparent BEFORE exec() shows it: the dialog is mapped at its final size and
    // position the instant exec() runs, so anything deferred to a timer lets the real
    // window flash at full size first — the "it appears, then jumps" part.
    dlg.setWindowOpacity(0.0);
    const auto restore = [guard] { if (guard) guard->setWindowOpacity(1.0); };
    // A snapshot taken while the dialog is definitely on screen. The close flight
    // re-photographs the CURRENT content as the dialog hides (grab() still renders a
    // hidden widget); this open-time shot is only its fallback.
    auto shotWhileOpen = std::make_shared<QPixmap>();

    // A 0-timer so this runs once exec() has laid the dialog out — geometry() is not the
    // final box before that.
    QTimer::singleShot(0, &dlg, [guard, anchorGuard, anchorRect, restore, shotWhileOpen] {
      if (!guard || !guard->isVisible()) { restore(); return; }
      const QRect target(guard->mapToGlobal(QPoint(0, 0)), guard->size());
      QWidget* host = hostFor(*guard);
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
    dlg.installEventFilter(new CloseFlight(&dlg, anchorGuard, anchorRect, shotWhileOpen));
  }

}  // namespace stencil::support
