#pragma once
// The application half of the webcore skin, MainWindow-free: the classic Windows style, the
// MS Sans Serif face, and the palette and icon hooks the restyle reads (support/skinPrefs.hpp).

namespace stencil::support {

  // On: Windows style, the skin's font, the hooks installed. Off: Fusion, the font it found, no hooks.
  void applyWebcoreLook(bool on);

}  // namespace stencil::support
