#pragma once
#include <limits>
#include <QList>
#include <QPointer>
#include <QString>

class QGraphicsOpacityEffect;
class QMenu;
class QScrollBar;
class QTimer;
class QToolBar;
class QToolButton;
class QVariantAnimation;

namespace stencil::core {
  struct FormulaContext;
  struct ProjectMeta;
}

namespace stencil::gui {

  class MainWindow;
  class DisintegrateOverlay;

  // How the editor is viewed: zoom steps, scrolling and the scrollbars' reveal, fullscreen and
  // its zoom flight, the tool rows shown or folded, and the page units the view reads in.
  class EditorView {
   public:
    explicit EditorView(MainWindow& w) : w(w) {}

    void onHovered(double imageX, double imageY);
    void showLinesForDrawing();
    void onPageSizeChanged();
    core::FormulaContext formulaContext() const;
    double currentLineLengthCm() const;
    void stampCanvasMeta(core::ProjectMeta& meta) const;
    void applyUnitToPageInputs();
    void applyUnitToPageCombo();
    void applyUnits(const QString& code);
    void syncUnitControls();
    void zoomIn();
    void zoomOut();
    // On: off. Off: the screen kind.
    void toggleFullscreen();
    // Into `inWindow`'s kind, out when it is the live one, a switch from the other (browser layer.js choose).
    void toggleFullscreenKind(bool inWindow);
    void syncFullscreenTip();
    void wireFullscreenButton();
    void fullscreenButtonClick(bool inWindow);
    void popupFullscreenMenu(QToolButton* btn, bool held);   // held: the press's release picks or closes
    // The Full Screen and Fullscreen in Window rows; the live kind's row is its exit.
    void addFullscreenKindRows(QMenu& menu, int iconPx);
    // The system's own switch (the green button, ctrl-cmd-F, the Window menu), which skips toggleFullscreen.
    void followSystemFullscreen(Qt::WindowStates was, Qt::WindowStates now);
    void setToolbarsVisible(bool on);
    void fsHoverTick();
    void setToolbarsShown(bool show, bool animate);
    void animateBarsHeight(const QList<QToolBar*>& bars, bool show);
    void scrollTo(int x, int y);
    // The zoom and the image point under the viewport's centre; `fit` while the picture sits at its fit.
    struct ViewAnchor { double scale = 0; bool fit = false; double x = 0, y = 0; };
    double fitScale() const;
    ViewAnchor viewAnchor() const;
    // Back to the anchor's zoom with its point centred; a fitted anchor refits instead.
    void restoreAnchor(const ViewAnchor& a);
    // A QGraphicsOpacityEffect per bar: QSS cannot express hidden-until-pan-then-fade.
    // `show` false only re-lays the canvas out: a zoom the user did not make shows no scrollbars.
    void revealCanvasScrollbars(bool show = true);
    void scheduleScrollbarHide();
    QScrollBar* canvasScrollBar(Qt::Orientation o) const;
    // FLIP out of / into the viewport box (js/ui/motion.js); the chosen zoom is preserved.
    void beginFullscreenZoom();
    void startFullscreenZoom();

    // QPointer: QWidget::setGraphicsEffect DELETES the previous effect, so a re-install dangles.
    QPointer<QGraphicsOpacityEffect> vScrollOpacity;
    QPointer<QGraphicsOpacityEffect> hScrollOpacity;
    QPointer<QMenu> fsMenu;   // the fullscreen toggle's menu of both kinds, built on first use
    QTimer* scrollbarHideTimer = nullptr;
    bool scrollbarHovered = false;
    QVariantAnimation* barsAnim = nullptr;

    void validateAndApplyFormulas();
    void syncFullscreenGlyph();
    void flipFullscreen();   // the live kind, on or off
    void markFullscreenBars(bool on);
    void fitCanvasToWindow(bool fill);
    // The rows are veiled for the flight, as the panel and the chat are: the motes ARE the rows.
    QPointer<gui::DisintegrateOverlay> barsSurfaceFlight(const QList<QToolBar*>& bars,
                                                         bool gather, int ms);
    void releaseBarsVeil(const QList<QToolBar*>& bars);
    void refreshStatusHintVisibility();

    // NaN until the cursor really hovers the canvas; the refreshers replay onHovered with these.
    double lastHoverX = std::numeric_limits<double>::quiet_NaN();
    double lastHoverY = std::numeric_limits<double>::quiet_NaN();

   private:
    MainWindow& w;
  };

}  // namespace stencil::gui
