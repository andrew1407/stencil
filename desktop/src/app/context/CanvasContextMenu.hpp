#pragma once
#include <QString>

class QCheckBox;
class QHBoxLayout;
class QPoint;
class QWidgetAction;

namespace stencil::gui {

  class MainWindow;

  // Browser browser/js/ui/contextMenu/contextMenu.js: builds the canvas menu's rows into the window's
  // ContextMenuParts, syncs them to the editor state and raises the menu.
  class CanvasContextMenu {
   public:
    explicit CanvasContextMenu(MainWindow& w) : w(w) {}

    void showContextMenu(const QPoint& globalPos);
    void syncContextActions();
    void buildContextActions();
    void buildDrawNowActions();
    void buildContextStyleActions();
    void buildContextTooltipActions();
    void buildUnitActions();
    QHBoxLayout* makeContextMenuRow(QWidgetAction*& act, int topM = 4, int botM = 4);
    void addContextCheckRow(const QString& text, bool checked, QCheckBox*& box,
                            QWidgetAction*& act);

    void showContextMenuFromKeyboard();

   private:
    MainWindow& w;
  };

}  // namespace stencil::gui
