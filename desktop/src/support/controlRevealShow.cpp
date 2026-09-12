#include "controlReveal.hpp"

namespace stencil::gui {

  // The slot slides with the dust, or the neighbour jumps sideways. Safe on null;
  // `dust=false` settles any flight and sets visibility outright.
  void revealControls(QWidget* w, bool show, bool dust) {
    if (!w) return;
    if (QObject* slide = w->property(REVEAL_SLIDE_PROPERTY).value<QObject*>())
      if (slide->property(REVEAL_OPENING_PROPERTY).toBool() != show) ctl::settleReveal(w);
    if (w->isVisibleTo(w->parentWidget()) == show) { w->setVisible(show); return; }
    ctl::settleReveal(w);
    if (!dust) { w->setVisible(show); return; }
    QWidget* host = w->window();
    if (w->property(NO_CONTROL_REVEAL_PROPERTY).toBool() || support::motionReduced()
        || !host || !host->isVisible()) {
      w->setVisible(show);
      return;
    }
    if (!show) {
      const QPixmap pm = ctl::groupShot(w);
      const QRect at(w->mapTo(host, QPoint(0, 0)), w->size());
      const int naturalW = w->width();
      const int savedMax = ctl::parkMaxWidth(w);
      ctl::flyReveal(w, pm, at, /*gather=*/false, CONTROL_REVEAL_OUT_MS);
      auto* shrink = new QPropertyAnimation(w, MAX_WIDTH_PROPERTY, w);
      shrink->setDuration(CONTROL_REVEAL_OUT_MS);
      shrink->setStartValue(naturalW);
      shrink->setEndValue(0);
      // Not InCubic: a strong ease-in barely moves for 200ms then snaps shut.
      shrink->setEasingCurve(QEasingCurve::InOutCubic);
      QPointer<QWidget> guard(w);
      QObject::connect(shrink, &QPropertyAnimation::finished, w, [guard, savedMax] {
        if (!guard) return;
        guard->setVisible(false);
        ctl::handBackMaxWidth(guard, savedMax);
      });
      ctl::trackSlide(w, shrink, /*opening=*/false);
      shrink->start(QAbstractAnimation::DeleteWhenStopped);
      return;
    }
    // Veil and width both start at zero BEFORE Show, so nothing paints at full size first.
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
      // Detached for the grab: a QGraphicsEffect's cached source can outlive a same-turn
      // setOpacity() and grab a stale (black) frame.
      guard->setGraphicsEffect(nullptr);
      guard->setMaximumWidth(savedMax);
      // The live width (an expanding group is wider than its hint), floored at the hint:
      // layouts reflow asynchronously and mid-swap the live width can be a stale sliver.
      if (QWidget* p = guard->parentWidget())
        if (QLayout* pl = p->layout()) pl->activate();
      const QSize hint = guard->sizeHint().expandedTo(QSize(1, guard->height()));
      const QSize natural(std::max(hint.width(), guard->width()), hint.height());
      if (guard->size() != natural) guard->resize(natural);
      const QPixmap pm = ctl::groupShot(guard);
      const int naturalW = natural.width();
      const QRect at(guard->mapTo(host, QPoint(0, 0)), natural);
      guard->setMaximumWidth(0);
      auto* freshVeil = new QGraphicsOpacityEffect(guard);
      freshVeil->setOpacity(0.0);
      guard->setGraphicsEffect(freshVeil);
      QPointer<QGraphicsOpacityEffect> freshVeilGuard(freshVeil);
      if (!ctl::flyReveal(guard, pm, at, /*gather=*/true, CONTROL_REVEAL_IN_MS)) {
        guard->setGraphicsEffect(nullptr);
        ctl::handBackMaxWidth(guard, savedMax);
        return;
      }
      auto* fade = new QPropertyAnimation(freshVeilGuard, "opacity", freshVeilGuard);
      fade->setDuration(CONTROL_REVEAL_IN_MS);
      fade->setKeyValueAt(0.0, 0.0);
      fade->setKeyValueAt(CONTROL_REVEAL_VEIL_STOP, 0.0);
      fade->setKeyValueAt(1.0, 1.0);
      QObject::connect(fade, &QPropertyAnimation::finished, guard, [guard] {
        if (guard) guard->setGraphicsEffect(nullptr);
      });
      fade->start(QAbstractAnimation::DeleteWhenStopped);
      auto* grow = new QPropertyAnimation(guard, MAX_WIDTH_PROPERTY, guard);
      grow->setDuration(CONTROL_REVEAL_IN_MS);
      grow->setStartValue(0);
      grow->setEndValue(naturalW);
      grow->setEasingCurve(QEasingCurve::OutCubic);
      ctl::trackSlide(guard, grow, /*opening=*/true);
      QObject::connect(grow, &QPropertyAnimation::finished, guard, [guard, savedMax] {
        if (guard) ctl::handBackMaxWidth(guard, savedMax);
      });
      grow->start(QAbstractAnimation::DeleteWhenStopped);
    });
  }


  // Opacity only — the widget keeps its layout slot (Qt has no `visibility: hidden`).
  // Forming waits behind the motes (browser markForm).
  void paintRevealInPlace(QWidget* w, QWidget* host, bool out, int outMs, int inMs) {
    ctl::settleReveal(w);
    auto* fx = new QGraphicsOpacityEffect(w);
    w->setGraphicsEffect(fx);
    const QRect at(w->mapTo(host, QPoint(0, 0)), w->size());
    // Opaque specks (ctl::markSpecks): tiles cut from line-art are nearly all transparent.
    const int cols = std::max(2, qRound(at.width() / 3.0));
    const int rows = std::max(2, qRound(at.height() / 3.0));
    const QPixmap specks = ctl::markSpecks(w->size(), cols, rows, host->devicePixelRatioF(),
                                           w->palette().color(QPalette::Window),
                                           w->palette().color(QPalette::WindowText));
    DisintegrateOverlay* cloud = DisintegrateOverlay::overPixmaps(
        specks, QPixmap(), at, host,
        out ? DisintegrateOverlay::Sweep::FALL : DisintegrateOverlay::Sweep::GATHER,
        cols, rows, out ? outMs : inMs, /*spread=*/1.0, /*pad=*/34,
        QString::fromLatin1(CONTROL_REVEAL_OBJECT_NAME));
    if (cloud) ctl::trackRevealFx(w, cloud);
    if (out || !cloud) {
      fx->setOpacity(out ? 0.0 : 1.0);
      return;
    }
    fx->setOpacity(0.0);
    auto* veil = new QPropertyAnimation(fx, "opacity", fx);
    veil->setDuration(inMs);
    veil->setKeyValueAt(0.0, 0.0);
    veil->setKeyValueAt(CONTROL_REVEAL_VEIL_STOP, 0.0);
    veil->setKeyValueAt(1.0, 1.0);
    QPointer<QGraphicsOpacityEffect> fxGuard(fx);
    QObject::connect(veil, &QPropertyAnimation::finished, fx,
                     [fxGuard] { if (fxGuard) fxGuard->setOpacity(1.0); });
    veil->start(QAbstractAnimation::DeleteWhenStopped);
  }
}  // namespace stencil::gui
