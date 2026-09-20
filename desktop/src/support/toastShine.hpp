#pragma once
// The golden shining a logo show's notice wears (browser js/ui/toastGlow.js + .notify-shine): the
// same for every show — the colour of the secret, not of the theme — following the pill's own
// rounded rectangle. A mouse-through child of the HOST, so the halo reaches past the pill's edges.
#include <QColor>
#include <QElapsedTimer>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPainter>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <cmath>

#include "dustKit.hpp"           // frameIntervalMs
#include "logoStageRules.hpp"    // the beat and the gold
#include "modalReveal.hpp"       // motionReduced

namespace stencil::support {

  class ToastShine : public QWidget {
   public:
    static constexpr int MARGIN = 30;      // room around the pill for the halo
    static constexpr int RINGS = 8;        // expanding outlines stand in for a blur (IdleCard's trick)
    static constexpr double REACH = 17;    // px the halo reaches past the pill at full breath
    static constexpr double ALPHA = 0.3;   // the innermost ring at full breath
    static constexpr double RADIUS = 6;    // the pill's own corner

    explicit ToastShine(QWidget* toast) : QWidget(toast->parentWidget()), toast_(toast) {
      setObjectName("toastShine");
      setAttribute(Qt::WA_TransparentForMouseEvents);
      setAttribute(Qt::WA_NoSystemBackground);
      // The pill's OWN move/resize places the halo, so a window resize never leaves it a frame
      // behind at the pill's last position. Polling it on the clock did exactly that.
      toast_->installEventFilter(this);
      connect(toast_, &QObject::destroyed, this, &QObject::deleteLater);
      follow();
      if (!motionReduced()) {
        auto* clock = new QTimer(this);
        clock->setTimerType(Qt::PreciseTimer);
        connect(clock, &QTimer::timeout, this, [this] { tick(); });
        clock->start(frameIntervalMs(this));
      }
      since_.restart();
      stackUnder(toast_);   // just under the pill: a halo round it, never over the words
      show();
    }

   protected:
    bool eventFilter(QObject* o, QEvent* e) override {
      if (o == toast_)
        switch (e->type()) {
          case QEvent::Move:
          case QEvent::Resize:
          case QEvent::Show:
            follow();
            [[fallthrough]];
          case QEvent::ZOrderChange:
            stackUnder(toast_);   // the stack raises its toasts as they arrive; stay just beneath
            break;
          case QEvent::Hide:
            hide();
            break;
          default:
            break;
        }
      return QWidget::eventFilter(o, e);
    }

    void paintEvent(QPaintEvent*) override {
      if (!toast_) return;
      const LogoStageConfig& cfg = logoStageConfig();
      const double beat = motionReduced()
          ? 0.5
          : 0.5 - 0.5 * std::cos((2 * 3.14159265358979323846 * since_.elapsed()) / cfg.beatMs);
      QPainter p(this);
      p.setRenderHint(QPainter::Antialiasing);
      p.setBrush(Qt::NoBrush);
      // The pill rides in and out on its own opacity effect; the halo wears it too, or it would
      // hang lit in mid-air before the notice had arrived (browser toastGlow.js does the same).
      p.setOpacity(toastOpacity());
      const QColor gold(cfg.toastGlow);
      const QRectF pill(MARGIN, MARGIN, toast_->width(), toast_->height());
      const double reach = REACH * (0.45 + 0.55 * beat);
      for (int i = RINGS; i >= 1; --i) {
        const double t = double(i) / RINGS;
        const double grow = reach * t;
        QColor c = gold;
        c.setAlphaF(ALPHA * (1 - t) * (0.5 + 0.5 * beat));
        p.setPen(QPen(c, (reach / RINGS) * 2, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
        p.drawRoundedRect(pill.adjusted(-grow, -grow, grow, grow), RADIUS + grow, RADIUS + grow);
      }
    }

   private:
    double toastOpacity() const {
      auto* fx = toast_ ? qobject_cast<QGraphicsOpacityEffect*>(toast_->graphicsEffect()) : nullptr;
      return fx ? fx->opacity() : 1.0;
    }
    void follow() {
      if (toast_) setGeometry(toast_->geometry().adjusted(-MARGIN, -MARGIN, MARGIN, MARGIN));
    }
    void tick() {
      if (!toast_) { deleteLater(); return; }
      update();   // the breath only: the filter above owns where the halo sits
    }

    QPointer<QWidget> toast_;
    QElapsedTimer since_;
  };

  inline void installToastShine(QLabel* toast) {
    if (toast) new ToastShine(toast);
  }

}  // namespace stencil::support
