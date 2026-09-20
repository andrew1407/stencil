#include "LogoStage.hpp"

#include "ModalBackdrop.hpp"   // the modal scrim + blur a show wears too
#include "theme.hpp"          // accentShade

#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QToolButton>
#include <cmath>

namespace stencil::gui {

  using support::StageEffect;

  namespace {
    // The halo is rendered at most this wide and scaled up; the ramp is smooth, so nothing shows.
    constexpr int HALO_PX = 220;
    constexpr double PI = 3.14159265358979323846;
    // 0 → 1 → 0 on the beat, the cosine the browser's keyframes ride.
    double beatAt(double ms, double span) { return 0.5 - 0.5 * std::cos((2 * PI * ms) / span); }
    double spinAt(double ms, double span) { return std::fmod(ms, span) / span * 360.0; }
    QColor withAlpha(QColor c, double a) {
      c.setAlphaF(std::clamp(a, 0.0, 1.0));
      return c;
    }
  }  // namespace

  // Qt has no backdrop-filter, so the window behind is photographed and blurred, the way a modal's
  // backdrop is (support/ModalBackdrop.hpp). The stage's own paint is suppressed for the shot, or
  // it photographs itself — and a resize needs a NEW one, or the old frame stretches over the new.
  void LogoStage::takeBackdrop() {
    if (!window_ || window_->width() < 8 || window_->height() < 8) { backdrop_ = QPixmap(); return; }
    photographing_ = true;
    // A notice stands ABOVE the stage, so it would be baked into the backdrop and then stretched.
    if (hooks_.hideNotices) hooks_.hideNotices(true);
    const QPixmap shot = window_->grab();
    if (hooks_.hideNotices) hooks_.hideNotices(false);
    photographing_ = false;
    backdrop_ = support::ModalBackdrop::blurred(shot, support::ModalBackdrop::BLUR_PX);
  }

  void LogoStage::paintCloud(QPainter& p, const support::StagePose& pose) {
    const QColor accent = hooks_.accent ? hooks_.accent() : QColor(0x7c, 0x3a, 0xed);
    // The cloud wears the mark's own scale, so it grows out of the logo and shrinks back into it.
    const double scale = size_ > 0 ? pose.size / size_ : 1.0;
    cloud_.draw(p, QPointF(pose.x, pose.y), since_.elapsed(), accent,
                accentShade(accent, support::isParticleDark()), support::isParticleDark(), scale);
  }

  void LogoStage::paintEvent(QPaintEvent*) {
    // Invisible to its own photograph, so the backdrop is the window behind and not the last frame.
    if (photographing_) return;
    if (!open_ && leftAt_ < 0) return;
    const support::LogoStageConfig& cfg = support::logoStageConfig();
    const double t = since_.elapsed();
    const double beat = reduced_ ? 0.5 : beatAt(t, cfg.beatMs);
    // A cloud show's light is a steady lamp the grains fly through: all its motion, and
    // everything a hold adds, belongs to the cloud. Without a cloud the light does it all.
    const double boost = hasCloud_ ? 1.0 : boostNow();
    // Never all the way down: a neon sign breathes, it does not go out.
    const double lit = hasCloud_ ? cfg.glowSteadyLit : cfg.glowFloor + (1 - cfg.glowFloor) * beat;
    const double reveal = reduced_ ? 1.0 : std::min(1.0, t / double(cfg.revealMs));
    const double p01 = leftAt_ < 0 ? reveal : 1.0 - std::min(1.0, (t - leftAt_) / double(cfg.hideMs));
    const double fade = leftAt_ < 0 ? std::min(1.0, reduced_ ? 1.0 : t / double(cfg.revealMs))
                                    : std::max(0.0, 1.0 - (t - leftAt_) / double(cfg.hideMs));
    const support::StagePose pose = support::revealTween(from_, support::StagePose{pos_.x(), pos_.y(), size_}, p01);
    const QColor accent = hooks_.accent ? hooks_.accent() : QColor(0x7c, 0x3a, 0xed);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    // The backdrop a modal puts up: the window behind, blurred, under the same scrim. Both
    // halves ride the fade, so the sharp window dissolves into the blurred one.
    p.setOpacity(fade);
    if (!backdrop_.isNull()) p.drawPixmap(rect(), backdrop_);
    p.fillRect(rect(), QColor(0, 0, 0, int(cfg.scrimAlpha * 255)));
    p.setOpacity(1.0);

    paintCloud(p, pose);

    // The halo: solid to the mark's edge, then falling off. It follows the mark's OWN rounded
    // square (the notice's shining does the same for its pill) — a circle round a square leaves
    // the four corners unlit and reads as a disc behind the art rather than a glow off it.
    const QPointF centre(pose.x, pose.y);
    // A hold widens its REACH as well as its brightness; that reach is what reads as intensity.
    const double half = pose.size * cfg.markEdgeShare;
    const double reach = cfg.glowReachShare * pose.size * lit * boost;
    if (half > 0) {
      // Capped at the show's own ceiling, never 1: opaque, the light stops reading as light and
      // the window behind it disappears. A hold's intensity is carried by the REACH.
      const double a = std::min(cfg.glowAlphaMax,
          (cfg.glowAlphaMin + (cfg.glowAlphaMax - cfg.glowAlphaMin) * lit) * boost) * fade;
      const double corner = pose.size * cfg.markCornerShare;
      const auto box = [&](double grow) {
        return QRectF(centre.x() - half - grow, centre.y() - half - grow,
                      2 * (half + grow), 2 * (half + grow));
      };
      p.setPen(Qt::NoPen);
      // ONE smooth gradient, computed per pixel: the alpha falls from `a` at the mark's own
      // rounded edge to nothing `reach` beyond it. Stacked fills gave the same falloff but cost a
      // pass over the whole halo EACH, and their 8-bit rounding piled up into a colour cast.
      // Computed small and scaled up — the ramp is smooth, so nothing of the scale shows.
      const double side = 2 * (half + reach);
      const int px = std::clamp(int(std::ceil(side)), 8, HALO_PX);
      if (halo_.size() != QSize(px, px)) halo_ = QImage(px, px, QImage::Format_ARGB32_Premultiplied);
      halo_.fill(Qt::transparent);
      const double k = side / px;                          // a halo pixel, in stage pixels
      const double flat = std::max(0.0, half - corner);    // the square the corners round off
      const int r8 = accent.red(), g8 = accent.green(), b8 = accent.blue();
      for (int yy = 0; yy < px; ++yy) {
        QRgb* row = reinterpret_cast<QRgb*>(halo_.scanLine(yy));
        const double qy = std::max(0.0, std::abs((yy + 0.5) * k - side / 2) - flat);
        for (int xx = 0; xx < px; ++xx) {
          const double qx = std::max(0.0, std::abs((xx + 0.5) * k - side / 2) - flat);
          const double d = std::hypot(qx, qy) - corner;    // signed, from the rounded edge
          const double t = d <= 0 ? 1.0 : (reach > 0 ? 1 - d / reach : 0.0);
          if (t <= 0) continue;
          row[xx] = qPremultiply(qRgba(r8, g8, b8, int(a * t * 255 + 0.5)));
        }
      }
      p.drawImage(QRectF(centre.x() - side / 2, centre.y() - side / 2, side, side), halo_);
    }

    // The ring: spokes just outside the mark, turning on the spin and breathing on the beat.
    if (effect_ == StageEffect::SUN) {
      const double a = (cfg.sunAlphaMin + (cfg.sunAlphaMax - cfg.sunAlphaMin) * beat) * boost * fade;
      const double r1 = pose.size * 0.5 + cfg.sunGapShare * pose.size;
      const double r2 = r1 + cfg.sunLengthShare * pose.size;
      const double spin = reduced_ ? 0.0 : spinAt(t, cfg.spinMs);
      for (const auto& [width, alpha] : {std::pair{cfg.sunSoftWidthShare * pose.size, a * 0.45},
                                         std::pair{cfg.sunBrightWidthShare * pose.size, a}}) {
        p.setPen(QPen(withAlpha(accent, alpha), std::max(1.0, width), Qt::SolidLine, Qt::RoundCap));
        for (int i = 0; i < cfg.sunSpokes; ++i) {
          const double rad = qDegreesToRadians(spin + i * (360.0 / cfg.sunSpokes));
          const QPointF dir(std::cos(rad), std::sin(rad));
          p.drawLine(centre + dir * r1, centre + dir * r2);
        }
      }
    }

    // The mark sits still — only its light breathes. A show that changes size says so itself.
    if (!mark_.isNull()) {
      p.setOpacity(fade);
      p.drawPixmap(QRectF(pose.x - pose.size / 2, pose.y - pose.size / 2, pose.size, pose.size),
                   mark_, QRectF(mark_.rect()));
    }
  }

}  // namespace stencil::gui
