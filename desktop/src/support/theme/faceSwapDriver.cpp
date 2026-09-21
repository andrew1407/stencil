#include "faceSwap.hpp"

namespace stencil::gui {

  // A superseded animation is stopped and deleteLater'd; RUNNING is the question, not there.
  bool faceSwapping(const QAbstractButton* btn) {
    if (!btn) return false;
    for (QVariantAnimation* a : btn->findChildren<QVariantAnimation*>(
             QString::fromLatin1(FACE_SWAP_ANIM_NAME), Qt::FindDirectChildrenOnly))
      if (a->state() == QAbstractAnimation::Running) return true;
    return false;
  }

  // Undoes something else's meddling (a QToolButton re-copies its action's icon on every
  // ActionChanged); never touches the state, keeps its hands off a running swap.
  void repaintFace(QAbstractButton* btn) {
    if (!btn || faceSwapping(btn)) return;
    bool known = false;
    const FaceSpec painted = detail::paintedFace(btn, &known);
    if (!known) return;
    btn->setIcon(themedIcon(painted.glyph, painted.glyphColor,
                            std::max(1, painted.iconSize)));
    if (!painted.label.isNull()) btn->setText(painted.label);
  }

  // `applyState` is the caller's flip, run ONCE at the pivot. It must SET the state absolutely, never
  // toggle: a superseded swap's applyState is what the button ends on. Reduced motion goes to `to`.
  void swapFace(QAbstractButton* btn, const FaceSpec& to,
                const std::function<void()>& applyState, int ms) {
    if (!btn || to.glyph.isEmpty()) return;
    if (!btn->property(FACE_BASE_SHEET_PROPERTY).isValid())
      btn->setProperty(FACE_BASE_SHEET_PROPERTY, btn->styleSheet());
    for (QVariantAnimation* old : btn->findChildren<QVariantAnimation*>(
             QString::fromLatin1(FACE_SWAP_ANIM_NAME), Qt::FindDirectChildrenOnly)) {
      old->stop();   // a mid-flight stop() never emits finished()
      old->deleteLater();
    }
    bool known = false;
    const FaceSpec from = detail::paintedFace(btn, &known);
    if (ms <= 0 || !known || !btn->isVisible() || support::motionReduced()) {
      if (applyState) applyState();
      detail::settleFace(btn, to);
      return;
    }
    auto applied = std::make_shared<bool>(false);
    auto* anim = new QVariantAnimation(btn);
    anim->setObjectName(QString::fromLatin1(FACE_SWAP_ANIM_NAME));
    anim->setDuration(ms);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    // Linear — the shaping lives in faceSwapFrame's two curves.
    QObject::connect(anim, &QVariantAnimation::valueChanged, btn,
                     [btn, from, to, applyState, applied](const QVariant& v) {
                       const FaceSwapFrame fr = faceSwapFrame(v.toDouble());
                       if (fr.incoming && !*applied) {
                         *applied = true;
                         if (applyState) applyState();
                       }
                       detail::paintFace(btn, fr.incoming ? to : from, fr);
                     });
    QObject::connect(anim, &QVariantAnimation::finished, btn, [btn, to, applyState, applied] {
      if (!*applied) {
        *applied = true;
        if (applyState) applyState();
      }
      detail::settleFace(btn, to);
    });
    detail::paintFace(btn, from, faceSwapFrame(0.0));   // the old face, still at rest
    anim->start(QAbstractAnimation::DeleteWhenStopped);
  }

}  // namespace stencil::gui
