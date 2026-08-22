#pragma once
// Filter transitions — the LIGHT twin of disintegrateOverlay's scatter.
//
// A row leaving because a FILTER excluded it is NOT a row being deleted. The dust says
// "gone for good", so a filter-out has to read differently: a quick fade that leads a
// height collapse, no particles, about a fifth of the scatter's runtime. Rows entering
// the filtered set play the same motion backwards, so the two directions are symmetric.
//
// Two consumers, one curve:
//   ListFilterFade — QListWidget rows (connectDialog's widget rows, projectsDialog's
//                    painted ones); it owns the clock and the per-row bookkeeping.
//   fadeFiltered   — a plain laid-out widget row with no slot to collapse
//                    (shortcutsDialog's grid cells): the fade alone.
//
// Header-only and Q_OBJECT-free (no signals/slots), so it needs no MOC.
#include "modalReveal.hpp"   // motionReduced()

#include <QGraphicsOpacityEffect>
#include <QListWidget>
#include <QListWidgetItem>
#include <QModelIndex>
#include <QObject>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <QVariantAnimation>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>

namespace stencil::gui {

  // A fifth of DisintegrateOverlay::kMs: filtering is a view change, not a removal, and
  // it has to keep up with typing in a search box.
  inline constexpr int kFilterFadeMs = 170;
  inline constexpr int kFilterFadeTickMs = 16;

  // Row state lives ON the item, so a list rebuild can never leak a half-faded row.
  // High roles: the dialogs' own data sits at Qt::UserRole..Qt::UserRole+7.
  inline constexpr int kFilterPresenceRole = Qt::UserRole + 40;    // 0 gone … 1 present
  inline constexpr int kFilterTargetRole = Qt::UserRole + 41;      // where it is heading
  inline constexpr int kFilterFullHeightRole = Qt::UserRole + 42;  // uncollapsed height

  // Set on a row WIDGET while its filter fade owns the opacity, so the scroll-edge
  // reveal keeps its hands off it (ScrollReveal::kEnteringProperty idiom).
  inline constexpr const char* kFilterFadeProperty = "stencilFiltering";
  inline constexpr const char* kFilterFadeAnimName = "stencilFilterFade";

  // Where the fade finishes, as a share of the presence range. The fade LEADS the
  // collapse: the row is invisible well before its slot has finished closing, so you
  // never see a squashed half-row with its buttons clipped.
  inline constexpr double kFilterFadeLead = 0.4;

  // Presence → opacity. Pure.
  inline double filterOpacity(double presence) {
    const double t = (std::clamp(presence, 0.0, 1.0) - kFilterFadeLead) / (1.0 - kFilterFadeLead);
    return std::clamp(t, 0.0, 1.0);
  }

  // Presence → the slot's share of its natural height, smoothstepped so the collapse
  // eases in and out instead of snapping. Pure.
  inline double filterHeightFraction(double presence) {
    const double t = std::clamp(presence, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
  }

  // …and in pixels. A settled row is left at exactly its natural height (no rounding
  // drift), which is what keeps a rapid sequence of filter changes exactly reversible.
  inline int filterHeight(int fullHeight, double presence) {
    if (fullHeight <= 0) return fullHeight;
    if (presence >= 1.0) return fullHeight;
    return std::max(0, int(std::lround(fullHeight * filterHeightFraction(presence))));
  }

  // A row's presence, for painted rows (an item-view delegate has no widget to read).
  // Rows the filter has never touched are fully present.
  inline double filterPresenceOf(const QModelIndex& idx) {
    const QVariant v = idx.data(kFilterPresenceRole);
    return v.isValid() ? std::clamp(v.toDouble(), 0.0, 1.0) : 1.0;
  }

  // Whether a row belongs to the CURRENT filtered set — the TARGET of any transition in
  // flight, so a row still fading out already counts as gone. Use this instead of
  // isHidden() wherever "the filtered view" is the pool (select-all, counts).
  inline bool filteredIn(const QListWidgetItem* it) {
    if (!it) return false;
    const QVariant t = it->data(kFilterTargetRole);
    return t.isValid() ? t.toDouble() > 0.5 : !it->isHidden();
  }

  // Drives a QListWidget's rows between "in the filtered set" and "out of it". One
  // shared tick for the whole list, so N rows cost one pass per frame.
  class ListFilterFade : public QObject {
   public:
    explicit ListFilterFade(QListWidget* list) : QObject(list), list_(list) {}

    // Applies one row's visual state — the height/opacity only the container knows how
    // to reach. Called for every row on every frame.
    std::function<void(QListWidgetItem*, double presence)> writeRow;
    // Run around each pass over the rows: `beforeFrame` to suppress whatever the
    // container hangs off itemChanged (setData fires it), `afterFrame` to repaint.
    std::function<void()> beforeFrame, afterFrame;

    // Re-target every row. `wanted` says whether a row belongs to the new filtered set;
    // rows whose answer changed animate, the rest stay put. Reduced motion — and a row
    // the filter has never seen, i.e. a freshly built list — jumps to the end state.
    void apply(const std::function<bool(QListWidgetItem*)>& wanted) {
      if (!list_) return;
      const bool instant = support::motionReduced();
      if (beforeFrame) beforeFrame();
      bool moving = false;
      for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* it = list_->item(i);
        const double target = wanted(it) ? 1.0 : 0.0;
        const bool known = it->data(kFilterPresenceRole).isValid();
        const double p = (instant || !known) ? target
                                             : it->data(kFilterPresenceRole).toDouble();
        write(it, p, target);
        if (p != target) moving = true;
      }
      if (afterFrame) afterFrame();
      if (moving) startTicking(); else stopTicking();
    }

    bool running() const { return timer_ && timer_->isActive(); }

    // Jump every in-flight row to its final state (dialog teardown): correct result, no
    // motion, nothing left half-faded behind a closing window.
    void finishNow() {
      if (!list_) return;
      if (beforeFrame) beforeFrame();
      for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* it = list_->item(i);
        const QVariant t = it->data(kFilterTargetRole);
        if (!t.isValid()) continue;
        write(it, t.toDouble(), t.toDouble());
      }
      if (afterFrame) afterFrame();
      stopTicking();
    }

   private:
    void write(QListWidgetItem* it, double p, double target) {
      it->setData(kFilterPresenceRole, p);
      it->setData(kFilterTargetRole, target);
      // Hidden only once it is BOTH gone and staying gone: a row on its way out has to
      // stay in the view long enough to be seen leaving.
      it->setHidden(p <= 0.0 && target <= 0.0);
      if (writeRow) writeRow(it, p);
    }

    void tick() {
      if (!list_) { stopTicking(); return; }
      const double step = double(kFilterFadeTickMs) / kFilterFadeMs;
      if (beforeFrame) beforeFrame();
      bool moving = false;
      for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* it = list_->item(i);
        const QVariant t = it->data(kFilterTargetRole);
        if (!t.isValid()) continue;
        const double target = t.toDouble();
        double p = it->data(kFilterPresenceRole).toDouble();
        if (p == target) continue;
        // Clamped ONTO the target, so a row always lands exactly on 0 or 1.
        p = p < target ? std::min(target, p + step) : std::max(target, p - step);
        write(it, p, target);
        if (p != target) moving = true;
      }
      if (afterFrame) afterFrame();
      if (!moving) stopTicking();
    }

    void startTicking() {
      if (!timer_) {
        timer_ = new QTimer(this);
        timer_->setInterval(kFilterFadeTickMs);
        connect(timer_, &QTimer::timeout, this, [this] { tick(); });
      }
      if (!timer_->isActive()) timer_->start();
    }
    void stopTicking() { if (timer_) timer_->stop(); }

    QListWidget* list_ = nullptr;
    QTimer* timer_ = nullptr;
  };

  // The same transition for a plain laid-out widget a filter shows/hides. A grid row has
  // no slot to collapse, so this is the fade alone; the widget is hidden (and handed back
  // without an effect) once it has gone. Rapid changes retarget ONE animation per widget,
  // never stack them, so the last filter always wins.
  inline void fadeFiltered(QWidget* w, bool show) {
    if (!w) return;
    auto* anim = w->findChild<QVariantAnimation*>(QString::fromLatin1(kFilterFadeAnimName),
                                                  Qt::FindDirectChildrenOnly);
    if (anim) anim->stop();   // mid-flight stop() never emits finished()
    if (support::motionReduced()) {   // straight to the end state
      w->setGraphicsEffect(nullptr);
      w->setVisible(show);
      return;
    }
    auto* fx = dynamic_cast<QGraphicsOpacityEffect*>(w->graphicsEffect());
    const double from = fx ? fx->opacity() : (w->isVisible() ? 1.0 : 0.0);
    const double to = show ? 1.0 : 0.0;
    if (from == to) {
      w->setVisible(show);
      if (!show) w->setGraphicsEffect(nullptr);
      return;
    }
    if (show) w->setVisible(true);   // it has to be up to be seen arriving
    if (!fx) {
      fx = new QGraphicsOpacityEffect(w);
      w->setGraphicsEffect(fx);
    }
    fx->setOpacity(from);
    if (!anim) {
      anim = new QVariantAnimation(w);
      anim->setObjectName(QString::fromLatin1(kFilterFadeAnimName));
      QObject::connect(anim, &QVariantAnimation::valueChanged, w, [w](const QVariant& v) {
        if (auto* e = dynamic_cast<QGraphicsOpacityEffect*>(w->graphicsEffect()))
          e->setOpacity(v.toDouble());
      });
      QObject::connect(anim, &QVariantAnimation::finished, w, [w, anim] {
        if (anim->endValue().toDouble() <= 0.0) w->setVisible(false);
        w->setGraphicsEffect(nullptr);   // hand the widget back untouched
      });
    }
    anim->setDuration(std::max(1, int(kFilterFadeMs * std::abs(to - from))));
    anim->setStartValue(from);
    anim->setEndValue(to);
    anim->start();
  }

}  // namespace stencil::gui
