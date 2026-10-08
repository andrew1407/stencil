#pragma once
// Hover "glass shimmer" — the desktop port of the browser's ui-shimmer rule
// (browser/css/layout/). Header-only and Q_OBJECT-free, so no MOC.
#include "modalReveal.hpp"   // support::motionReduced()

#include <QAbstractAnimation>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QLineEdit>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVariantAnimation>
#include <algorithm>

namespace stencil::gui {

  inline QEasingCurve shimmerEase() {   // the browser's `ease`: no slow start to sit out
    QEasingCurve e(QEasingCurve::BezierSpline);
    e.addCubicBezierSegment(QPointF(0.25, 0.1), QPointF(0.25, 1.0), QPointF(1.0, 1.0));
    return e;
  }

  // A row's band is the browser's ::after sweep (layout/shimmer.css ui-shimmer, IdleCard's too): a 0.9w
  // diagonal lit over its middle 24%, carried from 1.35w off the left edge to past the right as t runs 0→1.
  struct ShimmerSpan { qreal x0; qreal x1; };
  inline ShimmerSpan shimmerSweepSpan(const QRectF& box, qreal t) {
    const qreal x = box.left() - 1.35 * box.width() + t * 2.7 * box.width();
    return {x, x + 0.9 * box.width()};
  }
  inline constexpr int SHIMMER_ROW_MS = 750;   // the browser rows' ui-shimmer-row clock

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
          target(view ? view->viewport() : target), view(view),
          externalBands(externalBands) {
      // NO WA_TranslucentBackground (a top-level attribute that stops a child rendering);
      // WA_NoSystemBackground so the target shows through the unpainted parts.
      setAttribute(Qt::WA_TransparentForMouseEvents);
      setAttribute(Qt::WA_NoSystemBackground);
      setObjectName(QStringLiteral("shimmerOverlay"));
      anim = new QVariantAnimation(this);
      anim->setKeyValues({{0.0, 0.0}, {1.0, 1.0}});
      anim->setDuration(view ? SHIMMER_ROW_MS : 325);
      anim->setEasingCurve(shimmerEase());
      QObject::connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { setProgress(v.toReal()); });
      QObject::connect(anim, &QVariantAnimation::finished, this, [this] { setProgress(-1.0); });
      this->target->installEventFilter(this);
      if (this->view) this->target->setMouseTracking(true);   // so we get MouseMove without a button held
      else if (!externalBands) this->target->setAttribute(Qt::WA_Hover);   // hover moves back a late Enter
      setGeometry(this->target->rect());
      raise();
      show();   // stays present (transparent); paints only while the sweep animates
    }

  protected:
    bool eventFilter(QObject* o, QEvent* e) override {
      if (o == target) {
        switch (e->type()) {
          case QEvent::Resize: case QEvent::Move: case QEvent::Show:
            setGeometry(target->rect());
            raise();
            break;
          // One sweep per hover-in, from whichever of these lands first with the pointer inside: a
          // late Enter from elsewhere replays nothing, and a hover move stands in for a missing one.
          case QEvent::Enter:
          case QEvent::HoverEnter:
          case QEvent::HoverMove:
            if (!view && !externalBands && !hovered && target->isEnabled() && pointerInside(e)) {
              hovered = true;
              startSweep(rect());
            }
            break;
          case QEvent::Leave:
          case QEvent::HoverLeave:
            // Cancel the instant the cursor leaves, or a fast pass leaves a trail of sweeps.
          case QEvent::Hide:
            hovered = false;
            cancelSweep();
            break;
          case QEvent::EnabledChange:
          case QEvent::WindowDeactivate:
            // …and on disable / window loss: a suspended window would keep a frozen streak.
            // NOT in external-band mode — a popup menu's activation churn is not a hover-out.
            if (!externalBands) cancelSweep();
            break;
          case QEvent::MouseMove:
            if (view) {
              const QModelIndex idx = view->indexAt(static_cast<QMouseEvent*>(e)->pos());
              const int row = idx.isValid() ? idx.row() : -1;
              if (row != hoveredRow) {
                hoveredRow = row;
                if (row >= 0) {
                  QRect r = view->visualRect(idx);
                  r.setLeft(0);
                  r.setRight(target->width());
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
      if (progress < 0.0 || anim->state() != QAbstractAnimation::Running) return;
      const QRect b = band.isEmpty() ? rect() : band;
      if (b.width() <= 0 || b.height() <= 0) return;
      QPainter p(this);
      p.setRenderHint(QPainter::Antialiasing);
      if (view) {   // square rows: no clip, the browser tbody band's 24% white
        const ShimmerSpan s = shimmerSweepSpan(QRectF(b), progress);
        QLinearGradient g(s.x0, b.top(), s.x1, b.bottom());
        g.setColorAt(0.38, QColor(255, 255, 255, 0));
        g.setColorAt(0.50, QColor(255, 255, 255, 61));
        g.setColorAt(0.62, QColor(255, 255, 255, 0));
        p.fillRect(b, g);
        return;
      }
      const qreal bw = b.width() * 0.5;
      const qreal cx = b.left() - bw + progress * (b.width() + 2 * bw);   // off-left → off-right
      QLinearGradient g(cx - bw, b.top(), cx + bw, b.bottom());            // diagonal light band
      g.setColorAt(0.0, QColor(255, 255, 255, 0));
      g.setColorAt(0.5, QColor(255, 255, 255, 95));
      g.setColorAt(1.0, QColor(255, 255, 255, 0));
      const QVariant own = target->property(SHIMMER_RADIUS_PROPERTY);
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
    void holdHover() { hovered = true; }   // the pointer never left: a list it opened had grabbed it

  private:
    bool pointerInside(QEvent* e) const {   // a bare Enter is its own word
      const auto* at = dynamic_cast<QSinglePointEvent*>(e);
      return !at || QRectF(target->rect()).adjusted(-1, -1, 1, 1).contains(at->position());
    }
    // Reduced motion: no sweep at all — pure feedback with no end state (faceSwap / filterFade rule).
    void startSweep(const QRect& band) {
      if (support::motionReduced()) return;
      this->band = band;
      if (view) raise();   // cell widgets a row rebuild added since would otherwise cover the band
      anim->stop();
      setProgress(0.0);   // the first frame now, not at the animation's first tick
      anim->start();
    }
    void cancelSweep() {
      hoveredRow = -1;
      anim->stop();
      setProgress(-1.0);
    }
    void setProgress(qreal p) {
      progress = p;
      setProperty("sweepProgress", p);  // observable by the GUI test
      update();
    }
    QWidget* target;
    QAbstractItemView* view;
    bool externalBands = false;
    QVariantAnimation* anim = nullptr;
    qreal progress = -1.0;
    QRect band;
    int hoveredRow = -1;
    bool hovered = false;   // the pointer is over the whole-widget target: its one sweep played
  };

  inline ShimmerOverlay* installHoverShimmer(QWidget* target) {
    if (!target || target->property("_shimmer").toBool()) return nullptr;
    target->setProperty("_shimmer", true);   // guard against double-install
    return new ShimmerOverlay(target);        // parented to target
  }
  inline void installRowShimmer(QAbstractItemView* view) {
    if (!view || view->property("_shimmer").toBool()) return;
    view->setProperty("_shimmer", true);
    new ShimmerOverlay(nullptr, view);   // parented to the view's viewport
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
    if (root) QTimer::singleShot(0, root, [root] { installHoverShimmerIn(root); });
  }

}  // namespace stencil::gui
