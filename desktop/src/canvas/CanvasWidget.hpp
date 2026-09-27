#pragma once
#include "CanvasScene.hpp"
#include "IdleCard.hpp"
#include "gesture.hpp"
#include "holdGlue.hpp"
#include "liveMarks.hpp"
#include "pointerTuning.hpp"
#include "strokeGrowth.hpp"
#include <QElapsedTimer>
#include <QPoint>
#include <QRectF>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <vector>

// The drawing surface — browser twin renderer.js (what to draw) + zoom/pan.js (scale): the
// CanvasScene plus the pointer, the selection, the view and the motion.
namespace stencil::gui {

  namespace chain = core::chain;

  class CanvasWidget : public QWidget, public CanvasScene {
    Q_OBJECT
   public:
    enum class DrawMode { LINE, RECT };

    explicit CanvasWidget(QWidget* parent = nullptr);

    // `decoded` = the pixels of `path`, read off the GUI thread; false (nothing changes) without them.
    bool loadImage(const QString& path, const QImage& decoded);
    void restore(const QString& path, const core::Lines& lines, double scale,
                 const core::CropRect& cropRect, int rotationQuarters, const QImage& decoded);
    void rotateImage(bool clockwise);
    void applyCrop(const core::CropRect& rect, bool recalc);
    void loadFromImage(const QImage& img, bool keepZoom = false);   // keepZoom: no scale reset
    void loadFromImage(const QImage& img, const core::CropRect& cropRect, int rotationQuarters);
    void clearImage();
    void setIdleHintHidden(bool on);   // off while the clear's dust falls (.canvas-clearing)
    QRect idleCardGlobalRect() const;  // GLOBAL rect of the card; empty when it is not showing
    bool getIdleHintHidden() const { return idle.hidden; }

    void setScale(double scale);
    double getScale() const { return scale; }
    // Hit thresholds are constant ON SCREEN: base screen px ÷ zoom (browser parity).
    double hitRadius(double basePx) const { return basePx / (scale > 0 ? scale : 1.0); }

    void setLines(const core::Lines& lines);     // replace all, reset the history
    void commitLines(const core::Lines& lines);  // replace all, push ONE undo step
    bool commitLayout(const core::Lines& lines, const core::CropRect& crop, int quarters);  // ONE step; refits
    bool commitFilter(const QString& mode, const QColor& tint);  // the scene's pick, plus changed()
    void startNewLine();      // commit the in-progress line, begin a fresh one
    void deleteLastPoint();   // remove the last point of the in-progress line
    void clearAll();          // remove every line
    void unchainSelectedLine();
    void undo();
    void redo();

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
    int holdDrawDelay() const { return hold.delayMs; }

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
    void filterRestored(const QString& mode, const QColor& tint);  // an undo or redo put it back
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
    void sceneChanged() override { update(); }
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
    friend class GestureRoutes;   // each gesture's move and release
    void toggleLineIndex(int idx);
    void mutateSelectedLine(const std::function<void(core::Line&)>& set, bool commit = true);
    // The step undo and redo take: the scene restored, the view refitted when it changed.
    void takeHistoryStep(const std::optional<core::EditorMemento>& step);
    // Widget-space bounds of one line (-1 = in-progress), padded for stroke, rings and flights.
    // strokeFxRect(): lines with a vertex in flight; dragRect(): the ones an Alt-drag moves.
    QRect lineRect(int lineIdx) const;
    QRect strokeFxRect() const;
    QRect dragRect() const;
    LiveMarks liveMarks() const;
    void flyInPoint(int lineIdx, const core::Line& line, int ptIdx, const QPointF* from = nullptr);
    void flyInPoints(int lineIdx, const core::Line& line, int startIdx, int count);
    // A flight is keyed by LINE INDEX: anything that renumbers the lines (undo, restore, removal)
    // must ground them first, or the last one finishes on whatever line inherited its number.
    void resetStrokeFx();
    double fxNow() const;
    // mousePressEvent dispatch; handleCtrlClick returns true when it consumes the click.
    void beginAltDrag(const core::Point& ip, Qt::KeyboardModifiers mods, const QPoint& globalPos);
    // Alt+Ctrl: pull a NEW point out of the line under the cursor, breaking a closed area (core lineChain).
    bool beginPullOut(const core::Point& ip);
    void beginZoomRect(const QPoint& widgetPos);
    bool handleCtrlClick(const core::Point& ip);
    void handleDrawingClick(const core::Point& ip, Qt::KeyboardModifiers mods, const QPoint& widgetPos);
    void beginHold(const QPoint& widgetPos);
    void stopHold();
    void handleHoldTick();
    void holdStart(double widgetX, double widgetY);
    void holdDrop(double widgetX, double widgetY);
    void holdCommit();
    double holdNowMs() const;
    const core::Point* holdAnchor() const;
    bool nearCompareDivider(const QPoint& widgetPos) const;
    core::Point toImageSpace(int widgetX, int widgetY) const;
    void commitHistory();
    core::Line* mutablePanelLine();
    void createRect(double x1, double y1, double x2, double y2);
    void insertPointOnSegment(int lineIdx, int insertIdx, double x, double y);
    void addConnectedPoint(double x, double y);
    void closeContinuedShape();
    // THE one close route — click and hold-to-draw both come here (browser tryCloseShapeAt).
    bool tryCloseShapeAt(const core::Point& ip);
    // Caller must have validated continueLineIdx.
    void insertContinuationPoint(const core::Point& ip, bool advance);
    // closeGrabSize undoes the zoom on core::shouldCloseShape's slack so it is constant on screen.
    double closeGrabSize(const core::Line& line) const;
    bool updateHover(double imageX, double imageY);
    void applyHoverCursor(const core::Point& ip, Qt::KeyboardModifiers mods);
    void refreshHoverForModifiers();
    void adjustThicknessAtCursor(double imageX, double imageY, int dir);
    void scheduleEditCommit();  // debounced commitHistory for wheel edits
    // Pivot: ≥2 selected → combined bbox centre; 1 → focused point, else that line's bbox centre.
    void transformSelection(const std::function<void(std::vector<core::Point>&, double, double)>& op);
    double lineHitRadius() const { return hitRadius(pointerTuning::table().lineRadiusPx); }
    double grabHitRadius() const { return hitRadius(pointerTuning::table().grabRadiusPx); }
    // Cleared after any structural change — a stale index would ring a DIFFERENT point.
    void clearHoverCache();

    double scale = 1.0;
    bool isDrawing = false;  // gates left-click point adds
    DrawMode drawMode = DrawMode::LINE;
    // Canonical selection owner: with 2+ entries in the ascending selectedLines, selectedLineIdx is -1.
    int selectedLineIdx = -1;
    std::vector<int> selectedLines;
    int selectedPoint = -1;
    // Continuation: clicks extend the committed line at continueInsertIdx; -1 = not continuing.
    int continueLineIdx = -1;
    int continueInsertIdx = -1;
    HoverMarks hover;
    CanvasGesture gesture;
    HoldGlue hold;
    IdleCard idle{this};
    // Debounced so a wheel burst is one undo step (browser saveHistory debounce, DEBOUNCE.editCommitMs).
    QTimer editCommitTimer;
    // Every route that adds a point hands it to strokeFx; fxTimer repaints while any is moving.
    stroke::Fx strokeFx;
    QTimer fxTimer;
    QElapsedTimer fxClock;
  };

}
