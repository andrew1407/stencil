#pragma once
// Ease a window to a new HEIGHT instead of snapping there. adjustSize() is a jump, and a
// dialog whose rows appear and vanish (an Assistant provider switch) reads as a flicker.
// Browser twin: js/ui/motion/easeBoxHeight.js, on the same clock.
#include "motionPrefs.hpp"

#include <QEasingCurve>
#include <QLayout>
#include <QVariantAnimation>
#include <QWidget>

#include <algorithm>

namespace stencil::support {

  inline constexpr int WINDOW_RESIZE_MS = 380;   // openImageDialogParts.hpp OI_RESIZE_MS

  // One animation per window, restarted, so a run of changes chases the latest height. `from` is the
  // height BEFORE the change: Qt grows a window whose layout no longer fits at once.
  inline void easeWindowHeight(QWidget* w, int to, int from = -1,
                               int ms = WINDOW_RESIZE_MS) {
    if (!w || to <= 0) return;
    to = std::max(to, w->minimumSizeHint().height());
    if (motionReduced() || !w->isVisible()) {
      w->resize(w->width(), to);
      return;
    }
    const int start = from > 0 ? from : w->height();
    if (start == to) return;
    // A window cannot be resized under its layout's own minimum, and Qt has ALREADY jumped it to the
    // new one, so both stand down for the flight. Mid-flight clipping IS the reveal (.app-modal too).
    if (QLayout* l = w->layout()) l->setSizeConstraint(QLayout::SetNoConstraint);
    w->setMinimumHeight(0);
    if (start != w->height()) w->resize(w->width(), start);
    static constexpr const char* ANIM = "stencilWindowHeightEase";
    auto* anim = w->findChild<QVariantAnimation*>(QLatin1String(ANIM),
                                                  Qt::FindDirectChildrenOnly);
    if (!anim) {
      anim = new QVariantAnimation(w);
      anim->setObjectName(QLatin1String(ANIM));
      anim->setEasingCurve(QEasingCurve::OutCubic);
      QObject::connect(anim, &QVariantAnimation::valueChanged, w,
                       [w](const QVariant& v) { w->resize(w->width(), v.toInt()); });
      QObject::connect(anim, &QVariantAnimation::finished, w, [w] {
        if (QLayout* l = w->layout()) l->setSizeConstraint(QLayout::SetDefaultConstraint);
      });
    }
    anim->stop();
    anim->setDuration(ms);
    anim->setStartValue(start);
    anim->setEndValue(to);
    anim->start();
  }

}  // namespace stencil::support
