#pragma once
// The two installers of a skin: on, its look strategy, style and font go in; off, the theme's own come back.
// One per skin (webcore, aqua); the window's toggle calls them. Browser twin: ui/webcore/toggle.js and
// ui/aqua/toggle.js, which wear and shed the same way.
namespace stencil::support {

  void applyWebcoreLook(bool isOn);
  void applyAquaLook(bool isOn);

}  // namespace stencil::support
