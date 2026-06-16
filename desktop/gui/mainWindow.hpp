#pragma once
#include "core/formulaParser.hpp"
#include "core/pageMetrics.hpp"
#include "core/projectsStore.hpp"
#include "core/tooltipRows.hpp"
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
class QImage;

// Top-level window. Mirrors the composition done by browser/js/ui/layout.js +
// toolbar.js + the DrawingApp wiring: a toolbar of actions, the canvas in the
// center, and a status bar that reports pixel and page (cm) coordinates.
namespace stencil::gui {

  class CanvasWidget;
  class SelectionPanel;
  class Notifications;
  class CanvasTooltip;
  class MediaLoader;
  struct LaunchOptions;

  class MainWindow : public QMainWindow {
    Q_OBJECT
   public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Apply command-line launch options (the desktop counterpart of the browser's
    // URL deep-links). Called from main() AFTER show() so the async image / video
    // / network resolution runs on the event loop. See gui/launchOptions.hpp.
    void applyLaunchOptions(const LaunchOptions& opts);

   private slots:
    void openImage();
    // Blank-image creator (mirrors browser blankImageModal.js): pick a fill
    // color + px size (defaulting to the page at 96 dpi), then adopt the
    // generated image through the same path as a clipboard paste.
    void newBlankImage();
    // Crop dialog (mirrors browser cropModal.js): pick the page-shaped region of
    // the original image to show on the canvas. Confirms before discarding lines
    // when the orientation flips; the original image is never replaced.
    void openCropDialog();
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
    // Re-sync the persistent context-menu actions to the live canvas state
    // (enable flags, draw-mode label, marker/thickness seeds, group checks,
    // tooltip rows). Called as the first statement of showContextMenu().
    void syncContextActions();
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
    // ctor decomposition (behavior-preserving): the hotkeys-config read/merge,
    // and the contiguous signal-connection block (connect ORDER is preserved
    // verbatim). loadHotkeys() must run before buildActions(); wireSignals()
    // must run after buildActions/Context/Menus/Toolbar (it references their
    // widgets/actions) and before the persisted-state load.
    void loadHotkeys();
    void wireSignals();
    // buildToolbar() split into its three rows (call order preserves the
    // addToolBar/addToolBarBreak sequencing that fixes visual row order).
    void buildMainToolbar();
    void buildPageFormulaToolbar();
    void buildStyleToolbar();
    QString hotkey(const QString& id, const QString& fallback) const;
    core::PageSize currentPageDimensions() const;
    core::Point pageCoords(double imageX, double imageY) const;
    // Active display unit derived from settings_.units (cm default, else inches).
    core::UnitFormat unitFormat() const;
    // Apply the current unit to the custom page spinboxes + their suffix label.
    void applyUnitToPageInputs();
    // Single entry point for changing units: persists, syncs both UI surfaces
    // (View ▸ Units menu + toolbar combo), and refreshes every length readout.
    void applyUnits(const QString& code);
    // Push settings_.units into the menu actions + toolbar combo (no side effects).
    void syncUnitControls();
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
    // Load a saved project (by id) into THIS window's canvas, mirroring the
    // browser switchToProject(): set page size, restore image + lines + crop,
    // mark it active. Returns false if no project with that id exists.
    bool loadProjectIntoCanvas(const QString& id);
    // Open a saved project in a NEW top-level window, leaving this one untouched
    // (the desktop counterpart of the browser's "open in new tab"). The new
    // window owns itself (WA_DeleteOnClose) and reads projects from disk.
    void openProjectInNewWindow(const QString& id);
    // Launch support: open a saved project by NAME (case-insensitive; first
    // match), used by --project. Returns false when no such project exists.
    bool openProjectByName(const QString& name);
    // Launch support: adopt a freshly resolved --src image onto the canvas (path
    // non-empty for a local file, so it survives session/project saves), then
    // apply any pending --layout. Wired to MediaLoader::loaded.
    void onLaunchImageLoaded(const QImage& image, const QString& localPath);
    // Launch support: load a layout JSON from a local path or URL and adopt it
    // (shared applyLayoutJson guards apply). Used for --layout after --src loads.
    void applyLayoutFromSource(const QString& src);
    void newProjectFromCanvas();
    // Shared project-creation: build a Project from the current canvas, persist,
    // refresh, and notify. Used by openProjects' New action + newProjectFromCanvas.
    void createProject(const QString& name);
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
    QLabel* customUnitLabel_ = nullptr;  // "cm"/"in" suffix by the spinboxes
    QComboBox* unitCombo_ = nullptr;     // toolbar cm/in switch (mirrors the menu)
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
    QAction* actNewBlank_ = nullptr;
    QAction* actCrop_ = nullptr;
    QAction* actRotateLeft_ = nullptr;
    QAction* actRotateRight_ = nullptr;
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

    // Units submenu (View ▸ Units): cm | inches, persisted via settings_.units.
    QAction* actUnitCm_ = nullptr;
    QAction* actUnitIn_ = nullptr;

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

    // ── launch options (CLI) ──
    // Async resolver for --src (image / URL / video frame); created on first use.
    MediaLoader* mediaLoader_ = nullptr;
    // A --layout source held until the --src image has loaded, then applied once.
    QString pendingLaunchLayout_;
  };

}
