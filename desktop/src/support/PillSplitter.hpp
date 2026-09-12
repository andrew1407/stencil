#pragma once
// A QSplitter whose handle paints the browser's .chat-input-sizer pill: 44px (68px under
// the cursor), hairline at rest and accent while hovered or dragged.
#include <QApplication>
#include <QColor>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QSplitter>
#include <QSplitterHandle>
#include <QVariantAnimation>

namespace stencil::gui {

  namespace {
    constexpr int PILL_W = 44;
    constexpr int PILL_HOT_W = 68;
    constexpr int PILL_H = 3;
    constexpr int PILL_MS = 120;
    constexpr int PILL_RADIUS = 2;
  }  // namespace

  class PillSplitterHandle : public QSplitterHandle {
   public:
    PillSplitterHandle(Qt::Orientation o, QSplitter* parent)
        : QSplitterHandle(o, parent) {
      setAttribute(Qt::WA_Hover, true);
      anim_ = new QVariantAnimation(this);
      anim_->setDuration(PILL_MS);
      anim_->setEasingCurve(QEasingCurve::OutCubic);
      QObject::connect(anim_, &QVariantAnimation::valueChanged, this,
                       [this](const QVariant& v) {
                         hot_ = v.toDouble();
                         update();
                       });
    }
    void setPillColors(const QColor& rest, const QColor& accent) {
      rest_ = rest;
      accent_ = accent;
      update();
    }
    // Centre the pill on THIS widget: the handle spans input AND action buttons.
    void setPillReference(QWidget* ref) {
      ref_ = ref;
      update();
    }

    ~PillSplitterHandle() override { popCursor(); }

   protected:
    void enterEvent(QEnterEvent* e) override {
      animateTo(1.0);
      pushCursor();
      QSplitterHandle::enterEvent(e);
    }
    void leaveEvent(QEvent* e) override {
      if (!dragging_) animateTo(0.0);
      popCursor();
      QSplitterHandle::leaveEvent(e);
    }
    void hideEvent(QHideEvent* e) override {
      popCursor();  // the menu closing must not strand the resize cursor
      QSplitterHandle::hideEvent(e);
    }
    void mousePressEvent(QMouseEvent* e) override {
      dragging_ = true;
      animateTo(1.0);
      QSplitterHandle::mousePressEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
      dragging_ = false;
      const bool stillOver = rect().contains(e->position().toPoint());
      animateTo(stillOver ? 1.0 : 0.0);
      if (!stillOver) popCursor();
      QSplitterHandle::mouseReleaseEvent(e);
    }
    void paintEvent(QPaintEvent*) override {
      if (!rest_.isValid()) return;
      QPainter p(this);
      p.setRenderHint(QPainter::Antialiasing);
      const int w = qRound(PILL_W + (PILL_HOT_W - PILL_W) * hot_);
      int cx = width() / 2;
      if (ref_ && ref_->isVisible()) {
        // width()/2, NOT rect().center(): QRect::center() rounds DOWN for even widths.
        cx = mapFromGlobal(ref_->mapToGlobal(QPoint(ref_->width() / 2, 0))).x();
        cx = qBound(w / 2, cx, width() - w / 2);  // never overhang the handle
      }
      QColor c = rest_;
      c.setRed(qRound(rest_.red() + (accent_.red() - rest_.red()) * hot_));
      c.setGreen(qRound(rest_.green() + (accent_.green() - rest_.green()) * hot_));
      c.setBlue(qRound(rest_.blue() + (accent_.blue() - rest_.blue()) * hot_));
      const QRect pill(cx - w / 2, (height() - PILL_H) / 2, w, PILL_H);
      p.setPen(Qt::NoPen);
      p.setBrush(c);
      p.drawRoundedRect(pill, PILL_RADIUS, PILL_RADIUS);
    }

   private:
    // A widget's own cursor is ignored while a QMenu holds the popup grab; an application
    // override does take effect, pushed/popped strictly on enter/leave of THIS handle.
    void pushCursor() {
      if (cursorPushed_) return;
      QApplication::setOverrideCursor(
          orientation() == Qt::Horizontal ? Qt::SplitHCursor : Qt::SplitVCursor);
      cursorPushed_ = true;
    }
    void popCursor() {
      if (!cursorPushed_) return;
      QApplication::restoreOverrideCursor();
      cursorPushed_ = false;
    }
    void animateTo(double target) {
      if (qFuzzyCompare(hot_, target)) return;
      anim_->stop();
      anim_->setStartValue(hot_);
      anim_->setEndValue(target);
      anim_->start();
    }
    QVariantAnimation* anim_ = nullptr;
    double hot_ = 0.0;
    bool dragging_ = false;
    bool cursorPushed_ = false;
    QPointer<QWidget> ref_;  // pill centres on this (the input column)
    QColor rest_, accent_;
  };

  class PillSplitter : public QSplitter {
   public:
    using QSplitter::QSplitter;
    void setPillReference(QWidget* ref) {
      ref_ = ref;
      for (int i = 0; i < count(); ++i)
        if (QSplitterHandle* h = handle(i))
          static_cast<PillSplitterHandle*>(h)->setPillReference(ref);
    }
    void setPillColors(const QColor& rest, const QColor& accent) {
      rest_ = rest;
      accent_ = accent;
      // Every handle comes from createHandle(); qobject_cast would need Q_OBJECT (no moc here).
      for (int i = 0; i < count(); ++i)
        if (QSplitterHandle* h = handle(i))
          static_cast<PillSplitterHandle*>(h)->setPillColors(rest, accent);
    }

   protected:
    QSplitterHandle* createHandle() override {
      auto* h = new PillSplitterHandle(orientation(), this);
      if (rest_.isValid()) h->setPillColors(rest_, accent_);
      if (ref_) h->setPillReference(ref_);
      return h;
    }

   private:
    QPointer<QWidget> ref_;
    QColor rest_, accent_;
  };

}  // namespace stencil::gui
