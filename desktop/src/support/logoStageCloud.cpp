#include "logoStageCloud.hpp"

#include "logoStageRules.hpp"

#include <QRandomGenerator>
#include <algorithm>
#include <cmath>

namespace stencil::support {

  namespace {
    constexpr double PI = 3.14159265358979323846;
    double rnd() { return QRandomGenerator::global()->generateDouble(); }
    double lerp(const double range[2], double t) { return range[0] + (range[1] - range[0]) * t; }
  }  // namespace

  StageMote newStageMote(double size, double reach, const QPointF& dir) {
    const LogoStageConfig& cfg = logoStageConfig();
    // With a heading the grains form a PLUME clear of the mark: born a gap behind it, thrown
    // further back, so the trail reads beside the icon instead of under it. `dir`'s LENGTH is how
    // strongly it forms, so the ring becomes a tail as the mark picks up speed instead of at a step.
    const double pull = std::min(1.0, std::hypot(dir.x(), dir.y()));
    const double spread = PI - (PI - cfg.cloudTailSpreadTurns * 2 * PI) * pull;
    const double angle = pull > 0 ? std::atan2(-dir.y(), -dir.x()) + (rnd() - 0.5) * 2 * spread
                                  : rnd() * 2 * PI;
    const double speed = lerp(cfg.cloudSpeedShare, rnd()) * size * reach
                         * (1.0 + (cfg.cloudTailSpeedScale - 1.0) * pull);
    // At rest a grain is born along the mark's own rounded-square outline — a ring of radius size/2
    // buries its four corners under the art — and slides out to the plume's gap as the tail forms.
    const QPointF ring = markEdge(size, angle);
    const double gap = size * cfg.cloudTailGapShare;
    const QPointF at(ring.x() + (std::cos(angle) * gap - ring.x()) * pull,
                     ring.y() + (std::sin(angle) * gap - ring.y()) * pull);
    StageMote m;
    m.x = at.x();
    m.y = at.y();
    m.vx = std::cos(angle) * speed;
    m.vy = std::sin(angle) * speed;
    m.life = lerp(cfg.cloudLifeMs, rnd());
    m.r = lerp(cfg.cloudSizeShare, rnd()) * size * 0.5;
    m.w = rnd();
    m.len = size * 0.5;
    return m;
  }

  bool stepStageMote(StageMote& m, double dt) {
    const double k = dt / 1000.0, drag = std::exp(-dt / 700.0);
    m.x += m.vx * k;
    m.y += m.vy * k;
    m.vx *= drag;
    m.vy *= drag;
    m.age += dt;
    return m.age < m.life;
  }

  double stageMoteAlpha(const StageMote& m) {
    const double p = m.life > 0 ? m.age / m.life : 1.0;
    return p < 0.1 ? p / 0.1 : 1 - (p - 0.1) / 0.9;
  }

  int spawnStageCount(double boost, double dt) {
    const double n = logoStageConfig().cloudRate * boost * (dt / 16.7);
    return int(std::floor(n)) + (rnd() < std::fmod(n, 1.0) ? 1 : 0);
  }

  void LogoStageCloud::setStyle(ParticleStyle style, bool on) {
    style_ = style;
    on_ = on;
    motes_.clear();
  }

  void LogoStageCloud::clear() { motes_.clear(); }

  void LogoStageCloud::step(double dt, double size, double boost, const QPointF& dir) {
    if (!on_) return;
    const int room = logoStageConfig().cloudMaxLive - motes_.size();
    const int want = std::min(spawnStageCount(boost, dt), std::max(0, room));
    for (int i = 0; i < want; ++i) motes_.push_back(newStageMote(size, boost, dir));
    for (int i = motes_.size() - 1; i >= 0; --i)
      if (!stepStageMote(motes_[i], dt)) motes_.removeAt(i);
  }

  void LogoStageCloud::draw(QPainter& p, const QPointF& at, double ms, const QColor& accent,
                            const QColor& shade, bool dark, double scale) {
    if (!on_ || motes_.isEmpty()) return;
    for (const StageMote& m : motes_) {
      const double prog = m.life > 0 ? std::min(1.0, m.age / m.life) : 1.0;
      const StyleFrame f = styleFrame(style_, prog, prog, m.w, m.len, ms);
      const double alpha = stageMoteAlpha(m) * f.glow;
      if (alpha < 0.01) continue;
      const int tint = tintOf(m.w);
      const double mix = tint < 0 && style_ == ParticleStyle::DUST ? dustMix(m.w, false) : f.mix;
      p.setOpacity(std::clamp(alpha, 0.0, 1.0));
      sprites_.draw(p, at + QPointF((m.x + f.sx) * scale, (m.y + f.sy) * scale), m.r * f.scale * scale,
                    tintedStop(accent, shade, mix, tint, dark),
                    grainShape(style_, m.w), headingOf(m.vx, m.vy, false));
    }
    p.setOpacity(1.0);
  }

}  // namespace stencil::support
