#pragma once
#include <vector>

class QString;

namespace stencil::gui {

  class MainWindow;
  struct Project;

  // Spawning sibling editor windows, and the macOS Dock menu that lists them. Each window is
  // self-owned (WA_DeleteOnClose), so the long-lived Dock menu never dangles one.
  struct SiblingWindows {
    // Every window that has not completed an accepted close; a held close keeps it listed.
    static void noteOpened(MainWindow* w);
    static void noteClosed(MainWindow* w);
    static int openCount();
    // Closes every window, held ones included (QApplication::closeAllWindows stops at the first
    // refusal); with the quit rule on, the last accepted close ends the app.
    static void closeAll();
    static void setQuitWhenAllClosed(bool on);

    static void openIncognitoWindow();
    static void openProjectsWindow();
    static void openProjectWindowById(const QString& id);
    // Rebuilt from the calling window's projects. A no-op off macOS.
    static void refreshDockMenu(const std::vector<Project>& projects);
  };

}  // namespace stencil::gui
