#pragma once
#include "models.hpp"
#include <QBrush>
#include <QColor>
#include <QDockWidget>
#include <QString>
#include <QTimer>
#include <functional>
#include <vector>

class QTableWidget;
class QTableWidgetItem;
class QLabel;
class QPushButton;
class QToolButton;
class QWidget;
class QTabWidget;
class QTabBar;

// Points table + lines list — port of browser/js/ui/panel/selectionPanel.js coordinate table + #lines-
// list. The "Selected Line:" editor is SelectedLineBar.
namespace stencil::gui {

  class SelectionPanel : public QDockWidget {
    Q_OBJECT
   public:
    // One point's page coordinates formatted in the current unit (browser coordTable.js `X cm` /
    // `Y cm`).
    struct PageRow {
      QString x;
      QString y;
    };

    explicit SelectionPanel(QWidget* parent = nullptr);

    void restyleIcons(const QColor& iconColor, const QColor& binColor = QColor());

    // pageRows carries per-point page coords already run through MainWindow's pageCoords; empty
    // (no image) falls back to px-only rows (browser coordTable.js).
    void showLine(const core::Line* line, int selectedPoint,
                  const std::vector<PageRow>& pageRows = {});
    void setUnitLabel(const QString& label);
    void setMultiSelectCount(int n);
    // Mirrors browser drawingApp.js renderLinesList.
    void setLines(const core::Lines& lines, const std::vector<int>& selected);
    // Canvas-driven hover cross-highlight; -1 clears. Never scrolls either list.
    void setCanvasHover(int pointRow, int lineRow);
    void setToggleHint(const QString& hint);
    // ms <= 0 = jump. Every route into the collapse turns it.
    void spinCollapseChevron(qreal fromDeg, qreal toDeg, int ms);
    // Off in fullscreen, where the panel hides on its own (browser: the clone drops its chevron).
    void setCollapseChevronVisible(bool on);
    // A Lines row's line and point colour chips, the anchors their pickers grow from.
    QWidget* lineSwatchCell(int index) const;
    QWidget* pointSwatchCell(int index) const;
    QTableWidget* linesTable() const { return lines; }
    // True while the view is read-only (compare): the lists then leave Delete/Backspace to the window.
    std::function<bool()> readOnly;

   signals:
    void pointActivated(int index);        // user clicked / double-clicked a row
    void pointDeleteRequested(int index);  // user pressed Delete or clicked the row's 🗑
    // Hover cross-highlight, list → canvas (browser coordTable row mouseenter parity); -1 = left
    // the list.
    void pointRowHovered(int index);
    void lineRowHovered(int index);
    void lineListActivated(int index, bool multi);
    void lineListRemoveRequested(int index);
    // The row's colour chips: a click opens a picker once the double-click window passes, a
    // double-click resets instead — the line's colour, or its points' (browser lines/events.js).
    void lineSwatchPick(int index);
    void lineSwatchReset(int index);
    void pointSwatchPick(int index);
    void pointSwatchReset(int index);
    // A row's thickness or point size typed in place, already held to LIMITS (browser lines/numEdit.js).
    void lineThicknessEdited(int index, int thickness);
    void linePointSizeEdited(int index, int pointSize);
    // Inline px coord edit committed (axis 0 = x, 1 = y); mirrors browser coordTable.js double-
    // click-to-edit.
    void pointCoordChanged(int index, int axis, double value);

    void collapseRequested();

   protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    // Re-seeds the chevron at 0° (›): the last collapse left it at ‹, the floating re-open
    // button's glyph.
    void showEvent(QShowEvent* event) override;
    // The empty row's ink is an item foreground baked at creation, so a theme flip must re-tint
    // it.
    void changeEvent(QEvent* event) override;

   private:
    void buildLinesTab();   // SelectionPanelLines.cpp
    void styleLineRow(int i);
    QBrush rowWash(bool picked) const;
    QBrush rowInk() const;
    void applyUnitHeaders();
    void showEmptyPoints();
    // The lone "No … yet." row of either table; it hovers neutral grey (browser tr:hover).
    QTableWidgetItem* emptyMessage(const QString& text) const;
    static bool isEmptyRow(const QTableWidget* t, int row);
    // The I-beam over a cell a double-click edits, the hand over the rest of a row (browser cursor: text).
    void cellCursor(QTableWidget* t, int row, int col) const;
    QBrush emptyWash() const;

    QToolButton* collapseBtn = nullptr;  // header chevron: hide the panel (browser panel header)
    // Lives in the header row beside the chevron (browser .coord-panel-header); `tabs` hides its
    // own bar.
    QTabBar* tabBar = nullptr;
    QTabWidget* tabs = nullptr;     // Points | Lines pages
    QString unitLabel{"cm"};        // what the two page columns are headed with
    QTableWidget* points = nullptr;
    QTableWidget* lines = nullptr;  // Lines tab: the same table, one row per committed line
    QLabel* multiLabel = nullptr;   // "N lines selected" note (multi-select mode)
    QColor iconColor{"#cccccc"};  // current theme text colour for the panel chrome
    QColor binColor{"#cccccc"};   // --danger: the delete bin on BOTH tabs (browser .del-pt-btn)
    bool updating = false;        // suppress itemChanged while showLine repopulates

    int canvasHoverPointRow = -1;
    int canvasHoverLineRow = -1;
    std::vector<int> linesSelected;
  };

}
