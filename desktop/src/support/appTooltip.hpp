#pragma once
// The app's own control tooltip — the desktop port of #app-tooltip in
// browser/css/components.css, which fades over 90 ms instead of snapping.
//
// Qt's tooltip is a private QTipLabel: QSS has no transitions and there is no supported
// hook to animate the label Qt shows, so QEvent::ToolTip is swallowed app-wide and this
// frameless panel is shown in its place. Qt still owns the TIMING (SH_ToolTip_WakeUpDelay,
// pinned at 200 ms in main.cpp) and the content is still tipContent's rendering: only the
// motion changes — the fade, plus one shake of the KEYCAPS as a tip carrying them appears
// (browser/extension: .tip-key.key-shake). The panel itself never moves.
//
// Only a widget with its OWN non-empty toolTip() is taken over; item views resolve
// per-index tooltips inside viewportEvent, so those keep Qt's path untouched.
//
// Header-only and Q_OBJECT-free (no signals/slots of its own), so it needs no MOC.
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

#include "disintegrateOverlay.hpp"   // the shared surface dust
#include "modalReveal.hpp"   // support::motionReduced()
#include "tipContent.hpp"    // enrichedToolTip(), hasKeycaps(), blankKeycaps()

namespace stencil::gui {

  // The tooltip's body — the rendered rich text, and the keycaps' shake.
  //
  // The caps are painted PNGs inline in that ONE rich-text label (tipContent), not widgets,
  // so no layout can move them. They are LOCATED instead: the label is rendered twice, as
  // it is and with the cap faces blanked in boxes of the same size, and the pixels that
  // differ are the caps — wherever Qt's layout put them, caps mid-prose included. Each is
  // then blitted back at an offset with its resting slot clipped out. At rest the paint is
  // QLabel's own, untouched, so the settled tooltip renders exactly as it always did.
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
    // Where the caps are along the flick, 0 at rest — what the tests watch.
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
    QList<QRect> caps_;      // the caps' resting slots
    QList<QPixmap> pieces_;  // each cap, cut out of the settled render
    QPixmap flat_;           // the whole settled render
    bool hunted_ = false;
    int dx_ = 0;
    double deg_ = 0;
  };

  class AppTooltip : public QFrame {
   public:
    static constexpr int kFadeMs = 90;      // browser: #app-tooltip transition (the fallback)
    static constexpr int kMaxTipWidth = 380;   // browser: #app-tooltip max-width
    static constexpr int kShakeMs = 320;    // browser: keycapShake 0.32s, one per appearance
                                            // (TipBody holds its steps — the CAPS move, not this)
    // The tooltip is sand too (browser js/ui/controlTooltip.js)
    // It forms from motes streaming out of the control it describes and comes apart into
    // motes pouring back into it — on the shared tip clock (disintegrateOverlay.hpp):
    // short, so a flight is over before a toolbar sweep reaches the next control.
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

    // The offset the caps are at, for tests: 0 when settled. The panel never moves.
    int shakeOffset() const { return body_->capOffset(); }
    int keycapsShown() const { return body_->capCount(); }
    bool shaking() const { return shake_ && shake_->state() == QAbstractAnimation::Running; }
    // Queued, but holding until the tip's own motes have landed — see showFor().
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
    QRect origin_;   // where the dust forms out of (global); invalid = the owner's centre
  };

  // The app-wide filter that hands QEvent::ToolTip to AppTooltip. Q_OBJECT-free for the
  // same reason as the panel: it only overrides eventFilter.
  class AppTooltipFilter : public QObject {
   public:
    explicit AppTooltipFilter(QObject* parent = nullptr) : QObject(parent) {}

    AppTooltip* tip();

   protected:
    bool eventFilter(QObject* o, QEvent* e) override;

   private:
    void dismiss();

    AppTooltip* tip_ = nullptr;
    QPointer<QWidget> tracked_;   // the one widget this tip switched tracking on for
  };

  AppTooltipFilter* installAppTooltips();

  AppTooltip* appTooltip();

}  // namespace stencil::gui
