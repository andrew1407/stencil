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
  inline constexpr int ROW_SLIDE_PX = 2;
  inline constexpr int ROW_SLIDE_MS = 120;
  // Q_OBJECT-free, so found by NAME rather than findChild<T>.
  inline constexpr const char* ROW_SLIDE_NAME = "stencilRowSlide";
  inline constexpr const char* SLIDING_DELEGATE_NAME = "stencilSlidingRows";
  // Mirrored onto the VIEW so the tests can read it.
  inline constexpr const char* ROW_SLIDE_PX_PROPERTY = "rowSlidePx";

  class RowHoverSlide : public QObject {
   public:
    explicit RowHoverSlide(QAbstractItemView* view, int px = ROW_SLIDE_PX, int ms = ROW_SLIDE_MS)
        : QObject(view), view(view), px(px), ms(ms) {
      setObjectName(QString::fromLatin1(ROW_SLIDE_NAME));
      anim.setEasingCurve(QEasingCurve::InOutQuad);   // CSS `ease`
      QObject::connect(&anim, &QVariantAnimation::valueChanged, this,
                       [this](const QVariant& v) {
                         at = v.toDouble();
                         if (!this->view) return;
                         this->view->setProperty(ROW_SLIDE_PX_PROPERTY, int(std::lround(this->px * at)));
                         this->view->viewport()->update();
                       });
      view->setMouseTracking(true);
      view->viewport()->setMouseTracking(true);
      view->viewport()->installEventFilter(this);
    }

    int offsetFor(const QModelIndex& idx) const {
      if (!row.isValid() || QModelIndex(row) != idx) return 0;
      return int(std::lround(px * at));
    }

   protected:
    bool eventFilter(QObject* o, QEvent* e) override {
      if (view && o == view->viewport()) {
        switch (e->type()) {
          case QEvent::MouseMove:
            enter(view->indexAt(static_cast<QMouseEvent*>(e)->position().toPoint()));
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
      if (row.isValid() && QModelIndex(row) == idx) return;
      // The row left behind drops back at once: two rows easing reads as the list wobbling.
      row = idx;
      at = 0.0;
      anim.stop();
      if (view) view->setProperty(ROW_SLIDE_PX_PROPERTY, 0);
      if (!idx.isValid()) { if (view) view->viewport()->update(); return; }
      if (support::motionReduced()) {
        at = 1.0;
        view->setProperty(ROW_SLIDE_PX_PROPERTY, px);
        view->viewport()->update();
        return;
      }
      anim.setStartValue(0.0);
      anim.setEndValue(1.0);
      anim.setDuration(ms);
      anim.start();
    }

    QPointer<QAbstractItemView> view;
    int px;
    int ms;
    QPersistentModelIndex row;
    double at = 0.0;
    QVariantAnimation anim;
  };

  class SlidingRowDelegate : public QStyledItemDelegate {
   public:
    SlidingRowDelegate(RowHoverSlide* slide, QAbstractItemDelegate* inner, QObject* parent)
        : QStyledItemDelegate(parent), slide(slide), inner(inner) {
      setObjectName(QString::fromLatin1(SLIDING_DELEGATE_NAME));
    }

    QAbstractItemDelegate* getInner() const { return inner; }

    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& idx) const override {
      QStyleOptionViewItem o = opt;
      const int dx = slide ? slide->offsetFor(idx) : 0;
      o.rect.translate(dx, 0);
      if (inner) inner->paint(p, o, idx);
      else QStyledItemDelegate::paint(p, o, idx);
    }

    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
      return inner ? inner->sizeHint(opt, idx) : QStyledItemDelegate::sizeHint(opt, idx);
    }

   private:
    QPointer<RowHoverSlide> slide;
    QPointer<QAbstractItemDelegate> inner;
  };

  // Call again after changing the delegate. Guarded: a second call re-wraps rather than stacking.
  inline void installRowHoverSlide(QAbstractItemView* view) {
    if (!view) return;
    auto* slide = static_cast<RowHoverSlide*>(
        view->findChild<QObject*>(QString::fromLatin1(ROW_SLIDE_NAME)));
    if (!slide) slide = new RowHoverSlide(view);
    QAbstractItemDelegate* have = view->itemDelegate();
    if (have && have->objectName() == QLatin1String(SLIDING_DELEGATE_NAME))
      have = static_cast<SlidingRowDelegate*>(have)->getInner();
    view->setItemDelegate(new SlidingRowDelegate(slide, have, view));
  }

}  // namespace stencil::gui
