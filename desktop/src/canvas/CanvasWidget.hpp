#pragma once
#include "accentDefaults.hpp"
#include "cropGeometry.hpp"
#include "idleCardMotion.hpp"   // the idle card's glyph motion (iconMotion.json "image")
#include "HistoryStack.hpp"
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
    enum class DrawMode { LINE, RECT };

    explicit CanvasWidget(QWidget* parent = nullptr);

    // `decoded` = pixels the caller already read off the GUI thread, so a file is never decoded twice.
    bool loadImage(const QString& path, const QImage& decoded = QImage());
    void restore(const QString& path, const core::Lines& lines, double scale,
                 const core::CropRect& cropRect = {}, int rotationQuarters = 0,
                 const QImage& decoded = QImage());   // …pixels, if the caller has them
    const QString& getImagePath() const { return imagePath; }
    void setImagePath(const QString& path) { imagePath = path; }
    bool hasImage() const { return !image.isNull(); }
    int imageWidth() const { return image.width(); }
    int imageHeight() const { return image.height(); }

    // The untouched original; image is the cropped region.
    const QImage& getOriginalImage() const { return originalImage; }
    // The original with the rotation baked in — the pixel space cropRect lives in.
    QImage effectiveOriginalImage() const;
    core::CropRect getCropRect() const { return cropRect; }
    // Quarter-turns (0..3, clockwise) applied to the original before the crop.
    int getRotationQuarters() const { return rotationQuarters; }
    void rotateImage(bool clockwise);
    // Natural page size in cm (NOT orientation-swapped); shapes the default centered crop.
    void setPageCm(double widthCm, double heightCm);
    // `recalc`: lines are cleared on an orientation flip, else rescaled.
    void applyCrop(const core::CropRect& rect, bool recalc);

    void setScale(double scale);
    double getScale() const { return scale; }

    // Committed lines only; allLines() also includes the in-progress line (for save).
    const core::Lines& getLines() const { return lines; }
    const core::Line& getCurrentLine() const { return currentLine; }
    core::Lines allLines() const;
    void setLines(const core::Lines& lines);     // replace all, reset the history
    void commitLines(const core::Lines& lines);  // replace all, push ONE undo step

    void startNewLine();      // commit the in-progress line, begin a fresh one
    void deleteLastPoint();   // remove the last point of the in-progress line
    void clearAll();          // remove every line
    void unchainSelectedLine();
    void undo();
    void redo();
    bool canUndo() const { return history.canUndo(); }
    bool canRedo() const { return history.canRedo(); }

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
    int getSelectedPoint() const { return selectedPoint; }
    void selectPoint(int index);
    void deletePoint(int index);
    // axis 0 = x, 1 = y; image px; no clamping (browser drawingApp.js setPointCoord).
    void setPointCoord(int index, int axis, double value);
    void deselect();

    DrawMode getDrawMode() const { return drawMode; }
    void setDrawMode(DrawMode mode);
    // Returns the chosen index (-1 = none).
    int selectLineAt(double x, double y);
    int getSelectedLineIdx() const { return selectedLineIdx; }
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

    // image filters (port of browser/js/core/draw/renderer.js)
    void setFilter(const QString& mode);
    void setFilterColor(const QColor& tint);
    void setImageFilter(const QString& mode, const QColor& tint);
    const QString& getImageFilter() const { return imageFilter; }
    const QColor& getFilterColor() const { return filterColor; }

    // Compare view (browser DrawingApp.compareMode): none | original | vertical | horizontal. Transient.
    void setCompareMode(const QString& mode);
    const QString& getCompareMode() const { return compareMode; }
    bool isSplitCompare() const { return compareMode == "vertical" || compareMode == "horizontal"; }
    void setCompareSplit(double fraction);      // divider position 0..1
    double getCompareSplit() const { return compareSplit; }
    void setCompareHoldOriginal(bool on);
    bool getCompareHoldOriginal() const { return compareHoldOriginal; }
    // A blank page's colour IS the page, so compare keeps the tint on the "original" side.
    void setBlankPage(bool on);
    bool getBlankPage() const { return blankPage; }
    // A compare view is read-only: highlights and editing gestures are suppressed.
    bool compareReadOnly() const { return effectiveCompareMode() != "none"; }
    // True where the EDITED image shows — the only place the layout is drawn; gates the hover tip.
    bool compareShowsEdited(double imageX, double imageY) const;

    // Export variants (browser exportService.js): "current" filter + lines, "original" crop/rotation
    // only, "tint" filter only, "split" the compare composite (`withDivider` bakes the bar in).
    QImage renderToImage(const QString& variant, bool withDivider = false) const;
    QImage renderToImage(bool withOverlay) const;
    const QImage& getImage() const { return image; }
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
    bool getIdleHintHidden() const { return idleHintHidden; }

    // `preview` = a colour still being picked: the undo step is debounced (scheduleEditCommit).
    void setSelectedLineColor(const QString& color, bool preview = false);
    void setSelectedLineThickness(double thickness);
    void setSelectedLinePointSize(double pointSize);
    void setSelectedLinePointColor(const QString& pointColor, bool preview = false);
    void setSelectedLineStyle(const QString& style);
    void setSelectedLineFill(const QString& fillColor, bool preview = false);
    void deleteSelectedLine();

    bool getIsDrawing() const { return isDrawing; }

    // Hold-to-draw (browser holdDraw.js); the delay (ms) is the hold/dwell threshold from Settings.
    void setHoldDrawDelay(int ms);
    int holdDrawDelay() const { return holdDelayMs; }


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
    // Scale 1.0 for renderToImage, scale live. `highlight` rings are never baked into exports;
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
    void drawFill(QPainter& p, const core::Line& line, const class QPolygonF& poly) const;
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
      return (blankPage && imageFilter != QLatin1String("none") && !filteredImage.isNull())
                 ? filteredImage
                 : image;
    }
    QString effectiveCompareMode() const {
      return compareHoldOriginal ? QStringLiteral("original") : compareMode;
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
    // Caller must have validated continueLineIdx.
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

    QImage image;
    // cropRect.width == 0 means "no crop yet".
    QImage originalImage;
    core::CropRect cropRect;
    int rotationQuarters = 0;
    double pageWidthCm = 29.7;
    double pageHeightCm = 42.0;
    core::CropRect defaultCropRect() const;  // centered crop for the rotated original
    void rebuildCroppedFromOriginal();       // image <- rotated original ∩ cropRect
    QString imagePath;
    core::Lines lines;
    core::Line currentLine;
    core::HistoryStack history;
    double scale = 1.0;
    int selectedPoint = -1;
    bool showPoints = true;
    bool showLines = true;
    bool isDrawing = false;  // gates left-click point adds
    bool idleHintHidden = false;
    // Idle card hover blend 0..1 — the browser transitions over 0.2s rather than snapping.
    QRectF idleCardRect;
    bool idleCardHover = false;
    double idleCardHoverT = 0.0;
    QVariantAnimation* idleCardAnim = nullptr;
    double idleShimmerT = -1.0;
    QVariantAnimation* idleShimmerAnim = nullptr;
    // The glyph is stroked by hand (the app-wide watcher knows only QAbstractButtons and would drag
    // Qt6::Svg into every headless target): iconMotion.json `image` is evaluated in IdleCard.cpp. -1 = rest.
    double idleGlyphMs = -1.0;
    QVariantAnimation* idleGlyphAnim = nullptr;
    void setIdleCardHover(bool on);
    bool dark = false;
    QString accentKey = DEFAULT_ACCENT_KEY;  // brand accent for the rubber-band previews
    // DEFAULT_VISUALS' own values until setHighlightColors is called.
    QColor selGlow{"#ffc800"};
    QColor hoverRing{DEFAULT_ACCENT_HEX}, focusRing{DEFAULT_ACCENT_HEX};

    // Canonical selection owner; filters/render/line-edit only consume selectedLineIdx.
    DrawMode drawMode = DrawMode::LINE;
    int selectedLineIdx = -1;
    // Empty in single-select mode; with 2+ entries selectedLineIdx is -1 (browser selectedLines).
    std::vector<int> selectedLines;
    bool rectDrawActive = false;     // drag-to-create rectangle in progress
    QPoint rectDrawStart, rectDrawEnd;  // rubber-band corners (widget space)

    // Continuation: clicks extend the committed line at continueInsertIdx; -1 = not continuing.
    int continueLineIdx = -1;
    int continueInsertIdx = -1;

    // filteredImage is rebuilt lazily on paint when filterDirty is set.
    QString imageFilter = "none";
    QColor filterColor{DEFAULT_ACCENT_HEX};
    QImage filteredImage;
    bool filterDirty = true;

    QString compareMode = "none";        // none | original | vertical | horizontal
    double compareSplit = 0.5;           // divider position (0..1) for the split modes
    bool compareHoldOriginal = false;    // Alt+Shift+O momentary "peek original"
    bool draggingCompareSplit = false;   // divider drag in progress
    bool blankPage = false;              // generated solid-fill page (set by the owner)

    bool panning = false;        // Alt+left or middle-button drag
    QPoint lastPanPos;           // last cursor pos during a pan (GLOBAL space)
    bool zoomRectActive = false; // Shift+left drag rubber band
    QPoint zoomRectStart;        // rubber-band anchor (widget space)
    QPoint zoomRectEnd;          // rubber-band current corner (widget space)

    QString defColor = "#FFFF00";
    QString defPointColor = "";   // empty = points follow defColor
    double defThickness = 2.0;
    double defPointSize = 4.0;
    QString defStyle = "solid";

    // Hover under the cursor (-1 = none; lineIdx -1 with a valid pointIdx = the in-progress line).
    int hoverLineIdx = -1;
    int hoverPointIdx = -1;
    int hoverOverLineIdx = -1;
    // Hover from the panel lists (browser hoveredPtIdx / listHoverLineIdx).
    int listHoverPointIdx = -1;
    int listHoverLineIdx = -1;
    // Hit thresholds are constant ON SCREEN: base screen px ÷ zoom (browser parity).
    double hitRadius(double basePx) const { return basePx / (scale > 0 ? scale : 1.0); }
    // Cleared after any structural change — a stale index would ring a DIFFERENT point.
    void clearHoverCache();

    enum class DragKind { NONE, POINT, SEGMENT, LINE };
    DragKind dragKind = DragKind::NONE;
    int dragLineIdx = -1;   // line being edited (-1 = in-progress line, Point only)
    int dragPtIdx1 = -1;    // dragged point (Point) / grabbed segment endpoint 1
    int dragPtIdx2 = -1;    // grabbed segment endpoint 2 (Segment/Line fallback)
    core::Point dragStart;  // image-space cursor at gesture start
    std::vector<core::Point> dragOrig;  // snapshot of the line's points at start
    std::vector<std::pair<int, std::vector<core::Point>>> dragMultiOrig;
    bool dragMoved = false;             // any motion happened (gate history)

    // Debounced so a wheel burst is one undo step (browser saveHistory debounce, ~280 ms).
    QTimer editCommitTimer;

    // hold is the pure controller; holdTimer ticks it; holdClock supplies monotonic ms.
    core::HoldDrawController hold;
    QTimer holdTimer;
    QElapsedTimer holdClock;
    int holdDelayMs = 500;
    bool holdHasPreview = false;
    // A hold stroke extending BACKWARD from the first point prepends (index 0).
    bool holdPrepend = false;
    core::Point holdPreview;
    QPoint holdPressPos;

    // Every route that adds a point hands it to strokeFx; fxTimer repaints while any is moving.
    stroke::Fx strokeFx;
    QTimer fxTimer;
    QElapsedTimer fxClock;
  };

}
