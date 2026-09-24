// The stage cloud against the browser's (support/logo/logoStageCloud.cpp, js/ui/logo/stageCloud.js):
// the same seeded dust cloud, its live grains, and what a frame of it PAINTS — how many grains,
// at what opacity, how far out. Browser values printed from node with the same mulberry32 seed.
#include "logoStageCloud.hpp"

#include <QApplication>
#include <QPaintEngine>
#include <QPaintDevice>
#include <QPainter>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "../../support/check.hpp"

using namespace stencil::support;

namespace {
  // browser: a |= 0; a + 0x6D2B79F5; Math.imul ...
  StageRnd mulberry(uint32_t a) {
    return [a]() mutable {
      a += 0x6D2B79F5u;
      uint32_t t = (a ^ (a >> 15)) * (1u | a);
      t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
      return double(t ^ (t >> 14)) / 4294967296.0;
    };
  }

  struct Blit { double opacity; QPointF at; };

  class Recorder : public QPaintEngine {
   public:
    QVector<Blit> blits;
    double opacity = 1.0;
    Recorder() : QPaintEngine(QPaintEngine::AllFeatures) {}
    bool begin(QPaintDevice*) override { return true; }
    bool end() override { return true; }
    void updateState(const QPaintEngineState& s) override {
      if (s.state() & QPaintEngine::DirtyOpacity) opacity = s.opacity();
    }
    void drawPixmap(const QRectF&, const QPixmap&, const QRectF&) override {}
    void drawImage(const QRectF& r, const QImage&, const QRectF&, Qt::ImageConversionFlags) override {
      blits.push_back({opacity, r.center()});
    }
    Type type() const override { return QPaintEngine::User; }
  };

  class Canvas : public QPaintDevice {
   public:
    mutable Recorder engine;
    QPaintEngine* paintEngine() const override { return &engine; }
   protected:
    int metric(PaintDeviceMetric m) const override {
      switch (m) {
        case PdmWidth: case PdmHeight: return 1000;
        case PdmDepth: return 32;
        case PdmDpiX: case PdmDpiY: case PdmPhysicalDpiX: case PdmPhysicalDpiY: return 96;
        case PdmDevicePixelRatio: return 1;
        case PdmDevicePixelRatioScaled: return int(QPaintDevice::devicePixelRatioFScale());
        default: return 1000;
      }
    }
  };

  struct Browser { int frames, live, drawn; double meanAlpha, meanReach; };
  bool within(double got, double want, double tol) { return std::abs(got - want) <= tol; }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  constexpr double SIZE = 80, DT = 16.7;
  const QPointF centre(500, 500);
  for (const Browser& b : {Browser{12, 576, 576, 0.7899, 44.103}, Browser{120, 1366, 1301, 0.5402, 48.091}}) {
    std::printf("dust, seed 7, mark %gpx, %d frames of %gms:\n", SIZE, b.frames, DT);
    LogoStageCloud cloud;
    cloud.setStyle(ParticleStyle::DUST, true);
    const StageRnd rnd = mulberry(7);
    for (int i = 0; i < b.frames; ++i) cloud.step(DT, SIZE, 1.0, QPointF(0, 0), rnd);
    check(cloud.live() == b.live, "the live grain count is the browser's");

    Canvas canvas;
    QPainter p(&canvas);
    cloud.draw(p, centre, b.frames * DT, QColor(0x7c, 0x3a, 0xed), QColor(0x4c, 0x1d, 0x95), false);
    p.end();
    const QVector<Blit>& blits = canvas.engine.blits;
    double alpha = 0, reach = 0;
    for (const Blit& k : blits) {
      alpha += k.opacity;
      reach += std::hypot(k.at.x() - centre.x(), k.at.y() - centre.y());
    }
    const int n = std::max<int>(1, blits.size());
    std::printf("  painted %d (browser %d), mean alpha %.4f (%.4f), mean reach %.3f (%.3f)\n",
                int(blits.size()), b.drawn, alpha / n, b.meanAlpha, reach / n, b.meanReach);
    check(within(blits.size(), b.drawn, b.drawn * 0.02), "as many grains painted as the browser's");
    check(within(alpha / n, b.meanAlpha, 0.02), "each painted at the browser's faded alpha");
    check(within(reach / n, b.meanReach, 1.0), "reaching as far from the mark as the browser's");
  }
  check(drawnStageAlpha(0.005) == 0 && drawnStageAlpha(0.5) == 5.0 / 9, "the nine buckets");
  std::printf(failures ? "FAILED: %d\n" : "all passed (%d failures)\n", failures);
  return failures ? 1 : 0;
}
