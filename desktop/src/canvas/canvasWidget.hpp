#pragma once
#include "cropGeometry.hpp"
#include "idleCardMotion.hpp"   // the idle card's glyph motion (iconMotion.json "image")
#include "historyStack.hpp"
#include "holdDraw.hpp"
#include "models.hpp"
#include "chainEdit.hpp"
#include "strokeGrowth.hpp"
#include <QColor>
#include <QImage>
#include <QString>
#include <QWidget>
#include <QPoint>
#include <QRectF>
#include <QPolygonF>
#include <QElapsedTimer>
#include <QTimer>
#include <functional>
#include <vector>

class QNativeGestureEvent;
class QVariantAnimation;  // not transitively declared by <QWidget> (unlike QWheelEvent)

// The drawing surface. Mirrors browser/js/core/renderer.js (what to draw) and
// zoomPan.js (scale), implemented with QPainter. Drawing/geometry decisions reuse
// stencil::core; only the paint calls are Qt-specific.
namespace stencil::gui {

  struct Palette;  // theme.hpp; used by the drawLineScaled paint helpers below

  class CanvasWidget : public QWidget {
    Q_OBJECT
   public:
    // Drawing mode (port of browser drawingApp.js `drawMode` field ~101):
    // freehand polyline vs. drag-to-create rectangle.
    enum class DrawMode { Line, Rect };

    explicit CanvasWidget(QWidget* parent = nullptr);

    // `decoded` = pixels the caller already read (the open path decodes off the GUI
    // thread), so a file is never decoded twice.
    bool loadImage(const QString& path, const QImage& decoded = QImage());
    void restore(const QString& path, const core::Lines& lines, double scale,
                 const core::CropRect& cropRect = {}, int rotationQuarters = 0,
                 const QImage& decoded = QImage());   // …pixels, if the caller has them
    const QString& imagePath() const { return imagePath_; }
    // Adopt an on-disk path for the current in-memory original (pixels unchanged).
    // Used when a generated/remote/video image is written to the state dir so the
    // project + session can reload it later.
    void setImagePath(const QString& path) { imagePath_ = path; }
    bool hasImage() const { return !image_.isNull(); }
    int imageWidth() const { return image_.width(); }
    int imageHeight() const { return image_.height(); }

    // crop (shared cropGeometry; mirrors browser DrawingApp.applyCrop)
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
    // The in-progress line (empty points ⇒ none) — lets callers read committed + current
    // without allLines()'s whole-vector copy.
    const core::Line& currentLine() const { return currentLine_; }
    core::Lines allLines() const;
    void setLines(const core::Lines& lines);  // replace all, reset history

    void startNewLine();      // commit the in-progress line, begin a fresh one
    void deleteLastPoint();   // remove the last point of the in-progress line
    void clearAll();          // remove every line
    // Turn the selected closed area (a closed shape or a rect) back into an open line.
    void unchainSelectedLine();
    void undo();
    void redo();
    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }

    // Default visuals applied to newly drawn lines (from Settings).
    // `pointColor` is the default POINT colour for new lines; empty = follow `color`
    // (core::Line::pointColor). Defaulted so existing call sites keep compiling.
    void setDefaults(const QString& color, double thickness, double pointSize,
                     const QString& style, const QString& pointColor = QString());
    void setShowPoints(bool on);
    void setShowLines(bool on);
    void setDark(bool dark);
    // Brand-accent preset key (theme.hpp accentPresets); recolours the rubber-band
    // previews to match the rest of the app.
    void setAccent(const QString& accentKey);
    // Highlight styles (browser DEFAULT_VISUALS.selGlowColor/hoverRingColor/
    // focusRingColor, Settings-backed) — override the Palette's theme-derived
    // defaults so a user's choice actually shows on the canvas.
    void setHighlightColors(const QColor& selGlow, const QColor& hoverRing,
                            const QColor& focusRing);

    // Selection panel support: the line whose points are shown, and the focused
    // point within it (-1 = none).
    const core::Line* panelLine() const;
    // panelLine()'s index in lines() (-1 = the in-progress line / none) — lets the
    // panel map its point rows back onto canvas lines for the hover cross-highlight.
    int panelLineIdx() const;
    // Hover arriving FROM the panel lists: ring the panel line's point `ptIdx`
    // (points-table row) / glow line `lineIdx` (Lines-tab row). -1 clears.
    void setListHoverPoint(int ptIdx);
    void setListHoverLine(int lineIdx);
    int selectedPoint() const { return selectedPoint_; }
    void selectPoint(int index);
    void deletePoint(int index);
    // Set one coordinate (axis 0 = x, 1 = y) of the panel line's point `index` to `value` px
    // (image space). Port of browser drawingApp.js setPointCoord — the inline edit in the
    // points table. No clamping (mirrors the browser); commits history for a committed line.
    void setPointCoord(int index, int axis, double value);
    void deselect();

    DrawMode drawMode() const { return drawMode_; }
    void setDrawMode(DrawMode mode);
    // Hit-test the committed lines at image-space (x, y) and select the topmost
    // match (or clear when nothing is hit). Returns the chosen index (-1 = none).
    int selectLineAt(double x, double y);
    int selectedLineIdx() const { return selectedLineIdx_; }
    core::Line* selectedLine();
    const core::Line* selectedLine() const;
    // multi-line selection (Ctrl+Shift+click)
    // The full selection set (single or multi); [] when nothing is selected.
    std::vector<int> selectedIndices() const;
    // Number of selected lines (MainWindow gates the single-line panel when this is >= 2).
    int selectionCount() const { return static_cast<int>(selectedIndices().size()); }
    // Is line `i` part of the current selection (drives the render glow)?
    bool isLineSelected(int i) const;
    // Add/remove the line under image-space `ip` from the multi-select set (Ctrl+Shift+click).
    void toggleLineSelection(const core::Point& ip);
    // index-keyed selection (Lines tab list) — twins of the hit-test paths above
    void selectLineByIndex(int idx);            // single-select line `idx`
    void toggleLineSelectionByIndex(int idx);   // Ctrl+Shift+click a list row
    void removeLineByIndex(int idx);            // delete line `idx` (list 🗑)
    // keyboard transforms of the selection (MainWindow arrow-key handler)
    void rotateSelectedLine(double angleRad);   // Alt+R+←/→ and Ctrl+Shift+wheel
    void flipSelectedLine(bool horizontal);     // Alt+Shift+↑/↓ mirror about the bbox centre
    void nudgeSelected(double dx, double dy);   // arrow-key translate (image-space px)

    // image filters (port of browser/js/core/renderer.js)
    void setFilter(const QString& mode);
    void setFilterColor(const QColor& tint);
    // Unified entrypoint: set both filter mode + tint with a single repaint.
    void setImageFilter(const QString& mode, const QColor& tint);
    const QString& imageFilter() const { return imageFilter_; }
    const QColor& filterColor() const { return filterColor_; }

    // compare view (port of browser DrawingApp.compareMode)
    // Hold the edited result against the untouched original (crop + rotation only —
    // no filter, lines, points or layout). "none" = normal; "original" = original
    // alone; "vertical"/"horizontal" = split with a movable divider (original on the
    // left/top, current edit on the right/bottom). Transient view state, never saved.
    void setCompareMode(const QString& mode);
    const QString& compareMode() const { return compareMode_; }
    // True while a split compare view (vertical/horizontal) is actually showing —
    // the single source of truth MainWindow/DataExportController both gate on.
    bool isSplitCompare() const { return compareMode_ == "vertical" || compareMode_ == "horizontal"; }
    void setCompareSplit(double fraction);      // divider position 0..1
    double compareSplit() const { return compareSplit_; }
    // Alt+Shift+O momentary "peek at the original" override (shown while held).
    void setCompareHoldOriginal(bool on);
    bool compareHoldOriginal() const { return compareHoldOriginal_; }
    // Blank (generated solid-fill) page: its colour IS the page, so the compare
    // views keep the filter/tint on the "original" side — only the lines differ.
    void setBlankPage(bool on);
    bool blankPage() const { return blankPage_; }
    // A compare view (original / split / Alt+Shift+O peek) is read-only: selection/hover
    // highlights and editing gestures are all suppressed while active. The coordinate
    // readout and the hover tooltip stay — they are information, not editing.
    bool compareReadOnly() const { return effectiveCompareMode() != "none"; }
    // Does the image-space point fall in the region showing the EDITED image — the only
    // place the layout is drawn? "none" everywhere, "original" nowhere, a split on the
    // right/bottom of the divider. Gates the hover tooltip: a point the "before" half
    // covers isn't on screen, so there is nothing to label.
    bool compareShowsEdited(double imageX, double imageY) const;

    // Native-resolution render of an export variant (browser exportService.js parity):
    //   "current"  (default) — filter/tint + visible lines/points, per the show flags
    //   "original" — the cropped+rotated original alone: no filter, no annotations
    //   "tint"     — active filter/tint, but no lines/points
    //   "split"    — the split-compare composite, only while a split view is active;
    //               `withDivider` bakes the divider bar in (download) or not (copy).
    QImage renderToImage(const QString& variant, bool withDivider = false) const;
    // Convenience most call sites use: true == "current" (filter + overlay),
    // false == "tint" (filter only, no overlay).
    QImage renderToImage(bool withOverlay) const;
    const QImage& image() const { return image_; }
    QString imageBaseName() const;
    QString imageExt() const;
    // Adopt an in-memory image (clipboard paste / generated): replaces the current image,
    // clears the file path, and resets lines/history/scale. `keepZoom` skips the scale
    // reset — a blank recolor regenerates the SAME dimensions in place, so there is
    // nothing to refit (browser parity: drawingApp.js loadImageFromFile's opts.keepZoom).
    void loadFromImage(const QImage& img, bool keepZoom = false);
    // Adopt an in-memory image with a known geometry (a reopened server project):
    // applies rotation FIRST, then the crop (which lives in rotated-original space),
    // mirroring restore() for the in-memory case. A zero-width crop default-crops.
    void loadFromImage(const QImage& img, const core::CropRect& cropRect, int rotationQuarters);
    // Drop the current image + all lines/history back to the empty "Open an image"
    // canvas (mirrors the browser storage.newTemporary() reset). Used when clearing
    // the current project/editor.
    void clearImage();
    // Hold the empty-canvas invitation OFF while the clear's dust is still falling,
    // or the dashed box pops in underneath the particles (browser parity:
    // .canvas-clearing hides .idle-create). Clicks are ignored while hidden.
    void setIdleHintHidden(bool on);
    // The "＋ Blank image" card's box in GLOBAL coords (empty when it is not showing) —
    // what the blank-image dialog grows out of.
    QRect idleCardGlobalRect() const;
    bool idleHintHidden() const { return idleHintHidden_; }

    // selected-line mutators + delete (port of applySelectionChange
    // ~1674 and canvasDblClick delete ~1515)
    // `preview` = a colour still being chosen in the picker: the line changes at once, but
    // the undo step is debounced (scheduleEditCommit) so a drag doesn't bury the stack.
    void setSelectedLineColor(const QString& color, bool preview = false);
    void setSelectedLineThickness(double thickness);
    void setSelectedLinePointSize(double pointSize);
    // Point colour of the selected line(s); empty clears it back to the line colour.
    void setSelectedLinePointColor(const QString& pointColor, bool preview = false);
    void setSelectedLineStyle(const QString& style);
    void setSelectedLineFill(const QString& fillColor, bool preview = false);
    void deleteSelectedLine();

    // drawing-mode state machine (port of drawingApp.js)
    bool isDrawing() const { return isDrawing_; }

    // hold-to-draw (alternative flow; port of browser holdDraw.js)
    // A near-stationary plain-left press-and-hold auto-enters drawing and drops
    // the first point; dwelling drops more; release commits + exits drawing.
    // The delay (ms) is the hold/dwell threshold, surfaced in Settings.
    void setHoldDrawDelay(int ms);
    int holdDrawDelay() const { return holdDelayMs_; }

    // interactive editing (port of drawingApp.js Alt-drag + Alt/Ctrl wheel) — lives
    // entirely in the mouse/wheel handlers; no extra public API.

   signals:
    void hovered(double imageX, double imageY);  // image-space cursor position
    // Richer hover for the tooltip: image-space pos + global cursor +
    // modifier flags, emitted alongside hovered() on mouse move. `immediate` is true only
    // from refreshHoverForModifiers() — Shift/Ctrl changing what's shown for the SAME
    // hover updates at once (browser: tooltip.js refresh()); an ordinary mouse move
    // (immediate=false, the default) waits out MainWindow's reveal delay instead.
    void hoverDetail(double imageX, double imageY, const QPoint& globalPos,
                     Qt::KeyboardModifiers mods, bool immediate = false);
    void hoverLeft();  // cursor left the canvas -> hide tooltip
    // The pointer really LEFT the canvas widget (leaveEvent) — unlike hoverLeft,
    // which drags/pans also emit mid-canvas just to drop the tooltip. The coord
    // readout clears on this one (browser parity: mouseleave empties #coord-status).
    void canvasLeft();
    // The point/line under the canvas cursor changed (all -1 = nothing hovered).
    // lineIdx -1 with a valid ptIdx = a point of the in-progress line; overLineIdx
    // is the committed line under the cursor (point hit or stroke hit) for the
    // panel's Lines-list row tint.
    void canvasHoverChanged(int lineIdx, int ptIdx, int overLineIdx);
    void changed();                              // lines or history changed
    void selectionChanged();
    // A one-line toast for something the canvas did on its own (the browser's notify()):
    // MainWindow owns the notifier, the canvas only says what happened.
    void statusMessage(const QString& text);
    void contextRequested(const QPoint& globalPos);
    // Left-click on the imageless (idle) canvas: ask the main window to open
    // the blank-image creator (mirrors the browser idle-canvas icon).
    void blankImageRequested();
    void drawingModeChanged(bool drawing);
    void drawModeChanged(DrawMode mode);  // line vs. rect
    // Pan/zoom interactions. Deltas/positions are widget-space px;
    // MainWindow owns the scroll area and translates them.
    void panBy(int dx, int dy, bool fast);          // drag pan
    void fitRequested();                            // double-click fit
    void zoomAtCursor(int dir, const QPoint& posInWidget, bool fast);
    // Trackpad pinch (macOS native gesture): a continuous scale factor about the
    // cursor. MainWindow multiplies the current scale by it, anchored at posInWidget.
    void zoomByFactorAt(double factor, const QPoint& posInWidget);
    void zoomToRect(const QRectF& imageRect);       // image-space rect

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
    bool event(QEvent* event) override;  // intercepts QEvent::NativeGesture (trackpad pinch)
    void leaveEvent(QEvent* event) override;
    // App-wide filter: a modifier key (Shift/Ctrl/Alt) pressed or released while
    // the cursor is over the canvas re-applies the hover state so the tooltip and
    // cursor update immediately, without waiting for a mouse move.
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    // Shared multi-select toggle body for both toggleLineSelection entry points (no-op if idx out of range).
    void toggleLineIndex(int idx);

    // Apply `set` to the selected line (if any), then commit history, repaint,
    // and emit selectionChanged. Backs the setSelectedLine* mutators.
    void mutateSelectedLine(const std::function<void(core::Line&)>& set, bool commit = true);

    // Widget-space bounds of one line (-1 = the in-progress line), padded for its stroke,
    // point rings and the flight flourishes riding outside them — the rect update() needs
    // to repaint just that line. strokeFxRect() unions the ones with a vertex in flight;
    // dragRect() the ones an Alt-drag moves (a multi-selection moves as one).
    QRect lineRect(int lineIdx) const;
    QRect strokeFxRect() const;
    QRect dragRect() const;
    // The empty canvas: page fill + the "＋ Blank image" card (canvas/idleCard.cpp).
    void paintIdleCard(QPainter& p, const Palette& pal);
    // Scale-parameterized so renderToImage can draw the overlay at native resolution
    // (1.0) while the live view uses scale_. `lineIdx` (-1 = in-progress) and `highlight`
    // drive the hover/selection rings — never baked into exports. `live` separates the
    // screen from an export: only the live view flies a freshly-added vertex.
    void drawLineScaled(class QPainter& p, const core::Line& line, int lineIdx,
                        double scale, bool highlight, bool live = true) const;
    // The line's points as they are DRAWN this frame — its own, unless a vertex on it is
    // still in flight. Fills the caller's buffer, which is reused across lines.
    void flownPolygon(const core::Line& line, int lineIdx, double scale, bool live,
                      QPolygonF& poly) const;
    // The heat a flying vertex drags behind it (under the stroke), and the spark riding
    // it + the ring its landing pushes out (over everything).
    void drawStrokeWake(QPainter& p, const core::Line& line, const QPolygonF& poly,
                        int lineIdx, const QColor& stroke) const;
    void drawStrokeSpark(QPainter& p, const core::Line& line, const QPolygonF& poly,
                         int lineIdx, const QColor& pointFill) const;
    // Put the vertex at `ptIdx` of `line` (index `lineIdx`, -1 = in-progress) in the
    // air, and keep the frame timer running while anything is.
    void flyInPoint(int lineIdx, const core::Line& line, int ptIdx,
                    const QPointF* from = nullptr);
    void flyInPoints(int lineIdx, const core::Line& line, int startIdx, int count);
    // Ground every flight. A flight is keyed by LINE INDEX, so anything that renumbers
    // or replaces the lines (undo, a restore, a removal) must drop them or the last one
    // would finish on whatever line inherited its number.
    void resetStrokeFx();
    double fxNow() const;
    // drawLineScaled decomposed into ordered const paint passes; poly/stroke/pal
    // are built once in the head and threaded in by const& (no per-pass recompute).
    void drawFill(QPainter& p, const core::Line& line,
                  const class QPolygonF& poly) const;
    void drawGlow(QPainter& p, const core::Line& line, const QPolygonF& poly,
                  int lineIdx, bool highlight, const Palette& pal) const;
    void drawStroke(QPainter& p, const core::Line& line, const QPolygonF& poly,
                    const class QColor& stroke) const;
    void drawPoints(QPainter& p, const core::Line& line, const QPolygonF& poly,
                     int lineIdx, bool highlight, const QColor& stroke,
                     const Palette& pal, bool live) const;
    // How much bigger than its resting size a vertex is drawn right now (1.0 unless it
    // is in flight or still settling).
    double pointScaleAt(int lineIdx, const core::Line& line, int ptIdx, bool live) const;
    // mousePressEvent dispatch helpers; precedence comes from the call order
    // there. handleCtrlClick returns true when it consumes the click; false
    // falls through to a normal append.
    void beginAltDrag(const core::Point& ip, Qt::KeyboardModifiers mods,
                      const QPoint& globalPos);
    // Alt+Ctrl: pull a NEW point out of the line under the cursor and drag it, breaking a
    // closed area open at that spot (chainEdit.hpp). False when nothing is under it.
    bool beginPullOut(const core::Point& ip);
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

    // Compare view (port of renderer.js drawCompareSplit + pointerController divider drag).
    // Paints the untouched original over the "original" half, plus the movable divider
    // unless `withDivider` is false (the export path's clean split). Scale-parameterized
    // like drawLineScaled; the divider hit-test stays in widget space, i.e. scale_.
    void paintCompareSplit(QPainter& p, const QString& mode, double scale, bool withDivider = true) const;
    bool nearCompareDivider(const QPoint& widgetPos) const;
    // What the compare "original" side shows: the raw pixels for a picture, but a
    // BLANK page as currently coloured (fill + tint) — there is no earlier
    // picture to reveal. Caller rebuilds the filter cache first.
    const QImage& compareBaseImage() const {
      return (blankPage_ && imageFilter_ != QLatin1String("none") && !filteredImage_.isNull())
                 ? filteredImage_
                 : image_;
    }
    QString effectiveCompareMode() const {
      return compareHoldOriginal_ ? QStringLiteral("original") : compareMode_;
    }

    core::Point toImageSpace(int widgetX, int widgetY) const;
    void commitHistory();
    void applyDefaultsToCurrent();
    core::Line* mutablePanelLine();
    void createRect(double x1, double y1, double x2, double y2);
    void rebuildFilteredImage();

    // Point-insertion / continuation drawing (port of drawingApp.js). Insert a
    // point into an existing line between two points; add a point connected to
    // the current selection (or start a new line); close the line being extended.
    void insertPointOnSegment(int lineIdx, int insertIdx, double x, double y);
    void addConnectedPoint(double x, double y);
    void closeContinuedShape();
    // Close the stroke being drawn if `ip` lands on its first point. THE one close route:
    // the click path and hold-to-draw both come here (browser drawingApp.js
    // tryCloseShapeAt). Returns whether it closed.
    bool tryCloseShapeAt(const core::Point& ip);
    // Insert ip into the line being continued at the (clamped) insert cursor and select
    // it; advance the cursor unless prepending. Caller must have validated continueLineIdx_.
    void insertContinuationPoint(const core::Point& ip, bool advance);
    // core::shouldCloseShape's own slack, in image px — closeGrabSize undoes the zoom
    // around it so the grab circle is the same size on screen at any magnification.
    static constexpr double kCloseSlack = 8.0;
    double closeGrabSize(const core::Line& line) const;

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
    void scheduleEditCommit();  // debounced commitHistory for wheel edits
    // Apply an in-place per-line transform `op(points, cx, cy)` to the selection about its pivot
    // (≥2 selected → combined bbox centre; 1 → focused point, else that line's bbox centre), then
    // redraw + selectionChanged + debounced commit. Shared by rotateSelectedLine/flipSelectedLine.
    void transformSelection(const std::function<void(std::vector<core::Point>&, double, double)>& op);

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
    bool isDrawing_ = false;  // gates left-click point adds
    bool idleHintHidden_ = false;
    // The idle "＋ Blank image" card (browser .idle-create-btn): its last painted rect,
    // whether the cursor is inside it, and the 0..1 hover blend the paint interpolates —
    // the browser transitions colour/lift/shadow over 0.2s rather than snapping.
    QRectF idleCardRect_;
    bool idleCardHover_ = false;
    double idleCardHoverT_ = 0.0;
    QVariantAnimation* idleCardAnim_ = nullptr;
    // Hover glass sweep (browser ui-shimmer). -1 = no sweep in flight.
    double idleShimmerT_ = -1.0;
    QVariantAnimation* idleShimmerAnim_ = nullptr;
    // …and the GLYPH's own hover motion. This card strokes its glyph by hand (the app-wide
    // watcher knows only QAbstractButtons, and its header would drag Qt6::Svg into every
    // headless target), so iconMotion.json's `image` entry is evaluated in idleCard.cpp.
    // Elapsed ms into the play, -1 at rest.
    double idleGlyphMs_ = -1.0;
    QVariantAnimation* idleGlyphAnim_ = nullptr;
    // Drive idleCardHoverT_ toward `on`, repainting as it goes.
    void setIdleCardHover(bool on);
    bool dark_ = false;
    QString accentKey_ = "violet";  // brand accent for the rubber-band previews
    // Highlight styles (Settings-backed; DEFAULT_VISUALS' own values until
    // setHighlightColors is called, so an unwired build/test keeps the old look).
    QColor selGlow_{"#ffc800"};
    QColor hoverRing_{"#7c3aed"};
    QColor focusRing_{"#7c3aed"};

    // Selected committed-line index (-1 = none) + draw-mode/rect state.
    // Canonical owner; filters/render/line-edit only consume selectedLineIdx_.
    DrawMode drawMode_ = DrawMode::Line;
    int selectedLineIdx_ = -1;
    // Multi-line selection set (Ctrl+Shift+click to add/toggle). Empty in single-select
    // mode — selectedIndices() then falls back to [selectedLineIdx_]. With 2+ entries
    // selectedLineIdx_ is -1 and move/rotate act on all (browser drawingApp.selectedLines).
    std::vector<int> selectedLines_;
    bool rectDrawActive_ = false;     // drag-to-create rectangle in progress
    QPoint rectDrawStart_, rectDrawEnd_;  // rubber-band corners (widget space)

    // Continuation drawing: when drawing starts with a line selected, new clicks
    // extend that committed line at continueInsertIdx_ instead of building a fresh
    // currentLine_. -1 = not continuing. Port of drawingApp.js #continueLineIdx.
    int continueLineIdx_ = -1;
    int continueInsertIdx_ = -1;

    // Image filter (none | bw | sepia | invert | contour | custom tint)
    // cache. filteredImage_ is rebuilt lazily on paint when filterDirty_ is set.
    QString imageFilter_ = "none";
    QColor filterColor_{"#7c3aed"};
    QImage filteredImage_;
    bool filterDirty_ = true;

    // Compare view (transient; port of browser DrawingApp.compareMode/compareSplit).
    QString compareMode_ = "none";        // none | original | vertical | horizontal
    double compareSplit_ = 0.5;           // divider position (0..1) for the split modes
    bool compareHoldOriginal_ = false;    // Alt+Shift+O momentary "peek original"
    bool draggingCompareSplit_ = false;   // divider drag in progress
    bool blankPage_ = false;              // generated solid-fill page (set by the owner)

    // Pan/zoom-rect drag state.
    bool panning_ = false;        // Alt+left or middle-button drag
    QPoint lastPanPos_;           // last cursor pos during a pan (GLOBAL space)
    bool zoomRectActive_ = false; // Shift+left drag rubber band
    QPoint zoomRectStart_;        // rubber-band anchor (widget space)
    QPoint zoomRectEnd_;          // rubber-band current corner (widget space)

    QString defColor_ = "#FFFF00";
    QString defPointColor_ = "";   // empty = points follow defColor_
    double defThickness_ = 2.0;
    double defPointSize_ = 4.0;
    QString defStyle_ = "solid";

    // Hover highlight: line/point under the cursor (-1 = none; lineIdx -1 with a
    // valid pointIdx means a point of the in-progress line). Port of drawingApp.js
    // `hoverPt` (~1469) driving renderer.js's point hover ring.
    int hoverLineIdx_ = -1;
    int hoverPointIdx_ = -1;
    // Committed line under the cursor (point OR stroke hit) → panel Lines-list tint.
    int hoverOverLineIdx_ = -1;
    // Hover arriving from the panel lists (setListHoverPoint/Line): point ring on the
    // panel line / hover glow on a committed line. Ports of the browser's
    // hoveredPtIdx (coord-table row hover) and listHoverLineIdx.
    int listHoverPointIdx_ = -1;
    int listHoverLineIdx_ = -1;
    // Hit thresholds are constant ON SCREEN: base screen px ÷ zoom (browser parity).
    double hitRadius(double basePx) const { return basePx / (scale_ > 0 ? scale_ : 1.0); }
    // Drop every cached hover index after a structural change (point/line removal,
    // undo/redo, reload) — a stale index would ring a DIFFERENT point until the
    // next mousemove.
    void clearHoverCache();

    // Active Alt-drag gesture. Port of drawingApp.js point/segment/line drags.
    enum class DragKind { None, Point, Segment, Line };
    DragKind dragKind_ = DragKind::None;
    int dragLineIdx_ = -1;   // line being edited (-1 = in-progress line, Point only)
    int dragPtIdx1_ = -1;    // dragged point (Point) / grabbed segment endpoint 1
    int dragPtIdx2_ = -1;    // grabbed segment endpoint 2 (Segment/Line fallback)
    core::Point dragStart_;  // image-space cursor at gesture start
    std::vector<core::Point> dragOrig_;  // snapshot of the line's points at start
    // Whole-set drag snapshot when the grabbed line is part of a multi-selection (empty = single).
    std::vector<std::pair<int, std::vector<core::Point>>> dragMultiOrig_;
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

    // Vertices in flight: every route that adds a point hands it to strokeFx_, fxTimer_
    // repaints while any is moving, and fxClock_ is their shared monotonic ms.
    stroke::Fx strokeFx_;
    QTimer fxTimer_;
    QElapsedTimer fxClock_;
  };

}
