// Headless check of the motion preferences (src/support/motionPrefs.hpp) — the desktop
// half of browser/js/ui/motionPrefs.js: the five interface modes, the drawing-animation
// switch, and the promise that EVERY cloud in the app is behind the particle gate,
// because the gate lives inside the DisintegrateOverlay factories rather than in the
// hundred call sites. The persisted keys round-trip through the same
// settingsToJson/settingsFromJson pair the settings file uses.
#include "motionPrefsParts.hpp"

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  motionprefs::checkModeStorageAndStyles();
  motionprefs::checkGateAndGlyphs();

  return failures ? 1 : 0;
}
