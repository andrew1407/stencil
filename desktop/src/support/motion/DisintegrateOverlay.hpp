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
#include "dustTunings.hpp"   // the tip clocks + DisintegrateTunings
#include "motionPrefs.hpp"   // support::isDustAllowed()

#include <algorithm>
#include <cmath>
#include <vector>

namespace stencil::gui {

  QPoint dockAwayPoint(const QRect& picture, Qt::DockWidgetArea area,
                       double reach = 1.2);

  void holdFadeKeys(QVariantAnimation* fade, int ms);

  void fadeUpBehindDust(QWidget* w, int ms);

  class DisintegrateOverlay : public QWidget, public DisintegrateTunings {
   public:
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

    static QRect surfaceLayerRect(const QRect& pictureGlobal, const QPoint& targetGlobal);

    static DisintegrateOverlay* overSurface(const QPixmap& snap, const QRect& picture,
                                            QWidget* host, const QPoint& target, bool gather,
                                            int ms = 0, const QColor& ink = QColor(),
                                            int maxCells = SURFACE_MAX_CELLS,
                                            bool escapeHost = false, bool alwaysEscape = false);

    int durationMs() const { return ms; }   // the clock this cloud was started on
    QSize grid() const { return QSize(cols, rows); }   // cells; the GUI test reads these
    // HOST coordinates; the GUI test reads these.
    QPoint surfaceTarget() const { return target.toPoint(); }
    QRect surfacePicture() const { return picture; }
    bool gathering() const { return sweep == Sweep::SURFACE_IN; }
    const QPixmap& snapshot() const { return snap; }

    // `hostRect` is in HOST coordinates: a toast beside the docked chat must not paint
    // across the composer.
    void setPaintClip(const QRect& hostRect) { paintClip = hostRect; update(); }
    QRect getPaintClip() const { return paintClip; }   // the GUI test reads what may be painted

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
    bool glintAt(int cx, int cy) const { return glints[size_t(cy) * cols + cx]; }
    int tintAt(int cx, int cy) const { return tints[size_t(cy) * cols + cx]; }

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

    QPointer<QWidget> follow;
    QPoint followAt;            // parent coords at the last tick
    QPixmap snap;
    QPixmap base;          // overPixmaps only; null = nothing
    QImage cells;          // rebuilt when the grid changes
    std::vector<QColor> grains;
    std::vector<bool> glints;
    std::vector<int8_t> tints;    // -1 = the accent ramp
    std::vector<Mote> motes;   // per-frame scratch
    std::vector<QRect> cut;    // runs, Y-X sorted
    Sweep sweep = Sweep::ROWS;
    int cols = COLS;
    int rows = ROWS;
    int pad = 0;
    QRect picture;         // invalid = the whole box
    QPointF target;
    QPoint shift;          // host coords → this layer's, non-zero only when it escaped the host
    QRect paintClip;       // invalid = all
    QColor ink;            // invalid = none
    // Captured at build time: a mid-flight settings change never restyles a cloud in the air.
    support::ParticleStyle style = support::particleStyle();
    QColor accent = support::particleAccent();
    QColor shade = support::particleShade();
    bool dark = support::isParticleDark();
    support::MoteSprites sprites;
    QElapsedTimer clock;
    int ms = DUST_MS;
    double spread = 1.0;   // throw distance, as a share of a list row's
    double t = 0.0;
  };

  DisintegrateOverlay* flyTipDust(QWidget* subject, QWidget* host,
                                  const QPoint& originGlobal, bool gather, int ms,
                                  bool escapeHost, bool paintNow = false,
                                  bool alwaysEscape = false);

}  // namespace stencil::gui
