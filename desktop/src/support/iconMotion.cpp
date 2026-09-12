#include "iconMotion.hpp"

namespace stencil::gui {


  double icm::num(const QJsonObject& o, const char* k, double dflt) {
    const QJsonValue v = o.value(QLatin1String(k));
    return v.isDouble() ? v.toDouble() : dflt;
  }


  // The table writes easings as CSS cubic-beziers; these are Qt's nearest curves.
  // `defaults.*.qtEasing` names the two mode defaults, so only the per-part overrides
  // need mapping here.
  QEasingCurve::Type icm::easingFor(const QString& css, QEasingCurve::Type dflt) {
    if (css.startsWith(QLatin1String("cubic-bezier(0.16"))) return QEasingCurve::OutQuint;
    if (css.startsWith(QLatin1String("cubic-bezier(0.34"))) return QEasingCurve::OutBack;
    if (css.startsWith(QLatin1String("cubic-bezier(0.33"))) return QEasingCurve::OutCubic;
    if (css.startsWith(QLatin1String("cubic-bezier(0.4"))) return QEasingCurve::InOutQuad;
    return dflt;
  }

  QEasingCurve::Type icm::namedEasing(const QString& name, QEasingCurve::Type dflt) {
    if (name == QLatin1String("OutQuint")) return QEasingCurve::OutQuint;
    if (name == QLatin1String("OutBack")) return QEasingCurve::OutBack;
    if (name == QLatin1String("OutCubic")) return QEasingCurve::OutCubic;
    return dflt;
  }

  IconPose icm::readPose(const QJsonObject& o) {
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

  QVector<IconMotionPart> icm::readParts(const QJsonArray& arr, bool hold, int dfltMs,
                                         QEasingCurve::Type dfltEase) {
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

  QVector<icm::Tag> icm::scanTags(const QString& s) {
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

  const QVector<icm::Tag>& icm::tagsOf(const QString& glyph) {
    static QHash<QString, QVector<Tag>> cache;
    const auto it = cache.constFind(glyph);
    if (it != cache.constEnd()) return it.value();
    return *cache.insert(glyph, scanTags(iconMarkup(glyph)));
  }

  bool icm::hasHook(const Tag& t, const QString& hook) {
    if (t.cls.isEmpty()) return false;
    return t.cls.split(QLatin1Char(' '), Qt::SkipEmptyParts).contains(hook);
  }

  int icm::hookedCount(const QString& glyph, const QString& hook) {
    if (hook.isEmpty()) return 1;
    int n = 0;
    for (const Tag& t : tagsOf(glyph))
      if (hasHook(t, hook)) ++n;
    return std::max(1, n);
  }

  double icm::attr(const QString& tag, const char* name, double dflt) {
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
  QPointF icm::selfCentre(const Tag& t) {
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

  const QHash<QString, IconMotionSpec>& icm::table() {
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
}  // namespace stencil::gui
