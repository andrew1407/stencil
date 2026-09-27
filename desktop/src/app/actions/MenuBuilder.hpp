#pragma once

namespace stencil::gui {

  class MainWindow;

  // Builds the menu bar from the window's actions: File, Edit, Data, View, Project, Help.
  class MenuBuilder {
   public:
    explicit MenuBuilder(MainWindow& w) : w(w) {}

    void buildMenus();
    // Qt reads settings.nativeMenuBar only on (re)creation, so a runtime switch rebuilds.
    void applyMenuBarPlacement();

   private:
    MainWindow& w;
  };

}  // namespace stencil::gui
