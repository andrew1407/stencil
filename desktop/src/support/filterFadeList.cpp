#include "filterFade.hpp"

namespace stencil::gui {

  // An excluded row is gone at once; the rows LEFT arrive. A row the filter has never
  // seen (a fresh list) jumps to the end state.
  void ListFilterFade::apply(const std::function<bool(QListWidgetItem*)>& wanted) {
    if (!list_) return;
    dustBudget_ = 0;
    const bool instant = support::motionReduced();
    if (beforeFrame) beforeFrame();
    // A re-list landing on the same set must not replay, or every refresh flashes the list.
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
      // OUT: straight to nothing. IN: a row already open keeps its slot.
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


  // Browser js/ui/motion.js filterDust: the row is veiled (kFilterDustRole, honoured by
  // a delegate through filterInk) while its motes gather over its rect.
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
      // Re-fetched from `idx`: a live re-list can delete every row in the gap, and a
      // captured QListWidgetItem* was a use-after-free.
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
      // The veil is lifted for the photograph alone (grab() never reaches the screen):
      // a veiled row photographs as bare background.
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
      // Browser markForm stop.
      QTimer::singleShot(int(ms * kFilterDustVeilStop), self, unveil);
    };
    // A row being revealed has no box yet: wait out the slot opening.
    if (list_->visualItemRect(it).height() >= 8) fly();
    else QTimer::singleShot(kFilterFadeMs, this, fly);
  }


  // Dialog teardown.
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
    // Hidden only once BOTH gone and staying gone.
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
