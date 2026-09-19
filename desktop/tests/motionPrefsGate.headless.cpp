// The gate's truth table, that it sits inside the factories, the glyphs, and the settings keys.
#include "motionPrefsParts.hpp"

namespace motionprefs {

  void checkGateAndGlyphs() {
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

  }

}  // namespace motionprefs
