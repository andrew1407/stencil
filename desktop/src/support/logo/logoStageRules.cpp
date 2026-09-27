#include "logoStageRules.hpp"

#include <cmath>

namespace stencil::support {

  namespace {
    constexpr double PI = 3.14159265358979323846;
  }  // namespace

  const StageShow* showByName(const QString& name) {
    for (const StageShow& s : logoStageConfig().shows)
      if (s.name == name) return &s;
    return nullptr;
  }

  QStringList typedWords() {
    QStringList out;
    for (const StageShow& s : logoStageConfig().shows) out << s.name.toLower();
    return out;
  }

  // Two rows may share a colour, told apart by `motion` alone.
  QString resolveShow(const QString& accentKey, MotionMode mode) {
    const LogoStageConfig& cfg = logoStageConfig();
    if (accentKey.startsWith('#')) {
      const QString hex = accentKey.trimmed().toLower();
      const StageShow* any = nullptr;
      for (const StageShow& s : cfg.shows) {
        if (s.customHex == hex) return s.name;
        if (s.customHex == QLatin1String("*")) any = &s;
      }
      return any ? any->name : QString();
    }
    for (const StageShow& s : cfg.shows) {
      if (!s.accents.contains(accentKey)) continue;
      if (!s.motion.isEmpty() && motionModeFromKey(s.motion) != mode) continue;
      return s.name;
    }
    return QString();
  }

  bool showHasCloud(const QString& name, ParticleStyle* out) {
    const StageShow* s = showByName(name);
    if (!s || s->effect == StageEffect::WEBCORE) return false;   // a skin toggle opens no stage
    if (!s->motion.isEmpty()) {
      if (out) *out = s->effect == StageEffect::FIRE ? ParticleStyle::FIRE
                    : s->effect == StageEffect::WATER ? ParticleStyle::WATER : ParticleStyle::DUST;
      return true;
    }
    // No motion of its own: it wears the user's particle style, dust when they run none. NEON is
    // the light itself and SUN its own ring of beams, so neither ever wears a cloud.
    if (s->effect == StageEffect::NEON || s->effect == StageEffect::SUN) return false;
    if (out) *out = showParticleStyle();
    return true;
  }

  QPointF markEdge(double size, double angle) {
    const double c = std::cos(angle), s = std::sin(angle);
    const double half = size * logoStageConfig().markEdgeShare;
    const double r = std::min(size * logoStageConfig().markCornerShare, half);
    const double flat = half - r;   // half-extents of the square the corners round off
    const double ac = std::abs(c), as = std::abs(s);
    double t;
    if (ac > 0 && (half / ac) * as <= flat) t = half / ac;        // out through a vertical side
    else if (as > 0 && (half / as) * ac <= flat) t = half / as;   // out through a horizontal one
    else {
      // Out through a corner: the ray meets the arc of radius r centred on (flat, flat).
      const double k = ac * flat + as * flat;
      t = k + std::sqrt(std::max(0.0, k * k - (2 * flat * flat - r * r)));
    }
    return QPointF(c * t, s * t);
  }

  int bigLogoSize(int w, int h) { return qRound(std::min(w, h) * logoStageConfig().logoShare); }
  int bounceBigSize(int w, int h) { return qRound(std::min(w, h) * logoStageConfig().bounceBigShare); }
  int minLogoSize(int w, int h) { return qRound(std::min(w, h) * logoStageConfig().minShare); }

  bool roams(const QString& name) {
    const StageShow* s = showByName(name);
    return s && (s->effect == StageEffect::FOLLOW || s->effect == StageEffect::ESCAPE ||
                 s->effect == StageEffect::FLY);
  }

  int roamLogoSize(int w, int h) { return qRound(std::min(w, h) * logoStageConfig().roamShare); }

  QVector<QPointF> heartPoints(int w, int h, int n) {
    const LogoStageConfig& cfg = logoStageConfig();
    const int count = n > 0 ? n : cfg.heartPoints;
    QVector<QPointF> raw;
    raw.reserve(count);
    double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    for (int i = 0; i < count; ++i) {
      const double t = (2 * PI * i) / count;
      const double x = 16 * std::pow(std::sin(t), 3);
      const double y = -(13 * std::cos(t) - 5 * std::cos(2 * t) - 2 * std::cos(3 * t) - std::cos(4 * t));
      raw.push_back(QPointF(x, y));
      minX = std::min(minX, x); maxX = std::max(maxX, x);
      minY = std::min(minY, y); maxY = std::max(maxY, y);
    }
    const double k = (std::min(w, h) * (1 - 2 * cfg.heartInsetShare)) / std::max(maxX - minX, maxY - minY);
    const double bx = (minX + maxX) / 2, by = (minY + maxY) / 2;
    QVector<QPointF> out;
    out.reserve(count);
    for (const QPointF& p : raw)
      out.push_back(QPointF(std::round((w / 2.0 + (p.x() - bx) * k) * 100) / 100,
                            std::round((h / 2.0 + (p.y() - by) * k) * 100) / 100));
    return out;
  }

}  // namespace stencil::support
