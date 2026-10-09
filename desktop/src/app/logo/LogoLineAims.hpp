#pragma once
// The lines the header logo can land on (browser twin js/ui/drag/logoTargets.js): a Lines-tab row,
// the selected-line bar, or a line under the pointer on the canvas; each takes the toolbar's style
// as one undo step.
#include "LogoDrag.hpp"

namespace stencil::gui {

  class CanvasWidget;
  class SelectionPanel;
  struct Settings;

  // Where the lines show, and the settings holding the toolbar's style; all outlive the drag.
  struct LogoLineParts {
    CanvasWidget* canvas = nullptr;
    SelectionPanel* panel = nullptr;
    QWidget* bar = nullptr;     // the selected-line bar
    QWidget* frame = nullptr;   // the canvas region, which glows for a line on it
    const Settings* settings = nullptr;
  };

  // Fills `drag`'s lineAt, lineHover and styleLine.
  void addLogoLineHooks(LogoDragHooks& drag, LogoLineParts parts);

}  // namespace stencil::gui
