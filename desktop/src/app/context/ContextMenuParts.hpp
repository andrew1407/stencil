#pragma once
#include <QPoint>

class QAction;
class QActionGroup;
class QButtonGroup;
class QCheckBox;
class QLineEdit;
class QSpinBox;
class QWidget;
class QWidgetAction;

namespace stencil::gui {

  // Browser browser/js/ui/contextMenu/contextMenu.js: the canvas menu's own rows, owned by the window and reused on
  // every right-click.
  struct ContextMenuParts {
    QAction* drawLineNow = nullptr;
    QAction* drawRectNow = nullptr;

    QActionGroup* lineStyleGroup = nullptr;
    QAction* styleSolid = nullptr;
    QAction* styleDashed = nullptr;
    QAction* styleDotted = nullptr;
    QWidgetAction* pointSizeAction = nullptr;
    QWidgetAction* thicknessAction = nullptr;
    QSpinBox* pointSpin = nullptr;
    QSpinBox* thickSpin = nullptr;

    // Plain muted text, NOT QMenu::addSection(): that QAction isSeparator(), so the QSS paints a line.
    QWidgetAction* secImageAct = nullptr;
    QWidgetAction* secLayoutJsonAct = nullptr;
    QWidgetAction* secLineStyleAct = nullptr;
    QWidgetAction* secFilterAct = nullptr;
    QWidgetAction* secCoordFormulasAct = nullptr;
    QWidgetAction* secShowInTooltipAct = nullptr;

    // Hosted radios in an exclusive QButtonGroup keep the menu open, like the browser's inline radios.
    QButtonGroup* filterButtons = nullptr;
    QActionGroup* compareGroup = nullptr;
    QWidgetAction* filterNone = nullptr;
    QWidgetAction* filterBW = nullptr;
    QWidgetAction* filterSepia = nullptr;
    QWidgetAction* filterInvert = nullptr;
    QWidgetAction* filterContour = nullptr;
    QWidgetAction* filterCustom = nullptr;
    QAction* tintColorAction = nullptr;

    // Real QCheckBoxes in QWidgetActions, so a click flips them WITHOUT closing the menu.
    QWidgetAction* tooltipEnable = nullptr;
    QCheckBox* tooltipEnableCheck = nullptr;
    QWidgetAction* ttPage = nullptr;
    QWidgetAction* ttScreen = nullptr;
    QWidgetAction* ttCoords = nullptr;
    QCheckBox* ttPageCheck = nullptr;
    QCheckBox* ttScreenCheck = nullptr;
    QCheckBox* ttCoordsCheck = nullptr;

    QWidgetAction* allowFormulasAct = nullptr;
    QCheckBox* allowFormulas = nullptr;
    QWidgetAction* formulaXAct = nullptr;
    QWidgetAction* formulaYAct = nullptr;
    QLineEdit* formulaX = nullptr;
    QLineEdit* formulaY = nullptr;

    // The QWidgetAction owns the panel, so the typed script survives the menu closing.
    QWidgetAction* scriptAction = nullptr;
    QWidget* scriptPanel = nullptr;
    QWidget* scriptEditor = nullptr;
    QPoint menuAt;                     // where the last canvas menu was raised
    bool reopenScriptPending = false;  // …and whether to land on the script row this time
  };

}  // namespace stencil::gui
