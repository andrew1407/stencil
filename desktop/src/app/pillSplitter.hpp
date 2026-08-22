#pragma once
// ── The composer's resize grip, shared by every chat surface ────────────────
// A QSplitter whose handle paints the browser's .chat-input-sizer pill: a SHORT
// centred bar (44px, 68px under the cursor), hairline at rest and accent while
// hovered or dragged, with the colour lerping as it grows. It lives here because
// both chat composers need it — the dock's used a stylesheet handle whose only
// width lever was a symmetric margin, so the "pill" stretched with the panel and
// read as a fat accent band across the whole dock.
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
    constexpr int kPillW = 44;
    constexpr int kPillHotW = 68;
    constexpr int kPillH = 3;
    constexpr int kPillMs = 120;
    constexpr int kPillRadius = 2;
  }  // namespace

  class PillSplitterHandle : public QSplitterHandle {
   public:
    PillSplitterHandle(Qt::Orientation o, QSplitter* parent)
        : QSplitterHandle(o, parent) {
      setAttribute(Qt::WA_Hover, true);
      anim_ = new QVariantAnimation(this);
      anim_->setDuration(kPillMs);
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
    // Centre the pill on THIS widget instead of on the handle's full span.
    // The handle stretches across the whole composer row — input AND the
    // action buttons — so a pill centred on the handle sits visibly right of
    // the input the user is actually resizing. The whole handle stays
    // draggable; only the painted pill moves.
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
      const int w = qRound(kPillW + (kPillHotW - kPillW) * hot_);
      int cx = width() / 2;
      if (ref_ && ref_->isVisible()) {
        // width()/2, NOT rect().center(): QRect::center() rounds DOWN for even
        // widths, which put the pill a pixel left of the input's true middle.
        cx = mapFromGlobal(ref_->mapToGlobal(QPoint(ref_->width() / 2, 0))).x();
        cx = qBound(w / 2, cx, width() - w / 2);  // never overhang the handle
      }
      QColor c = rest_;
      // Lerp rest → accent so the colour tracks the growth.
      c.setRed(qRound(rest_.red() + (accent_.red() - rest_.red()) * hot_));
      c.setGreen(qRound(rest_.green() + (accent_.green() - rest_.green()) * hot_));
      c.setBlue(qRound(rest_.blue() + (accent_.blue() - rest_.blue()) * hot_));
      const QRect pill(cx - w / 2, (height() - kPillH) / 2, w, kPillH);
      p.setPen(Qt::NoPen);
      p.setBrush(c);
      p.drawRoundedRect(pill, kPillRadius, kPillRadius);
    }

   private:
    // QSplitterHandle already sets Qt::SplitVCursor on itself, but a widget's
    // own cursor is ignored while a QMenu holds the popup grab — the menu
    // owns the cursor. An application override is the mechanism that does
    // take effect, pushed/popped strictly on enter/leave of THIS handle (and
    // on hide/destroy), so it can never leak to the rest of the menu.
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
      // Every handle comes from createHandle() below, so the cast is safe
      // (qobject_cast would need a Q_OBJECT macro, which file-local classes
      // in this translation unit deliberately avoid — no moc pass for them).
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
