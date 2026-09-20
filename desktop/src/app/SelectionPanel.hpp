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

// Points table + lines list — port of browser/js/ui/selectionPanel.js coordinate table + #lines-
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

    void restyleIcons(const QColor& iconColor);

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

   signals:
    void pointActivated(int index);        // user clicked / double-clicked a row
    void pointDeleteRequested(int index);  // user pressed Delete or clicked the row's 🗑
    // Hover cross-highlight, list → canvas (browser coordTable row mouseenter parity); -1 = left
    // the list.
    void pointRowHovered(int index);
    void lineRowHovered(int index);
    void lineListActivated(int index, bool multi);
    void lineListRemoveRequested(int index);
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
    void styleLineRow(int i);
    void applyUnitHeaders();
    void showEmptyPoints();

    QToolButton* collapseBtn_ = nullptr;  // header chevron: hide the panel (browser panel header)
    // Lives in the header row beside the chevron (browser .coord-panel-header); `tabs_` hides its
    // own bar.
    QTabBar* tabBar_ = nullptr;
    QTabWidget* tabs_ = nullptr;     // Points | Lines pages
    QString unitLabel_{"cm"};        // what the two page columns are headed with
    QTableWidget* points_ = nullptr;
    QListWidget* lines_ = nullptr;   // Lines tab: one row per committed line
    QLabel* multiLabel_ = nullptr;   // "N lines selected" note (multi-select mode)
    QColor iconColor_{"#cccccc"};  // current theme text colour for the per-row 🗑 buttons
    bool updating_ = false;        // suppress itemChanged while showLine repopulates

    int canvasHoverPointRow_ = -1;
    int canvasHoverLineRow_ = -1;
    std::vector<int> linesSelected_;
  };

}
