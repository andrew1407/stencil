// The aqua skin against the browser's (support/aqua/*): the same wallpaper cells, the same word,
// the same colours — sample values printed from node (browser/tests/ui/aqua/rules.test.js) — and
// the overlay with no token left unfilled.
#include "../../../src/support/aqua/wallpaper.hpp"
#include "../../../src/support/aqua/table.hpp"
#include "../../../src/support/aqua/sheet.hpp"
#include "../../../src/support/aqua/still.hpp"
#include "skinPrefs.hpp"
#include "theme.hpp"

#include <QApplication>
#include <QRegularExpression>
#include <cmath>
#include <cstdio>

#include "../../support/check.hpp"

using namespace stencil::support;

namespace {
  bool near(double a, double b) { return std::abs(a - b) < 1e-6; }
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  const AquaConfig& cfg = aquaConfig();

  std::printf("the table:\n");
  {
    check(cfg.tokens[0].size() == 26 && cfg.tokens[1].size() == 26, "aqua.json qrc alias resolves: 26 tokens a theme");
    check(cfg.tokens[0].value("--aq-select").name() == "#2563eb", "the selection blue");
    check(cfg.tintBase == "#2563eb" && cfg.tint[0].size() == 8 && cfg.tint[1].size() == 8, "the tint recipe");
    check(cfg.tokens[1].value("--aq-face").name() == "#2d3036", "the graphite face is read too");
    check(cfg.aliases.value("--bg-keywords") == "--aq-window" && cfg.aliases.value("--accent") == "--aq-select"
          && cfg.aliases.size() > 40, "the base tokens the skin re-points");
    check(cfg.colors.size() == 7 && cfg.swooshes.size() == 3 && cfg.ramp.size() == 12, "the word's colours, the swooshes, the ramp");
    check(!cfg.offToast.isEmpty() && cfg.projectName == "somefruitcompany" && cfg.imageName == "somefruitcompany.png", "the strings");
    check(cfg.fontFamilies.first() == "Lucida Grande" && cfg.fontPx == 13, "the face list and its size");
  }

  std::printf("the gel an accent paints (browser rules.js gelTokens):\n");
  {
    check(mixHex(QColor("#2563eb"), QColor("#ffffff"), 0.35).name() == "#719af2", "mixHex rounds each channel");
    for (const bool dark : {false, true}) {
      const QHash<QString, QColor> gel = gelTokens(QColor("#2563eb"), dark);
      bool same = gel.size() == 10;
      for (auto it = gel.begin(); it != gel.end(); ++it)
        same = same && cfg.tokens[dark ? 1 : 0].value(it.key()) == it.value();
      check(same, dark ? "the Blue preset paints the table's own dark gel" : "the Blue preset paints the table's own gel");
      check(gelTokens(QColor(), dark) == gel, "no accent is the base");
    }
    const QHash<QString, QColor> violet = gelTokens(QColor("#7c3aed"), false);
    check(violet.value("--aq-gel-bottom").name() == "#7c3aed" && violet.value("--aq-gel-top").name() == "#aa7ff3",
          "violet: the accent itself, and 35% toward white");
    check(violet.value("--aq-on-select").name() == "#ffffff", "white ink on violet");
    check(gelTokens(QColor("#facc15"), false).value("--aq-on-select").name() == "#1a1a1a", "dark ink on a light accent");
    const auto onGel = [](const char* hex, bool dark) { return gelTokens(QColor(hex), dark).value("--aq-on-gel").name(); };
    check(onGel("#2563eb", false) == "#ffffff" && onGel("#2563eb", true) == "#ffffff", "white on Blue's gel, each face");
    check(onGel("#7c3aed", false) == "#ffffff" && onGel("#550000", false) == "#ffffff" && onGel("#eab308", true) == "#1a1a1a",
          "white on violet and maroon gel, dark on yellow");
    check(aquaLogoArt(QColor("#7c3aed"), false).contains("#7c3aed") && !aquaLogoArt(QColor("#7c3aed"), false).contains("%1"),
          "the header mark wears the accent's gel");
  }

  std::printf("the wallpaper (printed from node):\n");
  {
    const WallGrid g = wallGrid();
    check(g.cell == 2 && g.cols == 512 && g.rows == 384, "the grid");
    check(wallColorAt(0, 0, g) == "#0d2f7a" && wallColorAt(511, 383, g) == "#1d54a2", "the ramp's ends");
    check(wallColorAt(256, 100, g) == "#1d51ab" && wallColorAt(400, 300, g) == "#2d6bbd", "the ramp between");
    check(wallColorAt(260, 200, g) == "#5d9be6" && wallColorAt(260, 184, g) == "#5d9be6", "a swoosh's core");
    check(wallColorAt(260, 180, g) == "#2c69c3" && wallColorAt(260, 181, g) == "#4a8ad9", "its rim dithers");
    check(wallRuns(g).size() == 7044, "the same runs");
    const QImage img = paintAquaImage();
    check(img.size() == QSize(1024, 768) && img.pixelColor(520, 400).name() == "#5d9be6"
          && img.pixelColor(0, 0).name() == "#0d2f7a", "the painted image");
  }

  std::printf("the word (printed from node):\n");
  {
    const QVector<WordLine> lines = aquaWordLines(1024, 768);
    check(lines.size() == 7, "seven letters");
    check(near(lines[0].points[0].x(), 102.4) && near(lines[0].points[0].y(), 169.4)
          && near(lines[0].points[1].x(), 189.55), "S where the browser puts it");
    check(near(lines[6].points.last().x(), 834.45) && near(lines[6].points.last().y(), 291.4), "L's last point");
    check(lines[3].fillColor == "#7ccf3a" && lines[0].color == "#0b2a6e", "the gel colours and the stroke");
  }

  std::printf("the sheet and the palette:\n");
  {
    static const QRegularExpression token(QStringLiteral("%[A-Z0-9_]+%"));
    for (const bool dark : {false, true}) {
      const QString overlay = aquaOverlay(dark);
      check(overlay.size() > 3000 && !token.match(overlay).hasMatch(), "every %AQ_*% in the aqua sheet is one the skin fills");
      if (token.match(overlay).hasMatch()) std::printf("       unfilled: %s\n", qPrintable(token.match(overlay).captured(0)));
      const QString base = stencil::gui::buildStylesheet(dark, "blue");
      const QString sheet = buildAquaStylesheet(dark, "blue");
      check(sheet.startsWith(withoutHover(base)) && sheet.size() > withoutHover(base).size(),
            "the skin's sheet is the app's, hover taken out, with the overlay after it");
      static const QRegularExpression hovered(QStringLiteral("[^,{}]*(:hover|\\[hovered=\"true\"\\])[^,{}]*"));
      bool still = true;
      // next() on every pass: a short-circuited call never advanced the iterator, and a failure hung.
      for (auto it = hovered.globalMatch(sheet); it.hasNext();) still = !isHoverSelector(it.next().captured(0).trimmed()) && still;
      check(still, "no hover selector is left but a menu or popup-list row's");
    }
    check(aquaOverlay(false).contains("#719af2") && aquaOverlay(true).contains("#467aee"), "the overlay wears the theme's gel");
    check(aquaOverlay(false, "violet").contains("#aa7ff3") && !aquaOverlay(false, "violet").contains("#719af2"),
          "the overlay wears the chosen accent's gel");
    buildAquaStylesheet(false, "violet");
    check(skinAccent().name() == "#7c3aed" && aquaPalette(false).accent.name() == "#7c3aed", "the palette follows the accent");
    check(skinDropGlow().ring.name() == "#7c3aed", "the drop ring is the accent's gel edge");
    buildAquaStylesheet(false, "blue");
    check(aquaPalette(false).bgPage.name() == "#2c63bf" && aquaPalette(false).accent.name() == "#2563eb", "the light palette");
    check(aquaPalette(true).inputBg.name() == "#1e2024", "the graphite palette");
    const int gen = skinGeneration();
    setSkin(Skin::AQUA);
    check(isAqua() && !isWebcore() && skinGeneration() == gen + 1, "the aqua skin is its own state");
    setSkin(Skin::DEFAULT);
  }

  std::printf(failures ? "FAILED: %d\n" : "all passed (%d failures)\n", failures);
  return failures ? 1 : 0;
}
