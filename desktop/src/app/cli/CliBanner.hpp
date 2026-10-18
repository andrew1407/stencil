#pragma once
// The CLI stage's pinned logo as cells — the core's banner rows with a role per cell, the cat
// walking the frame under the meow skin — and the accent wipe's maths: the S icon turning like
// a clock hand, the seam's jumps, the wordmark's letters and the rule ahead. Ports of
// cli/src/console/render/logoFx/{clock,press}.zig; browser twin js/ui/cli/{banner,clock,wipe}.js.
#include "CliSkins.hpp"
#include <QChar>
#include <QPair>
#include <QVector>

namespace stencil::gui {

  enum class BannerRole { ACCENT, MARK, ART, PANEL };   // ART: the art frame's ground, PANEL the margin around it
  struct BannerCell {
    QChar ch;   // null: the second cell of a wide glyph before it
    BannerRole role = BannerRole::PANEL;
    bool wide = false;   // a fullwidth form takes two cells, as a terminal gives it
  };

  QVector<QVector<BannerCell>> bannerCells(const CliSkin* skin, bool compact, const CliSkinClock* clock);
  int bannerLabelRow(bool compact);     // the boxed row the wordmark sits beside
  int bannerHeaderRows(bool compact);   // frameHeight + 2 + 2 * curveRows

  // console/fx.json `wipe`, `press` and `rain`.
  struct CliFx {
    int wipeJumps = 14, wipeStepMs = 61, flourishStepMs = 115, ruleWipePct = 178, iconCols = 21, cellAspect = 2;
    double iconSpeed = 1.25;
    int pressMs = 95;
    int rainFrames = 90, rainFrameMs = 30, rainTrail = 14;
  };
  const CliFx& cliFx();

  namespace cliwipe {
    using Span = QPair<int, int>;   // [c0, c1)
    // Degrees clockwise from 12 o'clock; rows count cellAspect times as far as columns.
    double cellAngle(int row, int col, double cy, double cx);
    // The stretches of one row a hand `deg` into its turn has passed: 0, 1 or 2 of them.
    QVector<Span> sweptSpansAt(int row, int cols, double cy, double cx, double deg);
    // How far round the hand has come at seam column `x` of `reach`: one turn, iconSpeed times as fast.
    double iconDegrees(int x, int reach);
    // Which columns of header row `row` (1-based) wear the new accent at seam `x`.
    QVector<Span> iconSpans(int row, int headerRows, int cols, double deg, int x);
    struct RowSpan {
      int row, c0, c1;
    };
    QVector<RowSpan> wipeFrame(int headerRows, int cols, int x, int reach);
    // How far the rule under the header has turned over at seam `x`: it runs ahead of the seam.
    int ruleReach(int cols, int x, int reach);
    struct Event {
      int atMs;
      bool wash;   // else a letter
      int value;   // the seam x, or the letter index
    };
    QVector<Event> wipeSchedule(int reach, int letters);
  }  // namespace cliwipe

}  // namespace stencil::gui
