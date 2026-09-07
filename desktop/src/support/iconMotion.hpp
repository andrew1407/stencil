#pragma once
// Per-icon hover motion — every glyph mimes its OWN action.
//
// Port of browser/js/config/iconMotion.json (the canonical table, qrc-embedded here) and
// its CSS realisation in browser/css/animations.css. One generic tilt for everything is
// worse than none — a minus that swells reads as "increase" — so the trash lid lifts, the
// download arrow travels down and upload's up, the folder tips open, the chain links join,
// the sun shakes, the fullscreen corners extend (and RETRACT on the control that leaves
// fullscreen), plus grows, minus shrinks, the layers assemble, the sparkle dots type.
//
// How it works in Qt. QSvgRenderer has no CSS engine and cannot address a class, so a frame
// is produced by REWRITING the glyph's markup: a `transform` (and, for the draw-on marks, a
// stroke-dasharray/dashoffset) is injected into the start tag of each element carrying the
// table's `ic-*` hook, and the result goes down iconSet's ordinary rasterize path. The hooks
// are already in the shared canon (browser/js/config/icons.json) and are inert at rest.
//
// The trigger is one application-wide event filter (installIconMotion()): a button carries
// no glyph name, but iconSet::iconRequestForKey() traces its QIcon back to the glyph, colour
// and size it was built from — so no icon call site has to change. Nothing here moves a box:
// only the icon's own pixels change, so no control can reflow, and it composes with the
// shimmer sweep and the button lift instead of fighting them.
//
// Reduced motion (support::motionReduced() / STENCIL_NO_ANIM=1) cancels every motion: the
// rest pose IS each design's end state, so nothing is lost.
//
// Header-only and Q_OBJECT-free (no signals or slots of its own), so it needs no MOC.
#include "faceSwap.hpp"      // faceSwapping() — a face mid-swap owns the glyph
#include "iconSet.hpp"
#include "modalReveal.hpp"   // support::motionReduced()

#include <QAbstractAnimation>
#include <QAbstractButton>
#include <QCoreApplication>
#include <QEasingCurve>
#include <QEvent>
#include <QFile>
#include <QHash>
#include <QHoverEvent>
#include <QIcon>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPointF>
#include <QSignalBlocker>
#include <QString>
#include <QVariantAnimation>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace stencil::gui {

  // Set on a control that must keep its glyph out of this: the fold chevrons, whose
  // rotation is STATE (open/closed), not hover feedback — the browser's `[id^="toggle-"]`
  // opt-out (iconMotion.json trigger.excluded).
  inline constexpr const char* kNoIconMotionProperty = "stencilNoIconMotion";
  // Optional state a glyph's motion branches on — "active" picks iconMotion.json's
  // `variants.active` (the fullscreen control that LEAVES fullscreen). A checkable
  // button's checked state means the same thing and needs no property.
  inline constexpr const char* kIconStateProperty = "stencilIconState";
  inline constexpr const char* kIconMotionAnimName = "stencilIconMotion";
  // Set on a QMenu once its hovered() signal has been wired to the row motion.
  inline constexpr const char* kMenuHoverWiredProperty = "stencilIcmHovered";

  // ── The table ───────────────────────────────────────────────────────────────
  // A pose in the glyph's own 24-unit space. Absent fields are identity.
  struct IconPose {
    double tx = 0, ty = 0;
    double rotate = 0;    // degrees, clockwise (SVG y-down)
    double sx = 1, sy = 1;
    double skewX = 0;     // degrees
    double dashOffset = 0;
    bool hasDashOffset = false;
  };

  struct IconMotionKey {
    double at = 0;        // percent along the play, ascending
    IconPose pose;
  };

  struct IconMotionPart {
    QString hook;              // "" = the whole glyph
    QPointF origin{12, 12};    // transform pivot in view-box units
    bool originSelf = false;   // …or the part's OWN centre (CSS transform-box: fill-box)
    int durationMs = 220;
    int delayMs = 0;
    int staggerMs = 0;         // added per element sharing the hook, in markup order
    QEasingCurve::Type easing = QEasingCurve::OutQuint;
    double dashArray = 0;
    IconPose to;                   // hold: the pose held while hovered
    QVector<IconMotionKey> keys;   // settle: played once
  };

  struct IconMotionSpec {
    bool hold = true;
    QVector<IconMotionPart> parts;
    QVector<IconMotionPart> activeParts;   // variants.active, empty when there is none
    int totalMs = 0;                       // longest delay+stagger+duration over the parts
  };

  namespace icm {

    inline double num(const QJsonObject& o, const char* k, double dflt) {
      const QJsonValue v = o.value(QLatin1String(k));
      return v.isDouble() ? v.toDouble() : dflt;
    }

    // The table writes easings as CSS cubic-beziers; these are Qt's nearest curves.
    // `defaults.*.qtEasing` names the two mode defaults, so only the per-part overrides
    // need mapping here.
    inline QEasingCurve::Type easingFor(const QString& css, QEasingCurve::Type dflt) {
      if (css.startsWith(QLatin1String("cubic-bezier(0.16"))) return QEasingCurve::OutQuint;
      if (css.startsWith(QLatin1String("cubic-bezier(0.34"))) return QEasingCurve::OutBack;
      if (css.startsWith(QLatin1String("cubic-bezier(0.33"))) return QEasingCurve::OutCubic;
      if (css.startsWith(QLatin1String("cubic-bezier(0.4"))) return QEasingCurve::InOutQuad;
      return dflt;
    }

    inline QEasingCurve::Type namedEasing(const QString& name, QEasingCurve::Type dflt) {
      if (name == QLatin1String("OutQuint")) return QEasingCurve::OutQuint;
      if (name == QLatin1String("OutBack")) return QEasingCurve::OutBack;
      if (name == QLatin1String("OutCubic")) return QEasingCurve::OutCubic;
      return dflt;
    }

    inline IconPose readPose(const QJsonObject& o) {
      IconPose p;
      const QJsonArray t = o.value(QLatin1String("translate")).toArray();
      if (t.size() == 2) { p.tx = t.at(0).toDouble(); p.ty = t.at(1).toDouble(); }
      p.rotate = num(o, "rotate", 0);
      const double s = num(o, "scale", 1);
      p.sx = num(o, "scaleX", s);
      p.sy = num(o, "scaleY", s);
      p.skewX = num(o, "skewX", 0);
      if (o.contains(QLatin1String("dashOffset"))) {
        p.hasDashOffset = true;
        p.dashOffset = num(o, "dashOffset", 0);
      }
      return p;
    }

    inline QVector<IconMotionPart> readParts(const QJsonArray& arr, bool hold,
                                             int dfltMs, QEasingCurve::Type dfltEase) {
      QVector<IconMotionPart> out;
      for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        IconMotionPart p;
        p.hook = o.value(QLatin1String("hook")).toString();
        const QJsonArray org = o.value(QLatin1String("origin")).toArray();
        if (org.size() == 2) p.origin = QPointF(org.at(0).toDouble(), org.at(1).toDouble());
        p.originSelf = o.value(QLatin1String("originSelf")).toBool();
        p.durationMs = int(num(o, "durationMs", dfltMs));
        p.delayMs = int(num(o, "delayMs", 0));
        p.staggerMs = int(num(o, "stagger", 0));
        p.easing = easingFor(o.value(QLatin1String("easing")).toString(), dfltEase);
        p.dashArray = num(o, "dashArray", 0);
        if (hold) {
          // `qtFallback` stands in where the CSS pose needs 3-D (the folder's
          // perspective rotateX, which QSvgRenderer has no way to draw).
          const QJsonObject fb = o.value(QLatin1String("qtFallback")).toObject();
          p.to = readPose(fb.isEmpty() ? o.value(QLatin1String("to")).toObject() : fb);
        } else {
          for (const QJsonValue& kv : o.value(QLatin1String("keyframes")).toArray()) {
            const QJsonObject ko = kv.toObject();
            p.keys.push_back({num(ko, "at", 0), readPose(ko)});
          }
        }
        out.push_back(p);
      }
      return out;
    }

    // ── Markup surgery ────────────────────────────────────────────────────────
    // One start tag in the canon's inner markup: where an attribute can be injected,
    // its class list, and the tag text itself (for the originSelf centre).
    struct Tag {
      int insertAt = 0;
      QString cls;
      QString text;
    };

    inline QVector<Tag> scanTags(const QString& s) {
      QVector<Tag> out;
      int i = 0;
      while ((i = s.indexOf(QLatin1Char('<'), i)) >= 0) {
        const QChar next = i + 1 < s.size() ? s.at(i + 1) : QChar();
        if (next == QLatin1Char('/') || next == QLatin1Char('!') || next == QLatin1Char('?')) {
          ++i;
          continue;
        }
        int j = i + 1;
        QChar quote;
        bool quoted = false;
        for (; j < s.size(); ++j) {
          const QChar c = s.at(j);
          if (quoted) { if (c == quote) quoted = false; }
          else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { quoted = true; quote = c; }
          else if (c == QLatin1Char('>')) break;
        }
        if (j >= s.size()) break;
        Tag t;
        t.text = s.mid(i, j - i);
        t.insertAt = (j > i && s.at(j - 1) == QLatin1Char('/')) ? j - 1 : j;
        const int c = t.text.indexOf(QLatin1String("class=\""));
        if (c >= 0) {
          const int e = t.text.indexOf(QLatin1Char('"'), c + 7);
          if (e > 0) t.cls = t.text.mid(c + 7, e - c - 7);
        }
        out.push_back(t);
        i = j + 1;
      }
      return out;
    }

    inline const QVector<Tag>& tagsOf(const QString& glyph) {
      static QHash<QString, QVector<Tag>> cache;
      const auto it = cache.constFind(glyph);
      if (it != cache.constEnd()) return it.value();
      return *cache.insert(glyph, scanTags(iconMarkup(glyph)));
    }

    inline bool hasHook(const Tag& t, const QString& hook) {
      if (t.cls.isEmpty()) return false;
      return t.cls.split(QLatin1Char(' '), Qt::SkipEmptyParts).contains(hook);
    }

    inline int hookedCount(const QString& glyph, const QString& hook) {
      if (hook.isEmpty()) return 1;
      int n = 0;
      for (const Tag& t : tagsOf(glyph))
        if (hasHook(t, hook)) ++n;
      return std::max(1, n);
    }

    inline double attr(const QString& tag, const char* name, double dflt) {
      const QString key = QString::fromLatin1(name) + QStringLiteral("=\"");
      const int i = tag.indexOf(key);
      if (i < 0) return dflt;
      const int b = i + key.size();
      const int e = tag.indexOf(QLatin1Char('"'), b);
      if (e < 0) return dflt;
      bool ok = false;
      const double v = tag.mid(b, e - b).toDouble(&ok);
      return ok ? v : dflt;
    }

    // The part's OWN centre, for the dots/handles/LEDs that scale in place. Every
    // originSelf part in the table is a circle or a (dot-length) line, so the geometry
    // attributes answer it directly; anything else falls back to the glyph centre.
    inline QPointF selfCentre(const Tag& t) {
      if (t.text.startsWith(QLatin1String("<circle")))
        return QPointF(attr(t.text, "cx", 12), attr(t.text, "cy", 12));
      if (t.text.startsWith(QLatin1String("<line")))
        return QPointF((attr(t.text, "x1", 12) + attr(t.text, "x2", 12)) / 2,
                       (attr(t.text, "y1", 12) + attr(t.text, "y2", 12)) / 2);
      if (t.text.startsWith(QLatin1String("<rect")))
        return QPointF(attr(t.text, "x", 0) + attr(t.text, "width", 24) / 2,
                       attr(t.text, "y", 0) + attr(t.text, "height", 24) / 2);
      return QPointF(12, 12);
    }

    inline const QHash<QString, IconMotionSpec>& table() {
      static const QHash<QString, IconMotionSpec> t = [] {
        QHash<QString, IconMotionSpec> m;
        QFile f(QStringLiteral(":/config/iconMotion.json"));
        if (!f.open(QIODevice::ReadOnly)) return m;
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        const QJsonObject defs = root.value(QLatin1String("defaults")).toObject();
        const QJsonObject dHold = defs.value(QLatin1String("hold")).toObject();
        const QJsonObject dSettle = defs.value(QLatin1String("settle")).toObject();
        const int holdMs = int(num(dHold, "durationMs", 220));
        const int settleMs = int(num(dSettle, "durationMs", 320));
        const QEasingCurve::Type holdEase =
            namedEasing(dHold.value(QLatin1String("qtEasing")).toString(),
                        QEasingCurve::OutQuint);
        const QEasingCurve::Type settleEase =
            namedEasing(dSettle.value(QLatin1String("qtEasing")).toString(),
                        QEasingCurve::OutBack);

        const QJsonObject icons = root.value(QLatin1String("icons")).toObject();
        for (auto it = icons.begin(); it != icons.end(); ++it) {
          const QJsonObject o = it.value().toObject();
          const QString mode = o.value(QLatin1String("mode")).toString();
          if (mode == QLatin1String("none")) continue;
          IconMotionSpec spec;
          spec.hold = mode == QLatin1String("hold");
          const int dMs = spec.hold ? holdMs : settleMs;
          const QEasingCurve::Type dEase = spec.hold ? holdEase : settleEase;
          spec.parts = readParts(o.value(QLatin1String("parts")).toArray(), spec.hold, dMs, dEase);
          spec.activeParts = readParts(o.value(QLatin1String("variants"))
                                           .toObject()
                                           .value(QLatin1String("active"))
                                           .toObject()
                                           .value(QLatin1String("parts"))
                                           .toArray(),
                                       spec.hold, dMs, dEase);
          // Deliberate desktop divergence (user decision 2026-09-02): the close cross
          // draws 1.5× faster here than the canonical table — 270ms/stroke read as
          // sluggish in Qt. Duration and stagger scale together so the strokes still
          // land one after the other.
          if (it.key() == QLatin1String("x")) {
            for (auto* list : {&spec.parts, &spec.activeParts})
              for (IconMotionPart& p : *list) {
                p.durationMs = qRound(p.durationMs / 1.5);
                p.staggerMs = qRound(p.staggerMs / 1.5);
              }
          }
          for (const IconMotionPart& p : spec.parts)
            spec.totalMs = std::max(spec.totalMs,
                                    p.delayMs
                                        + p.staggerMs * (hookedCount(it.key(), p.hook) - 1)
                                        + p.durationMs);
          if (spec.totalMs > 0) m.insert(it.key(), spec);
        }
        return m;
      }();
      return t;
    }

    // ── Pose maths ────────────────────────────────────────────────────────────
    inline double mix(double a, double b, double u) { return a + (b - a) * u; }

    inline IconPose lerpPose(const IconPose& a, const IconPose& b, double u) {
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

    inline double ease(QEasingCurve::Type type, double u) {
      return QEasingCurve(type).valueForProgress(std::clamp(u, 0.0, 1.0));
    }

    // Geometric identity only — a dash offset is written as its own attribute pair.
    // A WHOLE number of turns counts: the quarter-turn buttons, the live-sync wheel and
    // the sun each end their play a full revolution on, which leaves the glyph exactly as
    // it was — so the rest frame is the canon's own markup, byte for byte, and nothing
    // marks a settled icon as still transformed.
    inline bool isIdentity(const IconPose& p) {
      const double spun = std::fmod(std::abs(p.rotate), 360.0);
      return std::abs(p.tx) < 1e-4 && std::abs(p.ty) < 1e-4
             && (spun < 1e-4 || 360.0 - spun < 1e-4) && std::abs(p.sx - 1) < 1e-4
             && std::abs(p.sy - 1) < 1e-4 && std::abs(p.skewX) < 1e-4;
    }

    // Where `part`'s `index`-th element sits at `elapsed` ms into the play.
    inline IconPose poseAt(const IconMotionPart& part, bool hold, int index, double elapsed) {
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
    inline QString transformAttr(const IconPose& p, const QPointF& origin) {
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

  }  // namespace icm

  // The glyph's markup posed for `elapsed` ms into `spec`. Pure — this is the whole
  // rendering half of the port, and what the headless test drives.
  inline QString iconMotionMarkup(const QString& glyph, const IconMotionSpec& spec,
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
  inline QString iconMotionMarkup(const QString& glyph, const IconMotionSpec& spec,
                                  double elapsed) {
    return iconMotionMarkup(glyph, spec, spec.parts, elapsed);
  }

  // The motion designed for `glyph`, or nullptr when it has none.
  inline const IconMotionSpec* iconMotionFor(const QString& glyph) {
    const auto it = icm::table().constFind(glyph);
    return it == icm::table().constEnd() ? nullptr : &it.value();
  }

  // ── The driver ──────────────────────────────────────────────────────────────
  // One per hovered button, parented to it. Holds the elapsed clock the parts read and
  // repaints the button's icon from the posed markup each frame.
  class IconMotionRunner : public QObject {
   public:
    IconMotionRunner(QAbstractButton* btn, const IconRequest& req, const IconMotionSpec* spec,
                     const QVector<IconMotionPart>* parts)
        : QObject(btn), btn_(btn), req_(req), spec_(spec), parts_(parts) {
      setObjectName(QString::fromLatin1(kIconMotionAnimName));
      anim_ = new QVariantAnimation(this);
      anim_->setStartValue(0.0);
      anim_->setEndValue(0.0);
      connect(anim_, &QVariantAnimation::valueChanged, this,
              [this](const QVariant& v) { paint(v.toDouble()); });
      connect(anim_, &QVariantAnimation::finished, this, [this] {
        elapsed_ = anim_->endValue().toDouble();
        if (elapsed_ <= 0.0 || !spec_->hold) rest();
      });
    }

    // Hover-enter: hold eases out to the pose, settle plays once.
    void enter() {
      if (spec_->hold) run(spec_->totalMs);
      else { elapsed_ = 0; run(spec_->totalMs); }
    }

    // Hover-leave: a hold eases back on the same curve, from wherever it got to; a
    // settle is left to finish, since its end state IS the rest pose.
    void leave() {
      if (spec_->hold) run(0);
    }

    bool running() const { return anim_->state() == QAbstractAnimation::Running; }
    double elapsedMs() const { return elapsed_; }
    const IconRequest& request() const { return req_; }

    // Hand the button its rest glyph back — the cached QIcon, so the next hover can
    // trace it to its glyph again.
    void rest() {
      anim_->stop();
      elapsed_ = 0;
      if (!btn_ || tookOver()) return;   // a theme flip already put a proper glyph there
      btn_->setIcon(themedIcon(req_.name, req_.color, req_.size, req_.shadow, req_.dpr, req_.gap));
    }

   private:
    void run(double target) {
      anim_->stop();
      const double from = elapsed_;
      if (qFuzzyCompare(from + 1, target + 1)) { paint(target); return; }
      anim_->setStartValue(from);
      anim_->setEndValue(target);
      // Linear: the shaping lives per part, in poseAt()'s easings.
      anim_->setDuration(std::max(1, int(std::abs(target - from))));
      anim_->start();
    }

    // True once something else (a theme flip, a face swap) has painted its own glyph
    // over ours: a frame WE painted is never a registered themedIcon.
    bool tookOver() const {
      IconRequest now;
      return btn_ && iconRequestForKey(btn_->icon().cacheKey(), &now)
             && (now.name != req_.name || now.color != req_.color || now.size != req_.size);
    }

    void paint(double elapsed) {
      elapsed_ = elapsed;
      if (!btn_) return;
      // A face mid-swap already owns this glyph — step out and leave it alone.
      if (faceSwapping(btn_) || tookOver()) { anim_->stop(); return; }
      const QString posed = iconMotionMarkup(req_.name, *spec_, *parts_, elapsed);
      // A disabled control renders the icon's Disabled variant, so only then is it built.
      btn_->setIcon(iconFromMarkup(posed, req_.color, req_.size, req_.shadow, req_.dpr,
                                   /*withDisabled=*/!btn_->isEnabled(), req_.gap));
    }

    QAbstractButton* btn_ = nullptr;
    IconRequest req_;
    const IconMotionSpec* spec_ = nullptr;
    const QVector<IconMotionPart>* parts_ = nullptr;
    QVariantAnimation* anim_ = nullptr;
    double elapsed_ = 0;
  };

  // The same runner for a MENU ROW: a QMenu's items are QActions, not buttons, so the
  // hover watcher drives this one from the menu's own mouse moves instead of Enter/
  // Leave (browser parity: a .chat-more-item / .ctx-item icon animates on row hover).
  // No face-swap check — action glyphs never face-swap.
  class ActionIconMotionRunner : public QObject {
   public:
    ActionIconMotionRunner(QAction* act, const IconRequest& req, const IconMotionSpec* spec,
                           const QVector<IconMotionPart>* parts)
        : QObject(act), act_(act), req_(req), spec_(spec), parts_(parts) {
      setObjectName(QString::fromLatin1(kIconMotionAnimName));
      anim_ = new QVariantAnimation(this);
      anim_->setStartValue(0.0);
      anim_->setEndValue(0.0);
      connect(anim_, &QVariantAnimation::valueChanged, this,
              [this](const QVariant& v) { paint(v.toDouble()); });
      connect(anim_, &QVariantAnimation::finished, this, [this] {
        elapsed_ = anim_->endValue().toDouble();
        if (elapsed_ <= 0.0 || !spec_->hold) rest();
      });
    }

    void enter() {
      if (spec_->hold) run(spec_->totalMs);
      else { elapsed_ = 0; run(spec_->totalMs); }
    }
    void leave() {
      if (spec_->hold) run(0);
    }
    const IconRequest& request() const { return req_; }

    void rest() {
      anim_->stop();
      elapsed_ = 0;
      if (!act_ || tookOver()) return;   // a theme flip already put a proper glyph there
      // Blocked: a bound toolbar button mirrors the action's icon via changed(), and a
      // posed/rest repaint here must never leak onto it (its own runner owns its glyph).
      const QSignalBlocker block(act_);
      act_->setIcon(themedIcon(req_.name, req_.color, req_.size, req_.shadow, req_.dpr, req_.gap));
    }

   private:
    void run(double target) {
      anim_->stop();
      const double from = elapsed_;
      if (qFuzzyCompare(from + 1, target + 1)) { paint(target); return; }
      anim_->setStartValue(from);
      anim_->setEndValue(target);
      anim_->setDuration(std::max(1, int(std::abs(target - from))));
      anim_->start();
    }

    bool tookOver() const {
      IconRequest now;
      return act_ && iconRequestForKey(act_->icon().cacheKey(), &now)
             && (now.name != req_.name || now.color != req_.color || now.size != req_.size);
    }

    void paint(double elapsed) {
      elapsed_ = elapsed;
      if (!act_) return;
      if (tookOver()) { anim_->stop(); return; }
      const QString posed = iconMotionMarkup(req_.name, *spec_, *parts_, elapsed);
      const QSignalBlocker block(act_);  // never let a bound toolbar button see this frame
      // A disabled row renders the icon's Disabled variant, so only then is it built.
      act_->setIcon(iconFromMarkup(posed, req_.color, req_.size, req_.shadow, req_.dpr,
                                   /*withDisabled=*/!act_->isEnabled(), req_.gap));
    }

    QAction* act_ = nullptr;
    IconRequest req_;
    const IconMotionSpec* spec_ = nullptr;
    const QVector<IconMotionPart>* parts_ = nullptr;
    QVariantAnimation* anim_ = nullptr;
    double elapsed_ = 0;
  };

  namespace icm {

    // Which pose set a control uses: `variants.active` on the fullscreen button that is
    // already in fullscreen, so the corners always show where the click takes you.
    inline const QVector<IconMotionPart>* partsFor(const QAbstractButton* btn,
                                                   const IconMotionSpec& spec) {
      const bool active = btn->isChecked()
                          || btn->property(kIconStateProperty).toString()
                                 == QLatin1String("active");
      return active && !spec.activeParts.isEmpty() ? &spec.activeParts : &spec.parts;
    }

    inline bool eligible(QAbstractButton* btn) {
      // A busy spin, a face mid-swap and the fold chevrons each already own their glyph.
      // A DISABLED control is deliberately eligible (iconMotion.json trigger.disabled):
      // it is still hovered and still explains itself through its tooltip, and a frozen
      // glyph read as a dead area of the toolbar rather than as a control that cannot
      // act right now — the motion says what it WOULD do, the grey says it cannot yet.
      if (!btn || support::motionReduced()) return false;
      if (btn->property(kNoIconMotionProperty).toBool()) return false;
      if (faceSwapping(btn)) return false;
      for (QVariantAnimation* a :
           btn->findChildren<QVariantAnimation*>(QStringLiteral("stencilIconSpin")))
        if (a->state() == QAbstractAnimation::Running) return false;
      return true;
    }

    // Q_OBJECT-free (no MOC), so there is no metaobject to qobject_cast through: the
    // runner is found by its unique object name and cast statically — logoHoverFx's
    // asLogoFx() idiom.
    inline IconMotionRunner* runnerOf(QAbstractButton* btn) {
      QObject* o = btn->findChild<QObject*>(QString::fromLatin1(kIconMotionAnimName),
                                            Qt::FindDirectChildrenOnly);
      return static_cast<IconMotionRunner*>(o);
    }

    inline ActionIconMotionRunner* runnerOfAction(QAction* act) {
      QObject* o = act->findChild<QObject*>(QString::fromLatin1(kIconMotionAnimName),
                                            Qt::FindDirectChildrenOnly);
      return static_cast<ActionIconMotionRunner*>(o);
    }

    // Which pose set a menu row uses — the action twin of partsFor above.
    inline const QVector<IconMotionPart>* partsForAction(const QAction* act,
                                                         const IconMotionSpec& spec) {
      const bool active = act->isChecked()
                          || act->property(kIconStateProperty).toString()
                                 == QLatin1String("active");
      return active && !spec.activeParts.isEmpty() ? &spec.activeParts : &spec.parts;
    }

  }  // namespace icm

  // The one application-wide hover watcher. Enter/Leave are rare events, so this costs
  // nothing at rest and needs no per-button installation — which is what lets every
  // dynamically built row, menu panel and dialog get the motion for free.
  inline constexpr const char* kIconMotionFilterName = "stencilIconMotionFilter";

  class IconMotionFilter : public QObject {
   public:
    explicit IconMotionFilter(QObject* parent) : QObject(parent) {
      setObjectName(QString::fromLatin1(kIconMotionFilterName));
    }

   protected:
    bool eventFilter(QObject* o, QEvent* e) override {
      const QEvent::Type type = e->type();
      // Keyboard navigation is a hover too (browser .ctx-kb): hovered() fires for the
      // row the arrows land on, where no mouse move ever will. Wired once per menu.
      if (type == QEvent::Show) {
        if (auto* menu = qobject_cast<QMenu*>(o);
            menu && !menu->property(kMenuHoverWiredProperty).toBool()) {
          menu->setProperty(kMenuHoverWiredProperty, true);
          QObject::connect(menu, &QMenu::hovered, this,
                           [this, menu](QAction* a) { hoverMenuAction(menu, a); });
        }
        return QObject::eventFilter(o, e);
      }
      // A menu's rows are QActions inside ONE widget, so their "hover" is the menu's
      // own mouse moves — the browser's .ctx-item / .chat-more-item icons animate on
      // row hover, and these do the same through ActionIconMotionRunner.
      if (type == QEvent::MouseMove || type == QEvent::Leave || type == QEvent::Hide) {
        if (auto* menu = qobject_cast<QMenu*>(o)) {
          if (type == QEvent::MouseMove)
            hoverMenuAction(menu,
                            menu->actionAt(static_cast<QMouseEvent*>(e)->position().toPoint()));
          else if (type == QEvent::Leave)
            hoverMenuAction(menu, nullptr);
          else {  // hidden mid-motion: the row snaps to its rest glyph for the next open
            if (QAction* cur = menuHover_.take(menu))
              if (ActionIconMotionRunner* r = icm::runnerOfAction(cur)) r->rest();
          }
          return QObject::eventFilter(o, e);
        }
      }
      // EnabledChange is deliberately NOT watched: a control greyed out under the pointer
      // keeps its motion, exactly as the browser's CSS trigger does.
      if (type != QEvent::Enter && type != QEvent::Leave && type != QEvent::Hide)
        return QObject::eventFilter(o, e);
      auto* btn = qobject_cast<QAbstractButton*>(o);
      if (!btn) return QObject::eventFilter(o, e);

      if (type == QEvent::Enter) {
        if (!icm::eligible(btn)) return QObject::eventFilter(o, e);
        IconMotionRunner* live = icm::runnerOf(btn);
        IconRequest req;
        // A button carries no glyph name — only the QIcon themedIcon() handed it. A
        // theme flip or a face swap replaces that icon, so the glyph is re-read on every
        // hover and a runner left over from a DIFFERENT glyph is retired, not reused.
        if (!iconRequestForKey(btn->icon().cacheKey(), &req)) {
          if (live) live->enter();   // mid-motion re-enter: the icon is our own posed one
          return QObject::eventFilter(o, e);
        }
        if (live && live->request().name == req.name && live->request().color == req.color
            && live->request().size == req.size) {
          live->enter();
          return QObject::eventFilter(o, e);
        }
        if (live) { live->setObjectName(QString()); live->deleteLater(); }
        const IconMotionSpec* spec = iconMotionFor(req.name);
        if (!spec) return QObject::eventFilter(o, e);
        auto* runner = new IconMotionRunner(btn, req, spec, icm::partsFor(btn, *spec));
        runner->enter();
      } else if (IconMotionRunner* live = icm::runnerOf(btn)) {
        if (type == QEvent::Leave) live->leave();
        else live->rest();   // hidden mid-motion: back to the rest pose
      }
      return QObject::eventFilter(o, e);
    }

   private:
    // The row the pointer is on, per menu — so moving to the next row eases the
    // previous glyph back exactly as leaving a button does.
    void hoverMenuAction(QMenu* menu, QAction* a) {
      QPointer<QAction>& cur = menuHover_[menu];
      if (cur == a) return;
      if (cur)
        if (ActionIconMotionRunner* r = icm::runnerOfAction(cur)) r->leave();
      cur = a;
      if (!a || a->isSeparator() || support::motionReduced()) return;
      if (a->property(kNoIconMotionProperty).toBool()) return;
      IconRequest req;
      // Same re-trace as the button path: a theme flip replaces the QIcon, so the
      // glyph is re-read on every hover and a stale runner retired, not reused.
      if (!iconRequestForKey(a->icon().cacheKey(), &req)) {
        if (ActionIconMotionRunner* live = icm::runnerOfAction(a)) live->enter();
        return;
      }
      if (ActionIconMotionRunner* live = icm::runnerOfAction(a)) {
        if (live->request().name == req.name && live->request().color == req.color
            && live->request().size == req.size) {
          live->enter();
          return;
        }
        live->setObjectName(QString());
        live->deleteLater();
      }
      const IconMotionSpec* spec = iconMotionFor(req.name);
      if (!spec) return;
      auto* runner = new ActionIconMotionRunner(a, req, spec, icm::partsForAction(a, *spec));
      runner->enter();
    }

    QHash<QObject*, QPointer<QAction>> menuHover_;
  };

  // Install the watcher on the application. Idempotent — every MainWindow calls it, and
  // only the first one takes.
  inline void installIconMotion() {
    QCoreApplication* app = QCoreApplication::instance();
    if (!app
        || app->findChild<QObject*>(QString::fromLatin1(kIconMotionFilterName),
                                    Qt::FindDirectChildrenOnly))
      return;
    app->installEventFilter(new IconMotionFilter(app));
  }

}  // namespace stencil::gui
