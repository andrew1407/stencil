#pragma once
// The app's own tooltip — port of #app-tooltip in browser/css/components.css. Qt's
// QTipLabel cannot be animated, so QEvent::ToolTip is swallowed app-wide; Qt keeps the
// timing (SH_ToolTip_WakeUpDelay in main.cpp). Item views keep Qt's path. Q_OBJECT-free.
#include <QApplication>
#include <QCursor>
#include <QEasingCurve>
#include <QEvent>
#include <QFrame>
#include <QHelpEvent>
#include <QImage>
#include <QDeadlineTimer>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLabel>
#include <QList>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QRegion>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QtGlobal>

#include <vector>

#include "disintegrateOverlay.hpp"
#include "modalReveal.hpp"
#include "tipContent.hpp"

namespace stencil::gui {

  // The keycaps are inline PNGs in one rich-text label, so they are LOCATED by rendering
  // twice (as is, and with the faces blanked) and diffing; each is blitted back offset.
  // At rest the paint is QLabel's own.
  class TipBody : public QLabel {
   public:
    // browser: @keyframes keycapShake (css/components.css) — one damped left/right flick.
    static constexpr int kStops = 6;
    static constexpr double kStopT[kStops] = {0.0, 0.15, 0.38, 0.62, 0.84, 1.0};
    static constexpr double kStopX[kStops] = {0.0, -3.0, 3.0, -2.0, 2.0, 0.0};
    static constexpr double kStopDeg[kStops] = {0.0, -3.0, 3.0, -2.0, 1.5, 0.0};

    explicit TipBody(QWidget* parent = nullptr) : QLabel(parent) {}

    void setTip(const QString& rich);

    int capCount();
    // 0 at rest — what the tests watch.
    int capOffset() const { return dx_; }

    void setShake(double t);
    void settle() { setShake(0.0); }

   protected:
    void paintEvent(QPaintEvent* e) override;

   private:
    static const QEasingCurve& ease() {  // browser: cubic-bezier(0.36, 0.07, 0.19, 0.97)
      static const QEasingCurve c = [] {
        QEasingCurve e(QEasingCurve::BezierSpline);
        e.addCubicBezierSegment(QPointF(0.36, 0.07), QPointF(0.19, 0.97), QPointF(1, 1));
        return e;
      }();
      return c;
    }

    void findCaps();
    QPixmap cut(const QRect& r, qreal dpr) const;

    QString tip_;
    QList<QRect> caps_;
    QList<QPixmap> pieces_;
    QPixmap flat_;
    bool hunted_ = false;
    int dx_ = 0;
    double deg_ = 0;
  };

  class AppTooltip : public QFrame {
   public:
    static constexpr int kFadeMs = 90;      // browser: #app-tooltip transition (the fallback)
    static constexpr int kMaxTipWidth = 380;   // browser: #app-tooltip max-width
    static constexpr int kShakeMs = 320;    // browser: keycapShake 0.32s, one per appearance
    // The tooltip is sand too (browser js/ui/controlTooltip.js), on the shared tip clock.
    static constexpr int kDustInMs = kTipDustInMs;
    static constexpr int kDustOutMs = kTipDustOutMs;
    static constexpr int kDustHandOverMs = gui::kDustHandOverMs;
    static constexpr int kGap = 15;         // cursor offset, as Qt's own tooltip uses
    static constexpr const char* kObjectName = "stencilAppTooltip";

    explicit AppTooltip(QWidget* parent = nullptr);

    QWidget* owner() const { return owner_.data(); }

    void showFor(QWidget* owner, const QString& text, const QPoint& globalPos,
                 const QRect& originGlobal = QRect());

    void moveTo(const QPoint& globalPos);

    void hideTip();

    void shakeKeys();

    int shakeOffset() const { return body_->capOffset(); }
    int keycapsShown() const { return body_->capCount(); }
    bool shaking() const { return shake_ && shake_->state() == QAbstractAnimation::Running; }
    bool shakePending() const { return shakeDelay_ && shakeDelay_->isActive(); }
    bool fadingOut() const { return closing_; }

   private:
    bool dust(bool gather);

    void place(const QPoint& cursor);
    void settleShake();

    QTimer* shakeDelay();

    TipBody* body_ = nullptr;
    QVariantAnimation* fade_ = nullptr;
    QVariantAnimation* shake_ = nullptr;
    QTimer* shakeDelay_ = nullptr;
    QPointer<QWidget> owner_;
    bool closing_ = false;
    QDeadlineTimer placeHold_{0};   // moveTo is refused until this lapses (the gather)
    QRect origin_;   // global; invalid = the owner's centre
  };

  class AppTooltipFilter : public QObject {
   public:
    explicit AppTooltipFilter(QObject* parent = nullptr) : QObject(parent) {}

    AppTooltip* tip();

   protected:
    bool eventFilter(QObject* o, QEvent* e) override;

   private:
    void dismiss();

    AppTooltip* tip_ = nullptr;
    QPointer<QWidget> tracked_;   // the widget this tip switched mouse tracking on for
  };

  AppTooltipFilter* installAppTooltips();

  AppTooltip* appTooltip();

}  // namespace stencil::gui
