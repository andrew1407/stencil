#pragma once
#include <QColor>
#include <QString>

namespace stencil::gui {

  class MainWindow;

  // The default line style the toolbar and the context menu set: colour, thickness, point size
  // and dash, pushed to the canvas and saved, plus the compare view's selector state.
  class StyleControls {
   public:
    explicit StyleControls(MainWindow& w) : w(w) {}

    // Browser twin: drawingApp.js lineColor/lineThickness/pointSize/lineStyle handlers.
    void onLineStyleControlChanged();
    void setCompareModeUi(const QString& mode);
    void applyLineStyle(const QString& style);
    QColor effectiveDefaultPointColor() const;

   private:
    MainWindow& w;
  };

}  // namespace stencil::gui
