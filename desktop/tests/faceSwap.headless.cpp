// Headless check of the shared toggle FACE swap (src/support/faceSwap.hpp) — the one
// exchange behind Draw's Start▶/Stop■ and its Line/Rect neighbour. Two halves are pinned
// here: the curve (both ends at rest, an invisible pivot, the turn reversing across it),
// and the driver on a live QToolButton — that a swap converges on the face asked for, that
// the caller's state flip runs exactly once, that reduced motion lands on the end state
// without animating, that rapid supersession always ends on the LAST face asked for, and
// that nothing of the exchange (a widget stylesheet, an oversized icon) survives it.
#include "faceSwap.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QToolButton>
#include <algorithm>
#include <cmath>
#include <cstdio>

using stencil::gui::FaceSpec;
using stencil::gui::faceSwapFrame;
using stencil::gui::faceSwapping;
using stencil::gui::kFaceGlyphProperty;
using stencil::gui::kFaceSwapMinScale;
using stencil::gui::kFaceSwapMs;
using stencil::gui::kFaceSwapPivot;
using stencil::gui::kFaceSwappingProperty;
using stencil::gui::kFaceSwapTurnDeg;
using stencil::gui::swapFace;

#include "support/check.hpp"

static void pumpFor(int ms) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// Wait for a condition rather than a clock: an offscreen animation driver ticks at
// whatever rate the harness gives it, and none of these assertions are about its speed.
static bool pumpUntil(const std::function<bool()>& done, int budgetMs = 4000) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < budgetMs) {
    if (done()) return true;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
  return done();
}

static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

static FaceSpec face(const QString& glyph, const QString& label, const QColor& c) {
  FaceSpec f;
  f.glyph = glyph;
  f.label = label;
  f.glyphColor = c;
  f.textColor = c;
  f.iconSize = 18;
  return f;
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  qunsetenv("STENCIL_NO_ANIM");   // this suite drives the real motion; one case re-sets it
  QApplication app(argc, argv);

  // ── the curve ─────────────────────────────────────────────────────────────────
  const auto start = faceSwapFrame(0.0);
  check(!start.incoming && near(start.alpha, 1.0) && near(start.deg, 0.0)
            && near(start.scale, 1.0),
        "t=0 is the OLD face at rest — a swap never opens with a jump");
  const auto end = faceSwapFrame(1.0);
  check(end.incoming && near(end.alpha, 1.0) && near(end.deg, 0.0) && near(end.scale, 1.0),
        "t=1 is the NEW face at rest — the motion ends where the button lives");
  const auto lastOut = faceSwapFrame(kFaceSwapPivot - 1e-6);
  const auto firstIn = faceSwapFrame(kFaceSwapPivot);
  check(lastOut.alpha < 0.01 && firstIn.alpha < 0.01,
        "the pivot is invisible — which is what hides the exchange (and the fill flip)");
  check(lastOut.deg > 0.0 && firstIn.deg < 0.0,
        "the turn REVERSES across the pivot: out at +115°, in from -115°, one continuous turn");
  check(near(std::fabs(firstIn.deg), kFaceSwapTurnDeg)
            && near(firstIn.scale, kFaceSwapMinScale),
        "the arriving glyph starts a full quarter-turn back and small (browser swapGlyphIn)");
  check(faceSwapFrame(0.02).alpha > faceSwapFrame(0.2).alpha
            && faceSwapFrame(0.2).alpha > faceSwapFrame(kFaceSwapPivot * 0.99).alpha,
        "the old face fades monotonically on its way out");
  check(faceSwapFrame(0.6).alpha < faceSwapFrame(0.8).alpha
            && faceSwapFrame(0.8).alpha < faceSwapFrame(0.99).alpha,
        "…and the new one monotonically in");
  check(faceSwapFrame(-5.0).alpha > 0.99 && faceSwapFrame(9.0).incoming,
        "out-of-range progress clamps instead of painting nonsense");

  // ── the driver ────────────────────────────────────────────────────────────────
  const QColor accent("#7b5cff");
  auto* btn = new QToolButton;
  btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  btn->setIconSize(QSize(18, 18));
  btn->show();
  pumpFor(30);

  // The FIRST face has nothing to leave from, so it goes straight on — a toolbar must
  // not animate itself into existence at boot.
  int applied = 0;
  swapFace(btn, face("play", "Start", accent), [&applied] { ++applied; });
  check(btn->text() == QLatin1String("Start") && !btn->icon().isNull(),
        "the first face is painted at once");
  check(!faceSwapping(btn), "…with no animation to wait on");
  check(applied == 1, "the caller's state flip still ran");
  check(btn->property(kFaceGlyphProperty).toString() == QLatin1String("play"),
        "the painted face is remembered — setDefaultAction cannot be trusted to hold it");

  // A real swap: the OLD word is still up on the first frame, and the exchange happens
  // at the pivot, not at the click.
  swapFace(btn, face("stop", "Stop", QColor(Qt::white)), [&applied] { ++applied; });
  check(faceSwapping(btn), "the swap runs as an animation");
  check(btn->text() == QLatin1String("Start"),
        "the outgoing word is still the old one as the turn begins");
  check(applied == 1, "the state flip waits for the pivot, where the face is invisible");
  check(btn->property(kFaceSwappingProperty).toBool(),
        "the label fade owns the button's colour while it runs");
  check(pumpUntil([&applied] { return applied == 2; }),
        "…and runs at the pivot, before the swap has finished");
  check(faceSwapping(btn) || btn->text() == QLatin1String("Stop"),
        "…which is mid-flight, not at the end");
  check(pumpUntil([btn] { return !faceSwapping(btn); }), "the animation converges and stops");
  check(btn->text() == QLatin1String("Stop"), "the button ends on the face it was asked for");
  check(btn->property(kFaceGlyphProperty).toString() == QLatin1String("stop"),
        "…glyph included");
  check(!btn->property(kFaceSwappingProperty).toBool() && btn->styleSheet().isEmpty(),
        "nothing of the fade survives: the button gets its own stylesheet back");
  check(applied == 2, "the state flip ran exactly once for the swap");

  // The WORD fades with the glyph, and it has to fade for REAL — a widget stylesheet Qt
  // never matched (a property selector wants a re-polish) would leave the label at full
  // strength under a turning glyph. Read as the darkest pixel in the label half of the
  // button: a dark word at rest, one faded into the background at the pivot.
  const auto labelDarkest = [btn] {
    const QImage im = btn->grab().toImage();
    int lo = 255;
    for (int y = 0; y < im.height(); ++y)
      for (int x = im.width() / 2; x < im.width(); ++x) {
        const QColor c = im.pixelColor(x, y);
        lo = std::min(lo, (c.red() + c.green() + c.blue()) / 3);
      }
    return lo;
  };
  swapFace(btn, face("play", "Start", QColor(Qt::darkBlue)));
  pumpUntil([btn] { return !faceSwapping(btn); });
  const int restDark = labelDarkest();
  int pivotDark = -1;
  swapFace(btn, face("stop", "Stop", QColor(Qt::darkBlue)),
           [&pivotDark, &labelDarkest] { pivotDark = labelDarkest(); });
  check(pumpUntil([&pivotDark] { return pivotDark >= 0; }), "the swap reaches its pivot");
  check(restDark < 100 && pivotDark > restDark + 60,
        "the word fades with the glyph — at the pivot it is all but gone");
  pumpUntil([btn] { return !faceSwapping(btn); });

  // The glyph is turned and scaled INSIDE its box, so a mid-swap frame can never grow
  // the button and shove the toolbar row sideways.
  const QSize box(18, 18);
  swapFace(btn, face("play", "Start", accent));
  pumpFor(kFaceSwapMs / 3);
  check(btn->icon().actualSize(box).width() <= box.width()
            && btn->icon().actualSize(box).height() <= box.height(),
        "a mid-turn glyph stays inside the icon box — no resize under the cursor");
  check(pumpUntil([btn] { return !faceSwapping(btn); })
            && btn->text() == QLatin1String("Start"),
        "…and it still lands on its face");

  // Rapid toggling (a held shortcut): every swap supersedes the one in flight, and the
  // button ends on the LAST face asked for — never a stale word or a stuck animation.
  int flips = 0;
  QString state = "Start";
  for (int i = 0; i < 7; ++i) {
    const bool toStop = i % 2 == 0;
    state = toStop ? "Stop" : "Start";
    swapFace(btn, face(toStop ? "stop" : "play", state, toStop ? QColor(Qt::white) : accent),
             [&flips] { ++flips; });
    pumpFor(kFaceSwapMs / 6);   // each toggle interrupts the one before it
  }
  check(pumpUntil([btn] { return !faceSwapping(btn); }),
        "a burst of toggles leaves no animation running");
  check(btn->text() == state, "…and the button shows the state the last toggle asked for");
  check(btn->property(kFaceGlyphProperty).toString() == QLatin1String("stop"),
        "…with that state's glyph");
  check(flips >= 1, "the surviving swap's state flip ran");
  check(!btn->property(kFaceSwappingProperty).toBool() && btn->styleSheet().isEmpty(),
        "…and the colour override is cleared, however many were interrupted");

  // Reduced motion: the end state at once, still exactly one state flip.
  qputenv("STENCIL_NO_ANIM", "1");
  applied = 0;
  swapFace(btn, face("play", "Start", accent), [&applied] { ++applied; });
  check(!faceSwapping(btn), "reduced motion does not animate");
  check(btn->text() == QLatin1String("Start") && applied == 1,
        "…it lands on the end state, state flip and all");
  check(btn->styleSheet().isEmpty(), "…leaving no colour override behind");
  qunsetenv("STENCIL_NO_ANIM");

  // Degenerate calls are no-ops, not crashes.
  swapFace(nullptr, face("play", "Start", accent));
  swapFace(btn, face(QString(), "Nope", accent));
  check(btn->text() == QLatin1String("Start"), "a null button / empty glyph changes nothing");

  delete btn;
  std::printf("%s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
