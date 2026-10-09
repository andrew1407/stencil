#pragma once
#include <limits>
#include <QList>
#include <QPointer>
#include <QString>

class QGraphicsOpacityEffect;
class QScrollBar;
class QTimer;
class QToolBar;
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
    void toggleFullscreen();
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
    void revealCanvasScrollbars();
    void scheduleScrollbarHide();
    QScrollBar* canvasScrollBar(Qt::Orientation o) const;
    // FLIP out of / into the viewport box (js/ui/motion.js); the chosen zoom is preserved.
    void beginFullscreenZoom();
    void startFullscreenZoom();

    // QPointer: QWidget::setGraphicsEffect DELETES the previous effect, so a re-install dangles.
    QPointer<QGraphicsOpacityEffect> vScrollOpacity;
    QPointer<QGraphicsOpacityEffect> hScrollOpacity;
    QTimer* scrollbarHideTimer = nullptr;
    bool scrollbarHovered = false;
    QVariantAnimation* barsAnim = nullptr;

    void validateAndApplyFormulas();
    void syncFullscreenGlyph();
    void markFullscreenBars(bool on);
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
