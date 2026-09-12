#include "controlReveal.hpp"

namespace stencil::gui {

  // Show or hide `w` with that sand, WHILE its own slot in the row also opens or
  // closes — a group appearing/disappearing at full width at once made its neighbour
  // (Data, Settings) jump sideways, disconnected from the dust still flying over it.
  // Visibility lands at once either way; a group already in the asked-for state, a
  // hidden window, and reduced motion all just set it. Safe on null.
  // `dust=false` is the plain half of a half-sand swap: settle any in-flight gather
  // and set visibility outright, no flight either way.
  void revealControls(QWidget* w, bool show, bool dust) {
    if (!w) return;
    // A slide running the other way is stale: drop it, and the cap it holds, first.
    if (QObject* slide = w->property(kRevealSlideProperty).value<QObject*>())
      if (slide->property(kRevealOpeningProperty).toBool() != show) ctl::settleReveal(w);
    if (w->isVisibleTo(w->parentWidget()) == show) { w->setVisible(show); return; }
    ctl::settleReveal(w);
    if (!dust) { w->setVisible(show); return; }
    QWidget* host = w->window();
    if (w->property(kNoControlRevealProperty).toBool() || support::motionReduced()
        || !host || !host->isVisible()) {
      w->setVisible(show);
      return;
    }
    if (!show) {
      // Photographed while it is still laid out, then the slot closes under the flying
      // dust — the cloud is a snapshot with a life of its own, so it does not wait.
      const QPixmap pm = ctl::groupShot(w);
      const QRect at(w->mapTo(host, QPoint(0, 0)), w->size());
      const int naturalW = w->width();
      const int savedMax = ctl::parkMaxWidth(w);
      ctl::flyReveal(w, pm, at, /*gather=*/false, kControlRevealOutMs);
      auto* shrink = new QPropertyAnimation(w, kMaxWidthProperty, w);
      shrink->setDuration(kControlRevealOutMs);
      shrink->setStartValue(naturalW);
      shrink->setEndValue(0);
      // A gentle S, not InCubic: a strong ease-in barely moves for its first 200ms and
      // then snaps shut, which reads as a glitch (the same reason the modal's own close
      // eases the way it does). The grow's OutCubic is its mirror.
      shrink->setEasingCurve(QEasingCurve::InOutCubic);
      QPointer<QWidget> guard(w);
      QObject::connect(shrink, &QPropertyAnimation::finished, w, [guard, savedMax] {
        if (!guard) return;
        guard->setVisible(false);
        ctl::handBackMaxWidth(guard, savedMax);   // hand sizing back to the layout
      });
      ctl::trackSlide(w, shrink, /*opening=*/false);
      shrink->start(QAbstractAnimation::DeleteWhenStopped);
      return;
    }
    // Going the other way the group has no geometry yet, so both the opacity veil AND
    // the width start at zero BEFORE Show — nothing is ever painted at full strength or
    // full width first.
    const int savedMax = ctl::parkMaxWidth(w);
    w->setMaximumWidth(0);
    auto* veil = new QGraphicsOpacityEffect(w);
    veil->setOpacity(0.0);
    w->setGraphicsEffect(veil);
    w->setVisible(true);
    QPointer<QWidget> guard(w);
    QPointer<QGraphicsOpacityEffect> veilGuard(veil);
    QTimer::singleShot(0, w, [guard, veilGuard, savedMax] {
      if (!guard || !veilGuard || guard->graphicsEffect() != veilGuard) return;
      QWidget* host = guard->window();
      if (!guard->isVisible() || !host) {
        guard->setGraphicsEffect(nullptr);
        ctl::handBackMaxWidth(guard, savedMax);
        return;
      }
      // Detached for the grab: a QGraphicsEffect's cached source can outlive a
      // same-turn setOpacity(), and grabbing through it risked a stale (black) frame.
      // The width is lifted the same way, just for the one measurement.
      guard->setGraphicsEffect(nullptr);
      guard->setMaximumWidth(savedMax);
      // Settle the row's positions, then measure the width the group will REALLY end at.
      // With the cap lifted and the parent laid out, that is its live box — and for an
      // EXPANDING group (the f(x,y) pair takes the slack its row hands it) that is wider
      // than its own size hint, which is only what its contents ask for. Flying the hint
      // made the fields widen the instant the dust handed over. The hint is
      // still the floor: the surrounding layouts reflow asynchronously, so mid-swap the
      // live width can be 0 or a stale sliver, and a flight sized off THAT was silently
      // declined (no dust) or eased to the sliver and snapped wide when the cap lifted.
      if (QWidget* p = guard->parentWidget())
        if (QLayout* pl = p->layout()) pl->activate();
      const QSize hint = guard->sizeHint().expandedTo(QSize(1, guard->height()));
      const QSize natural(std::max(hint.width(), guard->width()), hint.height());
      if (guard->size() != natural) guard->resize(natural);   // just for the grab
      const QPixmap pm = ctl::groupShot(guard);
      const int naturalW = natural.width();
      const QRect at(guard->mapTo(host, QPoint(0, 0)), natural);
      guard->setMaximumWidth(0);
      auto* freshVeil = new QGraphicsOpacityEffect(guard);
      freshVeil->setOpacity(0.0);
      guard->setGraphicsEffect(freshVeil);
      QPointer<QGraphicsOpacityEffect> freshVeilGuard(freshVeil);
      if (!ctl::flyReveal(guard, pm, at, /*gather=*/true, kControlRevealInMs)) {
        guard->setGraphicsEffect(nullptr);
        ctl::handBackMaxWidth(guard, savedMax);
        return;
      }
      auto* fade = new QPropertyAnimation(freshVeilGuard, "opacity", freshVeilGuard);
      fade->setDuration(kControlRevealInMs);
      fade->setKeyValueAt(0.0, 0.0);
      fade->setKeyValueAt(kControlRevealVeilStop, 0.0);
      fade->setKeyValueAt(1.0, 1.0);
      QObject::connect(fade, &QPropertyAnimation::finished, guard, [guard] {
        if (guard) guard->setGraphicsEffect(nullptr);   // however it ended, never left dimmed
      });
      fade->start(QAbstractAnimation::DeleteWhenStopped);
      auto* grow = new QPropertyAnimation(guard, kMaxWidthProperty, guard);
      grow->setDuration(kControlRevealInMs);
      grow->setStartValue(0);
      grow->setEndValue(naturalW);
      grow->setEasingCurve(QEasingCurve::OutCubic);
      ctl::trackSlide(guard, grow, /*opening=*/true);
      QObject::connect(grow, &QPropertyAnimation::finished, guard, [guard, savedMax] {
        if (guard) ctl::handBackMaxWidth(guard, savedMax);   // hand sizing back to the layout
      });
      grow->start(QAbstractAnimation::DeleteWhenStopped);
    });
  }


  // Paint `w` in or out IN PLACE — opacity only, the widget keeps its layout slot (Qt
  // has no `visibility: hidden`): opaque specks in the control's own colours fly over
  // it while a QGraphicsOpacityEffect holds the real face. Leaving hides at once under
  // the falling dust; forming waits behind the motes (browser markForm: held to
  // kControlRevealVeilStop, then up). The caller keeps its own state bookkeeping.
  void paintRevealInPlace(QWidget* w, QWidget* host, bool out, int outMs, int inMs) {
    ctl::settleReveal(w);   // one flight per control; takes the previous veil too
    auto* fx = new QGraphicsOpacityEffect(w);
    w->setGraphicsEffect(fx);
    const QRect at(w->mapTo(host, QPoint(0, 0)), w->size());
    // Icon-sized marks with a full throw, flown as opaque SPECKS in the control's own
    // colours (ctl::markSpecks) — tiles cut from a line-art glyph are nearly all
    // transparent, a flight nobody could see.
    const int cols = std::max(2, qRound(at.width() / 3.0));
    const int rows = std::max(2, qRound(at.height() / 3.0));
    const QPixmap specks = ctl::markSpecks(w->size(), cols, rows, host->devicePixelRatioF(),
                                           w->palette().color(QPalette::Window),
                                           w->palette().color(QPalette::WindowText));
    DisintegrateOverlay* cloud = DisintegrateOverlay::overPixmaps(
        specks, QPixmap(), at, host,
        out ? DisintegrateOverlay::Sweep::Fall : DisintegrateOverlay::Sweep::Gather,
        cols, rows, out ? outMs : inMs, /*spread=*/1.0, /*pad=*/34,
        QString::fromLatin1(kControlRevealObjectName));
    if (cloud) ctl::trackRevealFx(w, cloud);
    if (out || !cloud) {
      fx->setOpacity(out ? 0.0 : 1.0);   // leaving hides at once; the dust falls over it
      return;
    }
    fx->setOpacity(0.0);
    auto* veil = new QPropertyAnimation(fx, "opacity", fx);
    veil->setDuration(inMs);
    veil->setKeyValueAt(0.0, 0.0);
    veil->setKeyValueAt(kControlRevealVeilStop, 0.0);
    veil->setKeyValueAt(1.0, 1.0);
    QPointer<QGraphicsOpacityEffect> fxGuard(fx);
    QObject::connect(veil, &QPropertyAnimation::finished, fx,
                     [fxGuard] { if (fxGuard) fxGuard->setOpacity(1.0); });
    veil->start(QAbstractAnimation::DeleteWhenStopped);
  }
}  // namespace stencil::gui
