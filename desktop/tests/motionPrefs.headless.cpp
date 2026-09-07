// Headless check of the motion preferences (src/support/motionPrefs.hpp) — the desktop
// half of browser/js/ui/motionPrefs.js: the three interface modes, the drawing-animation
// switch, and the promise that EVERY cloud in the app is behind the particle gate,
// because the gate lives inside the DisintegrateOverlay factories rather than in the
// hundred call sites. The persisted keys round-trip through the same
// settingsToJson/settingsFromJson pair the settings file uses.
#include "fileStore.hpp"
#include "disintegrateOverlay.hpp"
#include "motionPrefs.hpp"

#include <QApplication>
#include <QJsonObject>
#include <QLabel>
#include <QPixmap>
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
    check(support::motionModeFromKey("particles") == MotionMode::Particles, "particles");
    check(support::motionModeFromKey("slide") == MotionMode::Slide, "slide");
    check(support::motionModeFromKey("none") == MotionMode::None, "none");
    check(support::motionModeFromKey("sparkles") == MotionMode::Particles,
          "an unknown key reads as the default, never as \"off\"");
    check(support::motionModeFromKey(QString()) == MotionMode::Particles, "…and so does an empty one");
    for (const MotionMode m : {MotionMode::Particles, MotionMode::Slide, MotionMode::None})
      check(support::motionModeFromKey(support::motionModeKey(m)) == m, "key round-trips");
  }

  // ── The truth table every animation in the app leans on ──
  std::printf("gates:\n");
  {
    support::setMotionMode(MotionMode::Particles);
    support::setDrawingAnimations(true);
    check(!support::motionReduced() && support::dustAllowed() && support::drawingMotionOk(),
          "particles: everything plays");
    support::setMotionMode(MotionMode::Slide);
    check(!support::motionReduced(), "slide still moves — each surface keeps its own flight");
    check(!support::dustAllowed(), "…just never out of dust");
    check(support::drawingMotionOk(), "…and the canvas is untouched by it");
    support::setMotionMode(MotionMode::None);
    check(support::motionReduced() && !support::dustAllowed() && !support::drawingMotionOk(),
          "none: nothing moves, the stroke included");

    // The drawing switch is independent of the mode.
    support::setMotionMode(MotionMode::Particles);
    support::setDrawingAnimations(false);
    check(!support::drawingMotionOk(), "the canvas is still…");
    check(support::dustAllowed(), "…while the windows still form out of dust");
    support::setDrawingAnimations(true);

    // STENCIL_NO_ANIM still overrides the preference, as it always has.
    qputenv("STENCIL_NO_ANIM", "1");
    check(support::motionReduced() && !support::dustAllowed() && !support::drawingMotionOk(),
          "the env opt-out wins over any stored mode");
    qunsetenv("STENCIL_NO_ANIM");
    check(support::dustAllowed(), "…and lets go again");
    // Offscreen (where these tests run) has no compositor: the mode may allow particles,
    // the platform still does not.
    check(!support::dustMotionOk(), "dustMotionOk is dustAllowed plus a real platform");
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
                                                        DisintegrateOverlay::Sweep::Fall,
                                                        6, 6, 200, 1.0) != nullptr;
      const bool surface = DisintegrateOverlay::overSurface(snap, at, &host, QPoint(4, 4),
                                                            true) != nullptr;
      check(over == rect && rect == pix && pix == surface,
            "all four factories answer alike");
      std::printf("  %s: %s\n", what, over ? "dust" : "none");
      return over;
    };

    support::setMotionMode(MotionMode::Particles);
    check(clouds("particles"), "particles: the clouds are built");
    support::setMotionMode(MotionMode::Slide);
    check(!clouds("slide"), "slide: no particles anywhere — the callers fall back to their own flight");
    support::setMotionMode(MotionMode::None);
    check(!clouds("none"), "none: nothing at all");
    support::setMotionMode(MotionMode::Particles);
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
    // An older settings.json has neither key and must come back moving, not silent.
    const Settings old = fileStore::settingsFromJson(QJsonObject());
    check(old.motionMode == "particles" && old.drawingAnimations,
          "absent keys -> the defaults, never \"no animation\"");
  }

  std::puts("motionPrefs: OK");
  return 0;
}
