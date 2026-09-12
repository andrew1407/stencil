#include "iconMotion.hpp"

namespace stencil::gui {

  IconMotionRunner::IconMotionRunner(QAbstractButton* btn, const IconRequest& req,
                                     const IconMotionSpec* spec,
                                     const QVector<IconMotionPart>* parts) : QObject(btn), btn_(btn), req_(req), spec_(spec), parts_(parts) {
    setObjectName(QString::fromLatin1(ICON_MOTION_ANIM_NAME));
    anim_ = new QVariantAnimation(this);
    anim_->setStartValue(0.0);
    anim_->setEndValue(0.0);
    connect(anim_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& v) { paint(v.toDouble()); });
    connect(anim_, &QVariantAnimation::finished, this, [this] {
      elapsed_ = anim_->endValue().toDouble();
      if (elapsed_ <= 0.0 || !spec_->hold) rest();
    });
  }


  // Hold eases out to the pose, settle plays once.
  void IconMotionRunner::enter() {
    if (spec_->hold) run(spec_->totalMs);
    else { elapsed_ = 0; run(spec_->totalMs); }
  }


  void ActionIconMotionRunner::enter() {
    if (spec_->hold) run(spec_->totalMs);
    else { elapsed_ = 0; run(spec_->totalMs); }
  }

  // A hold eases back on the same curve; a settle is left to finish (its end IS the rest pose).
  void IconMotionRunner::leave() {
    if (spec_->hold) run(0);
  }


  void ActionIconMotionRunner::leave() {
    if (spec_->hold) run(0);
  }

  // The cached QIcon, so the next hover can trace it to its glyph again.
  void IconMotionRunner::rest() {
    anim_->stop();
    elapsed_ = 0;
    if (!btn_ || hasTakenOver()) return;   // a theme flip already put a proper glyph there
    btn_->setIcon(themedIcon(req_.name, req_.color, req_.size, req_.dpr, req_.gap));
  }

  void ActionIconMotionRunner::rest() {
    anim_->stop();
    elapsed_ = 0;
    if (!act_ || hasTakenOver()) return;   // a theme flip already put a proper glyph there
    // A bound toolbar button mirrors the action's icon via changed(); a posed/rest repaint
    // here must never leak onto it.
    const QSignalBlocker block(act_);
    act_->setIcon(themedIcon(req_.name, req_.color, req_.size, req_.dpr, req_.gap));
  }

  void IconMotionRunner::run(double target) {
    anim_->stop();
    const double from = elapsed_;
    if (qFuzzyCompare(from + 1, target + 1)) { paint(target); return; }
    anim_->setStartValue(from);
    anim_->setEndValue(target);
    // Linear: the shaping lives per part, in poseAt()'s easings.
    anim_->setDuration(std::max(1, int(std::abs(target - from))));
    anim_->start();
  }


  void ActionIconMotionRunner::run(double target) {
    anim_->stop();
    const double from = elapsed_;
    if (qFuzzyCompare(from + 1, target + 1)) { paint(target); return; }
    anim_->setStartValue(from);
    anim_->setEndValue(target);
    anim_->setDuration(std::max(1, int(std::abs(target - from))));
    anim_->start();
  }

  // A frame WE painted is never a registered themedIcon.
  bool IconMotionRunner::hasTakenOver() const {
    IconRequest now;
    return btn_ && iconRequestForKey(btn_->icon().cacheKey(), &now)
           && (now.name != req_.name || now.color != req_.color || now.size != req_.size);
  }

  bool ActionIconMotionRunner::hasTakenOver() const {
    IconRequest now;
    return act_ && iconRequestForKey(act_->icon().cacheKey(), &now)
           && (now.name != req_.name || now.color != req_.color || now.size != req_.size);
  }

  void IconMotionRunner::paint(double elapsed) {
    elapsed_ = elapsed;
    if (!btn_) return;
    // A face mid-swap already owns this glyph — step out.
    if (faceSwapping(btn_) || hasTakenOver()) { anim_->stop(); return; }
    const QString posed = iconMotionMarkup(req_.name, *spec_, *parts_, elapsed);
    // A disabled control renders the Disabled variant, so only then is it built.
    btn_->setIcon(iconFromMarkup(posed, req_.color, req_.size, req_.dpr,
                                 /*withDisabled=*/!btn_->isEnabled(), req_.gap));
  }

  void ActionIconMotionRunner::paint(double elapsed) {
    elapsed_ = elapsed;
    if (!act_) return;
    if (hasTakenOver()) { anim_->stop(); return; }
    const QString posed = iconMotionMarkup(req_.name, *spec_, *parts_, elapsed);
    const QSignalBlocker block(act_);  // never let a bound toolbar button see this frame
    // A disabled row renders the Disabled variant, so only then is it built.
    act_->setIcon(iconFromMarkup(posed, req_.color, req_.size, req_.dpr,
                                 /*withDisabled=*/!act_->isEnabled(), req_.gap));
  }


  ActionIconMotionRunner::ActionIconMotionRunner(QAction* act, const IconRequest& req,
                                                 const IconMotionSpec* spec,
                                                 const QVector<IconMotionPart>* parts) : QObject(act), act_(act), req_(req), spec_(spec), parts_(parts) {
    setObjectName(QString::fromLatin1(ICON_MOTION_ANIM_NAME));
    anim_ = new QVariantAnimation(this);
    anim_->setStartValue(0.0);
    anim_->setEndValue(0.0);
    connect(anim_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& v) { paint(v.toDouble()); });
    connect(anim_, &QVariantAnimation::finished, this, [this] {
      elapsed_ = anim_->endValue().toDouble();
      if (elapsed_ <= 0.0 || !spec_->hold) rest();
    });
  }
}  // namespace stencil::gui
