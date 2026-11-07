#pragma once
// The application half of the webcore skin, MainWindow-free: the classic Windows style, the
// MS Sans Serif face, and the look the restyle reads (support/skin/SkinLookStrategy.hpp); its applier is
// declared in support/skin/skinAppliers.hpp. The window's toggle is app/theme/ThemePainterSkins.cpp.

namespace stencil::support {

  class SkinLookStrategy;

  const SkinLookStrategy& webcoreLook();

}  // namespace stencil::support
