#pragma once
// Showing and hiding a GROUP of controls as sand — the f(x,y) transform fields and the
// custom page's W/H boxes, each governed by another control that stays exactly where it
// is. Desktop port of browser js/ui/motion.js revealControls / markIn / markOut.
//
// The same engine as a removed row's scatter (disintegrateOverlay.hpp) and the
// checkbox's indicator (controlSwap.hpp), on a mark's clock and a mark's throw: a group
// of fields is not a window, so its motes stay near the box they came from. The group's
// own visibility is written SYNCHRONOUSLY in both directions — the layout never waits
// for a decoration — and the picture that flies is a snapshot, so nothing can be left
// half-shown if the flight is interrupted.
//
// Header-only and Q_OBJECT-free (no signals/slots of its own), so it needs no MOC.
#include "disintegrateOverlay.hpp"
#include "modalReveal.hpp"   // support::motionReduced()

#include <QAbstractAnimation>
#include <QColor>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QLayout>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPropertyAnimation>
#include <QRect>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace stencil::gui {

  // A mark's clock: the arrival is the half you watch, the departure is brisk (browser
  // MARK_IN_MS / MARK_OUT_MS).
  // A group's slot is a wider move than a single mark and reads as a snap at the mark's
  // clock, so it gets its own. Browser twin: REVEAL_GROUP_IN/OUT_MS.
  inline constexpr int kControlRevealInMs = 420;
  inline constexpr int kControlRevealOutMs = 320;
  // Motes about this big on screen, under a ceiling of their own — well below a window's:
  // the f(x,y) row is a few hundred pixels wide and a window's grain over it would build
  // thousands of cells for a third of a second (browser MARK_COLS x MARK_ROWS).
  inline constexpr int kControlRevealCellPx = 4;
  inline constexpr int kControlRevealMaxCells = 600;
  // The throw, as a share of a list row's — the same reduction the checkbox indicator
  // takes (kCheckSwapSpread): a toolbar group flinging motes 80px would read as the
  // window coming apart.
  inline constexpr double kControlRevealSpread = 0.32;
  // Room around the group for the motes to fly into. The far tail is transparent well
  // before it reaches this, so nothing visible is ever cut off.
  inline constexpr int kControlRevealPadPx = 18;
  // Where the real group waits while its motes gather — the browser's `@keyframes
  // markForm`, which holds it back until they have very nearly landed.
  inline constexpr double kControlRevealVeilStop = 0.62;
  inline constexpr const char* kControlRevealObjectName = "stencilControlReveal";
  // Set on a group that must not play this (nothing does yet; the escape hatch matches
  // controlSwap's kNoControlSwapProperty).
  inline constexpr const char* kNoControlRevealProperty = "stencilNoControlReveal";
  // The owner's handle on its one in-flight cloud, so settleReveal is O(1) instead of
  // a findChildren scan of the whole window (it runs several times per canvas edit).
  inline constexpr const char* kRevealFxProperty = "stencilRevealFx";

  namespace ctl {

    // Motes sized on SCREEN, thinned back under the MARK budget — the shared grid math
    // (DisintegrateOverlay::dustGrid) on this family's cell size and ceiling.
    inline void revealGrid(const QSize& size, int* cols, int* rows) {
      DisintegrateOverlay::dustGrid(size, kControlRevealCellPx, kControlRevealMaxCells,
                                    cols, rows);
    }

    // Remember `w`'s one in-flight cloud on the widget itself; the overlay self-deletes
    // on landing, so its destroyed() clears the handle and the stored pointer never dangles.
    inline void trackRevealFx(QWidget* w, DisintegrateOverlay* fx) {
      fx->setProperty("stencilRevealOwner", QVariant::fromValue<QObject*>(w));
      w->setProperty(kRevealFxProperty, QVariant::fromValue<QObject*>(fx));
      QObject::connect(fx, &QObject::destroyed, w,
                       [w] { w->setProperty(kRevealFxProperty, QVariant()); });
    }

    // Drop whatever `w` has in flight, veil included, leaving its visibility untouched.
    inline void settleReveal(QWidget* w) {
      if (!w) return;
      delete w->property(kRevealFxProperty).value<QObject*>();  // destroyed() clears the handle
      if (w->graphicsEffect()) w->setGraphicsEffect(nullptr);
    }

    // The picture that flies is the CONTROLS, never the strip behind them. QWidget::grab()
    // renders the window background under its children (the palette's Window brush — the
    // page colour), so a group photographed on a toolbar flew as a dark slab over a lighter
    // bar: the "black lines next to the inputs" in the user report. Rendering only the
    // CHILDREN onto a cleared surface leaves the gaps — a group is wider than its fields
    // whenever the row hands it slack — genuinely empty, so nothing but the fields flies.
    inline QPixmap groupShot(QWidget* w) {
      if (!w || w->width() < 1 || w->height() < 1) return QPixmap();
      const qreal dpr = w->devicePixelRatioF();
      QPixmap pm(qRound(w->width() * dpr), qRound(w->height() * dpr));
      pm.setDevicePixelRatio(dpr);
      pm.fill(Qt::transparent);
      w->render(&pm, QPoint(), QRegion(), QWidget::DrawChildren);
      return pm;
    }

    inline DisintegrateOverlay* flyReveal(QWidget* w, const QPixmap& pm, const QRect& at,
                                          bool gather, int ms) {
      QWidget* host = w->window();
      if (!host || pm.isNull() || at.width() < 4 || at.height() < 4) return nullptr;
      int cols = 1, rows = 1;
      revealGrid(at.size(), &cols, &rows);
      DisintegrateOverlay* fx = DisintegrateOverlay::overPixmaps(
          pm, QPixmap(), at, host,
          gather ? DisintegrateOverlay::Sweep::Gather : DisintegrateOverlay::Sweep::Fall,
          cols, rows, ms, kControlRevealSpread, kControlRevealPadPx,
          QString::fromLatin1(kControlRevealObjectName));
      if (fx) {
        trackRevealFx(w, fx);
        fx->setFollow(w);   // a sibling's slot opening in the same turn moves this one
      }
      return fx;
    }

    // A mark's motes are never copies of its own pixels — a 26px glyph is a few thin
    // strokes, and tiles cut from it are nearly all transparent, a flight nobody can
    // see. The browser paints anonymous SPECKS in the control's own colours instead
    // (motion.js speckPainter/markPaint: "Never faint: a mote you can barely see is a
    // flight you cannot follow"). Same recipe here, as a sheet overPixmaps slices cell
    // by cell: background lifted towards the text ink (MOTE_INK 42%), a stronger rim on
    // the border cells (MOTE_RIM_INK 66%), each speck sized and seated by the shared
    // per-cell hash so the field reads as sand rather than a mosaic.
    inline QPixmap markSpecks(const QSize& size, int cols, int rows, qreal dpr,
                              const QColor& bg, const QColor& ink) {
      QPixmap sheet(qMax(1, qRound(size.width() * dpr)), qMax(1, qRound(size.height() * dpr)));
      sheet.setDevicePixelRatio(dpr);
      sheet.fill(Qt::transparent);
      QPainter p(&sheet);
      p.setRenderHint(QPainter::Antialiasing, true);
      p.setPen(Qt::NoPen);
      const auto mixed = [&](double inkShare) {
        return QColor(qRound(bg.red() + (ink.red() - bg.red()) * inkShare),
                      qRound(bg.green() + (ink.green() - bg.green()) * inkShare),
                      qRound(bg.blue() + (ink.blue() - bg.blue()) * inkShare));
      };
      const QColor fill = mixed(0.42), rim = mixed(0.66);
      const double cw = double(size.width()) / cols;
      const double ch = double(size.height()) / rows;
      for (int cy = 0; cy < rows; ++cy)
        for (int cx = 0; cx < cols; ++cx) {
          const double n = DisintegrateOverlay::cellNoise(cx, cy);
          QColor c = (cx == 0 || cy == 0 || cx == cols - 1 || cy == rows - 1) ? rim : fill;
          c.setAlphaF(0.78 + n * 0.22);   // browser: never below 0.78
          const double grain = std::min({cw, ch, 7.0});   // SURFACE_SPECK_PX cap
          const double px = grain * (0.62 + n * 0.5);
          p.setBrush(c);
          p.drawEllipse(QRectF(cx * cw + (cw - px) / 2, cy * ch + (ch - px) / 2, px, px));
        }
      return sheet;
    }

  }  // namespace ctl

  // Property name shared by both flights' width animation.
  inline constexpr const char* kMaxWidthProperty = "maximumWidth";

  // Show or hide `w` with that sand, WHILE its own slot in the row also opens or
  // closes — a group appearing/disappearing at full width at once made its neighbour
  // (Data, Settings) jump sideways, disconnected from the dust still flying over it.
  // Visibility lands at once either way; a group already in the asked-for state, a
  // hidden window, and reduced motion all just set it. Safe on null.
  // `dust=false` is the plain half of a half-sand swap: settle any in-flight gather
  // and set visibility outright, no flight either way.
  inline void revealControls(QWidget* w, bool show, bool dust = true) {
    if (!w) return;
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
      const int savedMax = w->maximumWidth();
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
        guard->setMaximumWidth(savedMax);   // hand sizing back to the layout
      });
      shrink->start(QAbstractAnimation::DeleteWhenStopped);
      return;
    }
    // Going the other way the group has no geometry yet, so both the opacity veil AND
    // the width start at zero BEFORE Show — nothing is ever painted at full strength or
    // full width first.
    const int savedMax = w->maximumWidth();
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
      if (!guard->isVisible() || !host) { guard->setGraphicsEffect(nullptr); return; }
      // Detached for the grab: a QGraphicsEffect's cached source can outlive a
      // same-turn setOpacity(), and grabbing through it risked a stale (black) frame.
      // The width is lifted the same way, just for the one measurement.
      guard->setGraphicsEffect(nullptr);
      guard->setMaximumWidth(savedMax);
      // Settle the row's positions, then measure the width the group will REALLY end at.
      // With the cap lifted and the parent laid out, that is its live box — and for an
      // EXPANDING group (the f(x,y) pair takes the slack its row hands it) that is wider
      // than its own size hint, which is only what its contents ask for. Flying the hint
      // made the fields widen the instant the dust handed over (user report). The hint is
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
        guard->setMaximumWidth(savedMax);
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
      QObject::connect(grow, &QPropertyAnimation::finished, guard, [guard, savedMax] {
        if (guard) guard->setMaximumWidth(savedMax);   // hand sizing back to the layout
      });
      grow->start(QAbstractAnimation::DeleteWhenStopped);
    });
  }

  // Paint `w` in or out IN PLACE — opacity only, the widget keeps its layout slot (Qt
  // has no `visibility: hidden`): opaque specks in the control's own colours fly over
  // it while a QGraphicsOpacityEffect holds the real face. Leaving hides at once under
  // the falling dust; forming waits behind the motes (browser markForm: held to
  // kControlRevealVeilStop, then up). The caller keeps its own state bookkeeping.
  inline void paintRevealInPlace(QWidget* w, QWidget* host, bool out,
                                 int outMs = 200, int inMs = 260) {
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
