// Headless check of the per-icon hover motion (src/support/iconMotion.hpp) — the desktop
// port of browser/js/config/iconMotion.json, where every glyph mimes its OWN action.
//
// Three halves are pinned here:
//   • the TABLE — every design in the canon is loaded, and every part hook it addresses
//     really resolves to an element of that glyph in icons.json (the two files are one
//     contract; a renamed hook must fail here, not silently animate nothing);
//   • the MEANING — the semantic pins that make this table exist at all: plus GROWS and
//     minus SHRINKS, the fullscreen corners extend to enter and RETRACT to leave, the
//     trash hinges at its lid and never moves the can, download's arrow goes down and
//     upload's up;
//   • the CONTRACT — a motion converges on the rest pose, never changes the icon's box
//     (so no control can reflow), and does not run at all under reduced motion.
//
// Run with `--dump <dir>` to write the sampled frames out as PNGs for a visual pass.
#include "iconMotion.hpp"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QEventLoop>
#include <QImage>
#include <QRect>
#include <QToolButton>
#include <cmath>
#include <cstdio>

#include "support/check.hpp"

using namespace stencil::gui;

namespace {

  constexpr int kPx = 96;   // big enough for sub-unit moves to show up in the ink box

  QImage frame(const QString& glyph, const IconMotionSpec& spec,
               const QVector<IconMotionPart>& parts, double t) {
    const QString posed = iconMotionMarkup(glyph, spec, parts, t);
    return iconFromMarkup(posed, QColor(Qt::black), kPx, false, 1.0)
        .pixmap(kPx, kPx)
        .toImage()
        .convertToFormat(QImage::Format_ARGB32);
  }

  QImage frame(const QString& glyph, const IconMotionSpec& spec, double t) {
    return frame(glyph, spec, spec.parts, t);
  }

  // The tight box of everything the glyph actually inked.
  QRect inkBox(const QImage& im) {
    int l = im.width(), r = -1, t = im.height(), b = -1;
    for (int y = 0; y < im.height(); ++y)
      for (int x = 0; x < im.width(); ++x)
        if (qAlpha(im.pixel(x, y)) > 40) {
          l = std::min(l, x); r = std::max(r, x);
          t = std::min(t, y); b = std::max(b, y);
        }
    return r < 0 ? QRect() : QRect(QPoint(l, t), QPoint(r, b));
  }

  int inkCount(const QImage& im) {
    int n = 0;
    for (int y = 0; y < im.height(); ++y)
      for (int x = 0; x < im.width(); ++x)
        if (qAlpha(im.pixel(x, y)) > 40) ++n;
    return n;
  }

  // Topmost inked row within a column band — how high a part sits there.
  int topIn(const QImage& im, double x0, double x1) {
    const int a = int(im.width() * x0), b = int(im.width() * x1);
    for (int y = 0; y < im.height(); ++y)
      for (int x = a; x < b; ++x)
        if (qAlpha(im.pixel(x, y)) > 40) return y;
    return im.height();
  }

  void pumpFor(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
  }

  bool pumpUntil(const std::function<bool()>& done, int budgetMs = 4000) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < budgetMs) {
      if (done()) return true;
      QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return done();
  }

  void hover(QWidget* w, bool in) {
    if (in) {
      QEnterEvent e(QPointF(2, 2), QPointF(2, 2), w->mapToGlobal(QPoint(2, 2)));
      QApplication::sendEvent(w, &e);
    } else {
      QEvent e(QEvent::Leave);
      QApplication::sendEvent(w, &e);
    }
  }

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  QString dumpDir;
  for (int i = 1; i < argc - 1; ++i)
    if (QLatin1String(argv[i]) == QLatin1String("--dump")) dumpDir = QString::fromLocal8Bit(argv[i + 1]);

  // ── The table, against the glyph canon ──────────────────────────────────────
  const QHash<QString, IconMotionSpec>& table = icm::table();
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
            && mode("folder", true) && mode("link", true) && mode("maximize", true),
        "the hold designs are held (trash / download / upload / folder / link / maximize)");
  check(mode("sun", false) && mode("plus", false) && mode("minus", false)
            && mode("layers", false) && mode("sparkle", false) && mode("line", false)
            && mode("rect", false),
        "the settle designs play once (sun / plus / minus / layers / sparkle / line / rect)");

  // ── No layout shift, and convergence on the rest pose ────────────────────────
  bool sameBox = true, converges = true, restIsRest = true;
  for (auto it = table.begin(); it != table.end(); ++it) {
    const IconMotionSpec& s = it.value();
    const QImage base = iconFromMarkup(iconMarkup(it.key()), QColor(Qt::black), kPx, false, 1.0)
                            .pixmap(kPx, kPx).toImage();
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

  // ── The meanings ────────────────────────────────────────────────────────────
  // plus GROWS and minus SHRINKS. The direction IS the meaning: this pin is the whole
  // reason the table exists (a swelling minus reads as "increase").
  {
    const IconMotionSpec* plus = iconMotionFor(QStringLiteral("plus"));
    const IconMotionSpec* minus = iconMotionFor(QStringLiteral("minus"));
    const QRect p0 = inkBox(frame("plus", *plus, 0));
    const QRect pm = inkBox(frame("plus", *plus, plus->totalMs * 0.45));
    const QRect pe = inkBox(frame("plus", *plus, plus->totalMs));
    const QRect m0 = inkBox(frame("minus", *minus, 0));
    const QRect mm = inkBox(frame("minus", *minus, minus->totalMs * 0.45));
    check(pm.width() > p0.width() + 2, "plus GROWS at the peak of its play");
    check(pe.width() == p0.width(), "…and settles back to its default size");
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
    check(dLeft < 0 && dRight > 0,
          "the trash lid TILTS — its two ends move opposite ways, so it is hinged, not slid");
    check(std::abs(dRight) > 2 * std::abs(dLeft),
          "…and the hinge is at the lid's LEFT end: the far end swings much further");
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
    for (const char* g : {"check", "line", "rect", "file-text"}) {
      const IconMotionSpec* s = iconMotionFor(QLatin1String(g));
      const int early = inkCount(frame(g, *s, s->totalMs * 0.12));
      const int whole = inkCount(frame(g, *s, s->totalMs));
      check(early < whole * 9 / 10,
            (QByteArray(g) + ": the mark draws itself on, it is not just there").constData());
    }
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

  // ── The driver on a live button ─────────────────────────────────────────────
  installIconMotion();
  {
    auto* btn = new QToolButton;
    btn->setIconSize(QSize(18, 18));
    btn->resize(28, 28);
    btn->show();

    // Reduced motion: the preference wins, and the glyph stays in its rest pose.
    qputenv("STENCIL_NO_ANIM", "1");
    btn->setIcon(themedIcon(QStringLiteral("plus"), QColor(Qt::black), 18, false, 1.0));
    const qint64 restKey = btn->icon().cacheKey();
    hover(btn, true);
    pumpFor(60);
    check(btn->icon().cacheKey() == restKey, "reduced motion runs no icon motion at all");
    hover(btn, false);
    qunsetenv("STENCIL_NO_ANIM");

    // A settle plays and comes back on its own.
    btn->setIcon(themedIcon(QStringLiteral("plus"), QColor(Qt::black), 18, false, 1.0));
    const QSize box = btn->sizeHint();
    hover(btn, true);
    check(pumpUntil([&] { return btn->icon().cacheKey() != restKey; }, 500),
          "a hover starts the motion");
    check(btn->sizeHint() == box, "…without resizing the control");
    check(pumpUntil([&] { return btn->icon().cacheKey() == restKey; }, 2000),
          "…and the settle converges back on the rest glyph");
    hover(btn, false);

    // A hold holds while the pointer rests, and eases back when it leaves.
    btn->setIcon(themedIcon(QStringLiteral("trash"), QColor(Qt::black), 18, false, 1.0));
    const qint64 trashKey = btn->icon().cacheKey();
    hover(btn, true);
    check(pumpUntil([&] { return btn->icon().cacheKey() != trashKey; }, 500),
          "a hold motion moves on hover");
    pumpFor(350);
    check(btn->icon().cacheKey() != trashKey, "…and stays put while hovered");
    hover(btn, false);
    check(pumpUntil([&] { return btn->icon().cacheKey() == trashKey; }, 2000),
          "…then eases back on leave");

    // The opt-out (a fold chevron, whose angle is state) is honoured.
    btn->setProperty(kNoIconMotionProperty, true);
    btn->setIcon(themedIcon(QStringLiteral("chevron-up"), QColor(Qt::black), 18, false, 1.0));
    const qint64 chevKey = btn->icon().cacheKey();
    hover(btn, true);
    pumpFor(80);
    check(btn->icon().cacheKey() == chevKey, "an opted-out control keeps its glyph still");
    hover(btn, false);
    btn->setProperty(kNoIconMotionProperty, false);

    // A disabled control cannot act, so it does not react.
    btn->setEnabled(false);
    btn->setIcon(themedIcon(QStringLiteral("plus"), QColor(Qt::black), 18, false, 1.0));
    hover(btn, true);
    pumpFor(80);
    check(btn->icon().cacheKey() == restKey, "a disabled control does not react");
    delete btn;
  }

  // ── Optional visual dump ────────────────────────────────────────────────────
  if (!dumpDir.isEmpty()) {
    QDir().mkpath(dumpDir);
    for (auto it = table.begin(); it != table.end(); ++it) {
      const IconMotionSpec& s = it.value();
      for (double f : {0.0, 0.35, 0.6, 1.0})
        frame(it.key(), s, s.totalMs * f)
            .save(QStringLiteral("%1/%2@%3.png").arg(dumpDir, it.key()).arg(int(f * 100)));
    }
    for (double f : {0.0, 1.0}) {
      const IconMotionSpec* mx = iconMotionFor(QStringLiteral("maximize"));
      frame("maximize", *mx, mx->activeParts, mx->totalMs * f)
          .save(QStringLiteral("%1/maximize-active@%2.png").arg(dumpDir).arg(int(f * 100)));
    }
    std::printf("  dumped frames to %s\n", qPrintable(dumpDir));
  }

  std::printf("%s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
