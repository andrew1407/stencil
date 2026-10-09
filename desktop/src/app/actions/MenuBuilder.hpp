#pragma once

namespace stencil::gui {

  class MainWindow;

  // Builds the menu bar from the window's actions: File, Edit, Data, View, Project, Help.
  class MenuBuilder {
   public:
    explicit MenuBuilder(MainWindow& w) : w(w) {}

    void buildMenus();

   private:
    MainWindow& w;
  };

}  // namespace stencil::gui
