#pragma once
#include <QList>
#include <QString>
#include <functional>

class QAction;
class QHBoxLayout;
class QLabel;
class QToolButton;
class QToolBar;
class QWidget;

namespace stencil::gui {

  class MainWindow;

  // A section's caption in the skin's dress: the browser's spaced capitals, or aqua's plain 11px
  // bold (css/aqua/chrome.css .ctrl-section-label). Re-run on every skin change.
  void styleSectionCaption(QLabel* label);

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
    void wireShareButtonMenu();
    void buildNameCopyChip(QHBoxLayout* nameLay, const std::function<void(QToolButton*)>& sizeToRow);
    void buildPageFormulaToolbar();
    void buildStyleToolbar();
    void buildDrawViewToolbar();
    void buildImageInfoBar();
    // What dragging an icon does (app/drag): only the chat, dialog, canvas and zoom icons drag.
    void buildIconDrags();

    bool sectionButtonVisible(QAction* act, QToolButton* btn) const;

   private:
    MainWindow& w;
  };

}  // namespace stencil::gui
