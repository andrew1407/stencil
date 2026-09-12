#pragma once
// Disintegration ("the snap") — the desktop port of disintegrate() in browser/js/ui/motion.js.
// The widget is grabbed ONCE and sampled per cell; one clock, one repaint per frame,
// grains blitted from the dustKit.hpp sprite cache. Q_OBJECT-free, so no MOC.
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
#include "motionPrefs.hpp"   // support::isDustAllowed()

#include <algorithm>
#include <cmath>
#include <vector>

namespace stencil::gui {

  // The floating-tip clock family (browser controlTooltip.js / exportPreview.js).
  inline constexpr int TIP_DUST_IN_MS = 213;
  inline constexpr int TIP_DUST_OUT_MS = 157;
  // browser surfaceForm's invisible hold while motes gather; surfaceLeave's hand-over beat.
  inline constexpr double DUST_HOLD = 0.55;
  inline constexpr int DUST_HAND_OVER_MS = 60;

  QPoint dockAwayPoint(const QRect& picture, Qt::DockWidgetArea area,
                       double reach = 1.2);

  void holdFadeKeys(QVariantAnimation* fade, int ms);

  void fadeUpBehindDust(QWidget* w, int ms);

  class DisintegrateOverlay : public QWidget {
   public:
    // Every dust clock runs 1.5x faster than its browser twin (DISINTEGRATE_MS 1650).
    static constexpr int DUST_MS = 1100;
    static constexpr int ITEM_MS = DUST_MS;         // browser ITEM_DUST_MS
    static constexpr int CONN_MS = DUST_MS * 2 / 3; // browser CONN_DUST_MS
    static constexpr int COLS = 22;     // browser DISINTEGRATE_COLS
    static constexpr int ROWS = 11;     // browser DISINTEGRATE_ROWS
    static constexpr int DUST_CELL_PX = 7;   // browser motion.js MOTE_PX — keep the two in step
    static constexpr int DUST_MAX_CELLS = 7000;
    static constexpr const char* OBJECT_NAME = "stencilDisintegrate";
    // Surface flights (browser motion.js surfaceIn / surfaceOut): every mote aims at ONE point.
    static constexpr int SURFACE_IN_MS = 507;    // browser SURFACE_IN_MS 760 / 1.5
    static constexpr int SURFACE_OUT_MS = 313;   // browser SURFACE_OUT_MS 470 / 1.5
    static constexpr int SURFACE_CELL_PX = 6;    // browser SURFACE_MOTE_PX
    // One paintEvent per frame scales with cell count; more than this read as lag on a
    // tall dialog. browser SURFACE_COLS*ROWS; overSurface() takes an override.
    static constexpr int SURFACE_MAX_CELLS = 1380;
    static constexpr double SURFACE_SPREAD_PX = 34;   // browser SURFACE_SPREAD
    // Share of the way towards the window's INK a mote is lifted (browser MOTE_INK) —
    // without it a dark dialog's motes are invisible over a dark page.
    static constexpr double SURFACE_INK_MIX = 0.42;
    // Rim and glint cells go to 66% (browser MOTE_RIM_INK), expressed as the share of the
    // REMAINING way after the 42% lift.
    static constexpr double GLINT_MIX = (0.66 - 0.42) / (1.0 - 0.42);
    static constexpr double GLINT_HASH = 0.86;
    static constexpr int SPECK_PX = 7;   // browser SURFACE_SPECK_PX; scaled 0.62..1.12 by hash
    // The bend off the throw line (browser motion.js tileWaypoint), peaking mid-flight.
    static constexpr double SWIRL_SHARE = 0.32;
    static constexpr double SWIRL_MAX_PX = 44;
    static constexpr double WAYPOINT_ALONG = 0.62;   // browser WAYPOINT_ALONG: the two-leg turn
    static constexpr int MIN_TILE_MS = 160;           // browser MIN_TILE_MS: a late mote's floor
    // Turbulence and twinkle (browser dustCloud.js turbulenceAt / twinkleAt), off a fourth hash.
    static constexpr double TURBULENCE_SHARE = 0.06;   // of the throw…
    static constexpr double TURBULENCE_MAX_PX = 6;      // …capped
    static constexpr double TURBULENCE_WAVES[2] = {2.5, 4.5};   // waves per flight, by hash
    static constexpr double TWINKLE_DEPTH = 0.35;      // a glint's brightness swing
    static constexpr double TWINKLE_HZ[2] = {4, 7};    // …flickers a second, by hash
    // Alpha is coverage lifted: text covers a third of its cells (browser speckPainter "never faint").
    static constexpr double COVERAGE_LIFT = 2.5;

    // Rows: crumble top→bottom (browser tileScatter); Fall: an image drops; Gather is
    // Fall reversed (browser ghostIn vs ghostOut in js/ui/motion.js).
    enum class Sweep { ROWS, FALL, GATHER, SURFACE_IN, SURFACE_OUT };

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
    static constexpr double SURFACE_GATHER_SPLIT = 0.16;
    static constexpr double SURFACE_SCATTER_SPLIT = 0.18;
    static constexpr double ROW_SPLIT = 0.38;
    // Accent motes over a near-opaque window "blink lighter" at the hand-off, so the
    // gather fades out early and the scatter waits for the window to cut out.
    static constexpr double SURFACE_MOTE_FADE_FRAC = 0.55;   // of the post-hold span (in)
    static constexpr double SURFACE_MOTE_RISE_DELAY = 0.5;   // × split before motes rise (out)

    static QPointF turbulenceAt(double p, double tx, double ty, double w);
    static double twinkleAt(bool glint, double ms, double w);

    static double moteRadius(double cw, double ch, double n);

    static double scatterAlpha(double k);
    static double gatherAlpha(double k);

    static QImage sampleCells(const QPixmap& snap, int cols, int rows);
    static QColor cellColour(const QImage& cells, int cx, int cy);

    static void dustGrid(const QSize& size, int cellPx, int maxCells, int* cols, int* rows);

    static DisintegrateOverlay* over(QWidget* victim, QWidget* host, Sweep sweep = Sweep::ROWS,
                                     int cols = 0, int rows = 0, int ms = 0,
                                     const QColor& ink = QColor());

    static DisintegrateOverlay* overRect(QWidget* source, const QRect& rect, QWidget* host,
                                        Sweep sweep = Sweep::ROWS, bool dust = false,
                                        int dustCells = DUST_MAX_CELLS, int ms = DUST_MS,
                                        const QColor& ink = QColor(),
                                        const QPixmap& shot = QPixmap());

    static DisintegrateOverlay* overPixmaps(const QPixmap& particles, const QPixmap& base,
                                            const QRect& at, QWidget* host, Sweep sweep,
                                            int cols, int rows, int ms, double spread,
                                            int pad = 0, const QString& name = QString());

    // Slack past the picture/target: SURFACE_SPREAD_PX of jitter plus a rotated cell's corner.
    static constexpr int SURFACE_PAD_PX = 64;

    static QRect surfaceLayerRect(const QRect& pictureGlobal, const QPoint& targetGlobal);

    static DisintegrateOverlay* overSurface(const QPixmap& snap, const QRect& picture,
                                            QWidget* host, const QPoint& target, bool gather,
                                            int ms = 0, const QColor& ink = QColor(),
                                            int maxCells = SURFACE_MAX_CELLS,
                                            bool escapeHost = false, bool alwaysEscape = false);

    // HOST coordinates; the GUI test reads these.
    QPoint surfaceTarget() const { return target_.toPoint(); }
    QRect surfacePicture() const { return picture_; }
    bool gathering() const { return sweep_ == Sweep::SURFACE_IN; }
    const QPixmap& snapshot() const { return snap_; }

    // `hostRect` is in HOST coordinates: a toast beside the docked chat must not paint
    // across the composer.
    void setPaintClip(const QRect& hostRect) { paintClip_ = hostRect; update(); }
    QRect paintClip() const { return paintClip_; }   // the GUI test reads what may be painted

    void retarget(const QPoint& delta);

    void setFollow(QWidget* w);

   protected:
    struct Mote {
      QPointF at;
      double radius = 0;
      QColor color;
      support::GrainShape shape = support::GrainShape::DISC;   // dustKit.hpp grainShape
      double heading = 0;                                       // …lying along its travel
    };

    void finishGrain(Mote* out, double alpha, bool glint, double w, int tint,
                     double p, double away, double tx, double ty, bool fromFar,
                     const QPointF& dest) const;

    void paintEvent(QPaintEvent*) override;

    const QColor& grainColour(int cx, int cy, double) const;

    bool isGlint(int cx, int cy, double n) const;
    bool glintAt(int cx, int cy) const { return glints_[size_t(cy) * cols_ + cx]; }
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

    void start(int ms = DUST_MS);

    void sizeGridForDust(const QSize& size, int maxCells = DUST_MAX_CELLS,
                         int cellPx = DUST_CELL_PX);

    void syncFollow();

    QPointer<QWidget> follow_;
    QPoint followAt_;            // parent coords at the last tick
    QPixmap snap_;
    QPixmap base_;          // overPixmaps only; null = nothing
    QImage cells_;          // rebuilt when the grid changes
    std::vector<QColor> grains_;
    std::vector<bool> glints_;
    std::vector<int8_t> tints_;    // -1 = the accent ramp
    std::vector<Mote> motes_;   // per-frame scratch
    std::vector<QRect> cut_;    // runs, Y-X sorted
    Sweep sweep_ = Sweep::ROWS;
    int cols_ = COLS;
    int rows_ = ROWS;
    int pad_ = 0;
    QRect picture_;         // invalid = the whole box
    QPointF target_;
    QPoint shift_;          // host coords → this layer's, non-zero only when it escaped the host
    QRect paintClip_;       // invalid = all
    QColor ink_;            // invalid = none
    // Captured at build time: a mid-flight settings change never restyles a cloud in the air.
    support::ParticleStyle style_ = support::particleStyle();
    QColor accent_ = support::particleAccent();
    QColor shade_ = support::particleShade();
    bool dark_ = support::isParticleDark();
    support::MoteSprites sprites_;
    QElapsedTimer clock_;
    int ms_ = DUST_MS;
    double spread_ = 1.0;   // throw distance, as a share of a list row's
    double t_ = 0.0;
  };

  DisintegrateOverlay* flyTipDust(QWidget* subject, QWidget* host,
                                  const QPoint& originGlobal, bool gather, int ms,
                                  bool escapeHost, bool paintNow = false,
                                  bool alwaysEscape = false);

}  // namespace stencil::gui
