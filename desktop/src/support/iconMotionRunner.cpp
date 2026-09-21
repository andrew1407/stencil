#include "iconMotion.hpp"

namespace stencil::gui {

  IconMotionRunner::IconMotionRunner(QAbstractButton* btn, const IconRequest& req,
                                     const IconMotionSpec* spec,
                                     const QVector<IconMotionPart>* parts) : QObject(btn), btn(btn), req(req), spec(spec), parts(parts) {
    setObjectName(QString::fromLatin1(ICON_MOTION_ANIM_NAME));
    anim = new QVariantAnimation(this);
    anim->setStartValue(0.0);
    anim->setEndValue(0.0);
    connect(anim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& v) { paint(v.toDouble()); });
    connect(anim, &QVariantAnimation::finished, this, [this] {
      elapsed = anim->endValue().toDouble();
      if (elapsed <= 0.0 || !this->spec->hold) restPaint();
    });
  }


  // Hold eases out to the pose; a settle plays once and is spent until the pointer leaves.
  void IconMotionRunner::enter() {
    if (spec->hold) { run(spec->totalMs); return; }
    if (spent) return;
    spent = true;
    elapsed = 0;
    run(spec->totalMs);
  }


  void ActionIconMotionRunner::enter() {
    if (spec->hold) { run(spec->totalMs); return; }
    if (spent) return;
    spent = true;
    elapsed = 0;
    run(spec->totalMs);
  }

  // A hold eases back on the same curve; a settle is CANCELLED, the way the browser's
  // animation-name reverting off :hover drops its glyph straight back to the base style.
  void IconMotionRunner::leave() {
    if (spec->hold) run(0);
    else rest();
  }


  void ActionIconMotionRunner::leave() {
    if (spec->hold) run(0);
    else rest();
  }

  // The cached QIcon, so the next hover can trace it to its glyph again — and re-armed, so
  // that hover plays its settle afresh.
  void IconMotionRunner::rest() {
    spent = false;
    restPaint();
  }

  void ActionIconMotionRunner::rest() {
    spent = false;
    restPaint();
  }

  void IconMotionRunner::restPaint() {
    anim->stop();
    elapsed = 0;
    if (!btn || hasTakenOver()) return;   // a theme flip already put a proper glyph there
    btn->setIcon(themedIcon(req.name, req.color, req.size, req.dpr, req.gap));
  }

  void ActionIconMotionRunner::restPaint() {
    anim->stop();
    elapsed = 0;
    if (!act || hasTakenOver()) return;   // a theme flip already put a proper glyph there
    // A bound toolbar button mirrors the action's icon via changed(); a posed/rest repaint
    // here must never leak onto it.
    const QSignalBlocker block(act);
    act->setIcon(themedIcon(req.name, req.color, req.size, req.dpr, req.gap));
  }

  void IconMotionRunner::run(double target) {
    anim->stop();
    const double from = elapsed;
    if (qFuzzyCompare(from + 1, target + 1)) { paint(target); return; }
    anim->setStartValue(from);
    anim->setEndValue(target);
    // Linear: the shaping lives per part, in poseAt()'s easings.
    anim->setDuration(std::max(1, int(std::abs(target - from))));
    anim->start();
  }


  void ActionIconMotionRunner::run(double target) {
    anim->stop();
    const double from = elapsed;
    if (qFuzzyCompare(from + 1, target + 1)) { paint(target); return; }
    anim->setStartValue(from);
    anim->setEndValue(target);
    anim->setDuration(std::max(1, int(std::abs(target - from))));
    anim->start();
  }

  // A frame WE painted is never a registered themedIcon.
  bool IconMotionRunner::hasTakenOver() const {
    IconRequest now;
    return btn && iconRequestForKey(btn->icon().cacheKey(), &now)
           && (now.name != req.name || now.color != req.color || now.size != req.size);
  }

  bool ActionIconMotionRunner::hasTakenOver() const {
    IconRequest now;
    return act && iconRequestForKey(act->icon().cacheKey(), &now)
           && (now.name != req.name || now.color != req.color || now.size != req.size);
  }

  void IconMotionRunner::paint(double elapsed) {
    this->elapsed = elapsed;
    if (!btn) return;
    // A face mid-swap already owns this glyph — step out.
    if (faceSwapping(btn) || hasTakenOver()) { anim->stop(); return; }
    const QString posed = iconMotionMarkup(req.name, *spec, *parts, elapsed);
    // A disabled control renders the Disabled variant, so only then is it built.
    btn->setIcon(iconFromMarkup(posed, req.color, req.size, req.dpr,
                                 /*withDisabled=*/!btn->isEnabled(), req.gap));
  }

  void ActionIconMotionRunner::paint(double elapsed) {
    this->elapsed = elapsed;
    if (!act) return;
    if (hasTakenOver()) { anim->stop(); return; }
    const QString posed = iconMotionMarkup(req.name, *spec, *parts, elapsed);
    const QSignalBlocker block(act);  // never let a bound toolbar button see this frame
    // A disabled row renders the Disabled variant, so only then is it built.
    act->setIcon(iconFromMarkup(posed, req.color, req.size, req.dpr,
                                 /*withDisabled=*/!act->isEnabled(), req.gap));
  }


  ActionIconMotionRunner::ActionIconMotionRunner(QAction* act, const IconRequest& req,
                                                 const IconMotionSpec* spec,
                                                 const QVector<IconMotionPart>* parts) : QObject(act), act(act), req(req), spec(spec), parts(parts) {
    setObjectName(QString::fromLatin1(ICON_MOTION_ANIM_NAME));
    anim = new QVariantAnimation(this);
    anim->setStartValue(0.0);
    anim->setEndValue(0.0);
    connect(anim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& v) { paint(v.toDouble()); });
    connect(anim, &QVariantAnimation::finished, this, [this] {
      elapsed = anim->endValue().toDouble();
      if (elapsed <= 0.0 || !this->spec->hold) restPaint();
    });
  }
}  // namespace stencil::gui
