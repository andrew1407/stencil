#pragma once
// Filter transitions — the LIGHT twin of disintegrateOverlay's scatter.
//
// A filter is a QUESTION being re-answered, not a removal. A row it excludes was never
// destroyed — it is simply not the answer any more — so it does not play OUT at all: its
// slot closes the instant the answer changes. The whole effect belongs to the rows that
// are LEFT, which arrive as the new answer: a row already listed stays exactly where it
// is and re-forms in place, a row the filter reveals opens its slot first. Playing the
// excluded rows out put the eye on what you had just ruled out, and made every keystroke
// in a search box wait on an exit before showing you what you had asked for.
//
// Two consumers, one curve:
//   ListFilterFade — QListWidget rows (connectDialog's widget rows, projectsDialog's
//                    painted ones); it owns the clock and the per-row bookkeeping.
//   fadeFiltered   — a plain laid-out widget row with no slot to open
//                    (shortcutsDialog's grid cells): the fade alone.
//
// Header-only and Q_OBJECT-free (no signals/slots), so it needs no MOC.
#include "disintegrateOverlay.hpp"   // the arrival dust (dustRowIn)
#include "modalReveal.hpp"           // motionReduced()

#include <QGraphicsOpacityEffect>
#include <QListWidget>
#include <QListWidgetItem>
#include <QModelIndex>
#include <QObject>
#include <QPersistentModelIndex>
#include <QPointer>
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
  // The sand the rows that are LEFT arrive out of (browser js/ui/motion.js filterDust).
  // One shared mesh budget for the whole change, so a search that leaves a dozen rows
  // costs about what one removal does — and past the row ceiling the rest simply fade,
  // which is what the browser's scatterGridFor does too.
  inline constexpr int kFilterDustMaxRows = 8;
  inline constexpr int kFilterDustCells = 2400;
  // …on the FILTER's clock, not a removal's: a view change has to keep up with typing in
  // a search box (browser FILTER_DUST_MS).
  inline constexpr int kFilterDustMs = 253;
  // A row the list genuinely GAINS is not a filter keeping up with a keystroke: the
  // pinned session row a removal reveals, a project a listing brings in. It forms on the
  // browser's arrival clock (motion.js materialize, FILTER_DUST_MS 560) taken at the 1.5
  // ratio the rest of the desktop's motion runs at (kControlRevealInMs = 520 / 1.5).
  inline constexpr int kRowArriveMs = 373;
  // Named apart from the removal's scatter (DisintegrateOverlay::kObjectName) on purpose:
  // a filter's sand is an arrival, and anything counting live removals — the tests
  // included — must never mistake one for the other.
  inline constexpr const char* kFilterDustObjectName = "stencilFilterDust";
  // Where an arriving row's veil lifts, as a share of the gather — the browser's
  // `@keyframes markForm` stop. The motes ARE the row, so it waits behind them.
  inline constexpr double kFilterDustVeilStop = 0.62;

  // Row state lives ON the item, so a list rebuild can never leak a half-faded row.
  // High roles: the dialogs' own data sits at Qt::UserRole..Qt::UserRole+7.
  inline constexpr int kFilterPresenceRole = Qt::UserRole + 40;    // 0 gone … 1 present
  inline constexpr int kFilterTargetRole = Qt::UserRole + 41;      // where it is heading
  inline constexpr int kFilterFullHeightRole = Qt::UserRole + 42;  // uncollapsed height
  // …and the veil a row waits behind while its DUST gathers (0 hidden … 1 shown). The
  // motes ARE the row arriving, so the row itself must not be there yet — the browser's
  // `.mark-forming` / `@keyframes markForm`, written as a role because these rows are
  // painted by a delegate and have no widget to put an effect on.
  inline constexpr int kFilterDustRole = Qt::UserRole + 43;

  // Set on a row WIDGET while its filter fade owns the opacity, so the scroll-edge
  // reveal keeps its hands off it (ScrollReveal::kEnteringProperty idiom).
  inline constexpr const char* kFilterFadeProperty = "stencilFiltering";
  inline constexpr const char* kFilterFadeAnimName = "stencilFilterFade";

  // Where the fade finishes, as a share of the presence range. The fade LEADS the slot:
  // a row is still invisible while its slot is opening, so you never see a squashed
  // half-row with its buttons clipped.
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

  // How much of the row's INK to paint: its presence, held back further while its dust is
  // still gathering. A row with no dust in flight reads 1 and this is the fade alone.
  inline double filterInk(const QModelIndex& idx) {
    const QVariant v = idx.data(kFilterDustRole);
    const double veil = v.isValid() ? std::clamp(v.toDouble(), 0.0, 1.0) : 1.0;
    return filterOpacity(filterPresenceOf(idx)) * veil;
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

    // Called for each row that is LEFT when the answer really changed — the arrival a
    // caller may want to dust. Optional; the fade alone is a complete effect without it.
    std::function<void(QListWidgetItem*)> onArrive;

    // Re-target every row. `wanted` says whether a row belongs to the new filtered set.
    // A row it excludes is gone at once; the rows that are LEFT arrive — one already
    // listed keeps its slot and re-forms in place, one being revealed opens its slot
    // first. Reduced motion — and a row the filter has never seen, i.e. a freshly built
    // list — jumps to the end state.
    void apply(const std::function<bool(QListWidgetItem*)>& wanted) {
      if (!list_) return;
      dustBudget_ = 0;   // one shared mesh budget per filter change (browser scatterGridFor)
      const bool instant = support::motionReduced();
      if (beforeFrame) beforeFrame();
      // Did the answer really change? A re-list that lands on the same set must not
      // replay it, or every background refresh would flash the whole list.
      bool changed = false;
      for (int i = 0; i < list_->count() && !changed; ++i) {
        const QVariant prev = list_->item(i)->data(kFilterTargetRole);
        if (prev.isValid() && prev.toDouble() != (wanted(list_->item(i)) ? 1.0 : 0.0))
          changed = true;
      }
      bool moving = false;
      for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* it = list_->item(i);
        const double target = wanted(it) ? 1.0 : 0.0;
        const bool known = it->data(kFilterPresenceRole).isValid();
        // OUT: straight to nothing — there is no exit to watch. IN: a row already at
        // full height keeps it (its slot must not jump under the pointer); one still
        // closed opens from where it is.
        const double p = (instant || !known || target <= 0.0)
                             ? target
                             : it->data(kFilterPresenceRole).toDouble();
        if (onArrive && !instant && known && changed && target > 0.5) onArrive(it);
        write(it, p, target);
        if (p != target) moving = true;
      }
      if (afterFrame) afterFrame();
      if (moving) startTicking(); else stopTicking();
    }

    bool running() const { return timer_ && timer_->isActive(); }

    // The sand a row that is LEFT arrives out of (browser js/ui/motion.js filterDust):
    // the row is veiled at once (kFilterDustRole — the motes ARE the row; a delegate-
    // painted host honours it through filterInk) and its motes gather over the rect it
    // occupies. A row already at full height has that rect NOW; one being revealed is
    // still opening its slot, so its flight waits out the fade and reads the settled
    // box. Budgeted per filter change (apply() resets it): past the row ceiling the
    // rest simply fade. `host` is the window the flight is drawn on. Wire it from
    // onArrive on a delegate-painted list; nothing plays for a row the filter drops.
    void dustRowIn(QListWidgetItem* it, QWidget* host, int ms = kFilterDustMs) {
      if (!it || !list_ || !host || !host->isVisible() || support::motionReduced()) return;
      if (dustBudget_ >= kFilterDustMaxRows) return;
      const int cells = kFilterDustCells / kFilterDustMaxRows;
      ++dustBudget_;
      if (beforeFrame) beforeFrame();
      it->setData(kFilterDustRole, 0.0);
      if (afterFrame) afterFrame();
      QPointer<ListFilterFade> self(this);
      QPointer<QWidget> hostP(host);
      QListWidget* list = list_;
      QPersistentModelIndex idx(list_->indexFromItem(it));
      const auto fly = [self, list, idx, hostP, cells, ms] {
        if (!self || !hostP || !idx.isValid()) return;
        QListWidgetItem* row = list->item(idx.row());
        if (!row) return;
        // Re-fetched from `idx`, not captured by pointer: a live re-list can delete
        // every row in the gap, and a stale QListWidgetItem* here was a use-after-free.
        const auto unveil = [self, list, idx] {
          if (!self || !idx.isValid()) return;
          QListWidgetItem* row = list->item(idx.row());
          if (!row) return;
          if (self->beforeFrame) self->beforeFrame();
          row->setData(kFilterDustRole, 1.0);
          if (self->afterFrame) self->afterFrame();
        };
        const QRect r = list->visualItemRect(row);
        if (r.width() < 8 || r.height() < 8 || !hostP->isVisible()) { unveil(); return; }
        // The motes ARE the row, so they must be made of its picture. The row is veiled
        // by now (a delegate-painted one draws NOTHING at ink 0), so lift the veil for
        // the photograph alone — grab() renders into a pixmap, never to the screen, and
        // nothing repaints in between — then put it straight back. Photographing the
        // veiled row made the cloud out of the list's bare background: the row simply
        // appeared, with no arrival to see (user report).
        const auto setVeil = [&](double v) {
          if (self->beforeFrame) self->beforeFrame();
          row->setData(kFilterDustRole, v);
          if (self->afterFrame) self->afterFrame();
        };
        setVeil(1.0);
        const QPixmap shot = list->viewport()->grab(r);
        setVeil(0.0);
        auto* fx = DisintegrateOverlay::overRect(list->viewport(), r, hostP,
                                                 DisintegrateOverlay::Sweep::Gather,
                                                 /*dust=*/true, cells, ms,
                                                 QColor(), shot);
        if (!fx) { unveil(); return; }
        fx->setObjectName(QString::fromLatin1(kFilterDustObjectName));
        // Held back until the motes have very nearly landed — the browser's markForm stop.
        QTimer::singleShot(int(ms * kFilterDustVeilStop), self, unveil);
      };
      // Already listed: its box is real right now, so the motes start this frame and the
      // slot never moves. Being revealed: wait out the slot opening, then read the box.
      if (list_->visualItemRect(it).height() >= 8) fly();
      else QTimer::singleShot(kFilterFadeMs, this, fly);
    }

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
    int dustBudget_ = 0;   // rows already dusted by the filter change in flight
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
