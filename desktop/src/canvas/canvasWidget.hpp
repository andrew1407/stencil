#pragma once
#include "cropGeometry.hpp"
#include "historyStack.hpp"
#include "holdDraw.hpp"
#include "models.hpp"
#include <QColor>
#include <QImage>
#include <QString>
#include <QWidget>
#include <QPoint>
#include <QRectF>
#include <QElapsedTimer>
#include <QTimer>
#include <vector>

// The drawing surface. Mirrors browser/js/core/renderer.js (what to draw) and
// zoomPan.js (scale), implemented with QPainter. Drawing/geometry decisions reuse
// stencil::core; only the paint calls are Qt-specific.
namespace stencil::gui {

  struct Palette;  // theme.hpp; used by the drawLineScaled paint helpers below

  class CanvasWidget : public QWidget {
    Q_OBJECT
   public:
    // Drawing mode (S2; port of browser drawingApp.js `drawMode` field ~101):
    // freehand polyline vs. drag-to-create rectangle.
    enum class DrawMode { Line, Rect };

    explicit CanvasWidget(QWidget* parent = nullptr);

    bool loadImage(const QString& path);
    void restore(const QString& path, const core::Lines& lines, double scale,
                 const core::CropRect& cropRect = {}, int rotationQuarters = 0);
    const QString& imagePath() const { return imagePath_; }
    bool hasImage() const { return !image_.isNull(); }
    int imageWidth() const { return image_.width(); }
    int imageHeight() const { return image_.height(); }

    // ── crop (shared cropGeometry; mirrors browser DrawingApp.applyCrop) ──
    // The untouched original; the working `image_` is just the cropped region.
    const QImage& originalImage() const { return originalImage_; }
    // The original with the current rotation baked in (== originalImage_ when not
    // rotated). This is the pixel space cropRect_ lives in; the crop dialog
    // previews it.
    QImage effectiveOriginalImage() const;
    core::CropRect cropRect() const { return cropRect_; }
    // Non-destructive 90° rotation: quarter-turns (0..3, clockwise) applied to the
    // original before the crop is taken. Persisted alongside cropRect_.
    int rotationQuarters() const { return rotationQuarters_; }
    // Rotate the whole image a quarter turn (clockwise = right). The crop window
    // and every line follow the picture so the framing/drawing stay put.
    void rotateImage(bool clockwise);
    // Natural page dimensions (cm, NOT orientation-swapped) used to shape the
    // default centered crop. Set by MainWindow on page-size changes.
    void setPageCm(double widthCm, double heightCm);
    // Adopt a new crop rectangle (original-image pixels). With `recalc`, existing
    // lines are cleared on an orientation flip, else rescaled to the new size.
    void applyCrop(const core::CropRect& rect, bool recalc);

    void setScale(double scale);
    double scale() const { return scale_; }

    // Committed lines only; allLines() also includes the in-progress line (for save).
    const core::Lines& lines() const { return lines_; }
    core::Lines allLines() const;
    void setLines(const core::Lines& lines);  // replace all, reset history

    void startNewLine();      // commit the in-progress line, begin a fresh one
    void deleteLastPoint();   // remove the last point of the in-progress line
    void clearAll();          // remove every line
    void undo();
    void redo();
    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }

    // Default visuals applied to newly drawn lines (from Settings).
    void setDefaults(const QString& color, double thickness, double markerSize,
                     const QString& style);
    void setShowPoints(bool on);
    void setShowLines(bool on);
    void setDark(bool dark);
    // Brand-accent preset key (theme.hpp accentPresets); recolours the rubber-band
    // previews to match the rest of the app.
    void setAccent(const QString& accentKey);

    // Selection panel support: the line whose points are shown, and the focused
    // point within it (-1 = none).
    const core::Line* panelLine() const;
    int selectedPoint() const { return selectedPoint_; }
    void selectPoint(int index);
    void deletePoint(int index);
    void deselect();

    // ── selected-line + draw-mode state (S2) ──
    DrawMode drawMode() const { return drawMode_; }
    void setDrawMode(DrawMode mode);
    // Hit-test the committed lines at image-space (x, y) and select the topmost
    // match (or clear when nothing is hit). Returns the chosen index (-1 = none).
    int selectLineAt(double x, double y);
    int selectedLineIdx() const { return selectedLineIdx_; }
    core::Line* selectedLine();
    const core::Line* selectedLine() const;

    // ── image filters (S3; port of browser/js/core/renderer.js) ──
    void setFilter(const QString& mode);
    void setFilterColor(const QColor& tint);
    // Unified entrypoint: set both filter mode + tint with a single repaint.
    void setImageFilter(const QString& mode, const QColor& tint);
    const QString& imageFilter() const { return imageFilter_; }
    const QColor& filterColor() const { return filterColor_; }

    // ── render-to-image + image accessors (S6) ──
    // Native-resolution render of the (filtered) image; overlay draws the lines
    // honoring the current show flags when `withOverlay` is true.
    QImage renderToImage(bool withOverlay) const;
    const QImage& image() const { return image_; }
    QString imageBaseName() const;
    QString imageExt() const;
    // Adopt an in-memory image (clipboard paste / generated): replaces the
    // current image, clears the file path, and resets lines/history/scale.
    void loadFromImage(const QImage& img);

    // ── selected-line mutators + delete (S7; port of applySelectionChange
    // ~1674 and canvasDblClick delete ~1515) ──
    void setSelectedLineColor(const QString& color);
    void setSelectedLineThickness(double thickness);
    void setSelectedLineMarker(double markerSize);
    void setSelectedLineStyle(const QString& style);
    void setSelectedLineFill(const QString& fillColor);
    void deleteSelectedLine();

    // ── drawing-mode state machine (S2; port of drawingApp.js) ──
    bool isDrawing() const { return isDrawing_; }

    // ── hold-to-draw (alternative flow; port of browser holdDraw.js) ──
    // A near-stationary plain-left press-and-hold auto-enters drawing and drops
    // the first point; dwelling drops more; release commits + exits drawing.
    // The delay (ms) is the hold/dwell threshold, surfaced in Settings.
    void setHoldDrawDelay(int ms);
    int holdDrawDelay() const { return holdDelayMs_; }

    // ── interactive editing (port of drawingApp.js Alt-drag + Alt/Ctrl wheel) ──
    // Move a point/segment/whole line by Alt / Alt+Shift drag; bump thickness with
    // Alt+wheel; rotate the selected line with Ctrl+Shift+wheel. Exposed only via
    // mouse/wheel handlers — no extra public API.

   signals:
    void hovered(double imageX, double imageY);  // image-space cursor position
    // Richer hover for the tooltip (S12): image-space pos + global cursor +
    // modifier flags, emitted alongside hovered() on mouse move.
    void hoverDetail(double imageX, double imageY, const QPoint& globalPos,
                     Qt::KeyboardModifiers mods);
    void hoverLeft();  // cursor left the canvas -> hide tooltip
    void changed();                              // lines or history changed
    void selectionChanged();
    void contextRequested(const QPoint& globalPos);
    // Left-click on the imageless (idle) canvas: ask the main window to open
    // the blank-image creator (mirrors the browser idle-canvas icon).
    void blankImageRequested();
    void zoomStep(int dir);  // Ctrl+wheel: +1 = in, -1 = out
    void drawingModeChanged(bool drawing);
    void drawModeChanged(DrawMode mode);  // line vs. rect (S2)
    // Pan/zoom interactions (S7/S8/S9). Deltas/positions are widget-space px;
    // MainWindow owns the scroll area and translates them.
    void panBy(int dx, int dy, bool fast);          // S7 drag pan
    void fitRequested();                            // S7 double-click fit
    void zoomAtCursor(int dir, const QPoint& posInWidget, bool fast);  // S8
    void zoomToRect(const QRectF& imageRect);       // S9 (image-space rect)

   public slots:
    void startDrawingMode();
    void stopDrawingMode();

   protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;
    // App-wide filter: a modifier key (Shift/Ctrl/Alt) pressed or released while
    // the cursor is over the canvas re-applies the hover state so the tooltip and
    // cursor update immediately, without waiting for a mouse move.
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    // S6: drawLine became scale-parameterized so renderToImage can draw the
    // overlay at native resolution (scale 1.0) while the live view uses scale_.
    // `lineIdx` (-1 = the in-progress line) and `highlight` drive the hover/
    // selection rings, which are drawn live but never baked into exports.
    void drawLineScaled(class QPainter& p, const core::Line& line, int lineIdx,
                        double scale, bool highlight) const;
    // drawLineScaled decomposed into ordered const paint passes; poly/stroke/pal
    // are built once in the head and threaded in by const& (no per-pass recompute).
    void drawFill(QPainter& p, const core::Line& line,
                  const class QPolygonF& poly) const;
    void drawGlow(QPainter& p, const core::Line& line, const QPolygonF& poly,
                  int lineIdx, bool highlight, const Palette& pal) const;
    void drawStroke(QPainter& p, const core::Line& line, const QPolygonF& poly,
                    const class QColor& stroke) const;
    void drawMarkers(QPainter& p, const core::Line& line, const QPolygonF& poly,
                     int lineIdx, bool highlight, const QColor& stroke,
                     const Palette& pal) const;
    // mousePressEvent dispatch helpers (behavior-preserving split). Precedence
    // is preserved by the call order in mousePressEvent. handleCtrlClick returns
    // true when it consumes the click; false falls through to a normal append.
    void beginAltDrag(const core::Point& ip, Qt::KeyboardModifiers mods,
                      const QPoint& globalPos);
    void beginZoomRect(const QPoint& widgetPos);
    bool handleCtrlClick(const core::Point& ip);
    void handleDrawingClick(const core::Point& ip, Qt::KeyboardModifiers mods,
                            const QPoint& widgetPos);
    // mouseMoveEvent Alt-drag body (caller computes ip/shift + repaints).
    void updateDrag(const core::Point& ip, bool shift);

    // Hold-to-draw helpers. beginHold arms the controller on a plain-left press;
    // handleHoldTick is the timer slot; holdStart/holdDrop/holdCommit react to the
    // controller's start/drop/commit events; holdAnchor is the preview's origin.
    void beginHold(const QPoint& widgetPos);
    void stopHold();
    void handleHoldTick();
    void holdStart(double widgetX, double widgetY);
    void holdDrop(double widgetX, double widgetY);
    void holdCommit();
    double holdNowMs() const;
    const core::Point* holdAnchor() const;

    core::Point toImageSpace(int widgetX, int widgetY) const;
    void commitHistory();
    void applyDefaultsToCurrent();
    core::Line* mutablePanelLine();
    void createRect(double x1, double y1, double x2, double y2);  // S2
    void rebuildFilteredImage();  // S3

    // Point-insertion / continuation drawing (port of drawingApp.js). Insert a
    // point into an existing line between two points; add a point connected to
    // the current selection (or start a new line); close the line being extended.
    void insertPointOnSegment(int lineIdx, int insertIdx, double x, double y);
    void addConnectedPoint(double x, double y);
    void closeContinuedShape();
    // Insert ip into the line being continued at the (clamped) insert cursor and select
    // it; advance the cursor unless prepending. Caller must have validated continueLineIdx_.
    void insertContinuationPoint(const core::Point& ip, bool advance);

    // Interactive editing helpers (port of drawingApp.js). Refresh the hovered
    // point under the cursor (returns true when it changed), bump thickness of
    // the line under the cursor, and rotate the selected line.
    bool updateHover(double imageX, double imageY);
    // Apply the hover cursor for the given image-space position + modifiers
    // (shared by mouse-move and the modifier-key refresh).
    void applyHoverCursor(const core::Point& ip, Qt::KeyboardModifiers mods);
    // Re-emit hover signals + cursor for the current cursor position using the
    // live modifier state (driven by eventFilter on modifier key changes).
    void refreshHoverForModifiers();
    void adjustThicknessAtCursor(double imageX, double imageY, int dir);
    void rotateSelectedLine(double angleRad);
    void scheduleEditCommit();  // debounced commitHistory for wheel edits

    QImage image_;
    // Crop: the full original bitmap + the page-shaped sub-rectangle shown in
    // image_. cropRect_.width == 0 means "no crop yet". pageW/H drive the default
    // centered crop's aspect (set by MainWindow from the current page size).
    QImage originalImage_;
    core::CropRect cropRect_;
    // 90° quarter-turns (0..3, clockwise) baked into the original before cropping.
    int rotationQuarters_ = 0;
    double pageWidthCm_ = 29.7;
    double pageHeightCm_ = 42.0;
    core::CropRect defaultCropRect() const;  // centered crop for the rotated original
    void rebuildCroppedFromOriginal();       // image_ <- rotated original ∩ cropRect_
    QString imagePath_;
    core::Lines lines_;
    core::Line currentLine_;
    core::HistoryStack history_;
    double scale_ = 1.0;
    int selectedPoint_ = -1;
    bool showPoints_ = true;
    bool showLines_ = true;
    bool isDrawing_ = false;  // gates left-click point adds (S2)
    bool dark_ = false;
    QString accentKey_ = "violet";  // brand accent for the rubber-band previews

    // S2: selected committed-line index (-1 = none) + draw-mode/rect state.
    // Canonical owner; filters/render/line-edit only consume selectedLineIdx_.
    DrawMode drawMode_ = DrawMode::Line;
    int selectedLineIdx_ = -1;
    bool rectDrawActive_ = false;     // drag-to-create rectangle in progress
    QPoint rectDrawStart_, rectDrawEnd_;  // rubber-band corners (widget space)

    // Continuation drawing: when drawing starts with a line selected, new clicks
    // extend that committed line at continueInsertIdx_ instead of building a fresh
    // currentLine_. -1 = not continuing. Port of drawingApp.js #continueLineIdx.
    int continueLineIdx_ = -1;
    int continueInsertIdx_ = -1;

    // S3: image filter (none | bw | sepia | custom tint) cache. filteredImage_
    // is rebuilt lazily on paint when filterDirty_ is set.
    QString imageFilter_ = "none";
    QColor filterColor_{"#7c3aed"};
    QImage filteredImage_;
    bool filterDirty_ = true;

    // Pan/zoom-rect drag state (S7/S9).
    bool panning_ = false;        // Alt+left or middle-button drag
    QPoint lastPanPos_;           // last cursor pos during a pan (GLOBAL space)
    bool zoomRectActive_ = false; // Shift+left drag rubber band
    QPoint zoomRectStart_;        // rubber-band anchor (widget space)
    QPoint zoomRectEnd_;          // rubber-band current corner (widget space)

    QString defColor_ = "#FFFF00";
    double defThickness_ = 2.0;
    double defMarkerSize_ = 4.0;
    QString defStyle_ = "solid";

    // Hover highlight: line/point under the cursor (-1 = none; lineIdx -1 with a
    // valid pointIdx means a point of the in-progress line). Port of drawingApp.js
    // `hoverPt` (~1469) driving renderer.js's point hover ring.
    int hoverLineIdx_ = -1;
    int hoverPointIdx_ = -1;

    // Active Alt-drag gesture. Port of drawingApp.js point/segment/line drags.
    enum class DragKind { None, Point, Segment, Line };
    DragKind dragKind_ = DragKind::None;
    int dragLineIdx_ = -1;   // line being edited (-1 = in-progress line, Point only)
    int dragPtIdx1_ = -1;    // dragged point (Point) / grabbed segment endpoint 1
    int dragPtIdx2_ = -1;    // grabbed segment endpoint 2 (Segment/Line fallback)
    core::Point dragStart_;  // image-space cursor at gesture start
    std::vector<core::Point> dragOrig_;  // snapshot of the line's points at start
    bool dragMoved_ = false;             // any motion happened (gate history)

    // Debounced history commit for wheel-driven edits (thickness/rotation), so a
    // burst of wheel ticks collapses into one undo step (browser saveHistory
    // debounce, ~280 ms).
    QTimer editCommitTimer_;

    // Hold-to-draw state. hold_ is the pure controller; holdTimer_ ticks it while
    // engaged; holdClock_ supplies monotonic ms. holdPreview_ (image space) is the
    // ghost-line cursor while a hold stroke is active.
    core::HoldDrawController hold_;
    QTimer holdTimer_;
    QElapsedTimer holdClock_;
    int holdDelayMs_ = 500;
    bool holdHasPreview_ = false;
    // True while a hold stroke extends a line BACKWARD from its first point: new
    // points are prepended (inserted at index 0) so the line grows from its start.
    bool holdPrepend_ = false;
    core::Point holdPreview_;
    QPoint holdPressPos_;
  };

}
