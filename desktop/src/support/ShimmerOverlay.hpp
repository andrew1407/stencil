#pragma once
// Hover "glass shimmer" — the desktop port of the browser's ui-shimmer rule
// (browser/css/layout.css). Header-only and Q_OBJECT-free, so no MOC.
#include "modalReveal.hpp"   // support::motionReduced()

#include <QAbstractAnimation>
#include <QAbstractItemView>
#include <QEasingCurve>
#include <QEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QRect>
#include <QVariantAnimation>
#include <QAbstractButton>
#include <QComboBox>
#include <QLineEdit>
#include <QAbstractSpinBox>
#include <QTimer>
#include <QWidget>

#include <algorithm>

namespace stencil::gui {

  // C++ cannot read a QSS border-radius back, so the common control radius is the default
  // and anything rounder (the Close pill, the Controls pill) carries its own here.
  inline constexpr const char* SHIMMER_RADIUS_PROPERTY = "_shimmerRadius";
  inline constexpr int SHIMMER_RADIUS = 7;

  // A transparent, mouse-through child overlay; it lives/dies with its target.
  class ShimmerOverlay : public QWidget {
  public:
    // Whole-widget mode sweeps the target; view mode sweeps the hovered ROW of an item
    // view; external-band mode lets the owner drive sweepBand() (MenuShimmer.hpp).
    explicit ShimmerOverlay(QWidget* target, QAbstractItemView* view = nullptr,
                            bool externalBands = false)
        : QWidget(view ? view->viewport() : target),
          target_(view ? view->viewport() : target), view_(view),
          externalBands_(externalBands) {
      // NO WA_TranslucentBackground (a top-level attribute that stops a child rendering);
      // WA_NoSystemBackground so the target shows through the unpainted parts.
      setAttribute(Qt::WA_TransparentForMouseEvents);
      setAttribute(Qt::WA_NoSystemBackground);
      // Progress mirrored to a dynamic property so the GUI test can assert the band ADVANCES.
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
            if (!view_ && !externalBands_ && target_->isEnabled()) startSweep(rect());
            break;
          case QEvent::Leave:
            // Cancel the instant the cursor leaves, or a fast pass leaves a trail of sweeps.
          case QEvent::Hide:
            cancelSweep();
            break;
          case QEvent::EnabledChange:
          case QEvent::WindowDeactivate:
            // …and on disable / window loss: a suspended window would keep a frozen streak.
            // NOT in external-band mode — a popup menu's activation churn is not a hover-out.
            if (!externalBands_) cancelSweep();
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
      // Paint ONLY while running — a stopped animation must not leave a static band.
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
      const QVariant own = target_->property(SHIMMER_RADIUS_PROPERTY);
      const qreal r = std::min<qreal>(own.isValid() ? own.toReal() : SHIMMER_RADIUS,
                                      std::min(b.width(), b.height()) / 2.0);
      if (r > 0.5) {
        QPainterPath clip;
        clip.addRoundedRect(QRectF(b), r, r);
        p.setClipPath(clip);
      }
      p.fillRect(b, g);
    }

  public:
    void sweepBand(const QRect& band) { startSweep(band); }
    void cancel() { cancelSweep(); }

  private:
    // Reduced motion: no sweep at all — pure feedback with no end state (faceSwap / filterFade rule).
    void startSweep(const QRect& band) {
      if (support::motionReduced()) return;
      band_ = band;
      anim_->stop();
      anim_->start();
    }
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
    bool externalBands_ = false;
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

  // A control opts out with this property.
  inline constexpr const char* NO_SHIMMER_PROPERTY = "_noShimmer";

  // Every control under `root` that the browser sweeps. installHoverShimmer is guarded, so
  // calling this twice on the same tree costs nothing.
  inline void installHoverShimmerIn(QWidget* root) {
    if (!root) return;
    // ONE walk: findChildren<T*> per type is its own recursive descent, and this runs per row rebuild.
    for (QWidget* w : root->findChildren<QWidget*>()) {
      if (w->property(NO_SHIMMER_PROPERTY).toBool()) continue;
      if (qobject_cast<QAbstractButton*>(w) || qobject_cast<QComboBox*>(w) ||
          qobject_cast<QLineEdit*>(w) || qobject_cast<QAbstractSpinBox*>(w))
        installHoverShimmer(w);
    }
  }

  // …deferred a turn, for a window whose content is added AFTER this is called.
  inline void installHoverShimmerLater(QWidget* root) {
    if (!root) return;
    QTimer::singleShot(0, root, [root] { installHoverShimmerIn(root); });
  }

}  // namespace stencil::gui
