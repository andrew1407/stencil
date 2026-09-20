#include "LogoStage.hpp"

#include "ModalBackdrop.hpp"   // the modal scrim + blur a show wears too
#include "modalReveal.hpp"     // support::motionReduced()

#include <QApplication>
#include <QCursor>
#include <QRandomGenerator>
#include <QTimer>
#include <QToolButton>
#include <cmath>

namespace stencil::gui {

  using support::StageEffect;

  namespace {
    constexpr double PI = 3.14159265358979323846;
    double angle() { return QRandomGenerator::global()->generateDouble() * 2 * PI; }
  }  // namespace

  LogoStage::LogoStage(QWidget* window, QToolButton* logo, Hooks hooks)
      : QWidget(window), window_(window), logo_(logo), hooks_(std::move(hooks)) {
    setObjectName("logoStage");
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);
    hide();
    hold_ = new QTimer(this);
    hold_->setObjectName("logoHold");
    hold_->setSingleShot(true);
    connect(hold_, &QTimer::timeout, this, [this] {
      const QString name = heldShow();
      if (name.isEmpty()) return;
      fired_ = true;
      if (hooks_.stopClick) hooks_.stopClick();
      activateByName(name);
    });
    clock_ = new QTimer(this);
    clock_->setTimerType(Qt::PreciseTimer);
    connect(clock_, &QTimer::timeout, this, &LogoStage::tick);
    if (logo_) logo_->installEventFilter(this);
    qApp->installEventFilter(this);
  }

  int LogoStage::bigEnd(int w, int h) const {
    const bool bouncing = effect_ == StageEffect::SHRINK || effect_ == StageEffect::GROW;
    return bouncing ? support::bounceBigSize(w, h) : support::bigLogoSize(w, h);
  }

  QString LogoStage::heldShow() const {
    return support::resolveShow(hooks_.accentKey ? hooks_.accentKey() : QString(), support::motionMode());
  }

  bool LogoStage::activateByName(const QString& name) {
    const support::StageShow* spec = support::showByName(name);
    if (!spec || open_) return false;
    if (hooks_.bareWindow && !hooks_.bareWindow()) return false;
    // Every show's notice is the same: the egg on gold, wearing the golden shining.
    if (spec->effect == StageEffect::PINK) {
      if (hooks_.pinkVibe) hooks_.pinkVibe();
      if (hooks_.toast) hooks_.toast(support::logoStageConfig().toast);
      return true;
    }
    start(name);
    if (hooks_.toast) hooks_.toast(support::logoStageConfig().toast);
    return true;
  }

  void LogoStage::start(const QString& name) {
    const support::StageShow* spec = support::showByName(name);
    show_ = name;
    effect_ = spec->effect;
    reduced_ = support::motionReduced();
    takeBackdrop();
    setGeometry(window_->rect());
    const int w = width(), h = height();
    const bool roaming = support::roams(name);
    const double rest = roaming ? support::roamLogoSize(w, h)
                      : effect_ == StageEffect::GROW ? support::minLogoSize(w, h) : bigEnd(w, h);
    const double other = effect_ == StageEffect::GROW ? bigEnd(w, h) : support::minLogoSize(w, h);
    size_ = rest;
    bounce_ = support::bounceState(rest, other);
    fly_ = support::flyState(w / 2.0, h / 2.0, angle());
    chase_ = support::chaseState(w / 2.0, h / 2.0);
    heading_ = QPointF(0, 0);
    pos_ = QPointF(w / 2.0, h / 2.0);
    cursor_ = pos_;
    mark_ = hooks_.makeMark ? hooks_.makeMark(int(std::ceil(rest))) : QPixmap();
    support::ParticleStyle style = support::ParticleStyle::DUST;
    hasCloud_ = support::showHasCloud(name, &style) && !reduced_;
    cloud_.setStyle(style, hasCloud_);
    // The mark grows out of the header logo, as the browser's does.
    const QPoint at = logo_ && logo_->isVisible() ? logo_->mapTo(window_, logo_->rect().center()) : rect().center();
    from_ = support::StagePose{double(at.x()), double(at.y()), double(logo_ ? logo_->iconSize().width() : 32)};
    last_ = 0;
    leftAt_ = -1;
    open_ = true;
    held_ = false;
    boost_ = 1.0;
    since_.restart();
    if (hooks_.coverChrome) hooks_.coverChrome(true);
    raise();
    show();
    priorFocus_ = QApplication::focusWidget();
    setFocus(Qt::OtherFocusReason);
    clock_->start(support::frameIntervalMs(this));
    update();
  }

  // The mark is measured FROM the window, so a resize re-measures it and carries everything
  // placed in the old box across as a fraction of it — otherwise the show keeps the size and the
  // centre of the window it opened in. Browser twin: the tail of logoStage.js `fit`.
  void LogoStage::relayout() {
    if (!open_ && leftAt_ < 0) return;
    const QSize was = size();
    setGeometry(window_->rect());
    const int w = width(), h = height();
    const double kx = was.width() > 0 ? double(w) / was.width() : 1.0;
    const double ky = was.height() > 0 ? double(h) / was.height() : 1.0;
    if (kx == 1.0 && ky == 1.0) return;
    takeBackdrop();
    const double rest = support::roams(show_) ? support::roamLogoSize(w, h)
                      : effect_ == StageEffect::GROW ? support::minLogoSize(w, h) : bigEnd(w, h);
    const double other = effect_ == StageEffect::GROW ? bigEnd(w, h) : support::minLogoSize(w, h);
    support::bounceResize(bounce_, rest, other);
    size_ = effect_ == StageEffect::SHRINK || effect_ == StageEffect::GROW ? bounce_.size : rest;
    mark_ = hooks_.makeMark ? hooks_.makeMark(int(std::ceil(size_))) : QPixmap();
    pos_ = QPointF(pos_.x() * kx, pos_.y() * ky);
    chase_.x *= kx;  chase_.y *= ky;
    fly_.x *= kx;    fly_.y *= ky;
    if (logo_ && logo_->isVisible()) {
      const QPoint at = logo_->mapTo(window_, logo_->rect().center());
      from_ = support::StagePose{double(at.x()), double(at.y()), from_.size};
    }
    update();
  }

  void LogoStage::dismiss() {
    if (!open_) return;
    open_ = false;
    // Hand the keyboard back, or the window's own typing — the next word — never reaches it.
    if (priorFocus_) priorFocus_->setFocus(Qt::OtherFocusReason);
    else if (window_) window_->setFocus(Qt::OtherFocusReason);
    priorFocus_.clear();
    if (hooks_.coverChrome) hooks_.coverChrome(false);
    if (reduced_) {
      clock_->stop();
      cloud_.clear();
      hide();
      return;
    }
    leftAt_ = since_.elapsed();
  }

  void LogoStage::tick() {
    const support::LogoStageConfig& cfg = support::logoStageConfig();
    const double t = since_.elapsed();
    const double dt = std::min(50.0, last_ > 0 ? t - last_ : 16.7);
    last_ = t;
    rampBoost(dt);
    if (leftAt_ >= 0 && t - leftAt_ >= cfg.hideMs) {
      clock_->stop();
      cloud_.clear();
      hide();
      leftAt_ = -1;
      return;
    }
    if (!reduced_ && leftAt_ < 0) {
      const int w = width(), h = height();
      // The pointer is SAMPLED each frame, never taken on trust from a move event: one that does
      // not reach the stage leaves the chase with nothing to chase and the hand never applied.
      const QPoint here = mapFromGlobal(QCursor::pos());
      if (rect().contains(here)) cursor_ = QPointF(here);
      if (effect_ == StageEffect::FOLLOW || effect_ == StageEffect::ESCAPE) {
        support::chaseStep(chase_, cursor_, dt, size_, w, h, effect_ == StageEffect::ESCAPE);
        pos_ = QPointF(chase_.x, chase_.y);
        heading_ = support::headingOfState(chase_.vx, chase_.vy);
      } else if (effect_ == StageEffect::FLY) {
        support::flyStep(fly_, dt, size_, w, h);
        pos_ = QPointF(fly_.x, fly_.y);
        // No tail: this one is not chasing anything, so its cloud stays a ring on every side.
        heading_ = QPointF(0, 0);
      }
      if (effect_ == StageEffect::SHRINK || effect_ == StageEffect::GROW) size_ = support::bounceStep(bounce_, t);
      // Spawning tapers with the fade, so the hide has nothing new arriving into it.
      const double fade = leftAt_ < 0 ? std::min(1.0, t / double(cfg.revealMs))
                                      : std::max(0.0, 1.0 - (t - leftAt_) / double(cfg.hideMs));
      cloud_.step(dt, size_, boostNow() * fade, heading_);
      syncCursor();
    }
    update();
  }

  // The catch is the mark's OWN outline, not a circle round it: a circle reaches past the flat
  // edges and falls short of the corners, so the hand showed where the art was not. A fast mark
  // is past the pointer by the time a press lands, so it reaches ahead by grabLeadMs of travel.
  bool LogoStage::onMark(const QPoint& at) const {
    const double dx = at.x() - pos_.x(), dy = at.y() - pos_.y();
    const bool chasing = effect_ == StageEffect::FOLLOW || effect_ == StageEffect::ESCAPE;
    const double v = effect_ == StageEffect::FLY ? std::hypot(fly_.vx, fly_.vy)
                   : chasing ? std::hypot(chase_.vx, chase_.vy) : 0.0;
    const QPointF edge = support::markEdge(size_, std::atan2(dy, dx));
    const double lead = (v * support::logoStageConfig().flyGrabLeadMs) / 1000.0;
    return std::hypot(dx, dy) <= std::hypot(edge.x(), edge.y()) + lead;
  }

  // ONLY a press drives the light and the cloud: crossing the mark changes nothing.
  double LogoStage::boostNow() const { return boost_; }

  // The press EASES in and out over its own time constant — stepped to the target, the light
  // and the grain count both jumped.
  void LogoStage::rampBoost(double dt) {
    const support::LogoStageConfig& cfg = support::logoStageConfig();
    const double target = held_ ? cfg.holdBoost : 1.0;
    const double k = reduced_ ? 1.0 : 1.0 - std::exp(-dt / double(cfg.holdRampMs));
    boost_ += (target - boost_) * k;
  }

  // Off the mark, a press closes the stage. On it, a press is a HOLD: the light and the cloud
  // swell for as long as it lasts, and only the shows with a move of their own act on it.
  void LogoStage::pressed(const QPoint& at) {
    if (!onMark(at)) { dismiss(); return; }
    held_ = true;
    if (effect_ == StageEffect::SHRINK || effect_ == StageEffect::GROW)
      support::bounceImpulse(bounce_, since_.elapsed());
    else if (effect_ == StageEffect::FLY)
      support::flyPunch(fly_, angle());
  }

}  // namespace stencil::gui
