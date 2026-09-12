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

// The drawing surface — browser twin renderer.js (what to draw) + zoomPan.js (scale).
namespace stencil::gui {

  struct Palette;  // theme.hpp; used by the drawLineScaled paint helpers below

  class CanvasWidget : public QWidget {
    Q_OBJECT
   public:
    enum class DrawMode { Line, Rect };

    explicit CanvasWidget(QWidget* parent = nullptr);

    // `decoded` = pixels the caller already read off the GUI thread, so a file is never decoded twice.
    bool loadImage(const QString& path, const QImage& decoded = QImage());
    void restore(const QString& path, const core::Lines& lines, double scale,
                 const core::CropRect& cropRect = {}, int rotationQuarters = 0,
                 const QImage& decoded = QImage());   // …pixels, if the caller has them
    const QString& imagePath() const { return imagePath_; }
    void setImagePath(const QString& path) { imagePath_ = path; }
    bool hasImage() const { return !image_.isNull(); }
    int imageWidth() const { return image_.width(); }
    int imageHeight() const { return image_.height(); }

    // The untouched original; image_ is the cropped region.
    const QImage& originalImage() const { return originalImage_; }
    // The original with the rotation baked in — the pixel space cropRect_ lives in.
    QImage effectiveOriginalImage() const;
    core::CropRect cropRect() const { return cropRect_; }
    // Quarter-turns (0..3, clockwise) applied to the original before the crop.
    int rotationQuarters() const { return rotationQuarters_; }
    void rotateImage(bool clockwise);
    // Natural page size in cm (NOT orientation-swapped); shapes the default centered crop.
    void setPageCm(double widthCm, double heightCm);
    // `recalc`: lines are cleared on an orientation flip, else rescaled.
    void applyCrop(const core::CropRect& rect, bool recalc);

    void setScale(double scale);
    double scale() const { return scale_; }

    // Committed lines only; allLines() also includes the in-progress line (for save).
    const core::Lines& lines() const { return lines_; }
    const core::Line& currentLine() const { return currentLine_; }
    core::Lines allLines() const;
    void setLines(const core::Lines& lines);  // replace all, reset history

    void startNewLine();      // commit the in-progress line, begin a fresh one
    void deleteLastPoint();   // remove the last point of the in-progress line
    void clearAll();          // remove every line
    void unchainSelectedLine();
    void undo();
    void redo();
    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }

    // `pointColor` empty = follow `color`.
    void setDefaults(const QString& color, double thickness, double pointSize,
                     const QString& style, const QString& pointColor = QString());
    void setShowPoints(bool on);
    void setShowLines(bool on);
    void setDark(bool dark);
    void setAccent(const QString& accentKey);
    void setHighlightColors(const QColor& selGlow, const QColor& hoverRing,
                            const QColor& focusRing);

    // Selection panel: the line whose points are shown; focused point -1 = none.
    const core::Line* panelLine() const;
    int panelLineIdx() const;
    // Hover arriving FROM the panel lists; -1 clears.
    void setListHoverPoint(int ptIdx);
    void setListHoverLine(int lineIdx);
    int selectedPoint() const { return selectedPoint_; }
    void selectPoint(int index);
    void deletePoint(int index);
    // axis 0 = x, 1 = y; image px; no clamping (browser drawingApp.js setPointCoord).
    void setPointCoord(int index, int axis, double value);
    void deselect();

    DrawMode drawMode() const { return drawMode_; }
    void setDrawMode(DrawMode mode);
    // Returns the chosen index (-1 = none).
    int selectLineAt(double x, double y);
    int selectedLineIdx() const { return selectedLineIdx_; }
    core::Line* selectedLine();
    const core::Line* selectedLine() const;
    std::vector<int> selectedIndices() const;
    int selectionCount() const { return static_cast<int>(selectedIndices().size()); }
    bool isLineSelected(int i) const;
    void toggleLineSelection(const core::Point& ip);
    void selectLineByIndex(int idx);            // single-select line `idx`
    void toggleLineSelectionByIndex(int idx);   // Ctrl+Shift+click a list row
    void removeLineByIndex(int idx);            // delete line `idx` (list 🗑)
    void rotateSelectedLine(double angleRad);   // Alt+R+←/→ and Ctrl+Shift+wheel
    void flipSelectedLine(bool horizontal);     // Alt+Shift+↑/↓ mirror about the bbox centre
    void nudgeSelected(double dx, double dy);   // arrow-key translate (image-space px)

    // image filters (port of browser/js/core/renderer.js)
    void setFilter(const QString& mode);
    void setFilterColor(const QColor& tint);
    void setImageFilter(const QString& mode, const QColor& tint);
    const QString& imageFilter() const { return imageFilter_; }
    const QColor& filterColor() const { return filterColor_; }

    // Compare view (browser DrawingApp.compareMode): none | original | vertical | horizontal. Transient.
    void setCompareMode(const QString& mode);
    const QString& compareMode() const { return compareMode_; }
    bool isSplitCompare() const { return compareMode_ == "vertical" || compareMode_ == "horizontal"; }
    void setCompareSplit(double fraction);      // divider position 0..1
    double compareSplit() const { return compareSplit_; }
    void setCompareHoldOriginal(bool on);
    bool compareHoldOriginal() const { return compareHoldOriginal_; }
    // A blank page's colour IS the page, so compare keeps the tint on the "original" side.
    void setBlankPage(bool on);
    bool blankPage() const { return blankPage_; }
    // A compare view is read-only: highlights and editing gestures are suppressed.
    bool compareReadOnly() const { return effectiveCompareMode() != "none"; }
    // True where the EDITED image shows — the only place the layout is drawn; gates the hover tip.
    bool compareShowsEdited(double imageX, double imageY) const;

    // Export variants (browser exportService.js): "current" filter + lines, "original" crop/rotation
    // only, "tint" filter only, "split" the compare composite (`withDivider` bakes the bar in).
    QImage renderToImage(const QString& variant, bool withDivider = false) const;
    QImage renderToImage(bool withOverlay) const;
    const QImage& image() const { return image_; }
    QString imageBaseName() const;
    QString imageExt() const;
    // `keepZoom` skips the scale reset (browser loadImageFromFile opts.keepZoom).
    void loadFromImage(const QImage& img, bool keepZoom = false);
    // Rotation is applied FIRST, then the crop (rotated-original space); a zero-width crop default-crops.
    void loadFromImage(const QImage& img, const core::CropRect& cropRect, int rotationQuarters);
    void clearImage();
    // Held OFF while the clear's dust is falling (browser .canvas-clearing hides .idle-create).
    void setIdleHintHidden(bool on);
    // GLOBAL rect of the "＋ Blank image" card; empty when it is not showing.
    QRect idleCardGlobalRect() const;
    bool idleHintHidden() const { return idleHintHidden_; }

    // `preview` = a colour still being picked: the undo step is debounced (scheduleEditCommit).
    void setSelectedLineColor(const QString& color, bool preview = false);
    void setSelectedLineThickness(double thickness);
    void setSelectedLinePointSize(double pointSize);
    void setSelectedLinePointColor(const QString& pointColor, bool preview = false);
    void setSelectedLineStyle(const QString& style);
    void setSelectedLineFill(const QString& fillColor, bool preview = false);
    void deleteSelectedLine();

    bool isDrawing() const { return isDrawing_; }

    // Hold-to-draw (browser holdDraw.js); the delay (ms) is the hold/dwell threshold from Settings.
    void setHoldDrawDelay(int ms);
    int holdDrawDelay() const { return holdDelayMs_; }


   signals:
    void hovered(double imageX, double imageY);  // image-space cursor position
    // `immediate` is true only from refreshHoverForModifiers() — a modifier change updates at
    // once (browser tooltip.js refresh()); a mouse move waits out MainWindow's reveal delay.
    void hoverDetail(double imageX, double imageY, const QPoint& globalPos,
                     Qt::KeyboardModifiers mods, bool immediate = false);
    void hoverLeft();  // cursor left the canvas -> hide tooltip
    // The pointer really LEFT the widget (leaveEvent); hoverLeft also fires mid-canvas on drags.
    void canvasLeft();
    // lineIdx -1 with a valid ptIdx = the in-progress line; overLineIdx = committed line under the cursor.
    void canvasHoverChanged(int lineIdx, int ptIdx, int overLineIdx);
    void changed();                              // lines or history changed
    void selectionChanged();
    void statusMessage(const QString& text);
    void contextRequested(const QPoint& globalPos);
    void blankImageRequested();
    void drawingModeChanged(bool drawing);
    void drawModeChanged(DrawMode mode);  // line vs. rect
    // Widget-space px; MainWindow owns the scroll area.
    void panBy(int dx, int dy, bool fast);          // drag pan
    void fitRequested();                            // double-click fit
    void zoomAtCursor(int dir, const QPoint& posInWidget, bool fast);
    // Trackpad pinch: a continuous factor about the cursor.
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
    // A modifier key changing over the canvas re-applies the hover state without a mouse move.
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void toggleLineIndex(int idx);

    void mutateSelectedLine(const std::function<void(core::Line&)>& set, bool commit = true);

    // Widget-space bounds of one line (-1 = in-progress), padded for stroke, rings and flights.
    // strokeFxRect(): lines with a vertex in flight; dragRect(): the ones an Alt-drag moves.
    QRect lineRect(int lineIdx) const;
    QRect strokeFxRect() const;
    QRect dragRect() const;
    void paintIdleCard(QPainter& p, const Palette& pal);
    // Scale 1.0 for renderToImage, scale_ live. `highlight` rings are never baked into exports;
    // `live`: only the screen flies a freshly-added vertex.
    void drawLineScaled(class QPainter& p, const core::Line& line, int lineIdx,
                        double scale, bool highlight, bool live = true) const;
    // The points as DRAWN this frame (a vertex may be in flight); fills the caller's buffer.
    void flownPolygon(const core::Line& line, int lineIdx, double scale, bool live,
                      QPolygonF& poly) const;
    void drawStrokeWake(QPainter& p, const core::Line& line, const QPolygonF& poly,
                        int lineIdx, const QColor& stroke) const;
    void drawStrokeSpark(QPainter& p, const core::Line& line, const QPolygonF& poly,
                         int lineIdx, const QColor& pointFill) const;
    void flyInPoint(int lineIdx, const core::Line& line, int ptIdx,
                    const QPointF* from = nullptr);
    void flyInPoints(int lineIdx, const core::Line& line, int startIdx, int count);
    // A flight is keyed by LINE INDEX: anything that renumbers the lines (undo, restore, removal)
    // must ground them first, or the last one finishes on whatever line inherited its number.
    void resetStrokeFx();
    double fxNow() const;
    void drawFill(QPainter& p, const core::Line& line,
                  const class QPolygonF& poly) const;
    void drawGlow(QPainter& p, const core::Line& line, const QPolygonF& poly,
                  int lineIdx, bool highlight, const Palette& pal) const;
    void drawStroke(QPainter& p, const core::Line& line, const QPolygonF& poly,
                    const class QColor& stroke) const;
    void drawPoints(QPainter& p, const core::Line& line, const QPolygonF& poly,
                     int lineIdx, bool highlight, const QColor& stroke,
                     const Palette& pal, bool live) const;
    double pointScaleAt(int lineIdx, const core::Line& line, int ptIdx, bool live) const;
    // mousePressEvent dispatch; handleCtrlClick returns true when it consumes the click.
    void beginAltDrag(const core::Point& ip, Qt::KeyboardModifiers mods,
                      const QPoint& globalPos);
    // Alt+Ctrl: pull a NEW point out of the line under the cursor, breaking a closed area (chainEdit.hpp).
    bool beginPullOut(const core::Point& ip);
    void beginZoomRect(const QPoint& widgetPos);
    bool handleCtrlClick(const core::Point& ip);
    void handleDrawingClick(const core::Point& ip, Qt::KeyboardModifiers mods,
                            const QPoint& widgetPos);
    void updateDrag(const core::Point& ip, bool shift);

    void beginHold(const QPoint& widgetPos);
    void stopHold();
    void handleHoldTick();
    void holdStart(double widgetX, double widgetY);
    void holdDrop(double widgetX, double widgetY);
    void holdCommit();
    double holdNowMs() const;
    const core::Point* holdAnchor() const;

    // Compare split (renderer.js drawCompareSplit); the divider hit-test stays in widget space.
    void paintCompareSplit(QPainter& p, const QString& mode, double scale, bool withDivider = true) const;
    bool nearCompareDivider(const QPoint& widgetPos) const;
    // The "original" side: raw pixels, but a BLANK page as currently coloured. Caller rebuilds the filter cache.
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

    void insertPointOnSegment(int lineIdx, int insertIdx, double x, double y);
    void addConnectedPoint(double x, double y);
    void closeContinuedShape();
    // THE one close route — click and hold-to-draw both come here (browser tryCloseShapeAt).
    bool tryCloseShapeAt(const core::Point& ip);
    // Caller must have validated continueLineIdx_.
    void insertContinuationPoint(const core::Point& ip, bool advance);
    // core::shouldCloseShape's slack in image px; closeGrabSize undoes the zoom so it is constant on screen.
    static constexpr double CLOSE_SLACK = 8.0;
    double closeGrabSize(const core::Line& line) const;

    bool updateHover(double imageX, double imageY);
    void applyHoverCursor(const core::Point& ip, Qt::KeyboardModifiers mods);
    void refreshHoverForModifiers();
    void adjustThicknessAtCursor(double imageX, double imageY, int dir);
    void scheduleEditCommit();  // debounced commitHistory for wheel edits
    // Pivot: ≥2 selected → combined bbox centre; 1 → focused point, else that line's bbox centre.
    void transformSelection(const std::function<void(std::vector<core::Point>&, double, double)>& op);

    QImage image_;
    // cropRect_.width == 0 means "no crop yet".
    QImage originalImage_;
    core::CropRect cropRect_;
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
    // Idle card hover blend 0..1 — the browser transitions over 0.2s rather than snapping.
    QRectF idleCardRect_;
    bool idleCardHover_ = false;
    double idleCardHoverT_ = 0.0;
    QVariantAnimation* idleCardAnim_ = nullptr;
    double idleShimmerT_ = -1.0;
    QVariantAnimation* idleShimmerAnim_ = nullptr;
    // The glyph is stroked by hand (the app-wide watcher knows only QAbstractButtons and would drag
    // Qt6::Svg into every headless target): iconMotion.json `image` is evaluated in idleCard.cpp. -1 = rest.
    double idleGlyphMs_ = -1.0;
    QVariantAnimation* idleGlyphAnim_ = nullptr;
    void setIdleCardHover(bool on);
    bool dark_ = false;
    QString accentKey_ = "violet";  // brand accent for the rubber-band previews
    // DEFAULT_VISUALS' own values until setHighlightColors is called.
    QColor selGlow_{"#ffc800"};
    QColor hoverRing_{"#7c3aed"};
    QColor focusRing_{"#7c3aed"};

    // Canonical selection owner; filters/render/line-edit only consume selectedLineIdx_.
    DrawMode drawMode_ = DrawMode::Line;
    int selectedLineIdx_ = -1;
    // Empty in single-select mode; with 2+ entries selectedLineIdx_ is -1 (browser selectedLines).
    std::vector<int> selectedLines_;
    bool rectDrawActive_ = false;     // drag-to-create rectangle in progress
    QPoint rectDrawStart_, rectDrawEnd_;  // rubber-band corners (widget space)

    // Continuation: clicks extend the committed line at continueInsertIdx_; -1 = not continuing.
    int continueLineIdx_ = -1;
    int continueInsertIdx_ = -1;

    // filteredImage_ is rebuilt lazily on paint when filterDirty_ is set.
    QString imageFilter_ = "none";
    QColor filterColor_{"#7c3aed"};
    QImage filteredImage_;
    bool filterDirty_ = true;

    QString compareMode_ = "none";        // none | original | vertical | horizontal
    double compareSplit_ = 0.5;           // divider position (0..1) for the split modes
    bool compareHoldOriginal_ = false;    // Alt+Shift+O momentary "peek original"
    bool draggingCompareSplit_ = false;   // divider drag in progress
    bool blankPage_ = false;              // generated solid-fill page (set by the owner)

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

    // Hover under the cursor (-1 = none; lineIdx -1 with a valid pointIdx = the in-progress line).
    int hoverLineIdx_ = -1;
    int hoverPointIdx_ = -1;
    int hoverOverLineIdx_ = -1;
    // Hover from the panel lists (browser hoveredPtIdx / listHoverLineIdx).
    int listHoverPointIdx_ = -1;
    int listHoverLineIdx_ = -1;
    // Hit thresholds are constant ON SCREEN: base screen px ÷ zoom (browser parity).
    double hitRadius(double basePx) const { return basePx / (scale_ > 0 ? scale_ : 1.0); }
    // Cleared after any structural change — a stale index would ring a DIFFERENT point.
    void clearHoverCache();

    enum class DragKind { None, Point, Segment, Line };
    DragKind dragKind_ = DragKind::None;
    int dragLineIdx_ = -1;   // line being edited (-1 = in-progress line, Point only)
    int dragPtIdx1_ = -1;    // dragged point (Point) / grabbed segment endpoint 1
    int dragPtIdx2_ = -1;    // grabbed segment endpoint 2 (Segment/Line fallback)
    core::Point dragStart_;  // image-space cursor at gesture start
    std::vector<core::Point> dragOrig_;  // snapshot of the line's points at start
    std::vector<std::pair<int, std::vector<core::Point>>> dragMultiOrig_;
    bool dragMoved_ = false;             // any motion happened (gate history)

    // Debounced so a wheel burst is one undo step (browser saveHistory debounce, ~280 ms).
    QTimer editCommitTimer_;

    // hold_ is the pure controller; holdTimer_ ticks it; holdClock_ supplies monotonic ms.
    core::HoldDrawController hold_;
    QTimer holdTimer_;
    QElapsedTimer holdClock_;
    int holdDelayMs_ = 500;
    bool holdHasPreview_ = false;
    // A hold stroke extending BACKWARD from the first point prepends (index 0).
    bool holdPrepend_ = false;
    core::Point holdPreview_;
    QPoint holdPressPos_;

    // Every route that adds a point hands it to strokeFx_; fxTimer_ repaints while any is moving.
    stroke::Fx strokeFx_;
    QTimer fxTimer_;
    QElapsedTimer fxClock_;
  };

}
