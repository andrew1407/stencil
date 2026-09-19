// The canon: every design resolves to a glyph in icons.json, and no frame changes the icon's box.
#include "iconMotionParts.hpp"

void tableAgainstTheCanon(const QHash<QString, IconMotionSpec>& table) {
  check(table.size() >= 60, "the icon-motion canon loads from the qrc");

  bool hooksResolve = true, partsSane = true;
  for (auto it = table.begin(); it != table.end(); ++it) {
    if (iconMarkup(it.key()).isEmpty()) {
      hooksResolve = false;
      std::printf("       motion for a glyph the icon canon has not got: %s\n",
                  qPrintable(it.key()));
      continue;
    }
    QVector<IconMotionPart> all = it.value().parts;
    all += it.value().activeParts;
    for (const IconMotionPart& p : all) {
      if (p.hook.isEmpty()) continue;
      int n = 0;
      for (const icm::Tag& t : icm::tagsOf(it.key()))
        if (icm::hasHook(t, p.hook)) ++n;
      if (n == 0) {
        hooksResolve = false;
        std::printf("       %s: hook \"%s\" matches nothing in icons.json\n",
                    qPrintable(it.key()), qPrintable(p.hook));
      }
    }
    if (it.value().totalMs <= 0 || it.value().parts.isEmpty()) partsSane = false;
  }
  check(hooksResolve, "every part hook resolves to an element of its glyph");
  check(partsSane, "every design carries parts and a runtime");

  // The designs the user called out by name are all present and the right mode.
  const auto mode = [&](const char* g, bool hold) {
    const IconMotionSpec* s = iconMotionFor(QLatin1String(g));
    return s && s->hold == hold;
  };
  check(mode("trash", true) && mode("download", true) && mode("upload", true)
            && mode("folder", true) && mode("link", true) && mode("maximize", true)
            && mode("sun", true),
        "the hold designs are held (trash / download / upload / folder / link / maximize / sun)");
  check(mode("moon", false) && mode("plus", false) && mode("minus", false)
            && mode("layers", false) && mode("sparkle", false) && mode("line", false)
            && mode("rect", false),
        "the settle designs play once (moon / plus / minus / layers / sparkle / line / rect)");

  // ── No layout shift, and convergence on the rest pose ────────────────────────
  bool sameBox = true, converges = true, restIsRest = true;
  for (auto it = table.begin(); it != table.end(); ++it) {
    const IconMotionSpec& s = it.value();
    const QImage base = iconFromMarkup(iconMarkup(it.key()), QColor(Qt::black), PX, 1.0)
                            .pixmap(PX, PX).toImage();
    for (double f : {0.0, 0.25, 0.5, 0.75, 1.0}) {
      const QImage im = frame(it.key(), s, s.totalMs * f);
      if (im.size() != base.size()) { sameBox = false; std::printf("       %s: box changed\n", qPrintable(it.key())); }
    }
    // A settle ends where it started; a hold STARTS there (its end is the held pose).
    const QString rest = iconMotionMarkup(it.key(), s, s.hold ? 0.0 : double(s.totalMs));
    if (rest != iconMarkup(it.key())) {
      (s.hold ? restIsRest : converges) = false;
      std::printf("       %s: does not sit at the rest pose\n", qPrintable(it.key()));
    }
    // …and it really moves in between.
    if (iconMotionMarkup(it.key(), s, s.totalMs * 0.45) == iconMarkup(it.key())) {
      converges = false;
      std::printf("       %s: mid-play is indistinguishable from rest\n", qPrintable(it.key()));
    }
  }
  check(sameBox, "no frame of any motion changes the icon's box (no control can reflow)");
  check(restIsRest, "a hold motion's rest pose is the untouched glyph");
  check(converges, "a settle motion moves, then lands back on the untouched glyph");
}
