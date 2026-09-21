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

// The "Selected Line:" bar — port of browser/js/ui/panel/selectionPanel.js #selection-panel.
namespace stencil::gui {

  class SelectedLineBar : public QWidget {
    Q_OBJECT
   public:
    explicit SelectedLineBar(QWidget* parent = nullptr);

    // nullptr = nothing to show (MainWindow hides the row). Mirrors browser
    // showSelectionPanel/hideSelectionPanels.
    void showLine(const core::Line* line);
    void setDefaultFillColor(const QColor& color);
    void restyleIcons(const QColor& iconColor);

   protected:
    void resizeEvent(QResizeEvent* event) override;

   signals:
    // `preview` = still in the picker: apply now, undo step waits
    // (CanvasWidget::mutateSelectedLine debounces). Trailing and defaulted.
    void lineColorChanged(const QString& color, bool preview = false);
    void linePointColorChanged(const QString& pointColor, bool preview = false);
    void lineThicknessChanged(int thickness);
    void linePointSizeChanged(int pointSize);
    void lineStyleChanged(const QString& style);
    void lineFillChanged(const QString& fillColor, bool preview = false);  // "transparent" = no fill
    // browser #sel-unchain
    void unchainRequested();
    void deselectRequested();

   private:
    QWidget* card = nullptr;   // the bordered/rounded box; `this` is just its inset wrapper
    QPushButton* unchainBtn = nullptr;
    QFrame* fillSep = nullptr;   // the hairline that introduces the fill group
    void refitHeight();
    QPushButton* colorSwatch = nullptr;
    QPushButton* pointColorSwatch = nullptr;
    QSpinBox* thickness = nullptr;
    QSpinBox* pointSize = nullptr;
    QComboBox* style = nullptr;
    QWidget* fillField = nullptr;         // "Fill:" label + fillGroup, hidden as a unit
    QWidget* fillGroup = nullptr;        // locked-area fill, hidden otherwise
    QPushButton* fillSwatch = nullptr;
    QPushButton* fillClear = nullptr;
    QPushButton* deselectBtn = nullptr;

    QColor currentColor{"#FFFF00"};
    QColor currentPointColor{"#FFFF00"};
    QColor currentFill{"#ffffff"};
    QColor defaultFill{"#ffffff"};
    bool updating = false;   // suppress signals while showLine repopulates
  };

}  // namespace stencil::gui
