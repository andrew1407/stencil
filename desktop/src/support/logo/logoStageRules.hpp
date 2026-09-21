#pragma once
// The logo stage's table (browser/js/config/logoStage.json over the qrc) and the pure rules over
// it: which show an accent + motion mode opens, the cloud it wears, the stage sizes and the heart
// the pink show draws. Twin of browser/js/ui/logoStageRules.js, value for value.
#include <QColor>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

#include "motionPrefs.hpp"   // MotionMode, ParticleStyle

namespace stencil::support {

  enum class StageEffect { NEON, SUN, FIRE, WATER, DUST, SHRINK, GROW, FOLLOW, ESCAPE, PINK, FLY };

  struct StageShow {
    QString name;
    StageEffect effect = StageEffect::NEON;
    QStringList accents;
    QString motion;      // empty = any mode
    QString customHex;   // "*" = every other custom hex
  };

  struct LogoStageConfig {
    int holdMs = 3000;
    QString toast, toastGold, toastInk, toastGlow;
    QVector<StageShow> shows;
    // stage
    double logoShare = 0.66, bounceBigShare = 0.9, minShare = 0.1, roamShare = 0.08;
    double holdBoost = 3.2, scrimAlpha = 0.5;
    double markEdgeShare = 0.46875, markCornerShare = 0.203125;
    int revealMs = 520, hideMs = 360, beatMs = 1200, spinMs = 8000, holdRampMs = 240;
    // glow / sun
    double glowAlphaMin = 0.3, glowAlphaMax = 0.95, glowReachShare = 0.184, glowSteadyLit = 0.3, glowFloor = 0.45;
    int sunSpokes = 8;
    double sunGapShare = 0.08, sunLengthShare = 0.14, sunAlphaMin = 0.14, sunAlphaMax = 0.4;
    double sunSoftWidthShare = 0.024, sunBrightWidthShare = 0.012;
    // cloud
    double cloudRate = 48, cloudLifeMs[2] = {420, 900};
    double cloudSpeedShare[2] = {0.15, 0.55}, cloudSizeShare[2] = {0.006, 0.02};
    double cloudTailSpreadTurns = 0.17, cloudTailGapShare = 0.55, cloudTailSpeedScale = 8;
    double cloudTailMinSpeedPx = 40, cloudTailFullSpeedPx = 320;
    int cloudMaxLive = 1400;
    // per-effect
    int snapMs = 120, recoverMs = 900;
    double bounceStepShare = 0.3;
    double followStiffness = 9, followDragPerS = 3.4;
    double escapeRadiusPx = 300, escapeStiffness = 26, escapeDragPerS = 2.4;
    double flySpeedPx = 260, flyPunchPx = 640, flyDampPerS = 0.5, flyGrabLeadMs = 220;
    // pink
    QColor pinkBlank, pinkTint;
    QString heartStroke, heartFill;
    double heartThickness = 4, heartInsetShare = 0.1;
    int heartPoints = 22;
  };

  // Parsed once from :/config/logoStage.json.
  const LogoStageConfig& logoStageConfig();

  // The show a hold opens, or empty: a custom "#rrggbb" accent key picks by value, a preset by
  // its row, and a row naming a motion mode opens only under it.
  QString resolveShow(const QString& accentKey, MotionMode mode);
  const StageShow* showByName(const QString& name);
  QStringList typedWords();   // the show names, lower-cased

  // The cloud a show wears: its own for a styled show, the current one for a roaming show.
  bool showHasCloud(const QString& name, ParticleStyle* out);

  // Where a ray at `angle` from the centre meets the mark's own outline — a square of `size`
  // with the brand's rounded corners. A cloud is born along it, so the grains hug the art: a
  // circle buries the four corners, a sharp square leaves a bare crescent outside each one.
  QPointF markEdge(double size, double angle);

  int bigLogoSize(int w, int h);
  // A BOUNCING show (shrink, grow) travels the whole range, so its big end fills the window
  // rather than sitting at the still shows' resting size.
  int bounceBigSize(int w, int h);
  int minLogoSize(int w, int h);
  // A show that ROAMS wears a small mark: a thing moving over the window, not a backdrop.
  bool roams(const QString& name);
  int roamLogoSize(int w, int h);

  // The heart fitted to the centred square of a w×h image, y down, inset from its edges.
  QVector<QPointF> heartPoints(int w, int h, int n = 0);

}  // namespace stencil::support
