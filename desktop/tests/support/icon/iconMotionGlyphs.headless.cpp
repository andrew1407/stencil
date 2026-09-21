// What each glyph MEANS as it moves: plus draws itself and minus shrinks, the trash hinges at its
// lid, download goes down and upload up, and the marks that draw themselves are partly absent early.
#include "iconMotionParts.hpp"

void glyphMeanings() {
  // The meanings: plus DRAWS itself — the vertical stroke first, then the horizontal — and minus SHRINKS.
  // The sequence and direction IS the meaning (a swelling minus reads as "increase").
  {
    const IconMotionSpec* plus = iconMotionFor(QStringLiteral("plus"));
    const IconMotionSpec* minus = iconMotionFor(QStringLiteral("minus"));
    const QRect p0 = inkBox(frame("plus", *plus, 0));
    const QRect pm = inkBox(frame("plus", *plus, plus->totalMs * 0.45));
    const QRect pe = inkBox(frame("plus", *plus, plus->totalMs));
    check(p0.isNull(), "plus starts UNDRAWN — no ink before the strokes draw on");
    check(!pm.isNull() && pm.height() > pm.width() * 2,
          "mid-play only the VERTICAL stroke is down — drawn first, the way you'd write one");
    check(!pe.isNull() && std::abs(pe.width() - pe.height()) <= 1 && pe.width() > pm.width() + 10,
          "…then the horizontal strikes through and the whole cross lands");
    const QRect m0 = inkBox(frame("minus", *minus, 0));
    const QRect mm = inkBox(frame("minus", *minus, minus->totalMs * 0.45));
    check(mm.width() < m0.width() - 2, "minus SHRINKS at the peak of its play");
    check(inkBox(frame("minus", *minus, minus->totalMs)).width() == m0.width(),
          "…and settles back too");
  }

  // Fullscreen: the corners EXTEND to enter it and RETRACT to leave it, so the motion
  // always shows where the click takes you.
  {
    const IconMotionSpec* mx = iconMotionFor(QStringLiteral("maximize"));
    check(mx && !mx->activeParts.isEmpty(), "maximize carries its `active` variant");
    const QRect rest = inkBox(frame("maximize", *mx, 0));
    const QRect out = inkBox(frame("maximize", *mx, mx->parts.first().durationMs));
    const QRect in = inkBox(frame("maximize", *mx, mx->activeParts,
                                  mx->activeParts.first().durationMs));
    check(out.width() > rest.width() + 2 && out.height() > rest.height() + 2,
          "hovering ENTER fullscreen pushes the corners outward");
    check(in.width() < rest.width() - 2 && in.height() < rest.height() - 2,
          "…and hovering LEAVE fullscreen pulls them back in");
  }

  // The trash lid is HINGED at its left end: the far end of the lid swings while the
  // hinge end stays put, and the can below is never touched.
  {
    const IconMotionSpec* tr = iconMotionFor(QStringLiteral("trash"));
    const QImage a = frame("trash", *tr, 0);
    const QImage b = frame("trash", *tr, tr->totalMs);
    // Left of the hinge (x≈3-4.5 units) and beyond the can's right wall (x≈20.5-24):
    // the only ink in either band is the lid's own overhang.
    const int dLeft = topIn(b, 0.10, 0.19) - topIn(a, 0.10, 0.19);
    const int dRight = topIn(b, 0.86, 0.99) - topIn(a, 0.86, 0.99);
    // y grows DOWNWARD, so the lid's far end rising off the can is dRight < 0. The design also lifts the
    // whole lid 0.4u, so "hinged, not slid" is the far end travelling while the hinge end holds station.
    check(dRight < 0, "the trash lid OPENS — its far end rises off the can");
    check(std::abs(dRight) > 2 * std::abs(dLeft),
          "…hinged at the lid's LEFT end: that end barely moves while the far end swings");
    // The can body: the bottom third of the glyph is identical in both frames.
    bool bodyStill = true;
    for (int y = a.height() * 2 / 3; y < a.height() && bodyStill; ++y)
      for (int x = 0; x < a.width(); ++x)
        if ((qAlpha(a.pixel(x, y)) > 40) != (qAlpha(b.pixel(x, y)) > 40)) { bodyStill = false; break; }
    check(bodyStill, "…and the can itself never moves");
  }

  // One arrow, two directions — the whole difference between the download/upload pair.
  {
    const IconMotionSpec* dn = iconMotionFor(QStringLiteral("download"));
    const IconMotionSpec* up = iconMotionFor(QStringLiteral("upload"));
    check(inkBox(frame("download", *dn, dn->totalMs)).top()
              > inkBox(frame("download", *dn, 0)).top(),
          "download's arrow travels DOWN into the tray");
    check(inkBox(frame("upload", *up, up->totalMs)).top()
              < inkBox(frame("upload", *up, 0)).top(),
          "upload's arrow travels UP — the same arrow, the other way");
  }

  // The marks that DRAW themselves: a tick / a box / a segment is partly absent early
  // in the play and whole by the end (stroke-dashoffset, which Qt's SVG renderer honours).
  {
    for (const char* g : {"check", "line", "rect", "file-text", "x"}) {
      const IconMotionSpec* s = iconMotionFor(QLatin1String(g));
      const int early = inkCount(frame(g, *s, s->totalMs * 0.12));
      const int whole = inkCount(frame(g, *s, s->totalMs));
      check(early < whole * 9 / 10,
            (QByteArray(g) + ": the mark draws itself on, it is not just there").constData());
    }
  }

  // The theme pair TURN as whole glyphs. The sun is HELD one ray round (its eight rays are 45° apart, so
  // a single ray-space looks untouched); the moon only rocks, as a tipped crescent reads as another shape.
  {
    for (const char* g : {"sun", "moon"}) {
      const IconMotionSpec* s = iconMotionFor(QLatin1String(g));
      check(s && s->parts.size() == 1 && s->parts.first().hook.isEmpty(),
            (QByteArray(g) + ": the WHOLE glyph turns — one part, hooked to nothing").constData());
      // A hold rests at elapsed 0, a settle at the end of its play; either way the rest
      // frame is the untouched glyph, and either way something moves in between.
      const QImage rest = frame(g, *s, s->hold ? 0.0 : double(s->totalMs));
      bool moved = false;
      for (double f : {0.15, 0.3, 0.45, 0.6, 0.8})
        if (frame(g, *s, s->totalMs * f) != rest) moved = true;
      check(moved, (QByteArray(g) + ": …and turns on the way").constData());
    }
    const IconMotionSpec* sun = iconMotionFor(QStringLiteral("sun"));
    check(sun->hold && sun->parts.first().to.rotate == 45, "the sun turns by exactly one ray");
    const IconMotionSpec* moon = iconMotionFor(QStringLiteral("moon"));
    double swing = 0;
    for (const IconMotionKey& k : moon->parts.first().keys)
      swing = std::max(swing, std::abs(k.pose.rotate));
    check(swing > 0 && swing <= 12, "the moon waves — it does not tumble");
  }

  // The help mark TREMBLES INSIDE its ring: everything the play touches stays within the ring's inner
  // edge, so the ring itself never moves and the ? never swings out of it.
  {
    const IconMotionSpec* h = iconMotionFor(QStringLiteral("help"));
    const QImage rest = frame("help", *h, 0);
    const double c = PX / 2.0, limit = 8.5 / 24.0 * PX;   // the ring's inner edge is 9u
    bool inRing = true;
    for (double f : {0.15, 0.25, 0.4, 0.55, 0.8, 1.0}) {
      const QImage im = frame("help", *h, h->totalMs * f);
      for (int y = 0; y < im.height(); ++y)
        for (int x = 0; x < im.width(); ++x)
          if ((qAlpha(im.pixel(x, y)) > 40) != (qAlpha(rest.pixel(x, y)) > 40)
              && std::hypot(x + 0.5 - c, y + 0.5 - c) > limit)
            inRing = false;
    }
    check(inRing, "the help tremor moves only the mark, and only inside its ring");
  }

  // The close cross is struck out ONE STROKE AT A TIME: at the moment the first stroke
  // lands the second has not started, so the glyph is still a single diagonal.
  {
    const IconMotionSpec* xs = iconMotionFor(QStringLiteral("x"));
    const int one = inkCount(frame("x", *xs, xs->totalMs * 0.5));
    const int both = inkCount(frame("x", *xs, xs->totalMs));
    check(one > 0 && one < both * 6 / 10,
          "the x draws its first stroke whole before the second one starts");
    // Desktop draws the cross 1.5× faster than the canonical 270ms/stroke table
    // (user decision; see iconMotion.hpp): 180 + 180 stagger = 360ms in all.
    check(xs->totalMs == 360, "the x draw-on runs 1.5x faster than the canonical table");
  }

  // The picture DRAWS itself INSIDE its frame: no frame of the play puts ink outside the picture frame,
  // and the ridge really draws on rather than simply being there.
  {
    const IconMotionSpec* im = iconMotionFor(QStringLiteral("image"));
    const QRect box = inkBox(frame("image", *im, im->totalMs));
    bool inside = true;
    for (double f : {0.0, 0.15, 0.35, 0.55, 0.8, 1.0})
      if (!box.contains(inkBox(frame("image", *im, im->totalMs * f)))) inside = false;
    check(inside, "no frame of the `image` play puts ink outside the picture frame");
    // Strictly inside the frame's outline (5..19 of the 24 units), where only the ridge
    // and the little sun live.
    const QRect in(PX * 5 / 24, PX * 5 / 24, PX * 14 / 24, PX * 14 / 24);
    const int early = inkCount(frame("image", *im, im->totalMs * 0.12).copy(in));
    const int whole = inkCount(frame("image", *im, im->totalMs).copy(in));
    check(early < whole * 7 / 10, "…and the ridge inside it draws itself on");
    // The little sun DROPS in from above: over the sun's own columns (6..11.5u) the frame's outline rows
    // (2..4) are untouched in every frame, while the sun travels there, high early and home at the end.
    const int x0 = PX * 6 / 24, w = PX * 55 / 240;   // the sun's own columns, 6..11.5u
    const QRect outline(x0, 0, w, PX * 4 / 24);       // the frame's top stroke, 2..4u
    const QImage rest = frame("image", *im, im->totalMs);
    bool clear = true;
    for (double f : {0.0, 0.1, 0.2, 0.3, 0.45, 0.6, 0.8, 1.0})
      if (frame("image", *im, im->totalMs * f).copy(outline) != rest.copy(outline)) clear = false;
    check(clear, "…and the sun never crosses the frame's outline on its way in");
    const QRect band(x0, PX * 45 / 240, w, PX * 85 / 240);   // under the outline, 4.5..13u
    check(topIn(frame("image", *im, im->totalMs * 0.25).copy(band), 0, 1)
              < topIn(rest.copy(band), 0, 1),
          "…having started ABOVE the place it lands");
  }

  // The disguise COMES APART: the hat lifts off while the glasses drop away from it, so
  // the glyph gets taller at both ends — and still fits the icon box.
  {
    const IconMotionSpec* ic = iconMotionFor(QStringLiteral("incognito"));
    const QRect rest = inkBox(frame("incognito", *ic, 0));
    const QRect open = inkBox(frame("incognito", *ic, ic->totalMs));
    check(open.top() < rest.top(), "the hat lifts off");
    check(open.bottom() > rest.bottom(), "…and the glasses drop away from it");
    check(open.top() > 0 && open.bottom() < PX - 1, "…without either leaving the icon box");
  }

  // The staggered parts really are staggered: sparkle's three dots bounce in turn, so
  // early in the play the glyph is NOT symmetric between the first and last dot.
  {
    const IconMotionSpec* sp = iconMotionFor(QStringLiteral("sparkle"));
    const QString a = iconMotionMarkup("sparkle", *sp, 60);
    check(a.count(QStringLiteral("transform=")) >= 1
              && iconMotionMarkup("sparkle", *sp, 60) != iconMotionMarkup("sparkle", *sp, 200),
          "the sparkle dots type left to right rather than all at once");
  }
}
