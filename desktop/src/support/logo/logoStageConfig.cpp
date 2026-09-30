#include "logoStageRules.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// The logo stage's table, read once from common/config/logoStage.json over the qrc; the
// defaults in LogoStageConfig stand for any key the file leaves out.

namespace stencil::support {

  namespace {
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
      if (k == "webcore") return StageEffect::WEBCORE;
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
      c.typeGapMs = root.value("typeGapMs").toInt(c.typeGapMs);
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
      c.glowNeonBeatMs = g.value("neonBeatMs").toInt(c.glowNeonBeatMs);
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
      c.cloudStyleThrowShare = cl.value("styleThrowShare").toDouble(c.cloudStyleThrowShare);
      c.cloudLineMinPx = cl.value("lineMinPx").toDouble(c.cloudLineMinPx);
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

}  // namespace stencil::support
