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
      anim = new QVariantAnimation(this);
      anim->setDuration(PILL_MS);
      anim->setEasingCurve(QEasingCurve::OutCubic);
      QObject::connect(anim, &QVariantAnimation::valueChanged, this,
                       [this](const QVariant& v) {
                         hot = v.toDouble();
                         update();
                       });
    }
    void setPillColors(const QColor& rest, const QColor& accent) {
      this->rest = rest;
      this->accent = accent;
      update();
    }
    // Centre the pill on THIS widget: the handle spans input AND action buttons.
    void setPillReference(QWidget* ref) {
      this->ref = ref;
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
      if (!dragging) animateTo(0.0);
      popCursor();
      QSplitterHandle::leaveEvent(e);
    }
    void hideEvent(QHideEvent* e) override {
      popCursor();  // the menu closing must not strand the resize cursor
      QSplitterHandle::hideEvent(e);
    }
    void mousePressEvent(QMouseEvent* e) override {
      dragging = true;
      animateTo(1.0);
      QSplitterHandle::mousePressEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
      dragging = false;
      const bool stillOver = rect().contains(e->position().toPoint());
      animateTo(stillOver ? 1.0 : 0.0);
      if (!stillOver) popCursor();
      QSplitterHandle::mouseReleaseEvent(e);
    }
    void paintEvent(QPaintEvent*) override {
      if (!rest.isValid()) return;
      QPainter p(this);
      p.setRenderHint(QPainter::Antialiasing);
      const int w = qRound(PILL_W + (PILL_HOT_W - PILL_W) * hot);
      int cx = width() / 2;
      if (ref && ref->isVisible()) {
        // width()/2, NOT rect().center(): QRect::center() rounds DOWN for even widths.
        cx = mapFromGlobal(ref->mapToGlobal(QPoint(ref->width() / 2, 0))).x();
        cx = qBound(w / 2, cx, width() - w / 2);  // never overhang the handle
      }
      QColor c = rest;
      c.setRed(qRound(rest.red() + (accent.red() - rest.red()) * hot));
      c.setGreen(qRound(rest.green() + (accent.green() - rest.green()) * hot));
      c.setBlue(qRound(rest.blue() + (accent.blue() - rest.blue()) * hot));
      const QRect pill(cx - w / 2, (height() - PILL_H) / 2, w, PILL_H);
      p.setPen(Qt::NoPen);
      p.setBrush(c);
      p.drawRoundedRect(pill, PILL_RADIUS, PILL_RADIUS);
    }

   private:
    // A widget's own cursor is ignored while a QMenu holds the popup grab; an application
    // override does take effect, pushed/popped strictly on enter/leave of THIS handle.
    void pushCursor() {
      if (cursorPushed) return;
      QApplication::setOverrideCursor(
          orientation() == Qt::Horizontal ? Qt::SplitHCursor : Qt::SplitVCursor);
      cursorPushed = true;
    }
    void popCursor() {
      if (!cursorPushed) return;
      QApplication::restoreOverrideCursor();
      cursorPushed = false;
    }
    void animateTo(double target) {
      if (qFuzzyCompare(hot, target)) return;
      anim->stop();
      anim->setStartValue(hot);
      anim->setEndValue(target);
      anim->start();
    }
    QVariantAnimation* anim = nullptr;
    double hot = 0.0;
    bool dragging = false;
    bool cursorPushed = false;
    QPointer<QWidget> ref;  // pill centres on this (the input column)
    QColor rest, accent;
  };

  class PillSplitter : public QSplitter {
   public:
    using QSplitter::QSplitter;
    void setPillReference(QWidget* ref) {
      this->ref = ref;
      for (int i = 0; i < count(); ++i)
        if (QSplitterHandle* h = handle(i))
          static_cast<PillSplitterHandle*>(h)->setPillReference(ref);
    }
    void setPillColors(const QColor& rest, const QColor& accent) {
      this->rest = rest;
      this->accent = accent;
      // Every handle comes from createHandle(); qobject_cast would need Q_OBJECT (no moc here).
      for (int i = 0; i < count(); ++i)
        if (QSplitterHandle* h = handle(i))
          static_cast<PillSplitterHandle*>(h)->setPillColors(rest, accent);
    }

   protected:
    QSplitterHandle* createHandle() override {
      auto* h = new PillSplitterHandle(orientation(), this);
      if (rest.isValid()) h->setPillColors(rest, accent);
      if (ref) h->setPillReference(ref);
      return h;
    }

   private:
    QPointer<QWidget> ref;
    QColor rest, accent;
  };

}  // namespace stencil::gui
