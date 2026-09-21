// Headless check of the rich control tooltips (src/support/tipContent.cpp) — the desktop port of
// browser/js/ui/tip/content.js, carrying that suite's cases so the three renderings of a tooltip
// cannot drift. Split across tipContent*.headless.cpp; this TU owns the palette they render in.
#include "tipContentParts.hpp"

int main(int argc, char** argv) {
  QApplication app(argc, argv);  // the keycaps are PAINTED, so this needs a GUI app
  const auto pal = themePalette(false);

  parseCases();
  renderCases(pal);
  composeCases(pal);

  std::printf("%s\n", failures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILURES");
  return failures == 0 ? 0 : 1;
}
