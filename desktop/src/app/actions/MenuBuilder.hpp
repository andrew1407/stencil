#pragma once

namespace stencil::gui {

  class MainWindow;

  // Builds the menu bar from the window's actions: File, Edit, Data, View, Project, Help.
  class MenuBuilder {
   public:
    explicit MenuBuilder(MainWindow& w) : w(w) {}

    void buildMenus();
    // File ▸ Autosave, View ▸ Use System Menu Bar and Project ▸ Open In Targets…: the desktop-only
    // switches the browser's Visuals has no row for, placed into the menus buildMenus() made.
    void addDesktopSwitches();

   private:
    MainWindow& w;
  };

}  // namespace stencil::gui
