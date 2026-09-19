#include "ProjectRowDelegate.hpp"

#include "../support/controlReveal.hpp"
#include "../support/DissolveEffect.hpp"
#include "../support/filterFade.hpp"
#include "../support/motionPrefs.hpp"
#include "scrollReveal.hpp"

#include <QAbstractItemModel>
#include <QApplication>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <algorithm>

namespace stencil::gui {

  QSize ProjectRowDelegate::sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const {
    QSize s = QStyledItemDelegate::sizeHint(opt, idx);
    if (const auto* av = qobject_cast<const QAbstractItemView*>(opt.widget))
      s.setWidth(av->viewport()->width());
    // Three stacked text lines (name / meta / origin) need a floor the base
    // (icon + padding) does not guarantee on every platform.
    if (!idx.data(Qt::UserRole).isNull() || idx.data(TEMP_ROLE).toBool())
      s.setHeight(std::max(s.height(), 76));
    // A row leaving the filtered set collapses its slot (support/filterFade), so the
    // rows below it close the gap instead of jumping once it disappears.
    s.setHeight(filterHeight(s.height(), filterPresenceOf(idx)));
    return s;
  }

  void ProjectRowDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt,
                                 const QModelIndex& idx) const {
    // One initStyleOption per paint — recordIconRect and paintRow both read it.
    QStyleOptionViewItem o(opt);
    initStyleOption(&o, idx);
    recordIconRect(o, idx);   // the magnify hit test + dust origin read it
    const double dx = hoverSlideDx(o, idx);
    if (dx <= 0.0) { paintFaded(p, o, idx); return; }
    p->save();
    p->translate(dx, 0.0);
    paintFaded(p, o, idx);
    p->restore();
  }

  void ProjectRowDelegate::recordIconRect(const QStyleOptionViewItem& o, const QModelIndex& idx) const {
    QStyle* st = o.widget ? o.widget->style() : QApplication::style();
    iconRects_[idx.row()] =
        st->subElementRect(QStyle::SE_ItemViewItemDecoration, &o, o.widget);
  }

  double ProjectRowDelegate::hoverSlideDx(const QStyleOptionViewItem& opt, const QModelIndex& idx) const {
    const bool realRow = !idx.data(Qt::UserRole).isNull();
    const int key = idx.row();
    const bool over = realRow && (opt.state & QStyle::State_MouseOver);
    if (over) slideHover_ = key;
    else if (slideHover_ == key) slideHover_ = -1;
    if (support::motionReduced()) {  // the end state, at once
      if (over) return SLIDE_PX;
      slide_.remove(key);
      return 0.0;
    }
    double v = slide_.value(key, 0.0);
    if (over && !slide_.contains(key)) slide_.insert(key, v);
    if ((over && v < 1.0) || (!over && v > 0.0)) {
      if (const auto* av = qobject_cast<const QAbstractItemView*>(opt.widget))
        slideView_ = const_cast<QAbstractItemView*>(av);
      startSlideTick();
    }
    return SLIDE_PX * v;
  }

  void ProjectRowDelegate::startSlideTick() const {
    if (!slideTick_) {
      auto* self = const_cast<ProjectRowDelegate*>(this);
      slideTick_ = new QTimer(self);
      slideTick_->setInterval(16);
      QObject::connect(slideTick_, &QTimer::timeout, self, [this] {
        const double step = 16.0 / SLIDE_MS;
        bool active = false;
        for (auto it = slide_.begin(); it != slide_.end();) {
          const bool toward = it.key() == slideHover_;
          double v = std::clamp(it.value() + (toward ? step : -step), 0.0, 1.0);
          it.value() = v;
          if ((toward && v < 1.0) || (!toward && v > 0.0)) active = true;
          // Repaint only THIS row — a full-viewport update() repainted every
          // row per frame while only one or two animate.
          if (slideView_ && slideView_->model())
            slideView_->update(slideView_->model()->index(it.key(), 0));
          if (!toward && v <= 0.0) it = slide_.erase(it);
          else ++it;
        }
        if (!active) slideTick_->stop();
      });
    }
    if (!slideTick_->isActive()) slideTick_->start();
  }

  void ProjectRowDelegate::paintFaded(QPainter* p, const QStyleOptionViewItem& opt,
                                      const QModelIndex& idx) const {
    if (idx.data(DOOMED_ROLE).toBool()) return;   // scatter plays over the held-open slot
    // A filter fade rides one painter opacity over the whole row. Distinct from the
    // grain above on purpose: excluded is not deleted.
    const double fo = filterInk(idx);
    if (fo <= 0.004) return;   // faded out — its slot is still closing
    if (fo < 1.0) {
      p->save();
      p->setOpacity(p->opacity() * fo);
      paintRevealed(p, opt, idx);
      p->restore();
      return;
    }
    paintRevealed(p, opt, idx);
  }

  void ProjectRowDelegate::paintRevealed(QPainter* p, const QStyleOptionViewItem& opt,
                                         const QModelIndex& idx) const {
    const auto* av = qobject_cast<const QAbstractItemView*>(opt.widget);
    const double dissolve = revealDissolveForItem(av ? av->viewport() : nullptr, opt.rect);
    if (dissolve <= 0.001) { paintRow(p, opt, idx); return; }
    if (dissolve >= 0.999) return;   // fully out — nothing to draw
    // A painted row has no widget to hang a QGraphicsEffect on, so it is rendered into a scratch
    // pixmap and masked by hand - the same grain + bottom-to-top wipe DissolveEffect applies.
    const qreal dpr = p->device()->devicePixelRatioF();
    QPixmap buf(opt.rect.size() * dpr);
    buf.setDevicePixelRatio(dpr);
    buf.fill(Qt::transparent);
    {
      QPainter bp(&buf);
      QStyleOptionViewItem shifted(opt);
      shifted.rect.moveTo(0, 0);   // the scratch buffer's own origin
      paintRow(&bp, shifted, idx);
      bp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
      const int h = opt.rect.height();
      const int viewH = av && av->viewport() ? av->viewport()->height() : 0;
      const double visStart = h > 0 ? std::clamp(double(-opt.rect.top()) / h, 0.0, 1.0) : 0.0;
      const double visEnd = h > 0 ? std::clamp(double(viewH - opt.rect.top()) / h, 0.0, 1.0) : 1.0;
      bp.drawImage(0, 0, DissolveEffect::maskFor(opt.rect.size(), dissolve, dpr, visStart, visEnd));
    }
    p->drawPixmap(opt.rect.topLeft(), buf);
  }

}  // namespace stencil::gui
