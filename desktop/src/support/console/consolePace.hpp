#pragma once
// The console text speed and the typewriter pace it sets: motion.json `consoleSpeed` is the
// range, console/fx.json `reveal` the tuned clocks, scaled by (1 − s) / s so the default speed
// plays them as tuned. Header-only like motionPrefs.hpp; twin of browser/js/ui/console/reveal.js.
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QtGlobal>
#include <algorithm>
#include "motionPrefs.hpp"   // motionReduced
#include <cmath>

namespace stencil::support {

  struct ConsoleSpeedRange {
    double min = 0.01;
    double max = 1.0;
    double def = 0.25;
    double step = 0.01;
  };
  inline const ConsoleSpeedRange& consoleSpeedRange() {
    static const ConsoleSpeedRange r = [] {
      ConsoleSpeedRange out;
      QFile f(QStringLiteral(":/config/motion.json"));
      if (!f.open(QIODevice::ReadOnly)) return out;
      const QJsonObject s = QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("consoleSpeed")).toObject();
      out.min = s.value(QLatin1String("min")).toDouble(out.min);
      out.max = s.value(QLatin1String("max")).toDouble(out.max);
      out.def = s.value(QLatin1String("default")).toDouble(out.def);
      out.step = s.value(QLatin1String("step")).toDouble(out.step);
      return out;
    }();
    return r;
  }
  // NaN is the default, hundredths otherwise.
  inline double clampConsoleSpeed(double v) {
    const ConsoleSpeedRange& r = consoleSpeedRange();
    if (!std::isfinite(v)) return r.def;
    return std::round(std::clamp(v, r.min, r.max) * 100.0) / 100.0;
  }
  namespace detail {
    inline double& consoleSpeedState() {
      static double s = consoleSpeedRange().def;
      return s;
    }
  }  // namespace detail
  inline double consoleSpeed() { return detail::consoleSpeedState(); }
  inline void setConsoleSpeed(double v) { detail::consoleSpeedState() = clampConsoleSpeed(v); }

  // console/fx.json `reveal`: a line sweeps in over `jumps` steps of stepMs; one arriving within
  // quietMs of the last is a burst and takes burstJumps; past burstMaxMs of pouring it lands at once.
  struct RevealFx {
    int stepMs = 13;
    int jumps = 8;
    int burstJumps = 2;
    int quietMs = 250;
    int burstMaxMs = 900;
    int ceilingMs = 5000;
  };
  inline const RevealFx& revealFx() {
    static const RevealFx fx = [] {
      RevealFx out;
      QFile f(QStringLiteral(":/config/console/fx.json"));
      if (!f.open(QIODevice::ReadOnly)) return out;
      const QJsonObject r = QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("reveal")).toObject();
      out.stepMs = r.value(QLatin1String("stepMs")).toInt(out.stepMs);
      out.jumps = r.value(QLatin1String("jumps")).toInt(out.jumps);
      out.burstJumps = r.value(QLatin1String("burstJumps")).toInt(out.burstJumps);
      out.quietMs = r.value(QLatin1String("quietMs")).toInt(out.quietMs);
      out.burstMaxMs = r.value(QLatin1String("burstMaxMs")).toInt(out.burstMaxMs);
      out.ceilingMs = r.value(QLatin1String("ceilingMs")).toInt(out.ceilingMs);
      return out;
    }();
    return fx;
  }

  struct RevealPace {
    int stepMs = 13;
    int quietMs = 250;
    int burstMaxMs = 900;
    bool instant = false;
  };
  // A window never drops under its floor nor past the ceiling; the fastest speed is instant.
  inline RevealPace revealPace(double speed) {
    const RevealFx& fx = revealFx();
    const double s = clampConsoleSpeed(speed);
    const double k = (1.0 - s) / s;
    const auto scale = [&fx, k](int base, int floor) {
      const int v = int(std::lround(base * k));
      return v > floor ? std::min(v, fx.ceilingMs) : floor;
    };
    RevealPace p;
    p.stepMs = scale(fx.stepMs, 1);
    // The quiet window must outlast one line's own sweep, or every burst line gets the long form.
    p.quietMs = scale(fx.quietMs, fx.quietMs);
    p.burstMaxMs = scale(fx.burstMaxMs, fx.burstMaxMs);
    p.instant = s >= consoleSpeedRange().max || motionReduced();
    return p;
  }

  // How the output arriving now is swept: `jumps` 0 = at once.
  struct RevealPlan {
    int jumps = 0;
    int stepMs = 0;
  };
  class RevealTracker {
   public:
    RevealPlan plan(qint64 nowMs, double speed) {
      const RevealPace p = revealPace(speed);
      const bool inBurst = lastMs != 0 && nowMs - lastMs <= p.quietMs;
      if (!inBurst) burstMs = nowMs;
      lastMs = nowMs;
      if (p.instant || (inBurst && nowMs - burstMs > p.burstMaxMs)) return {};
      return {inBurst ? revealFx().burstJumps : revealFx().jumps, p.stepMs};
    }
    // A sweep ended now: the quiet window counts from its end (reveal.zig stamps it on the way out).
    void done(qint64 nowMs) { lastMs = nowMs; }
    void reset() { lastMs = burstMs = 0; }

   private:
    qint64 lastMs = 0;
    qint64 burstMs = 0;
  };

}  // namespace stencil::support
