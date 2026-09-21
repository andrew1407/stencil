#pragma once
// The cloud around the stage's mark: motes born on its edge, posed by the shared styleFrame and
// blitted through the shared sprite cache. Twin of browser/js/ui/logo/stageCloud.js.
#include <QColor>
#include <QPainter>
#include <QPointF>
#include <QVector>

#include "dustKit.hpp"
#include "motionPrefs.hpp"

namespace stencil::support {

  struct StageMote {
    double x = 0, y = 0, vx = 0, vy = 0, age = 0, life = 0, r = 0, w = 0, len = 0;
  };

  class LogoStageCloud {
   public:
    // A null style flies nothing: the show has no cloud, or motion is off.
    void setStyle(ParticleStyle style, bool on);
    void clear();
    int live() const { return int(motes.size()); }
    // `boost` is the stage's: 1 at rest, more under the pointer, most while it is held.
    void step(double dt, double size, double boost, const QPointF& dir = QPointF(0, 0));
    // `scale` is the mark's own: the grains grow out of the header logo with it and shrink back.
    void draw(QPainter& p, const QPointF& at, double ms, const QColor& accent, const QColor& shade,
              bool dark, double scale = 1.0);

   private:
    QVector<StageMote> motes;
    MoteSprites sprites;
    ParticleStyle style = ParticleStyle::DUST;
    bool on = false;
  };

  StageMote newStageMote(double size, double reach, const QPointF& dir = QPointF(0, 0));
  bool stepStageMote(StageMote& m, double dt);
  double stageMoteAlpha(const StageMote& m);
  int spawnStageCount(double boost, double dt);

}  // namespace stencil::support
