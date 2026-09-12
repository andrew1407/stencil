// Headless check of the motion preferences (src/support/motionPrefs.hpp) — the desktop
// half of browser/js/ui/motionPrefs.js: the five interface modes, the drawing-animation
// switch, and the promise that EVERY cloud in the app is behind the particle gate,
// because the gate lives inside the DisintegrateOverlay factories rather than in the
// hundred call sites. The persisted keys round-trip through the same
// settingsToJson/settingsFromJson pair the settings file uses.
#include "fileStore.hpp"
#include "DisintegrateOverlay.hpp"
#include "motionIcons.hpp"
#include "motionPrefs.hpp"

#include <QApplication>
#include <QJsonObject>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <cmath>
#include <cstdio>

using namespace stencil::gui;
namespace support = stencil::support;
using support::MotionMode;

#include "support/check.hpp"

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  // ── The mode key, as it is stored ──
  std::printf("mode keys:\n");
  {
    check(support::motionModeFromKey("particles") == MotionMode::PARTICLES, "particles");
    check(support::motionModeFromKey("water") == MotionMode::WATER, "water");
    check(support::motionModeFromKey("fire") == MotionMode::FIRE, "fire");
    check(support::motionModeFromKey("slide") == MotionMode::SLIDE, "slide");
    check(support::motionModeFromKey("none") == MotionMode::NONE, "none");
    check(support::motionModeFromKey("sparkles") == MotionMode::PARTICLES,
          "an unknown key reads as the default, never as \"off\"");
    check(support::motionModeFromKey(QString()) == MotionMode::PARTICLES, "…and so does an empty one");
    for (const MotionMode m : {MotionMode::PARTICLES, MotionMode::WATER, MotionMode::FIRE,
                               MotionMode::SLIDE, MotionMode::NONE})
      check(support::motionModeFromKey(support::motionModeKey(m)) == m, "key round-trips");
  }

  // ── Water and fire are particles too: the same gate, a different style on the grains ──
  std::printf("styles:\n");
  {
    using support::ParticleStyle;
    support::setMotionMode(MotionMode::PARTICLES);
    check(support::isDustAllowed() && support::particleStyle() == ParticleStyle::DUST, "particles: dust");
    support::setMotionMode(MotionMode::WATER);
    check(support::isDustAllowed() && !support::motionReduced() && support::particleStyle() == ParticleStyle::WATER,
          "water: the clouds still fly, as drops");
    support::setMotionMode(MotionMode::FIRE);
    check(support::isDustAllowed() && support::particleStyle() == ParticleStyle::FIRE, "fire: …as embers");
    support::setMotionMode(MotionMode::SLIDE);
    check(!support::isDustAllowed(), "slide: no particles of any style");
    support::setMotionMode(MotionMode::PARTICLES);

    // The palette: the theme's accent and its shade, violet until the theme speaks.
    check(support::particleAccent() == QColor(0x7c, 0x3a, 0xed), "violet by default");
    support::setParticlePalette(QColor(10, 20, 30), QColor());
    check(support::particleShade() == QColor(10, 20, 30), "no shade given: the accent stands in");
    support::setParticlePalette(QColor(0x7c, 0x3a, 0xed), QColor(0x6b, 0x32, 0xcc));
    check(support::paletteStop(QColor(0, 0, 0), QColor(250, 100, 50), 0.0) == QColor(0, 0, 0), "mix 0 is the accent");
    check(support::paletteStop(QColor(0, 0, 0), QColor(250, 100, 50), 1.0) == QColor(250, 100, 50), "mix 1 the shade");
    check(support::paletteStop(QColor(0, 0, 0), QColor(250, 100, 50), 0.5) == QColor(150, 60, 30),
          "…quantised to the six stops browser paletteCss declares (0.5 -> stop 3 of 5 = 60%)");
    check(support::paletteIndex(0.5) == 3 && support::paletteIndex(2.0) == 5 && support::paletteIndex(-1) == 0,
          "paletteIndex rounds and clamps like the browser's");

    // The tints: two grains in three ride that ramp, the rest wear one of five colours off
    // their own hash. Pinned to the browser's dustCloud.js tintOf (printed from node).
    check(support::tintOf(0.0) == -1 && support::tintOf(0.37) == -1 && support::tintOf(0.8) == -1,
          "most grains are the accent");
    check(support::tintOf(0.043) == 0 && support::tintOf(0.048) == 1 && support::tintOf(0.0529) == 2
              && support::tintOf(0.0579) == 3 && support::tintOf(0.0628) == 4 && support::tintOf(0.99) == 0,
          "…and the rest each take a fifth of the tints, on the browser's own picks");
    int tinted = 0;
    for (int i = 0; i < 4000; i++) if (support::tintOf(i / 4000.0) >= 0) tinted++;
    check(tinted > 4000 * 0.30 && tinted < 4000 * 0.38, "about a third of a cloud is tinted");
    const QColor violet(0x7c, 0x3a, 0xed);
    check(support::tintColour(violet, 1, false) == QColor(180, 180, 180)
              && support::tintColour(violet, 2, true) == QColor(110, 110, 110),
          "the two greys read on either theme, whatever the accent");
    check(support::tintColour(violet, 3, false) == QColor(183, 147, 245)
              && support::tintColour(violet, 3, true) == QColor(183, 147, 245),
          "…so does the accent blended 55/45 to white");
    // The other two follow the theme, or they would be invisible on one of them
    // (browser css/theme.css --dust-ink / --dust-accent-alt).
    check(support::tintColour(violet, 0, true) == QColor(255, 255, 255)
              && support::tintColour(violet, 0, false) == QColor(0x1f, 0x1f, 0x1f),
          "a white speck on the dark theme is soot on the light one");
    check(support::tintColour(violet, 4, false) == QColor(68, 32, 130)
              && support::tintColour(violet, 4, true) == QColor(216, 196, 250),
          "…and a deep accent on the light theme is a pale one on the dark");
    const QColor shade(0x6b, 0x32, 0xcc);
    check(support::tintedStop(violet, shade, 0.5, support::tintOf(0.37), true) == support::paletteStop(violet, shade, 0.5),
          "an untinted grain keeps its stop on the ramp");
    check(support::tintedStop(violet, shade, 0.5, support::tintOf(0.043), true) == QColor(255, 255, 255)
              && support::tintedStop(violet, shade, 0.0, support::tintOf(0.043), true) == QColor(255, 255, 255),
          "…a tinted one wears its tint whatever its mix says");
    support::setParticlePalette(violet, shade, true);
    check(support::isParticleDark(), "the theme rides along with the palette the overlays read");
    support::setParticlePalette(violet, shade, false);
    check(!support::isParticleDark(), "…and flips back");

    // styleFrame — the browser's dustCloud.js styleFrame, op for op (the sample values
    // below are that module's, printed from node).
    const auto near = [](double a, double b) { return std::abs(a - b) < 1e-5; };
    using namespace stencil::support;
    const support::StyleFrame d = support::styleFrame(ParticleStyle::DUST, 0.5, 0.5, 0.3, 100, 250);
    check(d.sx == 0 && d.sy == 0 && d.scale == 1 && d.glow == 1 && d.mix == 0, "dust is the identity");
    const support::StyleFrame w = support::styleFrame(ParticleStyle::WATER, 0.5, 0.5, 0.3, 100, 250);
    check(near(w.sx, -4.842915806) && near(w.sy, 21.6) && near(w.scale, 1.3)
              && near(w.glow, 0.986375816) && near(w.mix, 0.539229548),
          "water at mid-flight matches the browser");
    const support::StyleFrame f = support::styleFrame(ParticleStyle::FIRE, 0.5, 0.5, 0.3, 100, 250);
    check(near(f.sx, 2.351141009) && near(f.sy, -28.6) && near(f.scale, 1.254448337)
              && near(f.glow, 0.849847387) && near(f.mix, 0.45),
          "fire at mid-flight matches the browser");
    const support::StyleFrame f2 = support::styleFrame(ParticleStyle::FIRE, 0.25, 0.75, 0.8, 40, 1000);
    check(near(f2.sx, -0.699225639) && near(f2.sy, -15.273506474) && near(f2.scale, 1.241430926)
              && near(f2.glow, 0.986540542) && near(f2.mix, 0.7625),
          "…and off-centre, a short throw, late in the wake");
    const support::StyleFrame w2 = support::styleFrame(ParticleStyle::WATER, 0.75, 0.25, 0.1, 20, 333);
    check(near(w2.sx, -1.69621888) && near(w2.sy, 4.07293506) && near(w2.scale, 1.212132034)
              && near(w2.glow, 0.900655963) && near(w2.mix, 0.957049162),
          "…water likewise");
    // Gone at both ends, so a grain still sets off from and lands exactly on its rail.
    for (const ParticleStyle s : {ParticleStyle::WATER, ParticleStyle::FIRE})
      for (const double p : {0.0, 1.0}) {
        const support::StyleFrame e = support::styleFrame(s, p, p, 0.3, 100, 250);
        check(std::abs(e.sx) < 1e-9 && std::abs(e.sy) < 1e-9, "no nudge at either end");
      }
    check(w.sy > 0 && f.sy < 0, "water sags down the screen, fire lifts up it");
    check(support::styleFrame(ParticleStyle::FIRE, 0.5, 0.5, 0.3, 0, 250).sy == 0, "no throw, no lift");

    // The grain shapes — browser dustCloud.js grainShape / shapePolygon, op for op.
    using support::GrainShape;
    check(support::grainShape(ParticleStyle::DUST, 0.1) == GrainShape::DISC
              && support::grainShape(ParticleStyle::DUST, 0.9) == GrainShape::DISC, "dust is always a disc");
    check(support::grainShape(ParticleStyle::WATER, 0.1) == GrainShape::OVAL
              && support::grainShape(ParticleStyle::WATER, 0.8) == GrainShape::WAVE, "water: ovals and wave lines");
    check(support::grainShape(ParticleStyle::FIRE, 0.1) == GrainShape::TRIANGLE
              && support::grainShape(ParticleStyle::FIRE, 0.8) == GrainShape::STREAK, "fire: triangles and sparks");
    const QPolygonF tri = support::shapePolygon(GrainShape::TRIANGLE, QPointF(10, 20), 2, 0.5);
    check(tri.size() == 3 && near(tri[0].x(), 12.983781) && near(tri[0].y(), 21.630047)
              && near(tri[1].x(), 7.549259) && near(tri[1].y(), 20.940142)
              && near(tri[2].x(), 9.466961) && near(tri[2].y(), 17.429811),
          "a triangle lies along its heading exactly as the browser's does");
    check(support::shapePolygon(GrainShape::WAVE, QPointF(0, 0), 2, 0).size() == 18, "a wave line is a 9-sample ribbon");
    check(support::shapePolygon(GrainShape::STREAK, QPointF(0, 0), 2, 0).size() == 4, "a spark is a tapering quad");
    check(near(support::headingOf(0, 1, false), style::PI / 2) && near(support::headingOf(0, 1, true), 3 * style::PI / 2),
          "a gather flies its throw backwards");
  }

  // ── The truth table every animation in the app leans on ──
  std::printf("gates:\n");
  {
    support::setMotionMode(MotionMode::PARTICLES);
    support::setDrawingAnimations(true);
    check(!support::motionReduced() && support::isDustAllowed() && support::isDrawingMotionOk(),
          "particles: everything plays");
    support::setMotionMode(MotionMode::SLIDE);
    check(!support::motionReduced(), "slide still moves — each surface keeps its own flight");
    check(!support::isDustAllowed(), "…just never out of dust");
    check(support::isDrawingMotionOk(), "…and the canvas is untouched by it");
    support::setMotionMode(MotionMode::NONE);
    check(support::motionReduced() && !support::isDustAllowed() && !support::isDrawingMotionOk(),
          "none: nothing moves, the stroke included");

    // The drawing switch is independent of the mode.
    support::setMotionMode(MotionMode::PARTICLES);
    support::setDrawingAnimations(false);
    check(!support::isDrawingMotionOk(), "the canvas is still…");
    check(support::isDustAllowed(), "…while the windows still form out of dust");
    support::setDrawingAnimations(true);

    // STENCIL_NO_ANIM still overrides the preference, as it always has.
    qputenv("STENCIL_NO_ANIM", "1");
    check(support::motionReduced() && !support::isDustAllowed() && !support::isDrawingMotionOk(),
          "the env opt-out wins over any stored mode");
    qunsetenv("STENCIL_NO_ANIM");
    check(support::isDustAllowed(), "…and lets go again");
    // Offscreen (where these tests run) has no compositor: the mode may allow particles,
    // the platform still does not.
    check(!support::isDustMotionOk(), "isDustMotionOk is isDustAllowed plus a real platform");
  }

  // ── The gate is inside the factories, so no cloud can slip past it ──
  std::printf("factories:\n");
  {
    QWidget host;
    host.resize(200, 160);
    auto* victim = new QLabel("row", &host);
    victim->setGeometry(10, 10, 120, 24);
    host.show();
    QPixmap snap(120, 24);
    snap.fill(Qt::gray);
    const QRect at(10, 10, 120, 24);

    const auto clouds = [&](const char* what) {
      const bool over = DisintegrateOverlay::over(victim, &host) != nullptr;
      const bool rect = DisintegrateOverlay::overRect(&host, at, &host) != nullptr;
      const bool pix = DisintegrateOverlay::overPixmaps(snap, QPixmap(), at, &host,
                                                        DisintegrateOverlay::Sweep::FALL,
                                                        6, 6, 200, 1.0) != nullptr;
      const bool surface = DisintegrateOverlay::overSurface(snap, at, &host, QPoint(4, 4),
                                                            true) != nullptr;
      check(over == rect && rect == pix && pix == surface,
            "all four factories answer alike");
      std::printf("  %s: %s\n", what, over ? "dust" : "none");
      return over;
    };

    support::setMotionMode(MotionMode::PARTICLES);
    check(clouds("particles"), "particles: the clouds are built");
    support::setMotionMode(MotionMode::WATER);
    check(clouds("water"), "water: built too, wearing the style");
    support::setMotionMode(MotionMode::FIRE);
    check(clouds("fire"), "fire: likewise");
    support::setMotionMode(MotionMode::SLIDE);
    check(!clouds("slide"), "slide: no particles anywhere — the callers fall back to their own flight");
    support::setMotionMode(MotionMode::NONE);
    check(!clouds("none"), "none: nothing at all");
    support::setMotionMode(MotionMode::PARTICLES);
  }

  // ── The modes' glyphs (support/motionIcons.hpp — browser motionIcons.js) ──
  std::printf("icons:\n");
  {
    const auto inkOf = [](const QString& mode, double ms) {
      QImage img(32, 32, QImage::Format_ARGB32_Premultiplied);
      img.fill(Qt::transparent);
      QPainter p(&img);
      support::paintMotionIcon(p, QRectF(0, 0, 32, 32), mode, QColor(0x7c, 0x3a, 0xed), ms);
      p.end();
      int lit = 0;
      for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) if (qAlpha(img.pixel(x, y)) > 40) ++lit;
      return lit;
    };
    // The browser's own lengths (1125 / 900 / 825+4*90 / 450) on the desktop's 1.5x clock.
    check(support::motionIconMs("water") == 750 && support::motionIconMs("fire") == 600
              && support::motionIconMs("particles") == 790 && support::motionIconMs("none") == 300,
          "the drop, flame and specks take 1.5x the line and arrow");
    for (const char* mode : {"particles", "water", "fire", "slide", "none"}) {
      check(inkOf(mode, 1e9) > 20, "every mode paints a glyph at rest");
      check(!support::motionModeIcon(mode, QColor(Qt::red)).isNull(), "…and yields an icon");
    }
    // The hover plays IN: nothing (or less) at 0ms, the whole glyph at its own duration
    // (support::motionIconMs — the browser's, the drop / flame / specks 1.5x slower).
    // Measured at each mode's OWN end, so the speed-up above can never leave these stale.
    const auto endMs = [](const char* m) { return support::motionIconMs(m); };
    check(inkOf("water", 0.0) < inkOf("water", endMs("water")) / 4, "the drop starts faded out");
    check(inkOf("fire", 0.0) < inkOf("fire", endMs("fire")) / 4, "the flame starts unlit");
    check(inkOf("particles", 0.0) < inkOf("particles", endMs("particles")) / 4, "the specks start out of sight");
    check(inkOf("slide", 0.0) < inkOf("slide", endMs("slide")) / 4, "the arrow starts faded out");
    check(inkOf("none", 0.0) < inkOf("none", endMs("none")) && inkOf("none", 0.0) > 20,
          "the circle stays; only the line draws in");
    check(inkOf("none", endMs("none") / 2) > inkOf("none", 0.0)
              && inkOf("none", endMs("none") / 2) < inkOf("none", endMs("none")),
          "…half-drawn halfway");
  }

  // ── Both preferences ride in settings.json ──
  std::printf("persistence:\n");
  {
    Settings s;
    check(s.motionMode == "particles" && s.drawingAnimations,
          "the defaults match the browser's (particles, drawing on)");
    s.motionMode = "slide";
    s.drawingAnimations = false;
    const Settings back = fileStore::settingsFromJson(fileStore::settingsToJson(s));
    check(back.motionMode == "slide" && !back.drawingAnimations, "both round-trip");
    s.motionMode = "fire";
    check(fileStore::settingsFromJson(fileStore::settingsToJson(s)).motionMode == "fire", "…and so do the new modes");
    // An older settings.json has neither key and must come back moving, not silent.
    const Settings old = fileStore::settingsFromJson(QJsonObject());
    check(old.motionMode == "particles" && old.drawingAnimations,
          "absent keys -> the defaults, never \"no animation\"");
  }

  // The counter every check() feeds — without this the suite passed with failures in it.
  std::printf("motionPrefs: %s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
