#include "chatDockShared.hpp"

#include "../support/scrollReveal.hpp"        // ENTERING_PROPERTY — the entrance claims the effect
#include "../support/ShimmerOverlay.hpp"  // the shared hover sweep on every ghost button

#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QImage>
#include <QImageReader>
#include <QObject>
#include <QPointer>
#include <QStyle>
#include <QToolButton>
#include <QVariantAnimation>
#include <QWidget>

namespace stencil::gui::chatdock {

  // Sink + dissolve `w` in place, then delete it. The snapshot the scatter is made
  // of was taken already, so the two play together exactly as they do in the
  // browser: the bubble melts into its own dust rather than vanishing first.
  void fadeOutAndDelete(QWidget* w) {
    if (!w) return;
    // CLAIM the card's effect for the length of the fade. ScrollReveal installs its
    // own DissolveEffect on any card near a viewport edge, and setGraphicsEffect
    // DELETES the one already there — so a card that scrolled while it faded had the
    // effect this animation writes to freed under it, and the next frame crashed in
    // QGraphicsOpacityEffect::setOpacity. The entrance animation claims it the same
    // way (animateCardIn), which is why ScrollReveal skips those.
    w->setProperty(ScrollReveal::ENTERING_PROPERTY, true);
    // A card removed mid-entrance still has its appear animation running, and that
    // animation writes to the effect it installed. setGraphicsEffect() DELETES the
    // old effect, so installing a fresh one left the entrance holding a dangling
    // pointer — a use-after-free that crashed on the very next frame. Stop the
    // card's own animations first and REUSE whatever effect is already there.
    for (QVariantAnimation* a : w->findChildren<QVariantAnimation*>()) a->stop();
    auto* fx = qobject_cast<QGraphicsOpacityEffect*>(w->graphicsEffect());
    if (!fx) {
      fx = new QGraphicsOpacityEffect(w);
      w->setGraphicsEffect(fx);   // the widget owns the effect
    }
    const double from = fx->opacity();
    fx->setOpacity(from);
    auto* anim = new QVariantAnimation(w);
    anim->setDuration(CHAT_LEAVE_MS);
    anim->setStartValue(from);   // continue from wherever the entrance got to
    anim->setEndValue(0.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    // QPointer, not a raw capture: belt and braces for the same hazard — if anything
    // ever replaces the effect mid-flight again, the write is skipped, not fatal.
    QPointer<QGraphicsOpacityEffect> fxp(fx);
    QObject::connect(anim, &QVariantAnimation::valueChanged, w,
                     [fxp](const QVariant& v) { if (fxp) fxp->setOpacity(v.toDouble()); });
    QObject::connect(anim, &QVariantAnimation::finished, w, [w] {
      w->hide();
      w->deleteLater();
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
  }

  QToolButton* makeGhostButton(QWidget* parent, const QString& tooltip) {
    auto* b = new QToolButton(parent);
    b->setAutoRaise(true);
    b->setToolButtonStyle(Qt::ToolButtonIconOnly);
    b->setIconSize(QSize(HEADER_ICON, HEADER_ICON));
    b->setFixedSize(BUTTON_EDGE, BUTTON_EDGE);
    b->setToolTip(tooltip);
    b->setCursor(Qt::PointingHandCursor);
    // The browser shimmers every <button>, chat controls included (layout.css
    // ui-shimmer); this is the one factory behind ALL of them — title-bar
    // ghosts, the composer's send/attach/gear, the cards' Resend — so the
    // sweep lands on each exactly once. Mouse-through, so nothing about the
    // click target or the disabled styling changes.
    installHoverShimmer(b);
    return b;
  }

  // Re-run the stylesheet for a widget whose objectName just changed (Qt matches
  // selectors at polish time, not on every paint).
  void repolish(QWidget* w) {
    if (!w || !w->style()) return;
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
  }

  // EXIF-aware file decode shared by the attach dialog and paste/drop routing.
  QImage readImageFile(const QString& path) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    return reader.read();
  }

}  // namespace stencil::gui::chatdock
