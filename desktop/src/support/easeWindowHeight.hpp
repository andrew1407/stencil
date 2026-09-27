#pragma once
// Ease a window to a new HEIGHT instead of snapping there. adjustSize() is a jump, and a
// dialog whose rows appear and vanish (an Assistant provider switch) reads as a flicker.
// Browser twin: js/ui/motion/easeBoxHeight.js, on the same clock.
#include "motionPrefs.hpp"

#include <QCoreApplication>
#include <QEasingCurve>
#include <QLayout>
#include <QScreen>
#include <QVariantAnimation>
#include <QWidget>

#include <algorithm>

namespace stencil::support {

  inline constexpr int WINDOW_RESIZE_MS = 380;   // openImageDialogParts.hpp OI_RESIZE_MS

  // The height a window's content wants at the width it HAS. A wrapped label's own hints are
  // taken at the layout's narrowest width, which leaves a fixed-width window too tall.
  inline int naturalHeight(const QWidget* w) {
    const QLayout* l = w ? w->layout() : nullptr;
    if (l && l->hasHeightForWidth()) return l->totalHeightForWidth(w->width());
    return w ? w->sizeHint().height() : 0;
  }

  // The same trap for the minimum, at the width it HAS.
  inline int naturalMinimumHeight(const QWidget* w) {
    const QLayout* l = w ? w->layout() : nullptr;
    if (l && l->hasHeightForWidth()) return l->totalMinimumHeightForWidth(w->width());
    return w ? w->minimumSizeHint().height() : 0;
  }

  // An explicit minimum at this width, so the layout stops imposing its narrowest-width guess.
  // Only a non-zero minimum counts as explicit to Qt.
  inline void pinWidthMinimum(QWidget* w) {
    const QLayout* l = w ? w->layout() : nullptr;
    if (l && l->hasHeightForWidth()) w->setMinimumHeight(std::max(1, naturalMinimumHeight(w)));
  }

  // Runs `w`'s own layout afresh, then every layout request still queued.
  inline void relayout(QWidget* w) {
    if (QLayout* l = w ? w->layout() : nullptr) { l->invalidate(); l->activate(); }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
  }

  // A shown top-level keeps its middle, as the browser's flex-centred .app-modal does, inside the
  // screen. `midY` is the top plus half the height, exec()'s own centring sum, fixed for the flight.
  inline void setHeightAbout(QWidget* w, int h, int midY) {
    if (!w->isWindow() || !w->isVisible()) { w->resize(w->width(), h); return; }
    int top = midY - h / 2;
    if (const QScreen* s = w->screen()) {
      const QRect avail = s->availableGeometry();
      top = std::max(std::min(top, avail.bottom() + 1 - h), avail.top());
    }
    w->setGeometry(w->geometry().x(), top, w->width(), h);
  }

  // One animation per window, restarted, so a run of changes chases the latest height. `from` is the
  // height BEFORE the change: Qt grows a window whose layout no longer fits at once, top pinned.
  inline void easeWindowHeight(QWidget* w, int to, int from = -1,
                               int ms = WINDOW_RESIZE_MS) {
    if (!w || to <= 0) return;
    to = std::max(to, naturalMinimumHeight(w));
    const int start = from > 0 ? from : w->height();
    const int midY = w->geometry().y() + start / 2;
    if (motionReduced() || !w->isVisible()) {
      pinWidthMinimum(w);
      setHeightAbout(w, to, midY);
      return;
    }
    if (start == to) return;
    // A window cannot be resized under its layout's own minimum, and Qt has ALREADY jumped it to the
    // new one, so both stand down for the flight. Mid-flight clipping IS the reveal (.app-modal too).
    if (QLayout* l = w->layout()) l->setSizeConstraint(QLayout::SetNoConstraint);
    w->setMinimumHeight(0);
    if (start != w->height()) w->resize(w->width(), start);
    static constexpr const char* ANIM = "stencilWindowHeightEase";
    static constexpr const char* MID_Y = "stencilWindowHeightMidY";
    auto* anim = w->findChild<QVariantAnimation*>(QLatin1String(ANIM),
                                                  Qt::FindDirectChildrenOnly);
    if (!anim) {
      anim = new QVariantAnimation(w);
      anim->setObjectName(QLatin1String(ANIM));
      anim->setEasingCurve(QEasingCurve::OutCubic);
      QObject::connect(anim, &QVariantAnimation::valueChanged, w, [w, anim](const QVariant& v) {
        setHeightAbout(w, v.toInt(), anim->property(MID_Y).toInt());
      });
      QObject::connect(anim, &QVariantAnimation::finished, w, [w] {
        pinWidthMinimum(w);
        if (QLayout* l = w->layout()) l->setSizeConstraint(QLayout::SetDefaultConstraint);
      });
    }
    anim->stop();
    anim->setProperty(MID_Y, midY);
    anim->setDuration(ms);
    anim->setStartValue(start);
    anim->setEndValue(to);
    anim->start();
  }

}  // namespace stencil::support
