#pragma once
#include <QString>

class QAction;

namespace stencil::gui {

  class MainWindow;

  // Creates the window's QActions into its WindowActions, maps them to the hotkey table and
  // wires their handlers to the window. Runs once, from the constructor.
  class ActionsBuilder {
   public:
    explicit ActionsBuilder(MainWindow& w) : w(w) {}

    QAction* newAction(const QString& text, const QString& seq);
    // Phases in call order — menus and toolbar iterate the actions in CREATION order; never reorder.
    void buildActions();
    void createCoreActions();
    void createDataActions();
    void wireActionHandlers();
    void mapHotkeyActions();
    void setActionTooltips();

    void bindRevealAnchors();
    void bindRevealAnchor(QAction* a);

   private:
    MainWindow& w;
  };

}  // namespace stencil::gui
