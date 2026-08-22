#pragma once
// Hover "glass shimmer" — the desktop port of the browser's ui-shimmer rule
// (browser/css/layout.css): a soft left→right light sweep played once on hover
// over interactive controls, so icons and rows feel the same across surfaces.
// Qt style sheets cannot animate a sweep, so this is a transparent, mouse-through
// child overlay that paints an animated diagonal highlight.
//
// Shared by the toolbar/dialog controls (mainWindow.cpp) and the chat surfaces
// (chatDock.cpp + the context menu's assistant panel), so every icon button in
// the app shimmers identically. Header-only and Q_OBJECT-free (no signals or
// slots of its own), so it needs no MOC.
#include <QAbstractAnimation>
#include <QAbstractItemView>
#include <QEasingCurve>
#include <QEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QRect>
#include <QVariantAnimation>
#include <QWidget>

namespace stencil::gui {

  // ── Hover "glass shimmer": a left→right light sweep played on hover — the desktop match for
  // the browser/extension CSS shimmer. Qt style sheets can't animate a sweep, so this is a
  // transparent, mouse-through child overlay that paints an animated diagonal highlight.
  // installHoverShimmer(w) attaches one to any button; it lives/dies with its target.
  class ShimmerOverlay : public QWidget {
  public:
    // Whole-widget mode: sweeps the whole target on hover-enter. View mode (view != null): sweeps
    // the hovered ROW of an item view (points/lines panel), tracked via the viewport's mouse-move.
    explicit ShimmerOverlay(QWidget* target, QAbstractItemView* view = nullptr)
        : QWidget(view ? view->viewport() : target),
          target_(view ? view->viewport() : target), view_(view) {
      // A child overlay that alpha-blends over the target. NO WA_TranslucentBackground (that's a
      // top-level-window attribute and stops a child from rendering); WA_NoSystemBackground so
      // Qt doesn't erase our area and the target shows through the un-painted (transparent) parts.
      setAttribute(Qt::WA_TransparentForMouseEvents);
      setAttribute(Qt::WA_NoSystemBackground);
      // Named + progress mirrored to a dynamic property so the GUI test can
      // assert the band genuinely ADVANCES (regression guard: a band that
      // pops in at one position and never animates).
      setObjectName(QStringLiteral("shimmerOverlay"));
      anim_ = new QVariantAnimation(this);
      anim_->setStartValue(0.0);
      anim_->setEndValue(1.0);
      anim_->setDuration(325);
      anim_->setEasingCurve(QEasingCurve::InOutSine);
      QObject::connect(anim_, &QVariantAnimation::valueChanged, this,
                       [this](const QVariant& v) { setProgress(v.toReal()); });
      QObject::connect(anim_, &QVariantAnimation::finished, this,
                       [this] { setProgress(-1.0); });
      target_->installEventFilter(this);
      if (view_) target_->setMouseTracking(true);   // so we get MouseMove without a button held
      setGeometry(target_->rect());
      raise();
      show();   // stays present (transparent); paints only while the sweep animates
    }

  protected:
    bool eventFilter(QObject* o, QEvent* e) override {
      if (o == target_) {
        switch (e->type()) {
          case QEvent::Resize:
          case QEvent::Move:
          case QEvent::Show:
            setGeometry(target_->rect());
            raise();
            break;
          case QEvent::Enter:
            if (!view_ && target_->isEnabled()) startSweep(rect());
            break;
          case QEvent::Leave:
            // Cancel the sweep the instant the cursor leaves, so a fast pass over many items
            // doesn't leave a trail of animations still playing out on already-unhovered widgets.
          case QEvent::Hide:
          case QEvent::EnabledChange:
          case QEvent::WindowDeactivate:
            // …and whenever the target hides / disables / loses its window (a
            // modal dialog opening mid-sweep): a suspended window would keep
            // the half-painted band as a frozen streak otherwise.
            cancelSweep();
            break;
          case QEvent::MouseMove:
            if (view_) {
              const QModelIndex idx = view_->indexAt(static_cast<QMouseEvent*>(e)->pos());
              const int row = idx.isValid() ? idx.row() : -1;
              if (row != hoveredRow_) {
                hoveredRow_ = row;
                if (row >= 0) {
                  QRect r = view_->visualRect(idx);
                  r.setLeft(0);
                  r.setRight(target_->width());
                  startSweep(r);
                }
              }
            }
            break;
          default:
            break;
        }
      }
      return QWidget::eventFilter(o, e);
    }
    void paintEvent(QPaintEvent*) override {
      // Paint ONLY while the sweep is actually running — a stopped animation
      // must never leave a static mid-sweep gradient behind.
      if (progress_ < 0.0 || anim_->state() != QAbstractAnimation::Running) return;
      const QRect b = band_.isEmpty() ? rect() : band_;
      if (b.width() <= 0 || b.height() <= 0) return;
      const qreal bw = b.width() * 0.5;
      const qreal cx = b.left() - bw + progress_ * (b.width() + 2 * bw);   // off-left → off-right
      QLinearGradient g(cx - bw, b.top(), cx + bw, b.bottom());            // diagonal light band
      g.setColorAt(0.0, QColor(255, 255, 255, 0));
      g.setColorAt(0.5, QColor(255, 255, 255, 95));
      g.setColorAt(1.0, QColor(255, 255, 255, 0));
      QPainter p(this);
      p.setRenderHint(QPainter::Antialiasing);
      p.fillRect(b, g);
    }

  private:
    void startSweep(const QRect& band) { band_ = band; anim_->stop(); anim_->start(); }
    void cancelSweep() {
      hoveredRow_ = -1;
      anim_->stop();
      setProgress(-1.0);
    }
    void setProgress(qreal p) {
      progress_ = p;
      setProperty("sweepProgress", p);  // observable by the GUI test
      update();
    }
    QWidget* target_;
    QAbstractItemView* view_;
    QVariantAnimation* anim_ = nullptr;
    qreal progress_ = -1.0;
    QRect band_;
    int hoveredRow_ = -1;
  };

  inline void installHoverShimmer(QWidget* target) {
    if (target && !target->property("_shimmer").toBool()) {
      target->setProperty("_shimmer", true);   // guard against double-install
      new ShimmerOverlay(target);               // parented to target
    }
  }
  inline void installRowShimmer(QAbstractItemView* view) {
    if (view && !view->property("_shimmer").toBool()) {
      view->setProperty("_shimmer", true);
      new ShimmerOverlay(nullptr, view);   // parented to the view's viewport
    }
  }

}  // namespace stencil::gui
