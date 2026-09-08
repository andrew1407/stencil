#pragma once
#include "models.hpp"
#include <QColor>
#include <QDockWidget>
#include <QString>
#include <vector>

class QTableWidget;
class QLabel;
class QPushButton;
class QToolButton;
class QWidget;
class QTabWidget;
class QTabBar;
class QListWidget;

// Side panel listing the selected line's points and its measurements (point count,
// segment count, total length), plus a flat list of every committed line. Port of
// browser/js/ui/selectionPanel.js's coordinate table + #lines-list. The inline "Selected
// Line:" style editor is SelectedLineBar (a bar above the canvas, browser parity with
// #selection-panel) — not this dock.
namespace stencil::gui {

  class SelectionPanel : public QDockWidget {
    Q_OBJECT
   public:
    // One point's page coordinates, already formatted in the app's current unit — X and Y
    // as their own columns, mirroring the browser's `X cm` / `Y cm` pair (coordTable.js).
    struct PageRow {
      QString x;
      QString y;
    };

    explicit SelectionPanel(QWidget* parent = nullptr);

    // Re-tint the panel's line-art button icons (trash / x) to the active theme
    // text color. Called from MainWindow::applyTheme so they track light/dark,
    // like the toolbar icons (MainWindow::styleActionIcons).
    void restyleIcons(const QColor& iconColor);

    // Refresh from the currently shown line (browser coordTable.js, shown whenever any
    // line exists — panelLine(), not necessarily the selection) and which point within it
    // is focused (-1 = none). pageRows carries the per-point page (cm) coordinates already
    // run through MainWindow's pageCoords converter (so formulas + custom page apply
    // identically to the status bar / tooltip); mirrors browser/js/core/coordTable.js,
    // which shows px AND cm per point. An empty pageRows (e.g. no image yet) falls back
    // to px-only rows.
    void showLine(const core::Line* line, int selectedPoint,
                  const std::vector<PageRow>& pageRows = {});
    // Relabel the two page columns for the app's unit ("cm" / "in" / …) — the browser
    // rewrites the same two ths on every unit change (drawingApp.js updateUnitLabels).
    void setUnitLabel(const QString& label);
    // Show/hide the "N lines selected" multi-select note (n >= 2 shows it).
    void setMultiSelectCount(int n);
    // Rebuild the "Lines" tab list — one row per committed line (color chip, index, point
    // count, area badge), highlighting rows whose index is in `selected`. Mirrors browser
    // drawingApp.js renderLinesList; MainWindow calls it from onSelectionChanged.
    void setLines(const core::Lines& lines, const std::vector<int>& selected);
    // Canvas-driven hover cross-highlight: tint the points-table row `pointRow` (a point
    // of the shown line under the canvas cursor) and the Lines-tab row `lineRow` (the
    // committed line under it). -1 clears. Never scrolls either list.
    void setCanvasHover(int pointRow, int lineRow);
    // Add the panel-toggle keyboard shortcut to the header chevron's tooltip (e.g. "Hide panel (Alt+X)").
    void setToggleHint(const QString& hint);
    // Turn the header chevron over `ms` (ms <= 0 = jump). MainWindow drives it from
    // setPanelShown, so every route into the collapse — chevron, Alt+X, View menu — turns it.
    void spinCollapseChevron(qreal fromDeg, qreal toDeg, int ms);

   signals:
    void pointActivated(int index);        // user clicked / double-clicked a row
    void pointDeleteRequested(int index);  // user pressed Delete or clicked the row's 🗑
    // Hover cross-highlight, list → canvas: the cursor entered a points-table row /
    // Lines-tab row (-1 = it left the list). MainWindow forwards to the canvas's
    // setListHoverPoint / setListHoverLine (browser coordTable row mouseenter parity).
    void pointRowHovered(int index);
    void lineRowHovered(int index);
    // Lines tab: a row was clicked (multi = Ctrl/⌘+Shift held → toggle multi-select) or its 🗑 hit.
    void lineListActivated(int index, bool multi);
    void lineListRemoveRequested(int index);
    // Inline coord edit: a px X/Y cell was committed (axis 0 = x, 1 = y). Forwarded to the
    // canvas's setPointCoord (mirrors browser coordTable.js double-click-to-edit).
    void pointCoordChanged(int index, int axis, double value);

    // The header chevron was clicked → MainWindow slides the panel closed (browser #toggle-coord-panel).
    void collapseRequested();

   protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    // Re-seeds the header chevron at 0° (›): the last collapse left it turned to ‹, which is
    // the floating re-open button's glyph, not this one's.
    void showEvent(QShowEvent* event) override;
    // A theme flip repaints the panel, but the empty row's ink is an ITEM foreground —
    // baked when the row was made, so it kept the old theme's colour and went invisible
    // on the new one (user report). Re-tinted here.
    void changeEvent(QEvent* event) override;

   private:
    // Apply row `i`'s Lines-tab style: selected outline > canvas-hover tint > plain.
    void styleLineRow(int i);
    // Write the column headers, page columns included, for the current unit.
    void applyUnitHeaders();
    // The browser's "No points yet." row — one italic line across the whole table.
    void showEmptyPoints();

    QToolButton* collapseBtn_ = nullptr;  // header chevron: hide the panel (browser panel header)
    // The Points | Lines strip, which lives in the HEADER row beside the chevron (browser
    // .coord-panel-header) — `tabs_` keeps the pages and hides its own bar.
    QTabBar* tabBar_ = nullptr;
    QTabWidget* tabs_ = nullptr;     // Points | Lines pages
    QString unitLabel_{"cm"};        // what the two page columns are headed with
    QTableWidget* points_ = nullptr;
    QListWidget* lines_ = nullptr;   // Lines tab: one row per committed line
    QLabel* multiLabel_ = nullptr;   // "N lines selected" note (multi-select mode)
    QColor iconColor_{"#cccccc"};  // current theme text colour for the per-row 🗑 buttons
    bool updating_ = false;        // suppress itemChanged while showLine repopulates

    // Canvas-driven hover rows (setCanvasHover) + the Lines-tab selection snapshot
    // styleLineRow needs to restyle a single row without a full rebuild.
    int canvasHoverPointRow_ = -1;
    int canvasHoverLineRow_ = -1;
    std::vector<int> linesSelected_;
  };

}
