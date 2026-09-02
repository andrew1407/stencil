#pragma once
// The canvas ↔ points-panel separator grip — the desktop take on the browser's
// .panel-resizer::before (layout.css): a slim centred bar that turns accent and
// grows under the cursor. The separator itself is QMainWindow chrome (no widget,
// and QSS cannot animate an image swap — the old static swap read as a harsh
// jump, user report), so the grip is painted by this mouse-transparent overlay
// positioned over the separator; MainWindow's app-wide eventFilter drives the
// hot state from the hover/drag events the QMainWindow receives for the
// separator, on the same lerp the chat composer's PillSplitterHandle uses.
//
// Header-only and Q_OBJECT-free (no signals/slots of its own), so it needs no MOC.
#include "modalReveal.hpp"   // support::motionReduced()

#include <QColor>
#include <QEasingCurve>
#include <QPainter>
#include <QVariantAnimation>
#include <QWidget>

#include <cmath>

namespace stencil::gui {

  class DockGripOverlay : public QWidget {
   public:
    // The browser's exact numbers (.panel-resizer::before): a 3px bar, 34px at rest
    // in the hairline grey, 52px and accent hovered/dragged, radius 2 — with the
    // 120ms grow/tint ease the composer pill uses (the browser transitions 0.15s).
    static constexpr double kBarW = 3.0;
    static constexpr double kBarH = 34.0;
    static constexpr double kBarHotH = 52.0;
    static constexpr int kMs = 150;

    explicit DockGripOverlay(QWidget* parent) : QWidget(parent) {
      // Purely decorative: the separator under it keeps the drag.
      setAttribute(Qt::WA_TransparentForMouseEvents, true);
      setAttribute(Qt::WA_NoSystemBackground, true);
      anim_ = new QVariantAnimation(this);
      anim_->setDuration(kMs);
      anim_->setEasingCurve(QEasingCurve::OutCubic);
      QObject::connect(anim_, &QVariantAnimation::valueChanged, this,
                       [this](const QVariant& v) {
                         hot_ = v.toDouble();
                         update();
                       });
    }

    void setColors(const QColor& rest, const QColor& accent) {
      rest_ = rest;
      accent_ = accent;
      update();
    }

    void setHot(bool on) {
      const double to = on ? 1.0 : 0.0;
      if (qFuzzyCompare(hot_, to)) return;
      if (support::motionReduced()) {
        anim_->stop();
        hot_ = to;
        update();
        return;
      }
      anim_->stop();
      anim_->setStartValue(hot_);
      anim_->setEndValue(to);
      anim_->start();
    }

   protected:
    void paintEvent(QPaintEvent*) override {
      QPainter p(this);
      p.setRenderHint(QPainter::Antialiasing, true);
      const auto ch = [this](int a, int b) { return int(std::lround(a + (b - a) * hot_)); };
      const QColor c(ch(rest_.red(), accent_.red()), ch(rest_.green(), accent_.green()),
                     ch(rest_.blue(), accent_.blue()));
      const double h = kBarH + (kBarHotH - kBarH) * hot_;
      p.setPen(Qt::NoPen);
      p.setBrush(c);
      p.drawRoundedRect(QRectF((width() - kBarW) / 2.0, (height() - h) / 2.0, kBarW, h), 2, 2);
    }

   private:
    QVariantAnimation* anim_ = nullptr;
    double hot_ = 0.0;
    QColor rest_;
    QColor accent_;
  };

}  // namespace stencil::gui
