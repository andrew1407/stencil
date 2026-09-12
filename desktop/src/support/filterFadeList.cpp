#include "filterFade.hpp"

namespace stencil::gui {

  // Re-target every row. `wanted` says whether a row belongs to the new filtered set.
  // A row it excludes is gone at once; the rows that are LEFT arrive — one already
  // listed keeps its slot and re-forms in place, one being revealed opens its slot
  // first. Reduced motion — and a row the filter has never seen, i.e. a freshly built
  // list — jumps to the end state.
  void ListFilterFade::apply(const std::function<bool(QListWidgetItem*)>& wanted) {
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


  // The sand a row that is LEFT arrives out of (browser js/ui/motion.js filterDust):
  // the row is veiled at once (kFilterDustRole — the motes ARE the row; a delegate-
  // painted host honours it through filterInk) and its motes gather over the rect it
  // occupies. A row already at full height has that rect NOW; one being revealed is
  // still opening its slot, so its flight waits out the fade and reads the settled
  // box. Budgeted per filter change (apply() resets it): past the row ceiling the
  // rest simply fade. `host` is the window the flight is drawn on. Wire it from
  // onArrive on a delegate-painted list; nothing plays for a row the filter drops.
  void ListFilterFade::dustRowIn(QListWidgetItem* it, QWidget* host, int ms) {
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
      // appeared, with no arrival to see.
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
  void ListFilterFade::finishNow() {
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

  void ListFilterFade::write(QListWidgetItem* it, double p, double target) {
    it->setData(kFilterPresenceRole, p);
    it->setData(kFilterTargetRole, target);
    // Hidden only once it is BOTH gone and staying gone: a row on its way out has to
    // stay in the view long enough to be seen leaving.
    it->setHidden(p <= 0.0 && target <= 0.0);
    if (writeRow) writeRow(it, p);
  }

  void ListFilterFade::tick() {
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

  void ListFilterFade::startTicking() {
    if (!timer_) {
      timer_ = new QTimer(this);
      timer_->setInterval(kFilterFadeTickMs);
      connect(timer_, &QTimer::timeout, this, [this] { tick(); });
    }
    if (!timer_->isActive()) timer_->start();
  }
}  // namespace stencil::gui
