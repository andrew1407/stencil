#pragma once
// Every number the dust family runs on: the floating-tip clocks its callers share, and
// DisintegrateOverlay's own grid, flight and mote tunings, which it inherits as a base.

namespace stencil::gui {

  // The floating-tip clock family (browser controlTooltip.js / export/preview.js).
  inline constexpr int TIP_DUST_IN_MS = 213;
  inline constexpr int TIP_DUST_OUT_MS = 157;
  // browser surfaceForm's invisible hold while motes gather; surfaceLeave's hand-over beat.
  inline constexpr double DUST_HOLD = 0.55;
  inline constexpr int DUST_HAND_OVER_MS = 60;
  /* A TOOLTIP runs the same clock 1.5x slower — deliberately off the browser's pace. It shows
   * and hides on every hover, and at the shared speed the arrival reads as a flicker. */
  inline constexpr int TOOLTIP_DUST_IN_MS = 320;
  inline constexpr int TOOLTIP_DUST_OUT_MS = 236;
  inline constexpr int TOOLTIP_FADE_MS = 135;
  inline constexpr int TOOLTIP_HAND_OVER_MS = 90;
  /* The CANVAS image landing and being cleared. Faster than the shared DUST_MS below --
   * a full-viewport cloud at the list-row pace reads as a wait -- and deliberately the SAME
   * number as the browser's GHOST_MS, not the 1.5x the other dust clocks keep. */
  inline constexpr int CANVAS_DUST_MS = 760;

  // DisintegrateOverlay's own tunings, held apart so the class is declarations. It INHERITS
  // this, so every DisintegrateOverlay::DUST_MS spelling keeps resolving.
  struct DisintegrateTunings {
    // Every dust clock runs 1.5x faster than its browser twin (DISINTEGRATE_MS 1650).
    static constexpr int DUST_MS = 1100;
    static constexpr int ITEM_MS = DUST_MS;         // browser ITEM_DUST_MS
    static constexpr int CONN_MS = DUST_MS * 2 / 3; // browser CONN_DUST_MS
    static constexpr int COLS = 22;     // browser DISINTEGRATE_COLS
    static constexpr int ROWS = 11;     // browser DISINTEGRATE_ROWS
    static constexpr int DUST_CELL_PX = 7;   // browser surface/motion.js MOTE_PX — keep the two in step
    static constexpr int DUST_MAX_CELLS = 7000;
    static constexpr const char* OBJECT_NAME = "stencilDisintegrate";
    // Surface flights (browser surface/motion.js surfaceIn / surfaceOut): every mote aims at ONE point.
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
    // The bend off the throw line (browser surface/motion.js tileWaypoint), peaking mid-flight.
    static constexpr double SWIRL_SHARE = 0.32;
    static constexpr double SWIRL_MAX_PX = 44;
    static constexpr double WAYPOINT_ALONG = 0.62;   // browser WAYPOINT_ALONG: the two-leg turn
    static constexpr int MIN_TILE_MS = 160;           // browser MIN_TILE_MS: a late mote's floor
    // Turbulence and twinkle (browser dust/cloud.js turbulenceAt / twinkleAt), off a fourth hash.
    static constexpr double TURBULENCE_SHARE = 0.06;   // of the throw…
    static constexpr double TURBULENCE_MAX_PX = 6;      // …capped
    static constexpr double TURBULENCE_WAVES[2] = {2.5, 4.5};   // waves per flight, by hash
    static constexpr double TWINKLE_DEPTH = 0.35;      // a glint's brightness swing
    static constexpr double TWINKLE_HZ[2] = {4, 7};    // …flickers a second, by hash
    // Alpha is coverage lifted: text covers a third of its cells (browser speckPainter "never faint").
    static constexpr double COVERAGE_LIFT = 2.5;

    static constexpr double SURFACE_GATHER_SPLIT = 0.16;
    static constexpr double SURFACE_SCATTER_SPLIT = 0.18;
    static constexpr double ROW_SPLIT = 0.38;
    // Accent motes over a near-opaque window "blink lighter" at the hand-off, so the
    // gather fades out early and the scatter waits for the window to cut out.
    static constexpr double SURFACE_MOTE_FADE_FRAC = 0.55;   // of the post-hold span (in)
    static constexpr double SURFACE_MOTE_RISE_DELAY = 0.5;   // × split before motes rise (out)

    // Slack past the picture/target: SURFACE_SPREAD_PX of jitter plus a rotated cell's corner.
    static constexpr int SURFACE_PAD_PX = 64;
  };

}  // namespace stencil::gui
