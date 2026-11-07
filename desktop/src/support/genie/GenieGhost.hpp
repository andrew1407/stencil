#pragma once
// The genie flight: a snapshot poured into (or out of) its icon along genieWarp.hpp's funnel, on a
// ghost that spans both. The Sliding motion mode's window open/close (support/modal/modalRevealFlight.hpp).
// Browser port: js/ui/motion/genie/flight.js.
#include "genieWarp.hpp"
#include "stripPaint.hpp"

#include <QEasingCurve>
#include <QImage>
#include <QPixmap>
#include <QRegion>
#include <QVector>
#include <QWidget>
#include <functional>
#include <optional>

namespace stencil::support::genie {

  // The tuned lengths at the default speed (ms): the open is the unhurried half.
  inline constexpr int OPEN_MS = 560;
  inline constexpr int CLOSE_MS = 480;
  // Room past the union of window and icon, so the funnel's rounding never clips (px).
  inline constexpr int MARGIN_PX = 4;
  // Strips per damage rect: a frame repaints the funnel's footprint, not the ghost's whole box, so the
  // widgets underneath (the canvas, a sliding dock) repaint only where the picture was or now is.
  inline constexpr int DAMAGE_STRIPS = 8;
  // A fold's space beside its flight: closing, it waits for the picture's far edge to start moving (the warp's
  // LAG through the close's InOutSine, a time share); the flight outlasts the slide so it covers the landing.
  inline constexpr double CLOSE_SPACE_WAIT = 0.4;
  inline constexpr int SPACE_TAIL_MS = 40;
  struct SpaceClock { int delayMs; int ms; };
  inline SpaceClock spaceClock(bool isOpening, int ms) {
    const int wait = isOpening ? 0 : static_cast<int>(ms * CLOSE_SPACE_WAIT);
    return {wait, ms - wait};
  }

  class GenieGhost : public QWidget {
   public:
    // `win` and `icon` are in the coordinates of the ghost's parent (global for a window ghost).
    GenieGhost(QWidget* parent, QPixmap shot, const QRect& win, const QRect& icon, Side side);
    QRect span() const { return win.united(icon).adjusted(-MARGIN_PX, -MARGIN_PX, MARGIN_PX, MARGIN_PX); }
    void place() { setGeometry(span()); }
    // Re-cuts the bands for progress `p` and repaints only where the picture was or now is.
    void setProgress(double p);

   protected:
    void paintEvent(QPaintEvent* e) override;

   private:
    // A sideways flow still cut in many full-height columns: painted transposed (stripPaint.hpp).
    bool isTransposing() const { return (side == Side::RIGHT || side == Side::LEFT) && drawn.size() > TRANSPOSE_MIN_DRAWS; }
    // The strips' cover in parent coordinates, DAMAGE_STRIPS to a rect, a pixel past the seam overlap.
    QRegion footprint() const;

    QPixmap shot;
    QRect win;
    QRect icon;
    Side side;
    QVector<Strip> strips;
    QVector<Strip> drawn, drawnT;   // `strips` merged for painting, and transposed for a sideways flow
    QImage shotT;                   // `shot` transposed, made at the first sideways paint
    QImage scratch;                 // drawStripsAcross's column buffer, kept across frames
    QRegion painted;   // the last frame's footprint, cleared by the next
  };

  // The flight's curve, which a fold's space slide shares so the two land together.
  inline QEasingCurve flightEase(bool isOpening) { return isOpening ? QEasingCurve::OutSine : QEasingCurve::InOutSine; }

  // Steps `ghost` end to end on the app's frame pulse (support/vsync), after the space beside it, on
  // the clock alone so a slow frame never slows the flight; then runs `done` and retires the ghost.
  void runGhost(GenieGhost* ghost, bool isOpening, int ms, std::function<void()> done);

  // Plays the flight on `host` (a child ghost) or as its own frameless window (`asWindow`, rects GLOBAL then); false,
  // with nothing shown, without a host or picture. Without `ms` the window lengths play, OPEN_MS / CLOSE_MS.
  bool fly(QWidget* host, const QPixmap& shot, const QRect& winGlobal, const QRect& iconGlobal,
           bool isOpening, bool asWindow, std::function<void()> done, std::optional<int> ms = std::nullopt);

}  // namespace stencil::support::genie
