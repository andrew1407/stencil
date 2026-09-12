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

// The "Selected Line:" bar — port of browser/js/ui/selectionPanel.js #selection-panel.
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
    QWidget* card_ = nullptr;   // the bordered/rounded box; `this` is just its inset wrapper
    QPushButton* unchainBtn_ = nullptr;
    QFrame* fillSep_ = nullptr;   // the hairline that introduces the fill group
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
