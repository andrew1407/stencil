#pragma once
#include "core/historyStack.hpp"
#include "core/models.hpp"
#include <QColor>
#include <QImage>
#include <QString>
#include <QWidget>
#include <QPoint>
#include <QRectF>
#include <QTimer>
#include <vector>

// The drawing surface. Mirrors browser/js/core/renderer.js (what to draw) and
// zoomPan.js (scale), implemented with QPainter. Drawing/geometry decisions reuse
// stencil::core; only the paint calls are Qt-specific.
namespace stencil::gui {

  class CanvasWidget : public QWidget {
    Q_OBJECT
   public:
    // Drawing mode (S2; port of browser drawingApp.js `drawMode` field ~101):
    // freehand polyline vs. drag-to-create rectangle.
    enum class DrawMode { Line, Rect };

    explicit CanvasWidget(QWidget* parent = nullptr);

    bool loadImage(const QString& path);
    void restore(const QString& path, const core::Lines& lines, double scale);
    const QString& imagePath() const { return imagePath_; }
    bool hasImage() const { return !image_.isNull(); }
    int imageWidth() const { return image_.width(); }
    int imageHeight() const { return image_.height(); }

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
  };

}
