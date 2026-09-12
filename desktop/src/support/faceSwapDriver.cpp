#include "faceSwap.hpp"

namespace stencil::gui {

  // True while `btn` is mid-exchange. A superseded animation is stopped and deleteLater'd,
  // so it can still be a child for a turn of the loop — RUNNING is the question, not there.
  bool faceSwapping(const QAbstractButton* btn) {
    if (!btn) return false;
    for (QVariantAnimation* a : btn->findChildren<QVariantAnimation*>(
             QString::fromLatin1(kFaceSwapAnimName), Qt::FindDirectChildrenOnly))
      if (a->state() == QAbstractAnimation::Running) return true;
    return false;
  }

  // Put the REMEMBERED face back on the button, unchanged — for undoing something else's
  // meddling (a QToolButton re-copies its action's icon on every ActionChanged). It is not
  // a transition: it never touches the state, and it keeps its hands off a running swap,
  // which is already painting every frame.
  void repaintFace(QAbstractButton* btn) {
    if (!btn || faceSwapping(btn)) return;
    bool known = false;
    const FaceSpec painted = detail::paintedFace(btn, &known);
    if (!known) return;
    btn->setIcon(themedIcon(painted.glyph, painted.glyphColor,
                            std::max(1, painted.iconSize)));
    if (!painted.label.isNull()) btn->setText(painted.label);
  }

  // Swap `btn`'s face to `to`, leaving from whatever a previous swap painted.
  //
  // `applyState` is the caller's own flip (the accent fill, a repolish) and runs ONCE, at
  // the pivot, hidden behind the invisible frame. It must SET the state absolutely, never
  // toggle it: a swap superseded before its pivot is dropped, and the superseding one's
  // applyState is then what the button ends on — which is how holding the shortcut always
  // converges on the real state instead of stranding a stale face.
  //
  // Reduced motion (or a first paint, or a hidden button) goes straight to `to`.
  void swapFace(QAbstractButton* btn, const FaceSpec& to,
                const std::function<void()>& applyState, int ms) {
    if (!btn || to.glyph.isEmpty()) return;
    if (!btn->property(kFaceBaseSheetProperty).isValid())
      btn->setProperty(kFaceBaseSheetProperty, btn->styleSheet());
    for (QVariantAnimation* old : btn->findChildren<QVariantAnimation*>(
             QString::fromLatin1(kFaceSwapAnimName), Qt::FindDirectChildrenOnly)) {
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
    anim->setObjectName(QString::fromLatin1(kFaceSwapAnimName));
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
