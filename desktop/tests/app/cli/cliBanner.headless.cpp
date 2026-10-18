// The CLI stage's banner and skins (app/cli/CliBanner, CliSkins): the cells out of the core's
// rows with their roles, the clock-hand wipe's quadrants and schedule, the skin table over
// skins.json, its hues, pictures and the cat's walk. Browser twins: tests/ui/cli/{banner,clock,wipe}.test.js,
// tests/ui/cli/skin/{table,pictures}.test.js.
#include "CliBanner.hpp"
#include "CliSkins.hpp"

#include <QGuiApplication>
#include <cmath>
#include <cstdio>

#include "../../support/check.hpp"

using namespace stencil::gui;

namespace {
  bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }
  QString text(const QVector<QVector<BannerCell>>& cells) {
    QString out;
    for (const auto& row : cells) {
      for (const BannerCell& c : row) out += c.ch;
      out += QLatin1Char('\n');
    }
    return out;
  }
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);

  std::printf("the banner cells:\n");
  {
    const QVector<QVector<BannerCell>> cells = bannerCells(nullptr, false, nullptr);
    check(cells.size() == bannerHeaderRows(false) && cells.size() > 6, "the boxed S banner is as tall as its layout says");
    int accent = 0, mark = 0;
    for (const auto& row : cells)
      for (const BannerCell& c : row) {
        accent += c.role == BannerRole::ACCENT;
        mark += c.role == BannerRole::MARK;
      }
    check(accent > 20 && mark > 10, "borders wear the accent, the mark its own role");
    check(bannerLabelRow(false) > 0 && bannerLabelRow(false) < cells.size(), "the wordmark sits beside a body row");
    const QVector<QVector<BannerCell>> compact = bannerCells(nullptr, true, nullptr);
    check(compact.size() == bannerHeaderRows(true) && compact.size() < cells.size(), "the compact frame is shorter");
    const CliSkin* pie = cliSkinByWord("pieday");
    check(pie && pie->mark == "P", "the pie skin draws the P");
    const QVector<QVector<BannerCell>> pieCells = bannerCells(pie, false, nullptr);
    check(pieCells.size() == cells.size() && text(pieCells) != text(cells), "…so its banner differs in the mark alone");
  }

  std::printf("the clock hand:\n");
  {
    using namespace cliwipe;
    check(near(cellAngle(5, 11, 5, 11), 0), "the centre is at twelve");
    check(near(cellAngle(1, 11, 5, 11), 0), "straight up is twelve");
    check(near(cellAngle(5, 21, 5, 11), 90), "to the right is three");
    check(near(cellAngle(9, 11, 5, 11), 180), "straight down is six");
    check(near(cellAngle(5, 1, 5, 11), 270), "to the left is nine");
    check(sweptSpansAt(5, 21, 5, 11, 0).isEmpty(), "nothing swept at the start");
    check(sweptSpansAt(5, 21, 5, 11, 360) == QVector<Span>{Span(0, 21)}, "a full turn sweeps the row");
    const QVector<Span> half = sweptSpansAt(5, 21, 5, 11, 180);
    check(half.size() == 1 && half.first().first >= 10 && half.first().second == 21, "half a turn has swept the right half of the middle row");
    check(near(iconDegrees(0, 100), 0) && near(iconDegrees(100, 100), 360), "the hand starts at twelve and ends there");
    check(near(iconDegrees(40, 100), 180), "it turns iconSpeed times as fast as the seam (1.25 × 40% = half a turn)");
    const QVector<RowSpan> frame = wipeFrame(10, 60, 30, 60);
    bool pastIcon = false;
    for (const RowSpan& s : frame) pastIcon = pastIcon || (s.c0 == 21 && s.c1 == 30);
    check(pastIcon, "past the icon the seam itself paints the columns");
    check(ruleReach(100, 50, 100) == 89 && ruleReach(100, 100, 100) == 100, "the rule runs 178% ahead of the seam, capped at the width");
    const QVector<Event> events = wipeSchedule(70, 7);
    int washes = 0, letters = 0;
    bool ordered = true;
    for (int i = 0; i < events.size(); ++i) {
      washes += events[i].wash;
      letters += !events[i].wash;
      if (i) ordered = ordered && events[i].atMs >= events[i - 1].atMs;
    }
    check(washes == 13 && letters == 7 && ordered, "fourteen jumps make thirteen washes, each letter its own beat, in time order");
    check(cliFx().wipeStepMs == 61 && cliFx().flourishStepMs == 115 && cliFx().pressMs == 95 && cliFx().rainFrames == 90, "console/fx.json wipe, press and rain are read");
  }

  std::printf("the skins:\n");
  {
    check(cliSkins().size() == 8, "eight skins out of skins.json");
    check(cliListWord() == "eastereggs" && cliGold() == QColor(0xff, 0xd7, 0x00) && cliFrameMs() == 80, "the list word, the gold and the frame clock");
    const CliSkin* matrix = cliSkinByWord("MrAnderson");
    check(matrix && matrix->paintsCells && matrix->fg == QColor("#23d18b") && matrix->bg == QColor(Qt::black), "the matrix skin paints its cells, any casing");
    check(cliSkinByWord("nope") == nullptr, "an unknown word names no skin");
    const CliSkin* bifrost = cliSkinByWord("bifrost");
    check(bifrost && bifrost->paintsRuns && bifrost->slides && bifrost->mark == "M" && cliWordmark(bifrost) == "M I D G A R D", "the bifrost paints runs that slide under its own wordmark and M");
    check(cliWordmark(nullptr) == "S T E N C I L", "no skin: the plain wordmark");
    const CliSkin* rainbow = cliSkinByWord("sunafterrain");
    check(rainbow && cliHueAt(*rainbow, 0, 0) == QColor::fromHsvF(0, 0.75, 1) && cliHueAt(*rainbow, 1, 0) == QColor::fromHsvF(22 / 360.0, 0.75, 1),
          "a run's hue steps 22° a column");
    check(cliHueAt(*bifrost, 3, 3) == cliHueAt(*bifrost, 0, 0), "the bifrost slides a column a frame");
    CliSkinClock clock;
    clock.start(123456);
    check(clock.frame == 0 && !clock.sync(123456 + 79), "under a frame the clock stands");
    check(clock.sync(123456 + 80) && clock.frame == 1, "a frame later it moved");
    const std::optional<QColor> bulb = clock.nextBulb();
    check(bulb.has_value() && !clock.nextBulb().has_value(), "a bulb lights, then waits its turn");
    const CliSkin* fruit = cliSkinByWord("theyareinthetrees");
    const int a = cliPictureAt(*fruit, 7, 3, -1, clock), b = cliPictureAt(*fruit, 7, 3, -1, clock);
    check(a == b && !cliPicture(*fruit, a).isEmpty(), "the same frame draws the same picture");
    check(cliPictureAt(*fruit, 7, 3, a, clock) != a, "…and never the previous letter's picture");
    const CliSkin* pie = cliSkinByWord("pieday");
    bool pies = true;
    for (int c = 0; c < 20; ++c) pies = pies && cliPictureAt(*pie, 7, c, -1, clock) <= 1;
    check(pies, "the pie skin draws its two pies");
    const CliSkin* meow = cliSkinByWord("meow");
    const CatPose pose = cliCatPose(14, 4, clock);
    check(meow && meow->movesLogo && !pose.text.isEmpty() && pose.col >= 0 && pose.col + pose.cols <= 14 && pose.row >= 0 && pose.row < 4,
          "the cat stands inside the frame");
    const QVector<QVector<BannerCell>> walked = bannerCells(meow, false, &clock);
    int cats = 0;
    for (const auto& row : walked)
      for (const BannerCell& c : row) cats += c.role == BannerRole::MARK;
    check(cats > 0 && cats <= 9, "the meow banner shows the cat in place of the mark");
    CliSkinClock wideClock;
    wideClock.start(3);   // the face family with the fullwidth backtick
    bool wideSeen = false, secondHalf = false;
    const int rowWidth = bannerCells(nullptr, false, nullptr).at(2).size();   // a body row: border, margin, art, margin, border
    int catRowWidth = -1;
    for (const auto& row : bannerCells(meow, false, &wideClock)) {
      for (int c = 0; c < row.size(); ++c)
        if (row[c].role == BannerRole::MARK && row[c].wide) {
          wideSeen = true;
          catRowWidth = row.size();
          secondHalf = c + 1 < row.size() && row[c + 1].ch.isNull();
        }
    }
    check(wideSeen && secondHalf && catRowWidth == rowWidth, "a fullwidth glyph in the face takes two cells, the next one empty, the row no wider");
  }

  std::printf("the restyle:\n");
  {
    const QColor accent(0x7c, 0x3a, 0xed);
    const CliSkin* matrix = cliSkinByWord("mranderson");
    const CliSkin* meow = cliSkinByWord("meow");
    const CliSkin* rainbow = cliSkinByWord("sunafterrain");
    const CliSkin* fairy = cliSkinByWord("fairylight");
    check(cliAccentUnder(matrix, accent) == matrix->fg && cliAccentUnder(cliSkinByWord("theverybluescreen"), accent) == QColor(Qt::white),
          "under a cell painter the accent is its fg");
    check(cliAccentUnder(meow, accent) == cliGold() && cliAccentUnder(cliSkinByWord("pieday"), accent) == cliGold(), "under a picture skin the accent is gold");
    check(cliAccentUnder(rainbow, accent) == accent && cliAccentUnder(fairy, accent) == accent && cliAccentUnder(nullptr, accent) == accent,
          "the rainbow and the lights leave it, as does no skin");
    int restyled = 0;
    for (const CliSkin& s : cliSkins()) restyled += cliRestyles(&s);
    check(restyled == 7 && !cliRestyles(fairy) && !cliRestyles(nullptr), "every skin but the fairy lights re-dresses the rows (skin.zig restyles)");
    check(cliSwappable(QLatin1Char('/')) && cliSwappable(QLatin1Char('a')) && !cliSwappable(QLatin1Char(' ')) && !cliSwappable(QChar(0x2501)),
          "printable ASCII swaps for a picture, a space and a box glyph stay");
    check(cliRowKey("/open") == cliRowKey("/open") && cliRowKey("/open") != cliRowKey("/save"), "a row key is stable and tells rows apart");
  }

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}
