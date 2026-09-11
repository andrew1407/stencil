#pragma once
#include "models.hpp"
#include <QColor>
#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QSpinBox;
class QComboBox;
class QFrame;

// The "Selected Line:" bar shown above the canvas while a line is selected — port of
// browser/js/ui/selectionPanel.js's #selection-panel: one flat, amber-bordered row of
// inline controls, so its layout AND style match the browser's.
namespace stencil::gui {

  class SelectedLineBar : public QWidget {
    Q_OBJECT
   public:
    explicit SelectedLineBar(QWidget* parent = nullptr);

    // Populate from the selected line (nullptr = nothing to show — MainWindow hides the
    // whole row via the enclosing toolbar). Mirrors browser selectionPanel.js
    // showSelectionPanel/hideSelectionPanels.
    void showLine(const core::Line* line);
    // Settings' default fill color — what an unfilled locked area's swatch falls back to.
    void setDefaultFillColor(const QColor& color);
    // Re-tint the fill-clear "x" icon to the active theme text color.
    void restyleIcons(const QColor& iconColor);

   protected:
    void resizeEvent(QResizeEvent* event) override;

   signals:
    // `preview` = still being chosen in the picker: apply it to the picture now, but let
    // the undo step wait (CanvasWidget::mutateSelectedLine debounces it). Trailing and
    // defaulted, so connections that do not care can keep taking one argument.
    void lineColorChanged(const QString& color, bool preview = false);
    void linePointColorChanged(const QString& pointColor, bool preview = false);
    void lineThicknessChanged(int thickness);
    void linePointSizeChanged(int pointSize);
    void lineStyleChanged(const QString& style);
    void lineFillChanged(const QString& fillColor, bool preview = false);  // "transparent" = no fill
    // Break the closed area back into an open line (browser #sel-unchain).
    void unchainRequested();
    void deselectRequested();

   private:
    QWidget* card_ = nullptr;   // the bordered/rounded box; `this` is just its inset wrapper
    QPushButton* unchainBtn_ = nullptr;
    QFrame* fillSep_ = nullptr;   // the hairline that introduces the fill group
    // Re-assert the height this bar's content needs at its current width (see the .cpp).
    void refitHeight();
    QPushButton* colorSwatch_ = nullptr;
    QPushButton* pointColorSwatch_ = nullptr;
    QSpinBox* thickness_ = nullptr;
    QSpinBox* pointSize_ = nullptr;
    QComboBox* style_ = nullptr;
    QWidget* fillField_ = nullptr;         // "Fill:" label + fillGroup_, hidden as a unit
    QWidget* fillGroup_ = nullptr;        // locked-area fill, hidden otherwise
    QPushButton* fillSwatch_ = nullptr;
    QPushButton* fillClear_ = nullptr;
    QPushButton* deselectBtn_ = nullptr;

    QColor currentColor_{"#FFFF00"};
    QColor currentPointColor_{"#FFFF00"};
    QColor currentFill_{"#ffffff"};
    QColor defaultFill_{"#ffffff"};
    bool updating_ = false;   // suppress signals while showLine repopulates
  };

}  // namespace stencil::gui
