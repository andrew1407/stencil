#pragma once

class QWidget;

namespace stencil::gui {

  class CanvasTooltip;
  class DropZonesOverlay;
  class IncognitoOverlay;
  class ProjectDragZones;

  // The widgets floated over the window and its canvas viewport; each is a child the window owns.
  struct WindowOverlays {
    CanvasTooltip* tooltip = nullptr;
    IncognitoOverlay* incognito = nullptr;
    DropZonesOverlay* dropZones = nullptr;
    ProjectDragZones* projectZones = nullptr;
    // Built on the first chat-dock drag.
    QWidget* dockZones = nullptr;
  };

}  // namespace stencil::gui
