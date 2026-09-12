#include "dustKit.hpp"

namespace stencil::support {

  GrainShape grainShape(ParticleStyle s, double w) {
    const double pick = fract(w * 7.31 + 0.17);
    if (s == ParticleStyle::Water) return pick < shape::kWaterWaveShare ? GrainShape::Wave : GrainShape::Oval;
    if (s == ParticleStyle::Fire) return pick < shape::kFireStreakShare ? GrainShape::Streak : GrainShape::Triangle;
    return GrainShape::Disc;
  }


  // The heading of a grain thrown `dx, dy` from home; a gather flies the other way.
  double headingOf(double dx, double dy, bool fromFar) {
    return std::atan2(dy, dx) + (fromFar ? style::kPi : 0.0);
  }


  // A polygon shape at `at`, radius r, heading a (Disc and Oval are drawn as ellipses).
  QPolygonF shapePolygon(GrainShape s, const QPointF& at, double r, double a) {
    using namespace shape;
    QPolygonF out;
    const double c = std::cos(a), sn = std::sin(a);
    const auto put = [&](double u, double v) { out << QPointF(at.x() + u * c - v * sn, at.y() + u * sn + v * c); };
    if (s == GrainShape::Triangle) {
      put(kTriTip * r, 0); put(-kTriBase * r, kTriHalf * r); put(-kTriBase * r, -kTriHalf * r);
    } else if (s == GrainShape::Streak) {
      put(kStreakHead * r, kStreakHeadHalf * r); put(-kStreakTail * r, kStreakTailHalf * r);
      put(-kStreakTail * r, -kStreakTailHalf * r); put(kStreakHead * r, -kStreakHeadHalf * r);
    } else if (s == GrainShape::Wave) {
      for (int i = 0; i < kWaveSamples; i++) {
        const double k = double(i) / (kWaveSamples - 1);
        put((k - 0.5) * kWaveLen * r, std::sin(k * kWaveWaves * 2 * style::kPi) * kWaveAmp * r + kWaveHalf * r);
      }
      for (int i = kWaveSamples - 1; i >= 0; i--) {
        const double k = double(i) / (kWaveSamples - 1);
        put((k - 0.5) * kWaveLen * r, std::sin(k * kWaveWaves * 2 * style::kPi) * kWaveAmp * r - kWaveHalf * r);
      }
    }
    return out;
  }


  // Paint one grain — a disc, or any GrainShape at heading `a`. Antialiasing must be OFF
  // on `p` (the sprite carries its own), or the raster engine leaves its 1:1 blit path.
  void MoteSprites::draw(QPainter& p, const QPointF& at, double radius, const QColor& colour,
                         GrainShape shape, double a) {
    const double alpha = colour.alphaF();
    if (radius < 0.2 || alpha <= 1.0 / 255) return;
    const double dpr = p.device()->devicePixelRatio();
    if (dpr != dpr_) { cache_.clear(); dpr_ = dpr; }
    // A shaped grain is cached at half the radius resolution (every miss rasterises a
    // polygon, and a quarter-pixel of size is invisible on a moving drop).
    const bool disc = shape == GrainShape::Disc;
    const double step = disc ? kRadiusStep : kRadiusStep * 2;
    const int rb = std::min(kMaxRadiusSteps, std::max(1, int(std::lround(radius / step))));
    const double xd = at.x() * dpr, yd = at.y() * dpr;
    const double xf = std::floor(xd), yf = std::floor(yd);
    const int px = xd - xf >= 0.5 ? 1 : 0, py = yd - yf >= 0.5 ? 1 : 0;
    const int hs = disc ? 0
        : ((int(std::lround(a / (2 * style::kPi) * kHeadingSteps)) % kHeadingSteps) + kHeadingSteps) % kHeadingSteps;
    const quint32 key = (colourKey(colour) << 16) | quint32(rb << 10) | quint32(int(shape) << 7)
                      | quint32(hs << 2) | quint32(py << 1) | quint32(px);
    auto it = cache_.find(key);
    if (it == cache_.end()) {
      if (cache_.size() >= kMaxCached) cache_.clear();
      it = cache_.insert(key, build(rb * step, px, py, colour, shape, hs * 2 * style::kPi / kHeadingSteps));
    }
    const QImage& img = it.value();
    const int half = img.width() / 2;   // device px; the disc is centred at half (+ phase)
    p.setOpacity(alpha);
    p.drawImage(QPointF((xf - half) / dpr, (yf - half) / dpr), img);
  }


  // 5 bits a channel: a grain a 32nd of a step off its neighbour's colour shares its disc.
  quint32 MoteSprites::colourKey(const QColor& c) {
    return quint32((c.red() >> 3) << 10 | (c.green() >> 3) << 5 | (c.blue() >> 3));
  }


  // The exact shape, drawn with the painter's current brush (the cache's fallback, and
  // what every sprite is rasterised from).
  void MoteSprites::drawExact(QPainter& q, GrainShape shape, const QPointF& at, double r,
                              double a) {
    if (shape == GrainShape::Disc) { q.drawEllipse(at, r, r); return; }
    if (shape == GrainShape::Oval) {
      q.save();
      q.translate(at);
      q.rotate(a * 180.0 / style::kPi);
      q.drawEllipse(QPointF(0, 0), shape::kOvalRx * r, shape::kOvalRy * r);
      q.restore();
      return;
    }
    q.drawPolygon(shapePolygon(shape, at, r, a));
  }


  // How far a shape reaches from its centre, in radii — the sprite is only as big as
  // that (a blit costs by area, and a spark's tail is the longest reach).
  double MoteSprites::reachOf(GrainShape s) {
    using namespace shape;
    switch (s) {
      case GrainShape::Oval: return kOvalRx;
      case GrainShape::Wave: return std::hypot(kWaveLen / 2, kWaveAmp + kWaveHalf);
      case GrainShape::Triangle: return std::max(kTriTip, std::hypot(kTriBase, kTriHalf));
      case GrainShape::Streak: return kStreakTail;
      case GrainShape::Disc: break;
    }
    return 1.0;
  }

  QImage MoteSprites::build(double radius, int px, int py, const QColor& colour,
                            GrainShape shape, double a) const {
    const double r = radius * dpr_;
    const int half = int(std::ceil(r * reachOf(shape))) + 1;
    QImage img(half * 2, half * 2, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter q(&img);
    q.setRenderHint(QPainter::Antialiasing, true);
    q.setPen(Qt::NoPen);
    QColor solid = colour;
    solid.setAlphaF(1.0);
    q.setBrush(solid);
    drawExact(q, shape, QPointF(half + px * 0.5, half + py * 0.5), r, a);
    q.end();
    img.setDevicePixelRatio(dpr_);   // logical size = device / dpr, so it blits 1:1
    return img;
  }
}  // namespace stencil::support
