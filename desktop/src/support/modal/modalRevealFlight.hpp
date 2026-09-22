#pragma once
// The window flight itself: the open/close ease a dialog rides, the filter that holds a closing
// dialog alive until its dust has landed, and the dialog-level entry both directions go through.
#include "modalRevealParts.hpp"

namespace stencil::support {

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
    flyGhost(ghost, host, from, to, opening ? OPEN_MS : CLOSE_MS,
             opening ? 0.0 : 1.0, opening ? 1.0 : 0.0, opening ? 0.18 : 0.7,
             opening ? QEasingCurve::OutCubic : QEasingCurve::InCubic,
             [guard, opening, after] {
               if (guard && opening) guard->setWindowOpacity(1.0);
               if (after) after();
             });
  }

  // Parented to the dialog; fires once — a dialog can be hidden again on its way to
  // destruction, and a second flight would fly an already-landed ghost.
  class CloseFlight : public QObject {
   public:
    CloseFlight(QDialog* dlg, QPointer<QWidget> anchor, QRect anchorRect,
                std::shared_ptr<QPixmap> shot, QRect closeRect = QRect(),
                std::function<QRect(bool)> closeRectFor = {})
        : QObject(dlg), dlg(dlg), anchor(std::move(anchor)),
          anchorRect(anchorRect), closeRect(closeRect), closeRectFor(std::move(closeRectFor)),
          shot(std::move(shot)) {}

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
      if (event->type() == QEvent::Hide && watched == dlg && !flown) {
        flown = true;
        fly();
      }
      return QObject::eventFilter(watched, event);
    }

   private:
    void fly() {
      if (!dlg) return;
      // Asked HERE, not at install: the settings dialog live-applies its own Motion rows.
      if (motionReduced()) return;
      const QRect target = dlg->geometry();
      QWidget* host = hostFor(*dlg);
      // Re-photograph NOW (grab() renders a hidden widget): the open-time shot is stale
      // once the content changed; it is only the fallback for a render that yields nothing.
      QPixmap shot = dlg->grab();
      if (shot.isNull() && this->shot) shot = *this->shot;
      if (!host || !target.isValid() || shot.isNull()) return;
      // done() sets the result before the hide this runs on, so the outcome is already known.
      QRect outcome = closeRectFor ? closeRectFor(dlg->result() == QDialog::Accepted) : QRect();
      if (!outcome.isValid()) outcome = closeRect;
      QRect to = outcome.isValid() ? outcome
                                   : originRect(anchor.data(), target, anchorRect);
      if (!outcome.isValid() && !anchorOnScreen(anchor.data(), anchorRect)) {
        const QRect home = canvasHomeRect(host);
        if (home.isValid()) to = home;
      }
      if (to == target) return;
      if (flySurfaceDust(host, shot, target, to, false, inkOf(*dlg))) return;
      QLabel* ghost = makeGhost(host, shot, target);
      // Painted NOW: one deferred frame is the gap the dialog's disappearance shows through.
      ghost->repaint();
      flyGhost(ghost, host, target, to, CLOSE_MS, 1.0, 0.0, 0.7,
               QEasingCurve::InCubic, nullptr);
    }

    QPointer<QDialog> dlg;
    QPointer<QWidget> anchor;
    QRect anchorRect;
    QRect closeRect;
    std::function<QRect(bool)> closeRectFor;
    std::shared_ptr<QPixmap> shot;
    bool flown = false;
  };

  // Split from the public entry point so the watcher can play it WITHOUT claiming the dialog.
  void flyDialog(QDialog& dlg, QWidget* anchor, const QRect& anchorRect,
                 const QRect& closeRect = QRect(),
                 std::function<QRect(bool)> closeRectFor = {}) {
    QPointer<QDialog> guard(&dlg);
    QPointer<QWidget> anchorGuard(anchor);
    auto shotWhileOpen = std::make_shared<QPixmap>();

    if (!motionReduced()) {
      // Transparent BEFORE exec() maps it, else the real window flashes at full size first.
      dlg.setWindowOpacity(0.0);
      const auto restore = [guard] { if (guard) guard->setWindowOpacity(1.0); };

      // 0-timer: geometry() is not the final box until exec() has laid the dialog out.
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
        flyGhost(ghost, host, from, target, OPEN_MS, 0.0, 1.0, 0.18,
                 QEasingCurve::OutCubic, restore);
      });
    }

    // Driven off the dialog's own Hide, NOT QDialog::finished: done() hides first and
    // emits a beat later, and in that gap the window server has already unmapped it.
    dlg.installEventFilter(new CloseFlight(&dlg, anchorGuard, anchorRect, shotWhileOpen, closeRect,
                                           std::move(closeRectFor)));
  }
}  // namespace stencil::support
