#include "iconMotion.hpp"

namespace stencil::gui {

  IconPose icm::lerpPose(const IconPose& a, const IconPose& b, double u) {
    IconPose p;
    p.tx = mix(a.tx, b.tx, u);
    p.ty = mix(a.ty, b.ty, u);
    p.rotate = mix(a.rotate, b.rotate, u);
    p.sx = mix(a.sx, b.sx, u);
    p.sy = mix(a.sy, b.sy, u);
    p.skewX = mix(a.skewX, b.skewX, u);
    p.hasDashOffset = a.hasDashOffset || b.hasDashOffset;
    p.dashOffset = mix(a.dashOffset, b.dashOffset, u);
    return p;
  }

  double icm::ease(QEasingCurve::Type type, double u) {
    return QEasingCurve(type).valueForProgress(std::clamp(u, 0.0, 1.0));
  }


  // Geometric identity only — a dash offset is written as its own attribute pair.
  // A WHOLE number of turns counts: the quarter-turn buttons, the live-sync wheel and
  // the sun each end their play a full revolution on, which leaves the glyph exactly as
  // it was — so the rest frame is the canon's own markup, byte for byte, and nothing
  // marks a settled icon as still transformed.
  bool icm::isIdentity(const IconPose& p) {
    const double spun = std::fmod(std::abs(p.rotate), 360.0);
    return std::abs(p.tx) < 1e-4 && std::abs(p.ty) < 1e-4
           && (spun < 1e-4 || 360.0 - spun < 1e-4) && std::abs(p.sx - 1) < 1e-4
           && std::abs(p.sy - 1) < 1e-4 && std::abs(p.skewX) < 1e-4;
  }


  // Where `part`'s `index`-th element sits at `elapsed` ms into the play.
  IconPose icm::poseAt(const IconMotionPart& part, bool hold, int index, double elapsed) {
    const double local = elapsed - part.delayMs - double(part.staggerMs) * index;
    if (hold) {
      // The whole glyph shares one latch; each part eases its own pose out of identity.
      const double u = ease(part.easing,
                            part.durationMs > 0 ? local / part.durationMs : 1.0);
      return lerpPose(IconPose{}, part.to, u);
    }
    if (part.keys.isEmpty()) return IconPose{};
    // animation-fill-mode: both — the part waits in keyframe 0 through its delay and
    // stays on the last one after it lands.
    if (local <= 0) return part.keys.first().pose;
    const double pct = part.durationMs > 0 ? 100.0 * local / part.durationMs : 100.0;
    if (pct >= part.keys.last().at) return part.keys.last().pose;
    for (int i = 1; i < part.keys.size(); ++i) {
      const IconMotionKey& b = part.keys.at(i);
      if (pct > b.at) continue;
      const IconMotionKey& a = part.keys.at(i - 1);
      const double span = b.at - a.at;
      // The easing runs between each PAIR of keyframes, as CSS applies it.
      const double u = span > 0 ? ease(part.easing, (pct - a.at) / span) : 1.0;
      return lerpPose(a.pose, b.pose, u);
    }
    return part.keys.last().pose;
  }


  // The pose as an SVG transform list. transform-origin wraps the list, and the order
  // inside it is CSS's: translate, then rotate / scale / skew.
  QString icm::transformAttr(const IconPose& p, const QPointF& origin) {
    QString t;
    t += QStringLiteral("translate(%1 %2)").arg(origin.x()).arg(origin.y());
    if (std::abs(p.tx) > 1e-4 || std::abs(p.ty) > 1e-4)
      t += QStringLiteral(" translate(%1 %2)").arg(p.tx).arg(p.ty);
    if (std::abs(p.rotate) > 1e-4) t += QStringLiteral(" rotate(%1)").arg(p.rotate);
    if (std::abs(p.sx - 1) > 1e-4 || std::abs(p.sy - 1) > 1e-4)
      t += QStringLiteral(" scale(%1 %2)").arg(p.sx).arg(p.sy);
    if (std::abs(p.skewX) > 1e-4) t += QStringLiteral(" skewX(%1)").arg(p.skewX);
    t += QStringLiteral(" translate(%1 %2)").arg(-origin.x()).arg(-origin.y());
    return t;
  }


  // The glyph's markup posed for `elapsed` ms into `spec`. Pure — this is the whole
  // rendering half of the port, and what the headless test drives.
  QString iconMotionMarkup(const QString& glyph, const IconMotionSpec& spec,
                           const QVector<IconMotionPart>& parts, double elapsed) {
    const QString base = iconMarkup(glyph);
    if (base.isEmpty() || parts.isEmpty()) return base;
    const QVector<icm::Tag>& tags = icm::tagsOf(glyph);

    QString out = base;
    // Edits are applied from the END so the earlier insertion points stay valid.
    struct Edit { int at; QString attrs; };
    QVector<Edit> edits;
    QString wrap;

    for (const IconMotionPart& part : parts) {
      if (part.hook.isEmpty()) {
        const IconPose p = icm::poseAt(part, spec.hold, 0, elapsed);
        if (!icm::isIdentity(p)) wrap = icm::transformAttr(p, part.origin);
        continue;
      }
      int index = 0;
      for (const icm::Tag& t : tags) {
        if (!icm::hasHook(t, part.hook)) continue;
        const IconPose p = icm::poseAt(part, spec.hold, index, elapsed);
        QString attrs;
        // A landed draw-on mark is the whole mark: no dash attributes at all, so the
        // rest pose is byte-for-byte the canon's own markup.
        if (part.dashArray > 0 && p.hasDashOffset && std::abs(p.dashOffset) > 1e-3)
          attrs += QStringLiteral(" stroke-dasharray=\"%1\" stroke-dashoffset=\"%2\"")
                       .arg(part.dashArray)
                       .arg(p.dashOffset);
        if (!icm::isIdentity(p)) {
          const QPointF origin = part.originSelf ? icm::selfCentre(t) : part.origin;
          attrs += QStringLiteral(" transform=\"%1\"").arg(icm::transformAttr(p, origin));
        }
        if (!attrs.isEmpty()) edits.push_back({t.insertAt, attrs});
        ++index;
      }
    }
    std::sort(edits.begin(), edits.end(),
              [](const Edit& a, const Edit& b) { return a.at > b.at; });
    for (const Edit& e : edits) out.insert(e.at, e.attrs);
    if (!wrap.isEmpty())
      out = QStringLiteral("<g transform=\"%1\">%2</g>").arg(wrap, out);
    return out;
  }


  // …the base pose set, which is what every glyph but `maximize` ever uses.
  QString iconMotionMarkup(const QString& glyph, const IconMotionSpec& spec, double elapsed) {
    return iconMotionMarkup(glyph, spec, spec.parts, elapsed);
  }


  // The motion designed for `glyph`, or nullptr when it has none.
  const IconMotionSpec* iconMotionFor(const QString& glyph) {
    const auto it = icm::table().constFind(glyph);
    return it == icm::table().constEnd() ? nullptr : &it.value();
  }
}  // namespace stencil::gui
