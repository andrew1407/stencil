// Headless checks for the shared modal shell's small dialogs (support/modalChrome): chooseModal (the
// browser's confirmModal.js `choose` — OK returns the picked VALUE), promptModal's password mode and
// live validation, confirmModal's titleIcon, OpenInDialog's Telegram fallback when a link cannot fit
// the 64-char start payload, the hover shimmer's rounded clip, and addModalFooter's wrap (FooterWrap).
// Offscreen; every modal is answered from a 0-timer inside its own exec() loop.
#include "modalChromeParts.hpp"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QWidget host;
  host.show();

  modalchrome::checkChooseAndPrompt(host);
  modalchrome::checkIconsAndFooter(host);

  return failures ? 1 : 0;
}
