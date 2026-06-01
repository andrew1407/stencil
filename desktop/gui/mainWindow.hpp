#pragma once
#include "core/formulaParser.hpp"
#include "core/pageMetrics.hpp"
#include "core/projectsStore.hpp"
#include "fileStore.hpp"
#include <QColor>
#include <QHash>
#include <QMainWindow>
#include <QString>
#include <vector>

class QAction;
class QComboBox;
class QLabel;
class QScrollArea;
class QTimer;
class QDoubleSpinBox;
class QLineEdit;
class QCheckBox;
class QSpinBox;
class QToolButton;
class QJsonObject;
class QActionGroup;
class QWidgetAction;

// Top-level window. Mirrors the composition done by browser/js/ui/layout.js +
// toolbar.js + the DrawingApp wiring: a toolbar of actions, the canvas in the
// center, and a status bar that reports pixel and page (cm) coordinates.
namespace stencil::gui {

  class CanvasWidget;
  class SelectionPanel;
  class Notifications;
  class CanvasTooltip;

  class MainWindow : public QMainWindow {
    Q_OBJECT
   public:
    explicit MainWindow(QWidget* parent = nullptr);

   private slots:
    void openImage();
    void onHovered(double imageX, double imageY);
    void refreshActions();
    void onCanvasChanged();
    void onSelectionChanged();
    void onPageSizeChanged();
    void validateAndApplyFormulas();
    // Toolbar line-style row (S8): push the current default visuals to the canvas
    // and persist them. Mirrors browser drawingApp.js lineColor/lineThickness/
    // markerSize/lineStyle change handlers (~155-178).
    void onLineStyleControlChanged();
    // Single source of truth for the filter/style controls that exist in BOTH the
    // toolbar and the context menu: apply + persist + keep the two UIs in sync.
    void applyImageFilter(const QString& mode);
    void applyTintColor(const QColor& color);
    void applyLineStyle(const QString& style);
    // Paint a small color chip onto a swatch toolbutton (S8).
    void updateColorSwatch(QToolButton* btn, const QColor& color);
    void showContextMenu(const QPoint& globalPos);
    void onHoverDetail(double imageX, double imageY, const QPoint& globalPos,
                       Qt::KeyboardModifiers mods);
    // Data actions (S9): layout JSON export/import + clipboard, and image
    // save/copy/paste. Mirror browser drawingApp.js downloadJSON/uploadJSON/
    // copyLayoutToClipboard/applyPastedLayout (~2071-2222) and saveImage/
    // copyImageToClipboard + the paste listener (~2035-2152, ~557-592).
    void downloadLayout();
    void uploadLayout();
    void copyLayout();
    void pasteLayout();
    void saveImageFile();
    void copyImageToClipboard();
    void pasteImage();

   private:
    void buildActions();
    // S11: construct the persistent grouped actions + QWidgetActions used by the
    // nested right-click context menu (style/filter submenus, tooltip rows, the
    // draw-mode bridge). Mirrors the wiring done in browser/js/ui/contextMenu.js
    // wire() (~112-605). Called once, right after buildActions().
    void buildContextActions();
    void buildMenus();
    void buildToolbar();
    QString hotkey(const QString& id, const QString& fallback) const;
    core::PageSize currentPageDimensions() const;
    core::Point pageCoords(double imageX, double imageY) const;
    void zoomStep(int dir);
    void zoomIn();
    void zoomOut();
    void setZoom(double scale, bool syncCombo = true);
    void fitToWindow();
    void toggleFullscreen();
    void scrollTo(int x, int y);
    void setZoomAnchored(double newScale, const QPoint& cursorInViewport);
    void applyTheme();
    void toggleTheme();
    void applySettings(const Settings& s, bool persist);
    void openSettings();
    void scheduleAutosave();
    void saveSessionNow();
    void restoreSession();
    void openProjects();
    void newProjectFromCanvas();
    void saveToActiveProject();
    void openInfo();
    void openShortcuts();
    void updateStatusIdle();
    // S9: confirm-replace + dimension-mismatch guard, then adopt the parsed
    // layout onto the canvas. Shared by uploadLayout + pasteLayout. Mirrors
    // browser uploadJSON/applyPastedLayout (drawingApp.js ~2101-2222).
    void applyLayoutJson(const QJsonObject& obj);
    void keyPressEvent(QKeyEvent* event) override;

    // ── core widgets ──
    CanvasWidget* canvas_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    SelectionPanel* selPanel_ = nullptr;
    Notifications* notify_ = nullptr;
    CanvasTooltip* tooltip_ = nullptr;
    QLabel* status_ = nullptr;
    QComboBox* pageSize_ = nullptr;
    QComboBox* zoom_ = nullptr;
    QTimer* autosaveTimer_ = nullptr;

    // ── inline toolbar widget groups (S10 custom page, S11 formulas) ──
    // The QWidgetAction handle (…Act_) is toggled, not the widget, so the
    // toolbar re-lays-out and actually makes room for the inputs.
    QWidget* customGroup_ = nullptr;
    QAction* customGroupAct_ = nullptr;
    QDoubleSpinBox* customW_ = nullptr;
    QDoubleSpinBox* customH_ = nullptr;
    QCheckBox* allowFormulas_ = nullptr;
    QWidget* formulaGroup_ = nullptr;
    QAction* formulaGroupAct_ = nullptr;
    QLineEdit* formulaX_ = nullptr;
    QLineEdit* formulaY_ = nullptr;
    QLabel* formulaError_ = nullptr;

    // ── Style toolbar row (S8; browser toolbar.js Image + Line Style + Draw
    // sections ~24-63). Filter combo + tint swatch, default line color/thickness/
    // marker/style controls, and the line/rect draw-mode toggle. The toolbar sets
    // canvas DEFAULTS only — selected-line inline editing is owned by the
    // SelectionPanel (Step 10), per the plan's setSelectedLineStyle resolution.
    QToolButton* drawModeBtn_ = nullptr;
    QToolButton* lineColorBtn_ = nullptr;
    QSpinBox* lineThickness_ = nullptr;
    QSpinBox* markerSize_ = nullptr;
    QComboBox* lineStyle_ = nullptr;
    QComboBox* imageFilter_ = nullptr;
    QToolButton* filterColorBtn_ = nullptr;
    QAction* filterColorAct_ = nullptr;  // tint swatch action (hidden unless custom)
    QColor lineColorValue_{"#FFFF00"};
    QColor filterColorValue_{"#7c3aed"};

    // ── actions (shared by menu bar, toolbar, context menu) ──
    QAction* actOpen_ = nullptr;
    QAction* actStartDraw_ = nullptr;
    QAction* actStopDraw_ = nullptr;
    QAction* actNewLine_ = nullptr;
    QAction* actUndo_ = nullptr;
    QAction* actRedo_ = nullptr;
    QAction* actDeleteLast_ = nullptr;
    QAction* actClearAll_ = nullptr;
    QAction* actDeselect_ = nullptr;
    QAction* actZoomIn_ = nullptr;
    QAction* actZoomOut_ = nullptr;
    QAction* actFit_ = nullptr;
    QAction* actShowPoints_ = nullptr;
    QAction* actShowLines_ = nullptr;
    QAction* actAllowFormulas_ = nullptr;  // View toggle mirroring allowFormulas_
    QAction* actTooltip_ = nullptr;        // View toggle for the hover tooltip
    QAction* actTheme_ = nullptr;
    QAction* actPanel_ = nullptr;
    QAction* actFullscreen_ = nullptr;
    QAction* actSettings_ = nullptr;
    QAction* actProjects_ = nullptr;
    QAction* actNewProject_ = nullptr;
    QAction* actSaveProject_ = nullptr;
    QAction* actSaveSession_ = nullptr;
    QAction* actInfo_ = nullptr;
    QAction* actIncognito_ = nullptr;
    QAction* actShortcuts_ = nullptr;
    QAction* actQuit_ = nullptr;

    // ── Data actions (S9; browser toolbar.js Image/Layout buttons + the paste
    // listener). Layout JSON export/import + clipboard, image save/copy/paste.
    QAction* actDownloadJson_ = nullptr;
    QAction* actUploadJson_ = nullptr;
    QAction* actCopyLayout_ = nullptr;
    QAction* actPasteLayout_ = nullptr;
    QAction* actSaveImage_ = nullptr;
    QAction* actCopyImage_ = nullptr;
    QAction* actPasteImage_ = nullptr;

    // ── Context-menu submenu actions (S11; browser/js/ui/contextMenu.js). These
    // persistent actions/QWidgetActions are owned by `this` and reused on every
    // right-click so their checked/enabled/visible state stays live. The toolbar
    // and menu bar keep their own shared QActions; these cover the bits the
    // context menu adds on top (draw-mode bridge, instant rect, the Style/Filter/
    // Tooltip submenus).

    // Draw-mode bridge (contextMenu.js:416-421 ctx-drawmode-toggle) — flips the
    // canvas line<->rect mode and notifies. Label re-synced before each exec.
    QAction* actDrawModeToggle_ = nullptr;
    // Instant rectangle (contextMenu.js:425-431 ctx-draw-rect): rect mode + begin
    // drawing immediately.
    QAction* actDrawRectNow_ = nullptr;

    // Style submenu (contextMenu.js:39-57): marker/thickness spinboxes hosted in
    // QWidgetActions + an exclusive line-style radio group. Drive canvas defaults.
    QActionGroup* lineStyleGroup_ = nullptr;
    QAction* actStyleSolid_ = nullptr;
    QAction* actStyleDashed_ = nullptr;
    QAction* actStyleDotted_ = nullptr;
    QWidgetAction* markerSizeAction_ = nullptr;
    QWidgetAction* thicknessAction_ = nullptr;
    QSpinBox* markerSpin_ = nullptr;
    QSpinBox* thickSpin_ = nullptr;

    // Image Filter submenu (contextMenu.js:59-74): exclusive filter radio group +
    // a custom-tint picker action shown only for "custom".
    QActionGroup* filterGroup_ = nullptr;
    QAction* actFilterNone_ = nullptr;
    QAction* actFilterBW_ = nullptr;
    QAction* actFilterSepia_ = nullptr;
    QAction* actFilterCustom_ = nullptr;
    QAction* tintColorAction_ = nullptr;

    // Tooltip submenu rows (contextMenu.js:96-107): per-row visibility toggles.
    // Backed by the booleans below (no Settings persistence yet — see notes).
    QAction* actTtPage_ = nullptr;
    QAction* actTtScreen_ = nullptr;
    QAction* actTtCoords_ = nullptr;
    bool tooltipShowPage_ = true;
    bool tooltipShowScreen_ = true;
    bool tooltipShowCoords_ = true;

    // ── hotkeys (S13: defaults + user overrides, live re-apply) ──
    QHash<QString, QString> hotkeys_;
    QHash<QString, QString> hotkeyDefaults_;
    QHash<QString, QString> hotkeyLabels_;
    QHash<QString, QAction*> hotkeyActions_;

    // ── state ──
    Settings settings_;
    core::FormulaParser formula_;
    core::ProjectsStore projectsStore_;
    std::vector<Project> projectList_;
    QString activeProjectId_;
    bool incognito_ = false;
    double lastHoverX_ = 0.0;
    double lastHoverY_ = 0.0;
  };

}
