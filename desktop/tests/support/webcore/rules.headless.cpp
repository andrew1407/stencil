// The webcore skin against the browser's (support/webcore/*): the same cells, the same word,
// the same colours — sample values printed from node (browser/tests/ui/webcore/rules.test.js) —
// the icon set over every glyph, and the overlay with no token left unfilled.
#include "../../../src/support/webcore/icons.hpp"
#include "../../../src/support/webcore/image.hpp"
#include "../../../src/support/webcore/rules.hpp"
#include "../../../src/support/webcore/stylesheet.hpp"
#include "skinPrefs.hpp"
#include "theme.hpp"

#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <cmath>
#include <cstdio>

#include "../../support/check.hpp"

using namespace stencil::support;

namespace {
  bool near(double a, double b) { return std::abs(a - b) < 1e-6; }
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  const WebcoreConfig& cfg = webcoreConfig();

  std::printf("the table:\n");
  {
    check(cfg.tokens[0].size() == 18 && cfg.tokens[1].size() == 18, "webcore.json qrc alias resolves: 18 tokens a theme");
    check(cfg.tokens[0].value("--wc-desktop").name() == "#008080", "the light desktop is teal");
    check(cfg.tokens[1].value("--wc-face").name() == "#3c3c3c", "the dark face is read too");
    check(cfg.text == "STENCIL" && cfg.colors.size() == 7 && cfg.glyphs.size() == 7, "the word, its colours, its glyphs");
    check(!cfg.offToast.isEmpty() && cfg.projectName == "webcore" && cfg.imageName == "webcore.png", "the strings");
    check(cfg.fontFamilies.size() == 6 && cfg.fontPx == 15, "the face list and its size");
  }

  std::printf("the picture (printed from node):\n");
  {
    const CellGrid g = cellGrid();
    check(g.cell == 4 && g.cols == 256 && g.rows == 192 && g.horizon == 111, "the grid");
    check(hillTopCell(0, g) == 103 && hillTopCell(60, g) == 97 && hillTopCell(160, g) == 111 && hillTopCell(255, g) == 98, "the hill");
    check(cellColorAt(0, 0, g) == "#0000a8" && cellColorAt(128, 60, g) == "#4898f0", "the sky bands");
    check(cellColorAt(30, 100, g) == "#5cc85c" && cellColorAt(200, 150, g) == "#149614", "the grass");
    check(cellColorAt(100, 120, g) == "#5cc85c" && cellColorAt(101, 120, g) == "#30b030", "the dither");
    const QVector<CellRun> blocks = cloudBlocks();
    check(blocks.size() == 18 && blocks.first().x == 28 && blocks.first().y == 26 && blocks.first().w == 26, "the clouds");
    int covered = 0;
    for (const CellRun& r : cellRuns(g)) covered += r.w * r.h;
    check(covered == g.cols * g.rows, "the runs cover every cell once");
    const QImage img = paintWebcoreImage();
    check(img.size() == QSize(1024, 768), "the painter's size");
    const struct { int x, y; const char* hex; const char* what; } PIXELS[] = {
        {0, 0, "#0000a8", "sky"}, {160, 120, "#ffffff", "cloud"}, {160, 140, "#0050d8", "sky under it"},
        {512, 767, "#0a7a0a", "the foot"}, {1023, 300, "#70b8f8", "the pale band"},
        {400, 600, "#149614", "the middle grass"}};
    for (const auto& px : PIXELS)
      check(pixelColorAt(px.x, px.y) == px.hex && img.pixelColor(px.x, px.y).name() == px.hex, px.what);
  }

  std::printf("the word:\n");
  {
    const QVector<WordLine> lines = wordLines(1024, 768);
    check(lines.size() == 7, "seven lines");
    QSet<QString> colors;
    bool sky = true;
    for (const WordLine& l : lines) {
      colors.insert(l.fillColor);
      for (const QPointF& p : l.points) sky = sky && p.y() < 768 * 0.58;
    }
    check(colors.size() == 7 && sky, "one colour each, all in the sky");
    check(near(lines[0].points[0].x(), 102.4) && near(lines[0].points[0].y(), 154.04), "S starts at the top-left cell");
    check(near(lines[0].points[6].x(), 189.55) && near(lines[0].points[6].y(), 276.04), "…and reaches its foot");
    check(near(lines[6].points[3].x(), 921.6) && near(lines[6].points[3].y(), 258.61), "L's foot");
    check(lines[3].points.size() == 10 && lines[0].color == "#000000" && lines[0].thickness == 2, "N's diagonals, the stroke");
  }

  std::printf("the icons:\n");
  {
    QFile f(":/config/icons.json");
    check(f.open(QIODevice::ReadOnly), "icons.json reads");
    const QJsonObject canon = QJsonDocument::fromJson(f.readAll()).object();
    bool all = true;
    for (auto it = canon.begin(); it != canon.end(); ++it) all = all && hasPixelIcon(it.key());
    check(all && canon.size() >= 60, "every canon glyph has a pixel twin");
    for (const char* extra : {"power", "search", "more-vertical", "logo", "draw-mode-line", "draw-mode-rect"})
      check(hasPixelIcon(QLatin1String(extra)), extra);
    const QString doc = pixelIconSvg("save");
    check(doc.startsWith("<svg xmlns") && doc.contains("shape-rendering=\"crispEdges\"") && doc.contains("<rect"), "a crisp rect document");
    check(!doc.contains("currentColor") && pixelIconSvg("nosuch").isEmpty(), "colour baked; an unknown name is empty");
  }

  std::printf("the sheet and the palette:\n");
  {
    static const QRegularExpression token(QStringLiteral("%[A-Z0-9_]+%"));
    for (const bool dark : {false, true}) {
      const QString overlay = webcoreOverlay(dark);
      check(overlay.size() > 5000 && !token.match(overlay).hasMatch(), "every %WC_*% in webcore.qss is one the skin fills");
      if (token.match(overlay).hasMatch()) std::printf("       unfilled: %s\n", qPrintable(token.match(overlay).captured(0)));
      const QString base = stencil::gui::buildStylesheet(dark, "violet");
      check(buildWebcoreStylesheet(dark, "violet").startsWith(base) && buildWebcoreStylesheet(dark, "violet") != base,
            "the skin's sheet is the app's with the overlay after it");
    }
    check(webcoreOverlay(false).contains("#c0c0c0") && webcoreOverlay(true).contains("#3c3c3c"), "the overlay wears the theme's face");
    const stencil::gui::Palette light = webcorePalette(false);
    check(light.bgPage.name() == "#008080" && light.accent.name() == "#000080" && light.textMain.name() == "#000000", "the light palette");
    check(webcorePalette(true).inputBg.name() == "#202020", "the dark palette");
  }

  std::printf("the session switches:\n");
  {
    check(skin() == Skin::DEFAULT && !isWebcore() && !forcedDark().has_value(), "off at boot");
    const int gen = skinGeneration();
    setSkin(Skin::WEBCORE);
    check(isWebcore() && skinGeneration() == gen + 1, "on bumps the generation");
    setSkin(Skin::WEBCORE);
    check(skinGeneration() == gen + 1, "…once per change");
    setForcedDark(false);
    check(forcedDark().has_value() && !*forcedDark(), "a forced light");
    clearForcedDark();
    setSkin(Skin::DEFAULT);
    check(!forcedDark().has_value() && !isWebcore() && skinGeneration() == gen + 2, "off again");
    check(stencil::gui::themePalette(false, "violet").bgPage.name() == "#f0f0f0", "no hook: the app's own palette");
    setSkinPalette(&webcorePalette);
    check(stencil::gui::themePalette(false, "violet").bgPage.name() == "#008080", "hooked: the skin's");
    setSkinPalette(nullptr);
  }

  std::printf(failures ? "FAILED: %d\n" : "all passed (%d failures)\n", failures);
  return failures ? 1 : 0;
}
