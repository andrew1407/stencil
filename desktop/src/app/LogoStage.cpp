#include "LogoStage.hpp"

#include "ModalBackdrop.hpp"   // the modal scrim + blur a showWord wears too
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

  LogoStage::LogoStage(QWidget* host, QToolButton* logo, Hooks hooks)
      : QWidget(host), hostWindow(host), logo(logo), hooks(std::move(hooks)) {
    setObjectName("logoStage");
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);
    hide();
    hold = new QTimer(this);
    hold->setObjectName("logoHold");
    hold->setSingleShot(true);
    connect(hold, &QTimer::timeout, this, [this] {
      const QString name = heldShow();
      if (name.isEmpty()) return;
      fired = true;
      if (this->hooks.stopClick) this->hooks.stopClick();
      activateByName(name);
    });
    clock = new QTimer(this);
    clock->setTimerType(Qt::PreciseTimer);
    connect(clock, &QTimer::timeout, this, &LogoStage::tick);
    if (this->logo) this->logo->installEventFilter(this);
    qApp->installEventFilter(this);
  }

  int LogoStage::bigEnd(int w, int h) const {
    const bool bouncing = effect == StageEffect::SHRINK || effect == StageEffect::GROW;
    return bouncing ? support::bounceBigSize(w, h) : support::bigLogoSize(w, h);
  }

  QString LogoStage::heldShow() const {
    return support::resolveShow(hooks.accentKey ? hooks.accentKey() : QString(), support::motionMode());
  }

  bool LogoStage::activateByName(const QString& name) {
    const support::StageShow* spec = support::showByName(name);
    if (!spec || open) return false;
    if (hooks.bareWindow && !hooks.bareWindow()) return false;
    // Every showWord's notice is the same: the egg on gold, wearing the golden shining.
    if (spec->effect == StageEffect::PINK) {
      if (hooks.pinkVibe) hooks.pinkVibe();
      if (hooks.toast) hooks.toast(support::logoStageConfig().toast);
      return true;
    }
    start(name);
    if (hooks.toast) hooks.toast(support::logoStageConfig().toast);
    return true;
  }

  void LogoStage::start(const QString& name) {
    const support::StageShow* spec = support::showByName(name);
    showWord = name;
    effect = spec->effect;
    reduced = support::motionReduced();
    takeBackdrop();
    setGeometry(hostWindow->rect());
    const int w = width(), h = height();
    const auto [rest, other] = ends(w, h);
    markPx = rest;
    bounce = support::bounceState(rest, other);
    fly = support::flyState(w / 2.0, h / 2.0, angle());
    chase = support::chaseState(w / 2.0, h / 2.0);
    heading = QPointF(0, 0);
    markCentre = QPointF(w / 2.0, h / 2.0);
    cursorPos = markCentre;
    remakeMark();
    support::ParticleStyle style = support::ParticleStyle::DUST;
    hasCloud = support::showHasCloud(name, &style) && !reduced;
    cloud.setStyle(style, hasCloud);
    // The mark grows out of the header logo, as the browser's does.
    const QPoint at = logo && logo->isVisible() ? logo->mapTo(hostWindow, logo->rect().center()) : rect().center();
    from = support::StagePose{double(at.x()), double(at.y()), double(logo ? logo->iconSize().width() : 32)};
    last = refitAt = 0;
    leftAt = -1;
    open = true;
    held = false;
    boost = 1.0;
    since.restart();
    if (hooks.coverChrome) hooks.coverChrome(true);
    raise();
    show();
    priorFocus = QApplication::focusWidget();
    setFocus(Qt::OtherFocusReason);
    clock->start(support::frameIntervalMs(this));
    update();
  }

  // The mark is measured FROM the hostWindow, so a resize re-measures it and carries everything
  // placed in the old box across as a fraction of it — otherwise the showWord keeps the markPx and the
  // centre of the hostWindow it opened in. Browser twin: the tail of logoStage.js `fit`.
  void LogoStage::relayout() {
    if (!open && leftAt < 0) return;
    const QSize was = size();
    setGeometry(hostWindow->rect());
    const int w = width(), h = height();
    const double kx = was.width() > 0 ? double(w) / was.width() : 1.0;
    const double ky = was.height() > 0 ? double(h) / was.height() : 1.0;
    if (kx == 1.0 && ky == 1.0) return;
    const auto [rest, other] = ends(w, h);
    support::bounceResize(bounce, rest, other);
    markPx = effect == StageEffect::SHRINK || effect == StageEffect::GROW ? bounce.size : rest;
    refitAt = since.elapsed() + REFIT_MS;
    markCentre = QPointF(markCentre.x() * kx, markCentre.y() * ky);
    chase.x *= kx;  chase.y *= ky;
    fly.x *= kx;    fly.y *= ky;
    if (logo && logo->isVisible()) {
      const QPoint at = logo->mapTo(hostWindow, logo->rect().center());
      from = support::StagePose{double(at.x()), double(at.y()), from.size};
    }
    update();
  }

  void LogoStage::dismiss() {
    if (!open) return;
    open = false;
    // Hand the keyboard back, or the hostWindow's own typing — the next word — never reaches it.
    if (priorFocus) priorFocus->setFocus(Qt::OtherFocusReason);
    else if (hostWindow) hostWindow->setFocus(Qt::OtherFocusReason);
    priorFocus.clear();
    if (hooks.coverChrome) hooks.coverChrome(false);
    if (reduced) {
      clock->stop();
      cloud.clear();
      hide();
      return;
    }
    leftAt = since.elapsed();
  }

  void LogoStage::tick() {
    const support::LogoStageConfig& cfg = support::logoStageConfig();
    const double t = since.elapsed();
    const double dt = std::min(50.0, last > 0 ? t - last : 16.7);
    last = t;
    if (refitAt > 0 && t >= refitAt) refit();
    rampBoost(dt);
    if (leftAt >= 0 && t - leftAt >= cfg.hideMs) {
      clock->stop();
      cloud.clear();
      hide();
      leftAt = -1;
      return;
    }
    if (!reduced && leftAt < 0) {
      const int w = width(), h = height();
      // The pointer is SAMPLED each frame, never taken on trust from a move event: one that does
      // not reach the stage leaves the chase with nothing to chase and the hand never applied.
      const QPoint here = mapFromGlobal(QCursor::pos());
      if (rect().contains(here)) cursorPos = QPointF(here);
      if (effect == StageEffect::FOLLOW || effect == StageEffect::ESCAPE) {
        support::chaseStep(chase, cursorPos, dt, markPx, w, h, effect == StageEffect::ESCAPE);
        markCentre = QPointF(chase.x, chase.y);
        heading = support::headingOfState(chase.vx, chase.vy);
      } else if (effect == StageEffect::FLY) {
        support::flyStep(fly, dt, markPx, w, h);
        markCentre = QPointF(fly.x, fly.y);
        // No tail: this one is not chasing anything, so its cloud stays a ring on every side.
        heading = QPointF(0, 0);
      }
      if (effect == StageEffect::SHRINK || effect == StageEffect::GROW) markPx = support::bounceStep(bounce, t);
      // Spawning tapers with the fade, so the hide has nothing new arriving into it.
      const double fade = leftAt < 0 ? std::min(1.0, t / double(cfg.revealMs))
                                      : std::max(0.0, 1.0 - (t - leftAt) / double(cfg.hideMs));
      cloud.step(dt, markPx, boostNow() * fade, heading);
      syncCursor();
    }
    update();
  }

  // The catch is the mark's OWN outline, not a circle round it: a circle reaches past the flat
  // edges and falls short of the corners, so the hand showed where the art was not. A fast mark
  // is past the pointer by the time a press lands, so it reaches ahead by grabLeadMs of travel.
  bool LogoStage::onMark(const QPoint& at) const {
    const double dx = at.x() - markCentre.x(), dy = at.y() - markCentre.y();
    const bool chasing = effect == StageEffect::FOLLOW || effect == StageEffect::ESCAPE;
    const double v = effect == StageEffect::FLY ? std::hypot(fly.vx, fly.vy)
                   : chasing ? std::hypot(chase.vx, chase.vy) : 0.0;
    const QPointF edge = support::markEdge(markPx, std::atan2(dy, dx));
    const double lead = (v * support::logoStageConfig().flyGrabLeadMs) / 1000.0;
    return std::hypot(dx, dy) <= std::hypot(edge.x(), edge.y()) + lead;
  }

  // ONLY a press drives the light and the cloud: crossing the mark changes nothing.
  double LogoStage::boostNow() const { return boost; }

  // The press EASES in and out over its own time constant — stepped to the target, the light
  // and the grain count both jumped.
  void LogoStage::rampBoost(double dt) {
    const support::LogoStageConfig& cfg = support::logoStageConfig();
    const double target = held ? cfg.holdBoost : 1.0;
    const double k = reduced ? 1.0 : 1.0 - std::exp(-dt / double(cfg.holdRampMs));
    boost += (target - boost) * k;
  }

  // Off the mark, a press closes the stage. On it, a press is a HOLD: the light and the cloud
  // swell for as long as it lasts, and only the shows with a move of their own act on it.
  void LogoStage::pressed(const QPoint& at) {
    if (!onMark(at)) { dismiss(); return; }
    held = true;
    if (effect == StageEffect::SHRINK || effect == StageEffect::GROW)
      support::bounceImpulse(bounce, since.elapsed());
    else if (effect == StageEffect::FLY)
      support::flyPunch(fly, angle());
  }

}  // namespace stencil::gui
