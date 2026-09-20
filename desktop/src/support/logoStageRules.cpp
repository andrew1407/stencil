#include "logoStageRules.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

namespace stencil::support {

  namespace {
    constexpr double PI = 3.14159265358979323846;

    StageEffect effectFromKey(const QString& k) {
      if (k == "sun") return StageEffect::SUN;
      if (k == "fire") return StageEffect::FIRE;
      if (k == "water") return StageEffect::WATER;
      if (k == "dust") return StageEffect::DUST;
      if (k == "shrink") return StageEffect::SHRINK;
      if (k == "grow") return StageEffect::GROW;
      if (k == "follow") return StageEffect::FOLLOW;
      if (k == "escape") return StageEffect::ESCAPE;
      if (k == "pink") return StageEffect::PINK;
      if (k == "fly") return StageEffect::FLY;
      return StageEffect::NEON;
    }

    void readPair(const QJsonObject& o, const char* key, double (&out)[2]) {
      const QJsonArray a = o.value(QLatin1String(key)).toArray();
      if (a.size() == 2) { out[0] = a.at(0).toDouble(out[0]); out[1] = a.at(1).toDouble(out[1]); }
    }

    LogoStageConfig parseConfig() {
      LogoStageConfig c;
      QFile f(":/config/logoStage.json");
      if (!f.open(QIODevice::ReadOnly)) return c;
      const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
      c.holdMs = root.value("holdMs").toInt(c.holdMs);
      c.toast = root.value("toast").toString();
      c.toastGold = root.value("toastGold").toString();
      c.toastInk = root.value("toastInk").toString();
      c.toastGlow = root.value("toastGlow").toString();
      const QJsonObject shows = root.value("shows").toObject();
      for (auto it = shows.begin(); it != shows.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        StageShow s;
        s.name = it.key();
        s.effect = effectFromKey(o.value("effect").toString());
        for (const QJsonValue& a : o.value("accents").toArray()) s.accents << a.toString();
        s.motion = o.value("motion").toString();
        s.customHex = o.value("customHex").toString();
        c.shows.push_back(s);
      }
      const QJsonObject st = root.value("stage").toObject();
      c.logoShare = st.value("logoShare").toDouble(c.logoShare);
      c.bounceBigShare = st.value("bounceBigShare").toDouble(c.bounceBigShare);
      c.minShare = st.value("minShare").toDouble(c.minShare);
      c.roamShare = st.value("roamShare").toDouble(c.roamShare);
      c.markEdgeShare = st.value("markEdgeShare").toDouble(c.markEdgeShare);
      c.markCornerShare = st.value("markCornerShare").toDouble(c.markCornerShare);
      c.revealMs = st.value("revealMs").toInt(c.revealMs);
      c.hideMs = st.value("hideMs").toInt(c.hideMs);
      c.beatMs = st.value("beatMs").toInt(c.beatMs);
      c.spinMs = st.value("spinMs").toInt(c.spinMs);
      c.holdBoost = st.value("holdBoost").toDouble(c.holdBoost);
      c.holdRampMs = st.value("holdRampMs").toInt(c.holdRampMs);
      c.scrimAlpha = st.value("scrimAlpha").toDouble(c.scrimAlpha);
      const QJsonObject g = root.value("glow").toObject();
      c.glowAlphaMin = g.value("alphaMin").toDouble(c.glowAlphaMin);
      c.glowAlphaMax = g.value("alphaMax").toDouble(c.glowAlphaMax);
      c.glowReachShare = g.value("reachShare").toDouble(c.glowReachShare);
      c.glowSteadyLit = g.value("steadyLit").toDouble(c.glowSteadyLit);
      c.glowFloor = g.value("floor").toDouble(c.glowFloor);
      const QJsonObject s = root.value("sun").toObject();
      c.sunSpokes = s.value("spokes").toInt(c.sunSpokes);
      c.sunGapShare = s.value("gapShare").toDouble(c.sunGapShare);
      c.sunLengthShare = s.value("lengthShare").toDouble(c.sunLengthShare);
      c.sunAlphaMin = s.value("alphaMin").toDouble(c.sunAlphaMin);
      c.sunAlphaMax = s.value("alphaMax").toDouble(c.sunAlphaMax);
      c.sunSoftWidthShare = s.value("softWidthShare").toDouble(c.sunSoftWidthShare);
      c.sunBrightWidthShare = s.value("brightWidthShare").toDouble(c.sunBrightWidthShare);
      const QJsonObject cl = root.value("cloud").toObject();
      c.cloudRate = cl.value("rate").toDouble(c.cloudRate);
      readPair(cl, "lifeMs", c.cloudLifeMs);
      readPair(cl, "speedShare", c.cloudSpeedShare);
      readPair(cl, "sizeShare", c.cloudSizeShare);
      c.cloudMaxLive = cl.value("maxLive").toInt(c.cloudMaxLive);
      c.cloudTailSpreadTurns = cl.value("tailSpreadTurns").toDouble(c.cloudTailSpreadTurns);
      c.cloudTailGapShare = cl.value("tailGapShare").toDouble(c.cloudTailGapShare);
      c.cloudTailSpeedScale = cl.value("tailSpeedScale").toDouble(c.cloudTailSpeedScale);
      c.cloudTailMinSpeedPx = cl.value("tailMinSpeedPx").toDouble(c.cloudTailMinSpeedPx);
      c.cloudTailFullSpeedPx = cl.value("tailFullSpeedPx").toDouble(c.cloudTailFullSpeedPx);
      const QJsonObject b = root.value("bounce").toObject();
      c.snapMs = b.value("snapMs").toInt(c.snapMs);
      c.recoverMs = b.value("recoverMs").toInt(c.recoverMs);
      c.bounceStepShare = b.value("stepShare").toDouble(c.bounceStepShare);
      const QJsonObject fo = root.value("follow").toObject();
      c.followStiffness = fo.value("stiffness").toDouble(c.followStiffness);
      c.followDragPerS = fo.value("dragPerS").toDouble(c.followDragPerS);
      const QJsonObject e = root.value("escape").toObject();
      c.escapeRadiusPx = e.value("radiusPx").toDouble(c.escapeRadiusPx);
      c.escapeStiffness = e.value("stiffness").toDouble(c.escapeStiffness);
      c.escapeDragPerS = e.value("dragPerS").toDouble(c.escapeDragPerS);
      const QJsonObject fl = root.value("fly").toObject();
      c.flySpeedPx = fl.value("speedPx").toDouble(c.flySpeedPx);
      c.flyPunchPx = fl.value("punchPx").toDouble(c.flyPunchPx);
      c.flyDampPerS = fl.value("dampPerS").toDouble(c.flyDampPerS);
      c.flyGrabLeadMs = fl.value("grabLeadMs").toDouble(c.flyGrabLeadMs);
      const QJsonObject p = root.value("pink").toObject();
      c.pinkBlank = QColor(p.value("blank").toString());
      c.pinkTint = QColor(p.value("tint").toString());
      c.heartStroke = p.value("heartStroke").toString();
      c.heartFill = p.value("heartFill").toString();
      c.heartThickness = p.value("heartThickness").toDouble(c.heartThickness);
      c.heartInsetShare = p.value("heartInsetShare").toDouble(c.heartInsetShare);
      c.heartPoints = p.value("heartPoints").toInt(c.heartPoints);
      return c;
    }
  }  // namespace

  const LogoStageConfig& logoStageConfig() {
    static const LogoStageConfig cfg = parseConfig();
    return cfg;
  }

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
      if (!s.motion.isEmpty() && motionModeFromKey(s.motion) != mode) return QString();
      return s.name;
    }
    return QString();
  }

  bool showHasCloud(const QString& name, ParticleStyle* out) {
    const StageShow* s = showByName(name);
    if (!s) return false;
    if (!s->motion.isEmpty()) {
      if (out) *out = s->effect == StageEffect::FIRE ? ParticleStyle::FIRE
                    : s->effect == StageEffect::WATER ? ParticleStyle::WATER : ParticleStyle::DUST;
      return true;
    }
    // No motion of its own: it wears whatever particle style the user is running, and with
    // particles off there is no cloud and the light does the whole show. NEON is that light and
    // SUN its own ring of beams, so neither ever wears a cloud.
    if (s->effect == StageEffect::NEON || s->effect == StageEffect::SUN || !isDustAllowed())
      return false;
    if (out) *out = particleStyle();
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
