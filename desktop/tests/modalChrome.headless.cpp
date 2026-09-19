// Headless checks for the shared modal shell's small dialogs (support/modalChrome):
//   - chooseModal — the browser's confirmModal.js `choose`: a select of the options
//     under the message, OK returns the picked VALUE (not its label), Enter confirms,
//     Cancel / Escape hand back nothing;
//   - promptModal password mode — the field echoes dots and the trimmed token comes back;
//   - promptModal live validation — Save (and Enter) go dead with the reason shown under
//     the field until the text is saveable, then Enter saves;
//   - confirmModal titleIcon — the header wears the caller's glyph, not only the alert;
//   - OpenInDialog's Telegram fallback — a link that cannot fit the 64-char start
//     payload keeps the dialog OPEN with the browser's fallback row (the two bot
//     commands + the footer hint), while one that fits accepts with the Telegram outcome;
//   - the hover shimmer's rounded clip — the sweep stays inside the Close pill's shape;
//   - addModalFooter's wrap (FooterWrap) — the hint leads the buttons' row while it has
//     room, drops LEFT onto its own line above right-packed buttons when it hasn't, and
//     buttons that cannot share a line even alone wrap onto further right-packed lines.
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
