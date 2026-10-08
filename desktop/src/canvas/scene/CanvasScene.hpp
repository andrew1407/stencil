#pragma once
#include "accentDefaults.hpp"
#include "defaultVisuals.hpp"
#include "canvasCore.hpp"
#include "premulImage.hpp"

#include <QColor>
#include <QImage>
#include <QString>
#include <memory>

class QPainter;
class QPolygonF;

// The picture and its layout without a widget: the original, the crop and turn, the lines and
// their undo steps, the look, the filter and compare state, and the paint path that renders them —
// browser twin renderer.js. CanvasWidget is this plus the pointer, the view and the motion; an
// offscreen render (a thumbnail, a plan's sandbox) uses it alone.
namespace stencil::gui {

  struct LiveMarks;
  struct Palette;

  class CanvasScene {
   public:
    enum class CompareMode { NONE, ORIGINAL, VERTICAL, HORIZONTAL };

    CanvasScene();
    virtual ~CanvasScene();

    const QString& getImagePath() const { return imagePath; }
    void setImagePath(const QString& path) { imagePath = path; }
    bool hasImage() const { return !image.isNull(); }
    int imageWidth() const { return image.width(); }
    int imageHeight() const { return image.height(); }

    // The untouched original; image is the cropped region.
    const QImage& getOriginalImage() const { return originalImage; }
    // The original with the mirror and rotation baked in — the pixel space cropRect lives in.
    QImage effectiveOriginalImage() const;
    core::CropRect getCropRect() const { return cropRect; }
    // Quarter-turns (0..3, clockwise) applied to the original before the crop.
    int getRotationQuarters() const { return rotationQuarters; }
    // The original mirrored left-right BEFORE the turn: the shown picture is turn(mirror(original)).
    bool getMirrored() const { return mirrored; }
    // Natural page size in cm (NOT orientation-swapped); shapes the default centered crop.
    void setPageCm(double widthCm, double heightCm);
    // Bumped by every picture replacement and by a load that begins decoding off-thread: a decode
    // that lands under an older value lost to a newer load.
    quint64 pictureGeneration() const { return pictureGen; }
    quint64 beginPictureLoad() { return ++pictureGen; }

    // Committed lines only; allLines() also includes the in-progress line (for save).
    const core::Lines& getLines() const { return lines; }
    const core::Line& getCurrentLine() const { return currentLine; }
    core::Lines allLines() const;
    bool canUndo() const { return history.canUndo(); }
    bool canRedo() const { return history.canRedo(); }
    // The step on screen: the lines, the crop and turn they sit on, and the filter over them.
    core::EditorMemento memento() const;

    // The document's own edits; CanvasWidget's same-named ones add the view and the signals.
    // Rotation is applied FIRST, then the crop (rotated-original space); a zero-width crop default-crops.
    void loadFromImage(const QImage& img, const core::CropRect& cropRect, int rotationQuarters,
                       bool mirrored = false);
    // Pixels are adopted only under a path and never decoded here: `decoded` is read off the GUI thread.
    void restore(const QString& path, const core::Lines& lines, const core::CropRect& cropRect,
                 int rotationQuarters, const QImage& decoded, bool mirrored = false);
    void rotateImage(bool clockwise);
    // A left-right flip of the shown picture; the crop window and the lines follow — one undo step.
    void flipImage();
    // `recalc`: lines are cleared on an orientation flip, else rescaled.
    void applyCrop(const core::CropRect& rect, bool recalc);
    void setLines(const core::Lines& lines);     // replace all, reset the history
    void commitLines(const core::Lines& lines);  // replace all, push ONE undo step
    // Lines, crop and turn as ONE undo step, the picture rebuilt from the original as a restored
    // step's is; true when the crop or turn moved.
    // `mirrored` -1 keeps the mirror on screen, else 0/1 sets it.
    bool commitLayout(const core::Lines& lines, const core::CropRect& crop, int quarters, int mirrored = -1);
    bool undo();
    bool redo();

    // `pointColor` empty = follow `color`.
    void setDefaults(const QString& color, double thickness, double pointSize,
                     const QString& style, const QString& pointColor = QString());
    void setShowPoints(bool on);
    void setShowLines(bool on);
    void setDark(bool dark);
    void setAccent(const QString& accentKey);
    void setHighlightColors(const QColor& selGlow, const QColor& hoverRing,
                            const QColor& focusRing);

    // image filters (port of browser/js/core/draw/renderer.js)
    void setFilter(const QString& mode);
    void setFilterColor(const QColor& tint);
    void setImageFilter(const QString& mode, const QColor& tint);
    // A committed pick: ONE undo step when mode or tint differ from the step on screen's; none
    // without a picture.
    bool commitFilter(const QString& mode, const QColor& tint);
    const QString& getImageFilter() const { return imageFilter; }
    const QColor& getFilterColor() const { return filterColor; }

    // Compare view (browser DrawingApp.compareMode): none | original | vertical | horizontal. Transient.
    void setCompareMode(const QString& mode);
    QString getCompareMode() const;
    bool isSplitCompare() const { return compareMode == CompareMode::VERTICAL || compareMode == CompareMode::HORIZONTAL; }
    void setCompareSplit(double fraction);      // divider position 0..1
    double getCompareSplit() const { return compareSplit; }
    void setCompareHoldOriginal(bool on);
    bool getCompareHoldOriginal() const { return compareHoldOriginal; }
    // A blank page's colour IS the page, so compare keeps the tint on the "original" side.
    void setBlankPage(bool on);
    bool getBlankPage() const { return blankPage; }
    // A compare view is read-only: highlights and editing gestures are suppressed.
    bool compareReadOnly() const { return effectiveCompareMode() != CompareMode::NONE; }
    // True where the EDITED image shows — the only place the layout is drawn; gates the hover tip.
    bool compareShowsEdited(double imageX, double imageY) const;

    // Export variants (browser export/service.js): "current" filter + lines, "original" crop and
    // rotation only, "tint" filter only, "split" the composite (`withDivider` bakes the bar in).
    QImage renderToImage(const QString& variant, bool withDivider = false) const;
    QImage renderToImage(bool withOverlay) const;
    // What renderToImage reads, copied to render on another thread: the picture and its filter, the
    // lines and their look, and the palette resolved on this one. QImage copies share their pixels.
    std::shared_ptr<CanvasScene> renderCopy() const;
    const QImage& getImage() const { return image; }
    QString imageBaseName() const;
    QString imageExt() const;

   protected:
    // Something the view shows changed; a widget repaints.
    virtual void sceneChanged() {}

    core::CropRect defaultCropRect() const;  // centered crop for the rotated original
    void rebuildCroppedFromOriginal();       // image <- rotated original ∩ cropRect
    void rebuildFilteredImage();
    void applyDefaultsToCurrent();
    // Every undo step is taken here, so a filter pick is measured against the step on screen.
    void pushStep();
    void resetSteps();
    // True when the step's crop or turn differed from the one on screen and was rebuilt.
    bool restoreMemento(const core::EditorMemento& m);

    // Scale 1.0 for renderToImage, scale live. `highlight` rings are never baked into exports and
    // need `live`, the live view's marks; without them nothing is in flight.
    void drawLineScaled(QPainter& p, const core::Line& line, int lineIdx, double scale,
                        bool highlight, const LiveMarks* live) const;
    // Compare split (renderer.js drawCompareSplit); the divider hit-test stays in widget space.
    void paintCompareSplit(QPainter& p, CompareMode mode, double scale, bool withDivider = true) const;
    // The "original" side: raw pixels, but a BLANK page as currently coloured. Caller rebuilds the filter cache.
    const QImage& compareBaseImage() const;
    CompareMode effectiveCompareMode() const { return compareHoldOriginal ? CompareMode::ORIGINAL : compareMode; }

    QImage image;
    // cropRect.width == 0 means "no crop yet".
    QImage originalImage;
    core::CropRect cropRect;
    int rotationQuarters = 0;
    bool mirrored = false;
    double pageWidthCm = 29.7;
    double pageHeightCm = 42.0;
    QString imagePath;
    core::Lines lines;
    core::Line currentLine;
    core::EditorHistory history;

    bool showPoints = true;
    bool showLines = true;
    bool dark = false;
    QString accentKey = DEFAULT_ACCENT_KEY;  // brand accent for the rubber-band previews
    // DEFAULT_VISUALS' own values until setHighlightColors / setDefaults are called.
    QColor selGlow{defaultVisuals::table().selGlow};
    QColor hoverRing{defaultVisuals::table().hoverRing}, focusRing{defaultVisuals::table().focusRing};
    QString defColor = defaultVisuals::table().color;
    QString defPointColor = "";   // empty = points follow defColor
    double defThickness = defaultVisuals::table().thickness;
    double defPointSize = defaultVisuals::table().pointSize;
    QString defStyle = defaultVisuals::table().style;

    // filteredImage is rebuilt lazily on paint when filterDirty is set.
    QString imageFilter = "none";
    core::FilterMode filterMode = core::FilterMode::NONE;   // imageFilter, parsed once when it is set
    QColor filterColor{DEFAULT_ACCENT_HEX};
    QString stepFilter = "none";   // the filter and tint of the undo step on screen
    QColor stepTint{DEFAULT_ACCENT_HEX};
    QImage filteredImage;
    bool filterDirty = true;
    mutable PremulImage shownPremul, basePremul;   // what paintEvent blits, per source picture

    CompareMode compareMode = CompareMode::NONE;
    double compareSplit = 0.5;           // divider position (0..1) for the split modes
    bool compareHoldOriginal = false;    // Alt+Shift+O momentary "peek original"
    bool blankPage = false;              // generated solid-fill page (set by the owner)
    quint64 pictureGen = 0;
    // A render copy's palette: paintPalette's cache belongs to the GUI thread.
    std::shared_ptr<const Palette> pinnedPalette;

   private:
    // The points as DRAWN this frame (a vertex may be in flight); fills the caller's buffer.
    void flownPolygon(const core::Line& line, int lineIdx, double scale, const LiveMarks* live,
                      QPolygonF& poly) const;
    double pointScaleAt(int lineIdx, const core::Line& line, int ptIdx, const LiveMarks* live) const;
    void drawStrokeWake(QPainter& p, const core::Line& line, const QPolygonF& poly, int lineIdx,
                        const QColor& stroke, const LiveMarks& live) const;
    void drawStrokeSpark(QPainter& p, const core::Line& line, const QPolygonF& poly, int lineIdx,
                         const QColor& pointFill, const LiveMarks& live) const;
    void drawFill(QPainter& p, const core::Line& line, const QPolygonF& poly) const;
    void drawGlow(QPainter& p, const core::Line& line, const QPolygonF& poly, int lineIdx,
                  bool highlight, const Palette& pal, const LiveMarks* live) const;
    void drawStroke(QPainter& p, const core::Line& line, const QPolygonF& poly,
                    const QColor& stroke) const;
    void drawPoints(QPainter& p, const core::Line& line, const QPolygonF& poly, int lineIdx,
                    bool highlight, const QColor& stroke, const Palette& pal, const LiveMarks* live) const;
  };

}  // namespace stencil::gui
