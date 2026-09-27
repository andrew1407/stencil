#pragma once
#include <vector>

class QString;

namespace stencil::gui {

  struct Project;

  // Spawning sibling editor windows, and the macOS Dock menu that lists them. Each window is
  // self-owned (WA_DeleteOnClose), so the long-lived Dock menu never dangles one.
  struct SiblingWindows {
    static void openIncognitoWindow();
    static void openProjectsWindow();
    static void openProjectWindowById(const QString& id);
    // Rebuilt from the calling window's projects. A no-op off macOS.
    static void refreshDockMenu(const std::vector<Project>& projects);
  };

}  // namespace stencil::gui
