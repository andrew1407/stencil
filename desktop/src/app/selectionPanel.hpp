#pragma once
#include "models.hpp"
#include <QColor>
#include <QDockWidget>
#include <QString>
#include <vector>

class QTableWidget;
class QLabel;
class QSpinBox;
class QComboBox;
class QPushButton;
class QCheckBox;
class QWidget;

// Side panel listing the selected line's points and its measurements (point
// count, segment count, total length) AND an inline editor for the selected
// line's style (color, thickness, marker size, dash style, locked-area fill).
// Port of browser/js/ui/selectionPanel.js (markup) +
// browser/js/core/drawingApp.js showSelectionPanel/applySelectionChange wiring.
namespace stencil::gui {

  class SelectionPanel : public QDockWidget {
    Q_OBJECT
   public:
    explicit SelectionPanel(QWidget* parent = nullptr);

    // Re-tint the panel's line-art button icons (trash / x) to the active theme
    // text color. Called from MainWindow::applyTheme so they track light/dark,
    // like the toolbar icons (MainWindow::styleActionIcons).
    void restyleIcons(const QColor& iconColor);

    // Refresh from the currently selected line (nullptr = nothing selected) and
    // which point within it is focused (-1 = none). cmRows carries the per-point
    // page (cm) coordinates already run through MainWindow's pageCoords converter
    // (so formulas + custom page apply identically to the status bar / tooltip);
    // mirrors browser/js/core/coordTable.js, which shows px AND cm per point. An
    // empty cmRows (e.g. no image yet) falls back to px-only rows.
    // `line` drives the always-on points/coord table (browser coordTable.js,
    // shown whenever any line exists). `editorLine` gates AND populates the
    // inline editor (selectionPanel.js #selectionPanel), which the browser
    // reveals only on an explicit selection — pass canvas selectedLine() (null
    // when selectedLineIdx_ < 0) so the editor stays hidden for a mere fallback
    // line and its controls never silently no-op.
    void showLine(const core::Line* line, const core::Line* editorLine,
                  int selectedPoint, const std::vector<QString>& cmRows = {});

   signals:
    void pointActivated(int index);        // user clicked / double-clicked a row
    void pointDeleteRequested(int index);  // user pressed Delete or clicked the row's 🗑
    // Inline coord edit: a px X/Y cell was committed (axis 0 = x, 1 = y). Forwarded to the
    // canvas's setPointCoord (mirrors browser coordTable.js double-click-to-edit).
    void pointCoordChanged(int index, int axis, double value);

    // Inline-editor signals — MainWindow forwards these to the canvas's
    // setSelectedLine* mutators (browser/js/core/drawingApp.js:181-195).
    void lineColorChanged(const QString& color);
    void lineThicknessChanged(int thickness);
    void lineMarkerSizeChanged(int markerSize);
    void lineStyleChanged(const QString& style);
    void lineFillChanged(const QString& fillColor);  // "transparent" = no fill
    void lineDeleteRequested();
    void deselectRequested();

   protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

   private:
    // Re-paint a flat color chip onto a swatch button's icon (mirrors the
    // toolbar's MainWindow::updateColorSwatch; browser uses <input type=color>).
    void setSwatchColor(QPushButton* btn, const QColor& color);

    QTableWidget* points_ = nullptr;
    QLabel* measurements_ = nullptr;
    QColor iconColor_{"#cccccc"};  // current theme text colour for the per-row 🗑 buttons

    // Inline line editor (above the points list). Browser selectionPanel.js
    // ids: selColor / selThickness / selMarkerSize / selStyle / selFillGroup /
    // selFillEnabled / selFill / selFillClear / selDeselect.
    QWidget* editor_ = nullptr;
    QPushButton* colorSwatch_ = nullptr;   // selColor
    QSpinBox* thickness_ = nullptr;        // selThickness   (1..20)
    QSpinBox* markerSize_ = nullptr;       // selMarkerSize  (1..30)
    QComboBox* style_ = nullptr;           // selStyle
    QWidget* fillGroup_ = nullptr;         // selFillGroup (locked areas only)
    QCheckBox* fillEnabled_ = nullptr;     // selFillEnabled
    QPushButton* fillSwatch_ = nullptr;    // selFill
    QPushButton* fillClear_ = nullptr;     // selFillClear
    QPushButton* deleteLine_ = nullptr;    // delete the selected line
    QPushButton* deselectBtn_ = nullptr;   // selDeselect

    QColor currentColor_{"#FFFF00"};       // backing for colorSwatch_
    QColor currentFill_{"#3399ff"};        // backing for fillSwatch_
    bool updating_ = false;                // suppress signals during showLine
  };

}
