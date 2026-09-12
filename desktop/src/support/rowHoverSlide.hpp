#pragma once
// HoverSlide for an ITEM VIEW's rows — the browser's `.accent-dd-opt:hover
// { transform: translateX(2px) }`. Wraps the view's delegate. Q_OBJECT-free, no MOC.
#include "motionPrefs.hpp"   // support::motionReduced()

#include <QAbstractItemView>
#include <QEasingCurve>
#include <QEvent>
#include <QHoverEvent>
#include <QModelIndex>
#include <QMouseEvent>
#include <QObject>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QStyledItemDelegate>
#include <QVariantAnimation>

#include <cmath>

namespace stencil::gui {

  // Browser: translateX(2px) over `transform 0.12s ease`.
  inline constexpr int kRowSlidePx = 2;
  inline constexpr int kRowSlideMs = 120;
  // Q_OBJECT-free, so found by NAME rather than findChild<T>.
  inline constexpr const char* kRowSlideName = "stencilRowSlide";
  inline constexpr const char* kSlidingDelegateName = "stencilSlidingRows";
  // Mirrored onto the VIEW so the tests can read it.
  inline constexpr const char* kRowSlidePxProperty = "rowSlidePx";

  class RowHoverSlide : public QObject {
   public:
    explicit RowHoverSlide(QAbstractItemView* view, int px = kRowSlidePx, int ms = kRowSlideMs)
        : QObject(view), view_(view), px_(px), ms_(ms) {
      setObjectName(QString::fromLatin1(kRowSlideName));
      anim_.setEasingCurve(QEasingCurve::InOutQuad);   // CSS `ease`
      QObject::connect(&anim_, &QVariantAnimation::valueChanged, this,
                       [this](const QVariant& v) {
                         at_ = v.toDouble();
                         if (!view_) return;
                         view_->setProperty(kRowSlidePxProperty, int(std::lround(px_ * at_)));
                         view_->viewport()->update();
                       });
      view->setMouseTracking(true);
      view->viewport()->setMouseTracking(true);
      view->viewport()->installEventFilter(this);
    }

    int offsetFor(const QModelIndex& idx) const {
      if (!row_.isValid() || QModelIndex(row_) != idx) return 0;
      return int(std::lround(px_ * at_));
    }

   protected:
    bool eventFilter(QObject* o, QEvent* e) override {
      if (view_ && o == view_->viewport()) {
        switch (e->type()) {
          case QEvent::MouseMove:
            enter(view_->indexAt(static_cast<QMouseEvent*>(e)->position().toPoint()));
            break;
          case QEvent::Leave:
          case QEvent::Hide:
            enter(QModelIndex());
            break;
          default:
            break;
        }
      }
      return QObject::eventFilter(o, e);
    }

   private:
    void enter(const QModelIndex& idx) {
      if (row_.isValid() && QModelIndex(row_) == idx) return;
      // The row left behind drops back at once: two rows easing reads as the list wobbling.
      row_ = idx;
      at_ = 0.0;
      anim_.stop();
      if (view_) view_->setProperty(kRowSlidePxProperty, 0);
      if (!idx.isValid()) { if (view_) view_->viewport()->update(); return; }
      if (support::motionReduced()) {
        at_ = 1.0;
        view_->setProperty(kRowSlidePxProperty, px_);
        view_->viewport()->update();
        return;
      }
      anim_.setStartValue(0.0);
      anim_.setEndValue(1.0);
      anim_.setDuration(ms_);
      anim_.start();
    }

    QPointer<QAbstractItemView> view_;
    int px_;
    int ms_;
    QPersistentModelIndex row_;
    double at_ = 0.0;
    QVariantAnimation anim_;
  };

  class SlidingRowDelegate : public QStyledItemDelegate {
   public:
    SlidingRowDelegate(RowHoverSlide* slide, QAbstractItemDelegate* inner, QObject* parent)
        : QStyledItemDelegate(parent), slide_(slide), inner_(inner) {
      setObjectName(QString::fromLatin1(kSlidingDelegateName));
    }

    QAbstractItemDelegate* inner() const { return inner_; }

    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& idx) const override {
      QStyleOptionViewItem o = opt;
      const int dx = slide_ ? slide_->offsetFor(idx) : 0;
      o.rect.translate(dx, 0);
      if (inner_) inner_->paint(p, o, idx);
      else QStyledItemDelegate::paint(p, o, idx);
    }

    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
      return inner_ ? inner_->sizeHint(opt, idx) : QStyledItemDelegate::sizeHint(opt, idx);
    }

   private:
    QPointer<RowHoverSlide> slide_;
    QPointer<QAbstractItemDelegate> inner_;
  };

  // Call again after changing the delegate. Guarded: a second call re-wraps rather than stacking.
  inline void installRowHoverSlide(QAbstractItemView* view) {
    if (!view) return;
    auto* slide = static_cast<RowHoverSlide*>(
        view->findChild<QObject*>(QString::fromLatin1(kRowSlideName)));
    if (!slide) slide = new RowHoverSlide(view);
    QAbstractItemDelegate* have = view->itemDelegate();
    if (have && have->objectName() == QLatin1String(kSlidingDelegateName))
      have = static_cast<SlidingRowDelegate*>(have)->inner();
    view->setItemDelegate(new SlidingRowDelegate(slide, have, view));
  }

}  // namespace stencil::gui
