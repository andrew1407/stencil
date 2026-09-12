#pragma once
// Disintegration ("the snap") — the desktop port of disintegrate() in
// browser/js/ui/motion.js.
//
// The browser paints one round mote per grid cell in the element's own colours and lets
// CSS fly them. Qt does the same off a photograph: the widget is grabbed ONCE, its colour
// sampled per cell (sampleCells), and every frame draws the snapshot whole, cuts out the
// cells that have left it, and flies those as round grains — offset, bent, shrunk and
// faded by their own progress. One clock drives the lot, so it stays a single repaint per
// frame however many cells there are; grains are blitted from a sprite cache (dustKit.hpp
// MoteSprites) and the clock ticks at the screen's own refresh rate.
//
// Header-only and Q_OBJECT-free (no signals/slots), so it needs no MOC.
#include <QColor>
#include <QEasingCurve>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QPointF>
#include <QPointer>
#include <QPaintEvent>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QRectF>
#include <QRegion>
#include <QScreen>
#include <QSize>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

#include "dustKit.hpp"       // support::EaseLut / MoteSprites / frameIntervalMs
#include "motionPrefs.hpp"   // support::dustAllowed()

#include <algorithm>
#include <cmath>
#include <vector>

namespace stencil::gui {

  // The floating-tip clock family (browser controlTooltip.js / exportPreview.js):
  // tooltips, the export preview and popup menus gather/leave on this shared clock.
  inline constexpr int kTipDustInMs = 213;
  inline constexpr int kTipDustOutMs = 157;
  // Where the browser's surfaceForm keyframes hold a forming surface invisible while
  // its motes gather, and surfaceLeave's one-beat hand-over on the way out.
  inline constexpr double kDustHold = 0.55;
  inline constexpr int kDustHandOverMs = 60;

  QPoint dockAwayPoint(const QRect& picture, Qt::DockWidgetArea area,
                       double reach = 1.2);

  void holdFadeKeys(QVariantAnimation* fade, int ms);

  void fadeUpBehindDust(QWidget* w, int ms);

  class DisintegrateOverlay : public QWidget {
   public:
    // The desktop runs every dust clock 1.5x faster than the browser's twin of it
    // (DISINTEGRATE_MS 1650, user decision 2026-09-08): its grains are the window's own
    // pixels, and the browser's span read as slow here.
    static constexpr int kMs = 1100;
    // The clock a list passes for its own items — the row clock today, named apart because
    // the two have been parted before (browser twin: ITEM_DUST_MS).
    static constexpr int kItemMs = kMs;
    // …and the CONNECTIONS list, the odd one out: a row there is a URL you already know,
    // so it comes and goes half again as briskly (browser twin: CONN_DUST_MS).
    static constexpr int kConnMs = kMs * 2 / 3;
    // A fine grid: at 8x4 the cells read as big rectangles sliding apart, not as ash.
    // Cheap here — the snapshot is redrawn per cell, nothing is cloned.
    static constexpr int kCols = 22;     // browser DISINTEGRATE_COLS
    static constexpr int kRows = 11;     // browser DISINTEGRATE_ROWS
    // A falling IMAGE is sized on screen instead: a fixed grid over a big canvas gives
    // big rectangles, not dust (browser DUST_CELL_PX). Cells are capped in count so a
    // huge canvas can't cost more per frame than the effect is worth.
    static constexpr int kDustCellPx = 7;   // browser motion.js MOTE_PX — keep the two in step
    static constexpr int kDustMaxCells = 7000;
    static constexpr const char* kObjectName = "stencilDisintegrate";
    // A whole SURFACE is dust too (browser motion.js surfaceIn / surfaceOut)
    // A dialog, a popup menu and the tooltip form from motes streaming out of the control
    // that opened them and come apart into motes pouring back in. Same snapshot, same
    // hashes; only the flight differs — every mote aims at ONE point instead of falling.
    static constexpr int kSurfaceInMs = 507;    // browser SURFACE_IN_MS 760 / 1.5
    static constexpr int kSurfaceOutMs = 313;   // browser SURFACE_OUT_MS 470 / 1.5
    static constexpr int kSurfaceCellPx = 6;    // browser SURFACE_MOTE_PX
    // A window's whole cloud is redrawn in ONE paintEvent every frame, so the cost
    // scales with cell count — a few thousand cells per frame is what read as lag on
    // a big surface (a tall dialog, a full-height dock). browser SURFACE_COLS*ROWS.
    // overSurface() takes an override for a caller that wants a bigger budget.
    static constexpr int kSurfaceMaxCells = 1380;
    static constexpr double kSurfaceSpreadPx = 34;   // browser SURFACE_SPREAD
    // How far a surface's motes are lifted towards the window's own INK before they fly
    // (browser motion.js MOTE_INK). Without it the cloud is invisible: a window and the
    // one behind it are the same family of colour, so a dark dialog came apart into dark
    // motes over a dark page and the flight simply could not be seen. Mixing in the ink
    // keeps every mote the window's own colour and gives it something to read against,
    // and it flips with the theme for free — ink always contrasts with its background.
    static constexpr double kSurfaceInkMix = 0.42;
    // …and the RIM and the GLINTS further still (browser MOTE_RIM_INK 66%): the cells on
    // the picture's edge, and one inner cell in seven, are brighter grains — the contrast
    // that makes a cloud read as sand with sparkle in it rather than a haze. Applied on
    // top of the 42% lift, so this is the share of the REMAINING way to the ink.
    static constexpr double kGlintMix = (0.66 - 0.42) / (1.0 - 0.42);
    static constexpr double kGlintHash = 0.86;
    // The grain a mote is drawn at: the smaller of its cell and this, times 0.62..1.12
    // by its own hash (browser motion.js SURFACE_SPECK_PX / speckPainter).
    static constexpr int kSpeckPx = 7;
    // The bend (browser motion.js tileWaypoint)
    // No mote flies a straight line: each is pushed off its throw's own line, peaking
    // mid-flight, by a share of the throw (capped) to the side its third hash picks —
    // so a cloud churns instead of radiating in spokes.
    static constexpr double kSwirlShare = 0.32;
    static constexpr double kSwirlMaxPx = 44;
    // Where along the throw the browser's flights put that bend (WAYPOINT_ALONG): a
    // surface's and a row's motes fly TWO legs — home to the waypoint on one curve, the
    // waypoint to the far end on another — the mid keyframe of tileScatter and kin. The
    // turn is a flick, not the smooth arc a single sine bulge draws.
    static constexpr double kWaypointAlong = 0.62;
    // However late a mote sets off it still gets this long to fly (browser MIN_TILE_MS):
    // the floor keeps the last grains of a short flight from being a blink.
    static constexpr int kMinTileMs = 160;
    // Turbulence and twinkle (browser dustCloud.js turbulenceAt / twinkleAt)
    // A grain is pushed off its line SIDEWAYS by a slow wave of its own, strongest
    // mid-flight and gone at both ends, so the cloud churns as it goes and still lands
    // where it would; a GLINT (a rim cell, or one inner cell in seven) breathes in
    // brightness on a clock of its own. Both keyed off a fourth per-cell hash.
    static constexpr double kTurbulenceShare = 0.06;   // of the throw…
    static constexpr double kTurbulenceMaxPx = 6;      // …capped
    static constexpr double kTurbulenceWaves[2] = {2.5, 4.5};   // waves per flight, by hash
    static constexpr double kTwinkleDepth = 0.35;      // a glint's brightness swing
    static constexpr double kTwinkleHz[2] = {4, 7};    // …flickers a second, by hash
    // A cell's alpha is its coverage, lifted: the strokes of a word cover a third of
    // their cells, and a grain a third as strong as its ink is a flight nobody can
    // follow (browser speckPainter: "never faint"). A painted picture is unaffected.
    static constexpr double kCoverageLift = 2.5;

    // Which way the sweep runs. A ROW crumbles from its top edge downward and the grains
    // fall, fanning out (Rows — browser tileMotion / tileScatter, top→bottom);
    // an IMAGE falls apart from its top edge and the pieces drop (Fall = top→bottom);
    // GATHER is Fall played backwards — the motes start below where they belong and rise
    // into place, fading up, so an arriving image assembles bottom→top exactly as the
    // clear erodes it top-down (browser parity: ghostIn vs ghostOut in js/ui/motion.js).
    enum class Sweep { Rows, Fall, Gather, SurfaceIn, SurfaceOut };

    static double cellNoise(int cx, int cy);

    static QPointF swirlAt(double away, double tx, double ty, double q);

    static QPointF waypointOf(double tx, double ty, double q);

    static QPointF legAt(double t, double split, const support::EaseLut& first,
                         const support::EaseLut& second, const QPointF& a, const QPointF& b,
                         const QPointF& c);
    static double legScalar(double t, double split, const support::EaseLut& first,
                            const support::EaseLut& second, double a, double b, double c);

    static const support::EaseLut& surfaceLegEase();
    static const support::EaseLut& surfaceEase();
    static const support::EaseLut& rowLegEase();
    static const support::EaseLut& rowEase();
    static constexpr double kSurfaceGatherSplit = 0.16;
    static constexpr double kSurfaceScatterSplit = 0.18;
    static constexpr double kRowSplit = 0.38;
    // A surface's motes are the ACCENT now, not the window's own pixels, so a cloud over a
    // near-opaque window adds coloured light and the window "blinks lighter" at the
    // hand-off. The cloud must be clear while the window is substantially
    // opaque: the gather fades out early, the scatter holds off until the window cuts out.
    static constexpr double kSurfaceMoteFadeFrac = 0.55;   // of the post-hold span (in)
    static constexpr double kSurfaceMoteRiseDelay = 0.5;   // × split before motes rise (out)

    static QPointF turbulenceAt(double p, double tx, double ty, double w);
    static double twinkleAt(bool glint, double ms, double w);

    static double moteRadius(double cw, double ch, double n);

    static double scatterAlpha(double k);
    static double gatherAlpha(double k);

    static QImage sampleCells(const QPixmap& snap, int cols, int rows);
    static QColor cellColour(const QImage& cells, int cx, int cy);

    static void dustGrid(const QSize& size, int cellPx, int maxCells, int* cols, int* rows);

    static DisintegrateOverlay* over(QWidget* victim, QWidget* host, Sweep sweep = Sweep::Rows,
                                     int cols = 0, int rows = 0, int ms = 0,
                                     const QColor& ink = QColor());

    static DisintegrateOverlay* overRect(QWidget* source, const QRect& rect, QWidget* host,
                                        Sweep sweep = Sweep::Rows, bool dust = false,
                                        int dustCells = kDustMaxCells, int ms = kMs,
                                        const QColor& ink = QColor(),
                                        const QPixmap& shot = QPixmap());

    static DisintegrateOverlay* overPixmaps(const QPixmap& particles, const QPixmap& base,
                                            const QRect& at, QWidget* host, Sweep sweep,
                                            int cols, int rows, int ms, double spread,
                                            int pad = 0, const QString& name = QString());

    // Slack around a surface flight's own box: how far past the picture/target a mote
    // can still be drawn (kSurfaceSpreadPx of jitter plus a rotated cell's corner).
    static constexpr int kSurfacePadPx = 64;

    static QRect surfaceLayerRect(const QRect& pictureGlobal, const QPoint& targetGlobal);

    static DisintegrateOverlay* overSurface(const QPixmap& snap, const QRect& picture,
                                            QWidget* host, const QPoint& target, bool gather,
                                            int ms = 0, const QColor& ink = QColor(),
                                            int maxCells = kSurfaceMaxCells,
                                            bool escapeHost = false, bool alwaysEscape = false);

    // What a SURFACE flight is aimed at (the centre of the control it belongs to, in
    // HOST coordinates), where its snapshot sits, and which way it is going. The GUI
    // test reads these to prove a window really does come out of the icon that opened it.
    QPoint surfaceTarget() const { return target_.toPoint(); }
    QRect surfacePicture() const { return picture_; }
    bool gathering() const { return sweep_ == Sweep::SurfaceIn; }
    const QPixmap& snapshot() const { return snap_; }

    // Shift a still-flying SURFACE by `delta`, keeping its timing/phase — for a caller
    // whose stacking algorithm moved the widget mid-flight (a toast a burst bumped to a
    // new slot). Both the picture and the target move together, so every mote's path
    // (an offset RELATIVE to its home cell) still lands where it always would have,
    // just re-based on the corrected position.
    // Confine the painting to `hostRect` (HOST coordinates): a toast beside a docked chat
    // flies to a point behind the panel, and without this its motes streamed across the
    // composer. Clipped, they pour out from behind the panel's edge instead.
    void setPaintClip(const QRect& hostRect) { paintClip_ = hostRect; update(); }
    QRect paintClip() const { return paintClip_; }   // the GUI test reads what may be painted

    void retarget(const QPoint& delta);

    void setFollow(QWidget* w);

   protected:
    // One grain in the air: where, how big, what colour (alpha baked in).
    struct Mote {
      QPointF at;
      double radius = 0;
      QColor color;
      support::GrainShape shape = support::GrainShape::Disc;   // dustKit.hpp grainShape
      double heading = 0;                                       // …lying along its travel
    };

    void finishGrain(Mote* out, double alpha, bool glint, double w, int tint,
                     double p, double away, double tx, double ty, bool fromFar,
                     const QPointF& dest) const;

    void paintEvent(QPaintEvent*) override;

    const QColor& grainColour(int cx, int cy, double) const;

    bool isGlint(int cx, int cy, double n) const;
    bool glintAt(int cx, int cy) const { return glints_[size_t(cy) * cols_ + cx]; }
    // A cell's tint (-1 = the accent ramp), fixed for the flight like its glint.
    int tintAt(int cx, int cy) const { return tints_[size_t(cy) * cols_ + cx]; }

    QColor liftedGrain(int cx, int cy, double n) const;

    bool rowMote(const QRectF& box, int cx, int cy, double cw, double ch, Mote* out) const;

    bool fallingMote(const QRectF& box, int cx, int cy, double cw, double ch, Mote* out) const;

    bool surfaceMote(const QRectF& box, int cx, int cy, double cw, double ch, Mote* out) const;

   private:
    void placeForSurface(QWidget* host, const QRect& picture, const QPoint& target,
                         bool escapeHost, bool alwaysEscape = false);

    static QPixmap liftedToInk(const QPixmap& snap, const QColor& ink);

    DisintegrateOverlay(QWidget* host, const QPixmap& snap);

    void start(int ms = kMs);

    void sizeGridForDust(const QSize& size, int maxCells = kDustMaxCells,
                         int cellPx = kDustCellPx);

    void syncFollow();

    QPointer<QWidget> follow_;   // setFollow: the control this cloud stays anchored to
    QPoint followAt_;            // …and where it was (parent coords) at the last tick
    QPixmap snap_;
    QPixmap base_;          // the state left behind (overPixmaps only); null = nothing
    QImage cells_;          // snap_'s colour per grid cell (sampleCells); rebuilt when the grid changes
    std::vector<QColor> grains_;   // …and each cell's lifted grain colour, built with it
    std::vector<bool> glints_;     // …and whether it is a rim/glint cell (twinkles)
    std::vector<int8_t> tints_;    // …and the tint its hash gave it, -1 for the ramp
    std::vector<Mote> motes_;   // per-frame scratch: the grains in the air…
    std::vector<QRect> cut_;    // …and the cells cut out of the picture (runs, Y-X sorted)
    Sweep sweep_ = Sweep::Rows;
    int cols_ = kCols;
    int rows_ = kRows;
    int pad_ = 0;           // slack around the picture for the motes to fly into
    QRect picture_;         // where the snapshot sits (surfaces only); invalid = the whole box
    QPointF target_;        // the point a surface's motes stream out of / pour into
    QPoint shift_;          // host coords → this layer's, non-zero only when it escaped the host
    QRect paintClip_;       // setPaintClip: the host area the cloud may paint in; invalid = all
    QColor ink_;            // the owner's text colour the grains are lifted towards; invalid = none
    // The style and palette the cloud was built under (motionPrefs.hpp): a mid-flight
    // settings change never restyles a cloud already in the air.
    support::ParticleStyle style_ = support::particleStyle();
    QColor accent_ = support::particleAccent();
    QColor shade_ = support::particleShade();
    bool dark_ = support::particleDark();
    support::MoteSprites sprites_;   // the grains, drawn once each and blitted
    QElapsedTimer clock_;   // start(): the wall clock the flight reads
    int ms_ = kMs;          // …and its length
    double spread_ = 1.0;   // throw distance, as a share of a list row's
    double t_ = 0.0;
  };

  DisintegrateOverlay* flyTipDust(QWidget* subject, QWidget* host,
                                  const QPoint& originGlobal, bool gather, int ms,
                                  bool escapeHost, bool paintNow = false,
                                  bool alwaysEscape = false);

}  // namespace stencil::gui
