#pragma once
#include "models.hpp"
#include <QColor>
#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QSpinBox;
class QComboBox;
class QCheckBox;

// The "Selected Line:" bar shown above the canvas while a line is selected — port of
// browser/js/ui/selectionPanel.js's #selection-panel: one flat, amber-bordered row of
// inline controls, split out of the old dock-panel editor so its layout AND style match
// the browser's instead of a vertical, gradient-styled QFormLayout.
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
    void lineColorChanged(const QString& color);
    void linePointColorChanged(const QString& pointColor);
    void lineThicknessChanged(int thickness);
    void linePointSizeChanged(int pointSize);
    void lineStyleChanged(const QString& style);
    void lineFillChanged(const QString& fillColor);  // "transparent" = no fill
    void deselectRequested();

   private:
    QWidget* card_ = nullptr;   // the bordered/rounded box; `this` is just its inset wrapper
    QPushButton* colorSwatch_ = nullptr;
    QPushButton* pointColorSwatch_ = nullptr;
    QSpinBox* thickness_ = nullptr;
    QSpinBox* pointSize_ = nullptr;
    QComboBox* style_ = nullptr;
    QWidget* fillField_ = nullptr;         // "Fill:" label + fillGroup_, hidden as a unit
    QWidget* fillGroup_ = nullptr;        // locked-area fill, hidden otherwise
    QCheckBox* fillEnabled_ = nullptr;
    QPushButton* fillSwatch_ = nullptr;
    QPushButton* fillClear_ = nullptr;
    QPushButton* deselectBtn_ = nullptr;

    QColor currentColor_{"#FFFF00"};
    QColor currentPointColor_{"#FFFF00"};
    QColor currentFill_{"#3399ff"};
    QColor defaultFill_{"#3399ff"};
    bool updating_ = false;   // suppress signals while showLine repopulates
  };

}  // namespace stencil::gui
