#pragma once
// Disintegration ("the snap") — the desktop port of disintegrate() in
// browser/js/ui/motion.js.
//
// The browser paints one round mote per grid cell in the element's own colours and
// lets CSS fly them. Qt does the same off a photograph: the widget is grabbed ONCE,
// its colour is sampled per cell (sampleCells), and every frame draws the snapshot
// whole, cuts out the cells that have left it, and flies those as round grains of
// their own colour — offset, bent, shrunk and faded by their own progress. One
// clock drives the lot, so it stays a single repaint per frame however many cells
// there are; the grains are blitted from a sprite cache (dustKit.hpp MoteSprites),
// and the clock ticks at the screen's own refresh rate.
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

  // ── The floating-tip clock family (browser controlTooltip.js / exportPreview.js):
  // tooltips, the export preview and popup menus gather/leave on this shared clock.
  inline constexpr int kTipDustInMs = 213;
  inline constexpr int kTipDustOutMs = 157;
  // Where the browser's surfaceForm keyframes hold a forming surface invisible while
  // its motes gather, and surfaceLeave's one-beat hand-over on the way out.
  inline constexpr double kDustHold = 0.55;
  inline constexpr int kDustHandOverMs = 60;

  // Where a docked surface's dust comes from / returns to: `picture`'s centre pushed
  // out past the edge named by `area` by `reach`x that edge's own extent — the
  // browser's dockAwayPoint (motion.js).
  inline QPoint dockAwayPoint(const QRect& picture, Qt::DockWidgetArea area,
                              double reach = 1.2) {
    const bool horiz = area != Qt::TopDockWidgetArea && area != Qt::BottomDockWidgetArea;
    const int reachPx = qRound((horiz ? picture.width() : picture.height()) * reach);
    QPoint target = picture.center();
    if (area == Qt::LeftDockWidgetArea) target.setX(picture.left() - reachPx);
    else if (area == Qt::RightDockWidgetArea) target.setX(picture.right() + reachPx);
    else if (area == Qt::TopDockWidgetArea) target.setY(picture.top() - reachPx);
    else if (area == Qt::BottomDockWidgetArea) target.setY(picture.bottom() + reachPx);
    return target;
  }

  // The browser surfaceForm ramp: held at 0 until kDustHold, then up to 1 — the fade
  // every surface plays behind its own gathering dust. Works on any variant animation
  // (windowOpacity or a QGraphicsOpacityEffect's opacity alike).
  inline void holdFadeKeys(QVariantAnimation* fade, int ms) {
    fade->setKeyValues({});
    fade->setDuration(ms);
    fade->setKeyValueAt(0.0, 0.0);
    fade->setKeyValueAt(kDustHold, 0.0);
    fade->setKeyValueAt(1.0, 1.0);
  }

  // windowOpacity flavour for a top-level surface: waits invisible behind its own
  // gathering dust and fades up as the last motes land; never left dimmed.
  inline void fadeUpBehindDust(QWidget* w, int ms) {
    auto* fade = new QPropertyAnimation(w, "windowOpacity", w);
    holdFadeKeys(fade, ms);
    QPointer<QWidget> guard(w);
    QObject::connect(fade, &QPropertyAnimation::finished, w, [guard] {
      if (guard) guard->setWindowOpacity(1.0);
    });
    w->setWindowOpacity(0.0);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
  }

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
    // ── A whole SURFACE is dust too (browser motion.js surfaceIn / surfaceOut) ──
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
    // ── The bend (browser motion.js tileWaypoint) ──
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
    // ── Turbulence and twinkle (browser dustCloud.js turbulenceAt / twinkleAt) ──
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

    // Deterministic per-cell jitter — the same hash the browser uses, so the two
    // scatter alike. Returns 0..1.
    static double cellNoise(int cx, int cy) {
      const double h = std::sin(cx * 127.1 + cy * 311.7) * 43758.5453;
      return h - std::floor(h);
    }

    // The bend at `away` (0 home … 1 at the far end of the throw `tx, ty`): a push off
    // the throw's own line, by `q`'s side and amount. Shared with the combo's word
    // exchange (controlSwap.hpp), so the app's sand all bends alike.
    static QPointF swirlAt(double away, double tx, double ty, double q) {
      constexpr double kPi = 3.14159265358979323846;   // M_PI is not portable (MSVC)
      const double len = std::hypot(tx, ty);
      if (len < 0.5) return {};
      const double amp = (q - 0.5) * 2.0 * std::min(len * kSwirlShare, kSwirlMaxPx);
      const double s = std::sin(kPi * away) * amp;
      return QPointF(-ty / len * s, tx / len * s);
    }

    // The waypoint of a throw `tx, ty` (browser tileWaypoint): kWaypointAlong of the way
    // out, pushed perpendicular by `q`'s side and a capped share of the throw.
    static QPointF waypointOf(double tx, double ty, double q) {
      const double len = std::hypot(tx, ty);
      if (len < 0.5) return {};
      const double amp = (q - 0.5) * 2.0 * std::min(len * kSwirlShare, kSwirlMaxPx);
      return QPointF(tx * kWaypointAlong - ty / len * amp, ty * kWaypointAlong + tx / len * amp);
    }

    // A two-leg keyframe flight at time `t` (0..1): `a` to `b` over the first `split` on
    // curve `first`, then `b` to `c` on curve `second` — what CSS does with a mid
    // keyframe that carries its own animation-timing-function.
    static QPointF legAt(double t, double split, const support::EaseLut& first,
                         const support::EaseLut& second, const QPointF& a, const QPointF& b,
                         const QPointF& c) {
      if (t <= split) {
        const double e = first.at(split > 0 ? t / split : 1.0);
        return a + (b - a) * e;
      }
      const double e = second.at((t - split) / (1.0 - split));
      return b + (c - b) * e;
    }
    // …and a scalar (the grain's size) on the same two legs.
    static double legScalar(double t, double split, const support::EaseLut& first,
                            const support::EaseLut& second, double a, double b, double c) {
      if (t <= split) return a + (b - a) * first.at(split > 0 ? t / split : 1.0);
      return b + (c - b) * second.at((t - split) / (1.0 - split));
    }

    // The browser's curves, tabulated once (css/animations.css):
    // a surface's first leg into the bend, and its ease-out home (tileGatherSurface /
    // tileScatterSurface: cubic-bezier(0.3,0.3,0.6,0.8) then (0.16,1,0.3,1));
    static const support::EaseLut& surfaceLegEase() {
      static const support::EaseLut lut(0.3, 0.3, 0.6, 0.8);
      return lut;
    }
    static const support::EaseLut& surfaceEase() {
      static const support::EaseLut lut(0.16, 1.0, 0.3, 1.0);
      return lut;
    }
    // …and a row's (tileScatter: (0.3,0.4,0.7,0.8) into the bend at 38%, then the
    // flight's own (0.22,0.55,0.3,1) home).
    static const support::EaseLut& rowLegEase() {
      static const support::EaseLut lut(0.3, 0.4, 0.7, 0.8);
      return lut;
    }
    static const support::EaseLut& rowEase() {
      static const support::EaseLut lut(0.22, 0.55, 0.3, 1.0);
      return lut;
    }
    static constexpr double kSurfaceGatherSplit = 0.16;
    static constexpr double kSurfaceScatterSplit = 0.18;
    static constexpr double kRowSplit = 0.38;

    // The sideways push at progress `p` of a throw `tx, ty`, for a grain of hash `w`.
    static QPointF turbulenceAt(double p, double tx, double ty, double w) {
      constexpr double kPi = 3.14159265358979323846;
      const double len = std::hypot(tx, ty);
      if (len < 0.5) return {};
      const double waves = kTurbulenceWaves[0] + (kTurbulenceWaves[1] - kTurbulenceWaves[0]) * w;
      const double s = std::min(len * kTurbulenceShare, kTurbulenceMaxPx) * std::sin(kPi * p)
                     * std::sin(p * waves * 2 * kPi + w * 2 * kPi);
      return QPointF(-ty / len * s, tx / len * s);
    }
    // A glint's brightness at `ms` into the flight (1 for a plain grain).
    static double twinkleAt(bool glint, double ms, double w) {
      constexpr double kPi = 3.14159265358979323846;
      if (!glint) return 1.0;
      const double hz = kTwinkleHz[0] + (kTwinkleHz[1] - kTwinkleHz[0]) * w;
      return 1.0 - kTwinkleDepth * 0.5 * (1.0 + std::sin(ms * hz * 2 * kPi / 1000.0 + w * 2 * kPi));
    }

    // A grain's radius at home, for a `cw` x `ch` cell and its hash `n`.
    static double moteRadius(double cw, double ch, double n) {
      return std::min({cw, ch, double(kSpeckPx)}) * (0.62 + n * 0.5) * 0.5;
    }

    // A grain's alpha over its flight's TIME `k` (0..1) — the browser's tileScatter /
    // tileGather keyframes evaluated by hand. On the clock, not on the eased distance:
    // an ease-out covers most of the trip early, and a fade riding it was over before
    // the grain had visibly gone anywhere.
    static double scatterAlpha(double k) {
      return k < 0.38 ? 1.0 - k * (0.15 / 0.38) : 0.85 * (1.0 - (k - 0.38) / 0.62);
    }
    static double gatherAlpha(double k) {
      if (k < 0.22) return 0.75 * (k / 0.22);
      if (k < 0.58) return 0.75 + 0.15 * ((k - 0.22) / 0.36);
      return 0.9 + 0.1 * ((k - 0.58) / 0.42);
    }

    // The colour of every cell of `snap`, in one pass: the picture scaled down to the
    // grid — Qt's smooth scale is an area average — premultiplied so transparent pixels
    // weigh nothing. Read back with cellColour(), which lifts the coverage.
    static QImage sampleCells(const QPixmap& snap, int cols, int rows) {
      return snap.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied)
          .scaled(std::max(1, cols), std::max(1, rows), Qt::IgnoreAspectRatio,
                  Qt::SmoothTransformation);
    }
    static QColor cellColour(const QImage& cells, int cx, int cy) {
      if (cells.isNull() || cx < 0 || cy < 0 || cx >= cells.width() || cy >= cells.height()) return {};
      QColor c = cells.pixelColor(cx, cy);
      c.setAlphaF(std::min(1.0, c.alphaF() * kCoverageLift));
      return c;
    }

    // Motes sized on SCREEN (`cellPx` each), thinned back if that would exceed the
    // ceiling — browser motion.js reshapeGrid. Shared with controlReveal's mark grid.
    static void dustGrid(const QSize& size, int cellPx, int maxCells, int* cols, int* rows) {
      *cols = std::max(1, qRound(double(size.width()) / cellPx));
      *rows = std::max(1, qRound(double(size.height()) / cellPx));
      while (*cols * *rows > std::max(64, maxCells)) {
        // ceil(x/1.1) is x itself for x <= 10 — force a strict shrink or this spins forever.
        *cols = std::max(1, std::min(*cols - 1, int(std::ceil(*cols / 1.1))));
        *rows = std::max(1, std::min(*rows - 1, int(std::ceil(*rows / 1.1))));
      }
    }

    // Snapshot `victim` and scatter it inside `host` (which must outlive the play —
    // normally the window). Returns nullptr when there is nothing worth animating, so
    // callers can treat a failure as "just remove it".
    // `cols`/`rows` override the grid for a denser scatter (the chat surfaces ask for
    // one — a deleted message is a deliberate act and deserves more than a list row's
    // dust; browser motion.js CHAT_DISINTEGRATE_*). 0 keeps the defaults.
    // `ms` shortens the flight for a caller on its own clock (a chat card arriving —
    // llm/chatWidgets.cpp kChatArriveMs); 0 keeps the row default.
    // `ink` lifts the grains towards the victim's own text colour (kSurfaceInkMix, the
    // browser's speckPainter): a list row and the list it leaves are the same colour,
    // and unlifted its dust was invisible over the rows behind it. A PICTURE (the canvas
    // image) passes none — its grains are its own pixels.
    static DisintegrateOverlay* over(QWidget* victim, QWidget* host, Sweep sweep = Sweep::Rows,
                                     int cols = 0, int rows = 0, int ms = 0,
                                     const QColor& ink = QColor()) {
      if (!support::dustAllowed()) return nullptr;   // no particles in this motion mode
      if (!victim || !host || !victim->isVisible()) return nullptr;
      if (victim->width() < 8 || victim->height() < 8) return nullptr;
      const QPixmap snap = victim->grab();
      if (snap.isNull()) return nullptr;
      // Placed in the host's coordinates: the victim is about to be destroyed, and a
      // child of it would die mid-flight.
      const QPoint at = victim->mapTo(host, QPoint(0, 0));
      auto* fx = new DisintegrateOverlay(host, liftedToInk(snap, ink));
      fx->ink_ = ink;
      fx->sweep_ = sweep;
      if (sweep != Sweep::Rows) fx->sizeGridForDust(victim->size());
      if (cols > 0) fx->cols_ = cols;
      if (rows > 0) fx->rows_ = rows;
      fx->setGeometry(QRect(at, victim->size()));
      fx->show();
      fx->raise();
      // …and again once the pending layout has settled: removing the victim relayouts
      // its container, which restacks the host's children and would bury the particles.
      QTimer::singleShot(0, fx, [fx] { fx->raise(); });
      fx->start(ms > 0 ? ms : kMs);
      return fx;
    }

    // Same effect for a REGION of a widget — item views paint their rows, so a
    // deleted QListWidget/QTableWidget row has no widget of its own to grab.
    // `rect` is in `source` coordinates (a delegate's option.rect / visualItemRect).
    // `dust` sizes the grid from the rect's own pixels (kDustCellPx per mote) instead of the
    // fixed kCols x kRows — a wide, short LIST row split 22x11 came apart into a handful of
    // slabs rather than dust. Implied for the non-Rows sweeps, which always want motes.
    // `dustCells` caps the mote count for THIS overlay. Removing several rows at once means
    // several overlays repainting every frame, and the cost is their SUM — so a mass removal
    // divides the budget between them (browser motion.js scatterGridFor does the same).
    // `ms` shortens the flight for a motion that is not a removal — a filter change moves
    // rows in and out on its own short clock (support/filterFade.hpp kFilterDustMs).
    // `ink`: as over() — the row's text colour to lift its grains towards; none for a picture.
    static DisintegrateOverlay* overRect(QWidget* source, const QRect& rect, QWidget* host,
                                        Sweep sweep = Sweep::Rows, bool dust = false,
                                        int dustCells = kDustMaxCells, int ms = kMs,
                                        const QColor& ink = QColor()) {
      if (!support::dustAllowed()) return nullptr;   // no particles in this motion mode
      if (!source || !host || !source->isVisible()) return nullptr;
      if (rect.width() < 8 || rect.height() < 8) return nullptr;
      const QPixmap snap = source->grab(rect);
      if (snap.isNull()) return nullptr;
      const QPoint at = source->mapTo(host, rect.topLeft());
      auto* fx = new DisintegrateOverlay(host, liftedToInk(snap, ink));
      fx->ink_ = ink;
      fx->sweep_ = sweep;
      if (dust || sweep != Sweep::Rows) fx->sizeGridForDust(rect.size(), dustCells);
      fx->setGeometry(QRect(at, rect.size()));
      fx->show();
      fx->raise();
      QTimer::singleShot(0, fx, [fx] { fx->raise(); });
      fx->start(ms > 0 ? ms : kMs);
      return fx;
    }

    // The same scatter over an explicitly RENDERED pair of states, for a control whose
    // glyph belongs to the style rather than to a widget of its own (a checkbox
    // indicator): `particles` is the state that comes and goes, `base` the one left
    // behind, painted opaque underneath so whatever the real control already shows
    // never bleeds through. `at` is in host coordinates; `spread` scales the throw for
    // a control far smaller than a list row, and `ms` shortens it to click feedback.
    // `pad` widens the overlay around `at` WITHOUT moving the picture inside it: a row's
    // motes travel a fraction of its own width and clip harmlessly, but a 16px control's
    // leave its box at once, so the canvas has to be bigger than the thing on it.
    static DisintegrateOverlay* overPixmaps(const QPixmap& particles, const QPixmap& base,
                                            const QRect& at, QWidget* host, Sweep sweep,
                                            int cols, int rows, int ms, double spread,
                                            int pad = 0, const QString& name = QString()) {
      if (!support::dustAllowed()) return nullptr;   // no particles in this motion mode
      if (!host || particles.isNull() || at.width() < 2 || at.height() < 2) return nullptr;
      auto* fx = new DisintegrateOverlay(host, particles);
      if (!name.isEmpty()) fx->setObjectName(name);
      fx->base_ = base;
      fx->sweep_ = sweep;
      fx->cols_ = std::max(1, cols);
      fx->rows_ = std::max(1, rows);
      fx->spread_ = spread;
      fx->pad_ = std::max(0, pad);
      fx->setGeometry(at.adjusted(-fx->pad_, -fx->pad_, fx->pad_, fx->pad_));
      fx->show();
      fx->raise();
      QTimer::singleShot(0, fx, [fx] { fx->raise(); });
      fx->start(ms);
      return fx;
    }

    // Slack around a surface flight's own box: how far past the picture/target a mote
    // can still be drawn (kSurfaceSpreadPx of jitter plus a rotated cell's corner).
    static constexpr int kSurfacePadPx = 64;

    // Everything a flight can paint — picture plus target point, each padded. All GLOBAL.
    // A layer smaller than this crops the cloud; placeForSurface() tests the host on it.
    static QRect surfaceLayerRect(const QRect& pictureGlobal, const QPoint& targetGlobal) {
      QRect need = pictureGlobal;
      need |= QRect(targetGlobal, QSize(1, 1));
      return need.adjusted(-kSurfacePadPx, -kSurfacePadPx, kSurfacePadPx, kSurfacePadPx);
    }

    // A whole surface's flight. `picture` is where the snapshot sits and `target` the
    // point its motes stream out of (SurfaceIn) or pour into (SurfaceOut), both in HOST
    // coordinates; the overlay covers the whole host rather than just the picture,
    // because a mote's journey is out to that point and a surface-sized layer clips it.
    // `escapeHost` is for a surface that is its own top-level window (a dialog, a menu,
    // the tooltip), whose cloud must be free to leave the host too — see
    // placeForSurface(). Ones living INSIDE it leave it false: the docked chat and the
    // toasts aim past the window edge on purpose and want to be cropped there.
    // `alwaysEscape`: a surface that is its own window floating ABOVE the host (the
    // projects hover preview) hides a child layer's motes underneath itself — this
    // lifts the cloud into the top-level layer even when the flight would fit the
    // host, so it rides above both (browser stacking, where the dust is z-topmost).
    static DisintegrateOverlay* overSurface(const QPixmap& snap, const QRect& picture,
                                            QWidget* host, const QPoint& target, bool gather,
                                            int ms = 0, const QColor& ink = QColor(),
                                            int maxCells = kSurfaceMaxCells,
                                            bool escapeHost = false, bool alwaysEscape = false) {
      if (!support::dustAllowed()) return nullptr;   // no particles in this motion mode
      if (!host || snap.isNull() || picture.width() < 8 || picture.height() < 8) return nullptr;
      auto* fx = new DisintegrateOverlay(host, liftedToInk(snap, ink));
      fx->ink_ = ink;
      fx->sweep_ = gather ? Sweep::SurfaceIn : Sweep::SurfaceOut;
      fx->picture_ = picture;
      fx->target_ = QPointF(target);
      fx->sizeGridForDust(picture.size(), maxCells, kSurfaceCellPx);
      fx->placeForSurface(host, picture, target, escapeHost, alwaysEscape);
      fx->show();
      fx->raise();
      QTimer::singleShot(0, fx, [fx] { fx->raise(); });
      fx->start(ms > 0 ? ms : (gather ? kSurfaceInMs : kSurfaceOutMs));
      return fx;
    }

    // What a SURFACE flight is aimed at (the centre of the control it belongs to, in
    // HOST coordinates), where its snapshot sits, and which way it is going. The GUI
    // test reads these to prove a window really does come out of the icon that opened
    // it — the property the old ghost's start geometry used to carry.
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

    void retarget(const QPoint& delta) {
      if (delta.isNull()) return;
      picture_.translate(delta);
      target_ += QPointF(delta);
      update();
    }

    // Keep the cloud anchored to `w` for the rest of the flight: every tick re-maps the
    // control's top-left and shifts the cloud by however far it moved. A control revealed
    // beside a sibling is photographed where it sits at that instant, and the sibling's
    // slot then pushes it along the row. Browser twin: motion.js followDust (revealControls).
    // A tile flight IS its geometry; a surface flight shifts picture and target.
    void setFollow(QWidget* w) {
      follow_ = w;
      followAt_ = w && parentWidget() ? w->mapTo(parentWidget(), QPoint(0, 0)) : QPoint();
    }

   protected:
    // One grain in the air: where, how big, what colour (alpha baked in).
    struct Mote {
      QPointF at;
      double radius = 0;
      QColor color;
      support::GrainShape shape = support::GrainShape::Disc;   // dustKit.hpp grainShape
      double heading = 0;                                       // …lying along its travel
    };

    // Finish a grain: its shape, heading (`tx, ty` is its throw, `fromFar` a gather) and
    // colour. Every grain is painted from the accent palette by its mix, never in the
    // cell's own colour — the cell only said how much paint there was, which `alpha`
    // already carries. Dust twinkles; water and fire take styleFrame's touch instead.
    void finishGrain(Mote* out, double alpha, bool glint, double w,
                     double p, double away, double tx, double ty, bool fromFar) const {
      const double len = std::hypot(tx, ty);
      out->shape = support::grainShape(style_, w);
      out->heading = support::headingOf(tx, ty, fromFar);
      if (style_ == support::ParticleStyle::Dust) {
        out->color = support::paletteStop(accent_, shade_, support::dustMix(w, glint));
        out->color.setAlphaF(std::clamp(alpha * twinkleAt(glint, t_ * ms_, w), 0.0, 1.0));
        return;
      }
      const support::StyleFrame sf = support::styleFrame(style_, p, away, w, len, t_ * ms_);
      out->at += QPointF(sf.sx, sf.sy);
      out->radius *= sf.scale;
      out->color = support::paletteStop(accent_, shade_, sf.mix);
      out->color.setAlphaF(std::clamp(alpha * sf.glow, 0.0, 1.0));
    }

    void paintEvent(QPaintEvent*) override {
      if (snap_.isNull()) return;
      if (cells_.width() != cols_ || cells_.height() != rows_) {
        cells_ = sampleCells(snap_, cols_, rows_);
        // Every grain's colour, once: the picture never changes under a flight, and
        // reading a QColor back out of the cell image per grain per frame was a third
        // of the frame at 4000 cells.
        grains_.resize(size_t(cols_) * rows_);
        glints_.assign(size_t(cols_) * rows_, false);
        for (int cy = 0; cy < rows_; ++cy)
          for (int cx = 0; cx < cols_; ++cx) {
            const double n = cellNoise(cx, cy);
            grains_[size_t(cy) * cols_ + cx] = liftedGrain(cx, cy, n);
            glints_[size_t(cy) * cols_ + cx] = isGlint(cx, cy, n);
          }
      }
      QPainter p(this);
      if (paintClip_.isValid()) p.setClipRect(paintClip_.translated(shift_));
      p.setRenderHint(QPainter::SmoothPixmapTransform, true);
      // Where the picture itself sits — the whole overlay unless `pad` widened it, or a
      // SURFACE placed it somewhere inside a host-sized layer.
      // `shift_` is zero for a child layer; an escaped one draws host coords in its own.
      const QRectF box = picture_.isValid() ? QRectF(picture_.translated(shift_))
                                            : QRectF(rect()).adjusted(pad_, pad_, -pad_, -pad_);
      if (box.width() <= 0 || box.height() <= 0) return;
      // Every cell still at home IS the picture: the snapshot is blitted through a clip
      // that leaves out the cells that have left it, so the front reads as the thing
      // grinding into grains of its own colour rather than a dot screen popping over it.
      // A gathering picture closes the same way, cell by landed cell. A CLIP, never a
      // clear: this is usually a child widget painting into the window's own backing
      // store, and a Source-mode clear there punched a black hole through the window.
      const double cw = box.width() / cols_;
      const double ch = box.height() / rows_;
      const bool surface = sweep_ == Sweep::SurfaceIn || sweep_ == Sweep::SurfaceOut;
      motes_.clear();
      cut_.clear();
      for (int cy = 0; cy < rows_; ++cy) {
        // Cell edges are rounded so neighbours share one; the picture's OUTER edge
        // rounds up, or a fractional box left an uncleared hairline of it down the side.
        const int y0 = qRound(box.y() + cy * ch);
        const int y1 = cy == rows_ - 1 ? int(std::ceil(box.bottom())) : qRound(box.y() + (cy + 1) * ch);
        int runStart = -1;   // the run of departed cells being merged into one rect
        for (int cx = 0; cx <= cols_; ++cx) {
          Mote m;
          const bool away = cx < cols_
              && (surface ? surfaceMote(box, cx, cy, cw, ch, &m)
                  : sweep_ == Sweep::Rows ? rowMote(box, cx, cy, cw, ch, &m)
                                          : fallingMote(box, cx, cy, cw, ch, &m));
          if (away) {
            if (runStart < 0) runStart = cx;
            if (m.radius > 0.25 && m.color.alphaF() > 0.01) motes_.push_back(m);
          } else if (runStart >= 0) {
            // QRegion::setRects wants Y-X sorted, non-abutting rects: one per run.
            const int x1 = cx == cols_ ? int(std::ceil(box.right())) : qRound(box.x() + cx * cw);
            cut_.push_back(QRect(QPoint(qRound(box.x() + runStart * cw), y0), QPoint(x1 - 1, y1 - 1)));
            runStart = -1;
          }
        }
      }
      // The state left behind, under the particles — it shows wherever a cell has gone.
      if (!base_.isNull()) p.drawPixmap(box, base_, QRectF(base_.rect()));
      if (surface) {
        // A SURFACE is never shown cell by cell: the browser's motes are the window
        // until they land, and the window itself fades up behind them once they mostly
        // have (surfaceForm: held at 0 to kDustHold, then up) — or, leaving, cuts to
        // nothing over its first beat while the sand is still where it stood
        // (surfaceLeave). Cut out per cell, the front was a blocky staircase of
        // photograph that no browser surface ever shows.
        const double fade = sweep_ == Sweep::SurfaceIn
            ? (t_ < kDustHold ? 0.0 : (t_ - kDustHold) / (1.0 - kDustHold))
            : std::max(0.0, 1.0 - t_ / kSurfaceScatterSplit);
        if (fade > 0.0) {
          p.setOpacity(fade);
          p.drawPixmap(box, snap_, QRectF(snap_.rect()));
          p.setOpacity(1.0);
        }
      } else {
        QRegion keep(box.toAlignedRect());
        if (!cut_.empty()) {
          QRegion gone;
          gone.setRects(cut_.data(), int(cut_.size()));
          keep -= gone;
        }
        if (!keep.isEmpty()) {
          p.save();
          p.setClipRegion(keep, Qt::IntersectClip);
          p.drawPixmap(box, snap_, QRectF(snap_.rect()));
          p.restore();
        }
      }
      // The grains: blitted from the sprite cache, never rasterised here — antialiasing
      // OFF, or the raster engine leaves its 1:1 fast path (dustKit.hpp MoteSprites).
      p.setRenderHint(QPainter::Antialiasing, false);
      p.setRenderHint(QPainter::SmoothPixmapTransform, false);
      for (const Mote& m : motes_) sprites_.draw(p, m.at, m.radius, m.color, m.shape, m.heading);
      p.setOpacity(1.0);
    }

    // A cell's grain colour, from the per-flight table paintEvent builds.
    const QColor& grainColour(int cx, int cy, double) const {
      return grains_[size_t(cy) * cols_ + cx];
    }

    // The browser speckPainter's RIM and GLINT cells: the picture's edge, and one inner
    // cell in seven — lifted further towards the ink, and the ones that twinkle.
    bool isGlint(int cx, int cy, double n) const {
      return cx == 0 || cy == 0 || cx == cols_ - 1 || cy == rows_ - 1 || n > kGlintHash;
    }
    bool glintAt(int cx, int cy) const { return glints_[size_t(cy) * cols_ + cx]; }

    // A cell's colour, lifted further towards the ink where the browser's speckPainter
    // paints a RIM or a GLINT.
    QColor liftedGrain(int cx, int cy, double n) const {
      QColor c = cellColour(cells_, cx, cy);
      if (!ink_.isValid() || c.alphaF() <= 0.02) return c;
      if (!isGlint(cx, cy, n)) return c;
      return QColor(qRound(c.red() + (ink_.red() - c.red()) * kGlintMix),
                    qRound(c.green() + (ink_.green() - c.green()) * kGlintMix),
                    qRound(c.blue() + (ink_.blue() - c.blue()) * kGlintMix), c.alpha());
    }

    // One grain of a ROW flight — the browser's tileMotion + tileScatter keyframes, op
    // for op: the row crumbles from its top edge downward, each grain falling and
    // fanning out (signed, so the cloud spreads both ways), through the bend at 38% of
    // its flight on the first leg's curve and home… out, on the flight's own.
    // Same contract as fallingMote: false = the cell is the picture right now.
    bool rowMote(const QRectF& box, int cx, int cy, double cw, double ch, Mote* out) const {
      const double n = cellNoise(cx, cy);
      const double m = cellNoise(cx + 41, cy + 17);
      const double q = cellNoise(cx + 97, cy + 53);
      // 0 at the TOP row (goes first), 1 at the bottom (goes last). The sweep is a fifth
      // of the span (browser: "a mote still at its 0% pose past the row's own collapse
      // is a dot screen sitting where the row was"), plus the tile's own jitter.
      const double progress = rows_ > 1 ? double(cy) / (rows_ - 1) : 0.0;
      const double delay = progress * 0.2 + n * (60.0 / 900.0);
      // The flight is what is left of the span, floored (kMinTileMs) so a late grain
      // still flies rather than blinks.
      const double flight = std::max(double(kMinTileMs) / std::max(1, ms_), 1.0 - delay);
      const double t = (t_ - delay) / flight;
      if (t <= 0.0) return false;   // not yet left: the picture
      *out = Mote{};
      if (t >= 1.0) return true;    // gone
      const QColor cell = grainColour(cx, cy, n);
      if (cell.alphaF() <= 0.02) return true;   // nothing was painted here
      const double tx = (m - 0.5) * 66 * spread_;
      const double ty = (26 + progress * 30 + n * 44) * spread_;
      const QPointF home(box.x() + (cx + 0.5) * cw, box.y() + (cy + 0.5) * ch);
      const double w = cellNoise(cx + 13, cy + 71);
      const QPointF far = home + QPointF(tx, ty);
      const QPointF bend = home + waypointOf(tx, ty, q);
      out->at = legAt(t, kRowSplit, rowLegEase(), rowEase(), home, bend, far) + turbulenceAt(t, tx, ty, w);
      // tileScatter's scale: 1 at home, its far size (0.3..0.6) out there, halfway at the bend.
      const double farScale = 0.3 + n * 0.3;
      out->radius = moteRadius(cw, ch, n)
          * legScalar(t, kRowSplit, rowLegEase(), rowEase(), 1.0, 1.0 - (1.0 - farScale) * 0.5, farScale);
      finishGrain(out, cell.alphaF() * (0.78 + n * 0.22) * scatterAlpha(t), glintAt(cx, cy), w,
                  t, t, tx, ty, false);
      return true;
    }

    // One grain of a FALL / GATHER flight (browser motion.js ghostOut / ghostIn —
    // dustParts + drawDust). Returns false while the cell is at home —
    // not yet left, or already landed — and is then simply the picture. True means the
    // cell is cut out of it, with `out` the grain to draw (a radius of 0 once it is gone).
    bool fallingMote(const QRectF& box, int cx, int cy, double cw, double ch, Mote* out) const {
      const double n = cellNoise(cx, cy);
      // A second, decorrelated hash for the SIDEWAYS drift, a third for the bend. With
      // one hash driving everything, whole diagonals moved together and the thing tore
      // like a sheet instead of coming apart (browser motion.js tileMotion twin).
      const double m = cellNoise(cx + 41, cy + 17);
      const double q = cellNoise(cx + 97, cy + 53);
      // Fall starts at the top; Gather at the bottom — it is Fall rewound, so the cell
      // that leaves first is the last one home.
      const double progress = rows_ > 1
          ? (sweep_ == Sweep::Fall ? double(cy) / (rows_ - 1)
                                   : double(rows_ - 1 - cy) / (rows_ - 1))
          : 0.0;
      const double delay = progress * 0.45 + n * 0.08;
      const bool gather = sweep_ == Sweep::Gather;
      double t = (t_ - delay) / std::max(0.05, 1.0 - delay);
      if (!gather && t <= 0.0) return false;   // not yet left: the picture
      if (gather && t >= 1.0) return false;    // landed: the picture
      t = std::clamp(t, 0.0, 1.0);
      // How far from home the cell is: 1 = out there, 0 = in place. Scattering runs
      // 0→1; gathering is the same journey read backwards, eased so a mote covers most
      // of the distance early and settles (browser dustEase).
      const double away = gather ? std::pow(1.0 - t, 3.0) : t;
      *out = Mote{};
      if (away >= 1.0) return true;   // gone, or not yet set off: cut, nothing to draw
      const QColor cell = grainColour(cx, cy, n);
      if (cell.alphaF() <= 0.02) return true;   // nothing was painted here
      // A falling image drops (and accelerates, hence away²); a gathering one comes
      // FROM below and rises home — the fall inverted. The sideways fan is signed, so
      // the cloud spreads both ways.
      const double drift = (22 + progress * 34 + n * 30) * spread_;
      const double tx = (m - 0.5) * 66 * spread_;
      const double ty = drift * 1.6;
      const double fall = sweep_ == Sweep::Fall ? away * away : away;
      const QPointF home(box.x() + (cx + 0.5) * cw, box.y() + (cy + 0.5) * ch);
      out->at = home + QPointF(away * tx, fall * ty) + swirlAt(away, tx, ty, q);
      out->radius = moteRadius(cw, ch, n) * (1.0 - away * (0.65 - n * 0.3));
      // No twinkle on a falling picture (browser drawDust has none); a styled one still
      // breathes, on the same fourth hash every cloud keys its style off.
      finishGrain(out, cell.alphaF() * (0.78 + n * 0.22) * (gather ? gatherAlpha(t) : scatterAlpha(t)),
                  false, cellNoise(cx + 13, cy + 71), t, away, tx, ty, gather);
      return true;
    }

    // One grain of a SURFACE flight — the Qt twin of browser motion.js surfaceMotion plus
    // the tileGatherSurface / tileScatterSurface keyframes. The path is the cell's own
    // offset to the target, so every mote converges there instead of falling; the two
    // decorrelated hashes only fan the arrival, the third bends it. The delay rides the
    // DISTANCE, so the edge nearest the point goes first and the far one last. Halved
    // for a SCATTER (browser motion.js surfaceMotion delayScale): held at its 0% pose
    // for up to 45% of the flight, a mote is indistinguishable from the surface not
    // having reacted yet — on a big surface (a full-height docked chat panel) that read
    // as nothing moving at all until a sudden, late flick, not sand leaving.
    // Same contract as fallingMote: false = the cell is the picture right now.
    bool surfaceMote(const QRectF& box, int cx, int cy, double cw, double ch, Mote* out) const {
      const double n = cellNoise(cx, cy);
      const double m = cellNoise(cx + 41, cy + 17);
      const double q = cellNoise(cx + 97, cy + 53);
      const QPointF home(box.x() + (cx + 0.5) * cw, box.y() + (cy + 0.5) * ch);
      const QPointF tgt = target_ + QPointF(shift_);
      const double toX = tgt.x() - home.x();
      const double toY = tgt.y() - home.y();
      // Normalised against the longest trip any cell in this box makes, so the sweep
      // fills the whole flight whatever the point's distance is.
      const double reach = std::hypot(toX, toY);
      const double far = std::hypot(box.width(), box.height()) + reach;
      const double progress = far > 0 ? std::min(1.0, reach / far) : 0.0;
      const bool gather = sweep_ == Sweep::SurfaceIn;
      const double delay = (progress * 0.45 + n * 0.12) * (gather ? 1.0 : 0.5);
      double t = (t_ - delay) / std::max(0.05, 1.0 - delay);
      if (!gather && t <= 0.0) return false;   // still the surface
      *out = Mote{};
      // A gathering grain is NOTHING until it sets off: parked at the point with hundreds
      // of others it filled the icon with a solid blob of the accent (user report).
      if (gather && t <= 0.0) return true;     // cut from the picture, nothing drawn yet
      t = std::clamp(t, 0.0, 1.0);
      if (!gather && t >= 1.0) return true;    // a scattered mote that has finished is gone
      const QColor cell = grainColour(cx, cy, n);
      if (cell.alphaF() <= 0.02) return true;
      // The whole cloud's own alpha (browser dustHostOut / dustHostIn): a forming cloud
      // stays whole until 58% of the span, then fades out under the surface fading up —
      // a LANDED mote sits at home at full size until then, a grain of the dot screen
      // the window is cross-fading out of, never a hole. A leaving cloud fades in over
      // the first beat, as the surface under it cuts out.
      // The cloud starts leaving exactly where the window starts fading UP (kDustHold),
      // so the two alphas stay complementary. They used to overlap for a few percent at
      // full strength each: harmless while a grain was the window's own pixel, but the
      // grains are the ACCENT now, so the overlap added coloured light and the window
      // flashed lighter just before it landed (user report).
      const double host = gather
          ? (t_ < kDustHold ? 1.0 : 1.0 - (t_ - kDustHold) / (1.0 - kDustHold))
          : std::min(1.0, t_ / kSurfaceScatterSplit);
      if (gather && t >= 1.0) {
        out->at = home;
        out->radius = moteRadius(cw, ch, n);
        finishGrain(out, cell.alphaF() * (0.78 + n * 0.22) * host, glintAt(cx, cy),
                    cellNoise(cx + 13, cy + 71), 1.0, 0.0, toX, toY, true);
        return true;
      }
      // A gathering mote waits at the point until its delay is up, which is what makes
      // the stream read as pouring out. The ramps ride the clock (browser
      // tileGatherSurface / tileScatterSurface), not the eased distance — see scatterAlpha.
      const double alpha = host * (gather ? (t < 0.45 ? 0.55 + 0.45 * (t / 0.45) : 1.0)
                                          : (t < 0.55 ? 1.0 - t * 0.18 : 0.9 * (1.0 - (t - 0.55) / 0.45)));
      const double tx = toX + (m - 0.5) * kSurfaceSpreadPx;
      const double ty = toY + (n - 0.5) * kSurfaceSpreadPx;
      // Two legs (the browser's mid keyframe): a gather flies far → bend over its first
      // 16% on the leg curve, then bend → home on the ease-out; a scatter home → bend
      // over 18%, then out. Most of the distance goes early either way — the bend sits
      // where it can be seen — and the turn is a flick, which is what sand does.
      const double w = cellNoise(cx + 13, cy + 71);
      const QPointF point = home + QPointF(tx, ty);
      const QPointF bend = home + waypointOf(tx, ty, q);
      const QPointF wobble = turbulenceAt(t, tx, ty, w);
      const double farScale = 0.12 + n * 0.25;
      const double midScale = 1.0 - (1.0 - farScale) * 0.5;
      if (gather) {
        out->at = legAt(t, kSurfaceGatherSplit, surfaceLegEase(), surfaceEase(), point, bend, home) + wobble;
        out->radius = moteRadius(cw, ch, n)
            * legScalar(t, kSurfaceGatherSplit, surfaceLegEase(), surfaceEase(), farScale, midScale, 1.0);
      } else {
        out->at = legAt(t, kSurfaceScatterSplit, surfaceLegEase(), surfaceEase(), home, bend, point) + wobble;
        out->radius = moteRadius(cw, ch, n)
            * legScalar(t, kSurfaceScatterSplit, surfaceLegEase(), surfaceEase(), 1.0, midScale, farScale);
      }
      finishGrain(out, cell.alphaF() * alpha * (0.78 + n * 0.22), glintAt(cx, cy), w,
                  t, gather ? 1.0 - t : t, tx, ty, gather);
      return true;
    }

   private:
    // A child widget is clipped by its parent, so a flight that leaves the host (a dialog
    // dragged off it or taller than it, a menu past its edge) was cropped at the window
    // border. When it needs room the host hasn't got, the layer becomes a frameless,
    // input-transparent top-level window spanning the whole trip; `host` stays its
    // QObject parent, and picture_/target_ stay in HOST coords via `shift_`.
    void placeForSurface(QWidget* host, const QRect& picture, const QPoint& target,
                         bool escapeHost, bool alwaysEscape = false) {
      const QRect hostBox(host->mapToGlobal(QPoint(0, 0)), host->size());
      QRect need = surfaceLayerRect(picture.translated(hostBox.topLeft()),
                                    host->mapToGlobal(target));
      // Nothing to escape for: it fits (unless the caller wants the top layer even
      // then — see overSurface's alwaysEscape), the caller draws inside the host by
      // design, or there is no desktop to escape ONTO — offscreen's virtual screen is
      // a fixed box unrelated to any real one, so the tests keep the plain child layer.
      if (!escapeHost || (hostBox.contains(need) && !alwaysEscape) || !hostBox.isValid()
          || QGuiApplication::platformName() == QLatin1String("offscreen")) {
        setGeometry(host->rect());
        return;
      }
      // Never bigger than the desktop it can actually be seen on.
      QRect desktop;
      for (const QScreen* s : QGuiApplication::screens()) desktop |= s->geometry();
      if (desktop.isValid()) need &= desktop;
      if (need.width() < 8 || need.height() < 8) { setGeometry(host->rect()); return; }
      // Above the host, never focusable/clickable, no shadow. Qt::ToolTip, not Qt::Window:
      // a plain top-level window steals an open QMenu/QComboBox popup's platform grab and
      // closes it instantly (confirmed live) — ToolTip is the same non-activating kind
      // AppTooltip already floats over an open menu with.
      setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                     | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus
                     | Qt::NoDropShadowWindowHint);
      setAttribute(Qt::WA_ShowWithoutActivating, true);
      shift_ = hostBox.topLeft() - need.topLeft();
      setGeometry(need);
    }

    // The snapshot, mixed towards `ink` — one pass over the picture, so every mote is
    // already lifted by the time it is drawn. An invalid ink leaves it exactly as taken.
    static QPixmap liftedToInk(const QPixmap& snap, const QColor& ink) {
      if (!ink.isValid() || snap.isNull()) return snap;
      QPixmap out = snap;
      QPainter p(&out);
      p.setCompositionMode(QPainter::CompositionMode_SourceAtop);   // tints, never spreads
      p.fillRect(out.rect(), QColor(ink.red(), ink.green(), ink.blue(),
                                    qRound(255 * kSurfaceInkMix)));
      return out;
    }

    DisintegrateOverlay(QWidget* host, const QPixmap& snap) : QWidget(host), snap_(snap) {
      setObjectName(kObjectName);   // findable without a Q_OBJECT (this class stays MOC-free)
      setAttribute(Qt::WA_TransparentForMouseEvents, true);
      setAttribute(Qt::WA_NoSystemBackground, true);
      setAttribute(Qt::WA_TranslucentBackground, true);
      hide();
    }

    // The clock: a plain timer at the screen's refresh interval (dustKit.hpp
    // frameIntervalMs) reading a wall clock, not a QVariantAnimation — Qt's animation
    // timer ticks 60 times a second whatever the display does, and a cloud at 60 on a
    // 120Hz screen read as the coarser thing next to the browser's. The per-cell delays
    // own the shaping; the clock is linear.
    void start(int ms = kMs) {
      ms_ = std::max(1, ms);
      clock_.start();
      auto* tick = new QTimer(this);
      tick->setTimerType(Qt::PreciseTimer);
      tick->setInterval(support::frameIntervalMs(this));
      connect(tick, &QTimer::timeout, this, [this, tick] {
        t_ = std::min(1.0, clock_.nsecsElapsed() / 1e6 / ms_);
        syncFollow();
        update();
        if (t_ >= 1.0) {
          tick->stop();
          deleteLater();
        }
      });
      tick->start();
    }

    // Dust motes sized on screen rather than as a share of the image, thinned back
    // if that would exceed the per-frame ceiling.
    void sizeGridForDust(const QSize& size, int maxCells = kDustMaxCells,
                         int cellPx = kDustCellPx) {
      // Water and fire grid coarser (browser motion.js makeDustStage does the same).
      if (style_ != support::ParticleStyle::Dust) cellPx = qRound(cellPx * support::kStyledCellScale);
      dustGrid(size, cellPx, maxCells, &cols_, &rows_);
    }

    void syncFollow() {
      if (!follow_ || !parentWidget()) return;
      const QPoint now = follow_->mapTo(parentWidget(), QPoint(0, 0));
      const QPoint delta = now - followAt_;
      if (delta.isNull()) return;
      followAt_ = now;
      if (picture_.isValid()) retarget(delta);
      else move(pos() + delta);
    }

    QPointer<QWidget> follow_;   // setFollow: the control this cloud stays anchored to
    QPoint followAt_;            // …and where it was (parent coords) at the last tick
    QPixmap snap_;
    QPixmap base_;          // the state left behind (overPixmaps only); null = nothing
    QImage cells_;          // snap_'s colour per grid cell (sampleCells); rebuilt when the grid changes
    std::vector<QColor> grains_;   // …and each cell's lifted grain colour, built with it
    std::vector<bool> glints_;     // …and whether it is a rim/glint cell (twinkles)
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
    support::MoteSprites sprites_;   // the grains, drawn once each and blitted
    QElapsedTimer clock_;   // start(): the wall clock the flight reads
    int ms_ = kMs;          // …and its length
    double spread_ = 1.0;   // throw distance, as a share of a list row's
    double t_ = 0.0;
  };

  // Grab-and-fly for the floating-tip family (tooltips, the export preview, popup
  // menus, the projects hover preview): photograph `subject` and fly it as surface
  // dust inside `host`, out of / into `originGlobal`. Handles the size gate, host
  // mapping and ink lift; `paintNow` paints the first frame synchronously (a closing
  // tip hands over in one beat); `alwaysEscape` lifts the cloud into the top-level
  // layer even when it would fit the host (a tip window floating ABOVE the host would
  // hide a child layer's motes — see overSurface). Callers keep their own pre-guards
  // (motion, owner visibility, subject state).
  inline DisintegrateOverlay* flyTipDust(QWidget* subject, QWidget* host,
                                         const QPoint& originGlobal, bool gather, int ms,
                                         bool escapeHost, bool paintNow = false,
                                         bool alwaysEscape = false) {
    if (!subject || !host || !host->isVisible()) return nullptr;
    const QRect target(subject->mapToGlobal(QPoint(0, 0)), subject->size());
    if (target.width() < 8 || target.height() < 8) return nullptr;
    const QPixmap shot = subject->grab();
    if (shot.isNull()) return nullptr;
    const QRect box(host->mapFromGlobal(target.topLeft()), target.size());
    DisintegrateOverlay* fx = DisintegrateOverlay::overSurface(
        shot, box, host, host->mapFromGlobal(originGlobal), gather, ms,
        subject->palette().color(QPalette::WindowText),
        DisintegrateOverlay::kSurfaceMaxCells, escapeHost, alwaysEscape);
    if (fx && paintNow) fx->repaint();
    return fx;
  }

}  // namespace stencil::gui
