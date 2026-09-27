#pragma once
#include <QList>
#include <QString>

class QAction;
class QToolBar;
class QWidget;

namespace stencil::gui {

  class MainWindow;

  // Builds the editor's toolbar rows (browser toolbar.js) into the window's ToolbarControls and
  // wires each control to the window. Runs once, from the constructor.
  class ToolbarBuilder {
   public:
    explicit ToolbarBuilder(MainWindow& w) : w(w) {}

    void buildToolbar();
    // Call order preserves the addToolBar/addToolBarBreak row sequencing.
    void buildMainToolbar();
    void buildHeaderRow();
    void buildLogoStage();
    void buildToolSectionsRow();
    void buildFormulaFields();
    QToolBar* toolRow() const;
    QWidget* makeToolSection(const QString& title, const QList<QAction*>& actions,
                             const QList<QWidget*>& extras = {}, const QList<QWidget*>& leading = {});
    void buildProjectNameGroup(QToolBar* bar);
    void buildPageFormulaToolbar();
    void buildStyleToolbar();
    void buildDrawViewToolbar();
    void buildImageInfoBar();

    bool sectionButtonVisible(QAction* act, QToolButton* btn) const;

   private:
    MainWindow& w;
  };

}  // namespace stencil::gui
