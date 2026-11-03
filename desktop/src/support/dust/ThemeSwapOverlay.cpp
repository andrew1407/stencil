#include "ThemeSwapOverlay.hpp"

#include <QApplication>
#include <QCursor>
#include <QDockWidget>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace stencil::gui {

  namespace {
    int motionMs(const char* key, int fallback) {
      QFile f(QStringLiteral(":/config/motion.json"));
      if (!f.open(QIODevice::ReadOnly)) return fallback;
      const int v = QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("ui")).toObject()
                        .value(QLatin1String(key)).toInt(0);
      return v > 0 ? v : fallback;
    }
  }  // namespace

  int ThemeSwapOverlay::swapMs() {
    static const int ms = motionMs("THEME_SWAP_MS", 280);
    return ms;
  }

  int ThemeSwapOverlay::dustLifeMs() {
    static const int ms = motionMs("SWAP_DUST_LIFE_MS", 340);
    return ms;
  }

  // NOT a plain ease-out: the wipe is a CIRCLE, so area grows as r^2 and OutCubic read as a snap.
  // Same shape as the browser's cubic-bezier(0.4, 0.25, 0.95, 1) (browser/css/animations/).
  double ThemeSwapOverlay::bezierY(double t, double x1, double y1, double x2, double y2) {
    return support::bezierY(t, x1, y1, x2, y2);
  }

  double ThemeSwapOverlay::grainEase(double t) {
    static const std::array<double, GRAIN_STEPS + 1> curve = [] {
      std::array<double, GRAIN_STEPS + 1> c{};
      for (int i = 0; i <= GRAIN_STEPS; i++)
        c[i] = bezierY(double(i) / GRAIN_STEPS, 0.22, 0.55, 0.3, 1.0);
      c.front() = 0.0;
      c.back() = 1.0;
      return c;
    }();
    return curve[std::clamp(int(std::lround(t * GRAIN_STEPS)), 0, GRAIN_STEPS)];
  }


  double ThemeSwapOverlay::edgeJitter(support::ParticleStyle s, int k, int points) {
    const double t = double(k) / points;
    if (s == support::ParticleStyle::WATER)
      return WATER_AMP * std::sin(TAU * WATER_WAVES * t) + WATER_RIPPLE_AMP * std::sin(TAU * WATER_RIPPLE * t + 1);
    if (s == support::ParticleStyle::FIRE) {
      const double tongue = std::floor(t * FIRE_TONGUES), u = t * FIRE_TONGUES - tongue;
      const double h = FIRE_BASE + FIRE_VARY * dustNoise(int(tongue), 5);
      return h * std::pow(std::sin(TAU / 2 * u), 3) - FIRE_DIP + FIRE_JAG * (dustNoise(k, 7) * 2 - 1);
    }
    return 0.0;
  }


  // The ring's base overshoots by the deepest dip so the finished front clears the corner.
  double ThemeSwapOverlay::edgeDipOf(support::ParticleStyle s) {
    if (s == support::ParticleStyle::WATER) return WATER_AMP + WATER_RIPPLE_AMP;
    if (s == support::ParticleStyle::FIRE) return FIRE_DIP + FIRE_JAG;
    return 0.0;
  }


  // Pure — the headless test pins it to the coverage contract motion.test.js holds swapEdgePolygon to.
  double ThemeSwapOverlay::edgeRadiusAt(int k, double e, double full, support::ParticleStyle s) {
    return e * full * edgeBaseOf(s) * (1 + edgeJitter(s, k));
  }


  // The shared scatter hash (browser tileNoise, DisintegrateOverlay::cellNoise).
  double ThemeSwapOverlay::dustNoise(int a, int b) {
    const double h = std::sin(a * 127.1 + b * 311.7) * 43758.5453;
    return h - std::floor(h);
  }

  bool ThemeSwapOverlay::dustMoteAt(int i, double ms, const QPointF& origin, double full,
                                    const QSizeF& bounds, DustMote* out,
                                    support::ParticleStyle s) {
    const double n = dustNoise(i, 3), m = dustNoise(i + 57, 11), q = dustNoise(i + 13, 29);
    const double u = DUST_MIN_T + m * (DUST_MAX_T - DUST_MIN_T);
    const double life = (ms - u * swapMs()) / dustLifeMs();
    if (life <= 0.0 || life >= 1.0) return false;
    const double angle = n * TAU;
    // Just behind even the deepest dip, so grains and clip read as one front.
    const double r = swapEase(u) * full * (1 - edgeDipOf(s)) - q * 6;
    if (r <= 0) return false;
    const double hx = origin.x() + std::cos(angle) * r;
    const double hy = origin.y() + std::sin(angle) * r;
    if (hx < -16 || hy < -16 || hx > bounds.width() + 16 || hy > bounds.height() + 16) return false;
    // The browser's swapDustFrame, op for op.
    const double e = grainEase(life);
    const double o = life < GRAIN_FLARE
                         ? grainEase(life / GRAIN_FLARE)
                         : 1.0 - grainEase((life - GRAIN_FLARE) / (1.0 - GRAIN_FLARE));
    const double alpha = (0.75 + q * 0.25) * o;
    if (alpha < 1.0 / 255) return false;   // below one 8-bit step — nothing to paint
    const double d = 8 + q * 14;
    const double dx = std::round(std::cos(angle) * d + (m - 0.5) * 14);
    const double dy = std::round(std::sin(angle) * d + (0.5 - q) * 14);
    out->x = hx + dx * e;
    out->y = hy + dy * e;
    out->alpha = alpha;
    out->size = (2.5 + n * 3.5) * (1.0 - 0.7 * e);
    out->accent = i % 4 == 0;
    out->life = life;
    out->w = dustNoise(i + 71, 13);
    out->len = std::hypot(dx, dy);
    out->heading = support::headingOf(dx, dy, false);
    return true;
  }


  // Call with the colours as they stood BEFORE the restyle (browser dust/swapDust.js swapDustPaint palette).
  void ThemeSwapOverlay::seedDust(const QColor& accent, const QColor& shade, bool dark) {
    if (!accent.isValid()) return;
    dustAccent = accent;
    dustShade = shade.isValid() ? shade : accent;
    dustDark = dark;
    dust = true;
  }
  QList<QPointer<ThemeSwapOverlay>> ThemeSwapOverlay::captureWindows(QWidget* owner, QPoint origin, const QColor& accent,
                                                                     const QColor& shade, bool dark, bool fromTop) {
    const auto seeded = [&](ThemeSwapOverlay* fx) {
      if (fx) fx->fromTop = fromTop;
      if (fx && support::isDustAllowed()) fx->seedDust(accent, shade, dark);
      return QPointer<ThemeSwapOverlay>(fx);
    };
    QList<QPointer<ThemeSwapOverlay>> out{seeded(capture(owner, origin))};
    if (!owner) return out;
    const QPoint at = origin.x() >= 0 ? owner->mapToGlobal(origin) : QCursor::pos();
    const QWidget* focus = QApplication::focusWidget();
    for (QWidget* w : QApplication::topLevelWidgets()) {
      if (w == owner || !w->isVisible() || w->testAttribute(Qt::WA_TransparentForMouseEvents)) continue;
      const Qt::WindowType type = w->windowType();
      if (type != Qt::Dialog && type != Qt::Sheet && type != Qt::Window && !qobject_cast<QDockWidget*>(w)) continue;
      QWidget* root = w;   // the window it belongs to: a dialog over a dialog over this one is still this one's
      while (root->parentWidget()) root = root->parentWidget()->window();
      if (root != owner) continue;
      const QPoint from = focus && focus->window() == w ? focus->mapTo(w, focus->rect().center()) : w->mapFromGlobal(at);
      if (ThemeSwapOverlay* fx = capture(w, from)) out << seeded(fx);
    }
    return out;
  }

}  // namespace stencil::gui
