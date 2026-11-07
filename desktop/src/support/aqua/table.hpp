#pragma once
// The aqua skin's table (common/config/aqua.json over the qrc) and the pure rules over it: the
// wallpaper's cells and the word's lines, the glyphs borrowed from webcore.json. Twin of
// browser/js/ui/aqua/rules.js, value for value.
#include "../webcore/rules.hpp"

#include <QColor>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace stencil::support {

  struct AquaSwoosh {
    int x0 = 0, span = 1, base = 0, amp = 0, width = 1, edge = 0;
    QString color, edgeColor;
  };

  // One gel token's recipe (aqua.json `tint`): the accent moved toward `toward` by `share`.
  struct AquaTint { QString name, toward; double share = 0; };

  struct AquaConfig {
    QHash<QString, QColor> tokens[2];   // [dark], by CSS custom-property name
    QHash<QString, QString> aliases;    // base theme token -> the --aq-* token it reads
    QString tintBase;
    QVector<AquaTint> tint[2];          // [dark]
    QStringList fontFamilies;
    int fontPx = 13, fontPt = 10;
    int width = 1024, height = 768, cell = 2;
    QStringList ramp;
    QVector<AquaSwoosh> swooshes;
    QString stroke;
    double thickness = 3;
    QStringList colors;
    double widthShare = 0.8, centerYShare = 0.3;
    QString offToast, projectName, imageName;
  };

  // Parsed once from :/config/aqua.json.
  const AquaConfig& aquaConfig();
  // The gel header mark (common/config/svgArt.json aquaLogo.svg), read once; %1..%4 are the gel.
  const QString& aquaLogoSvg();
  // The mark with its gel in the tokens `accent` paints.
  QString aquaLogoArt(const QColor& accent, bool dark);

  // `a` moved toward `b` by `share` in sRGB, each channel rounded (browser rules.js mixHex).
  QColor mixHex(const QColor& a, const QColor& b, double share);
  // The gel tokens an accent paints, both on-inks included; an invalid accent is tint.base,
  // whose gel is the table's own (browser rules.js gelTokens).
  QHash<QString, QColor> gelTokens(const QColor& accent, bool dark);

  struct WallGrid { int cell = 2, cols = 512, rows = 384; };
  WallGrid wallGrid();
  QString rampAt(int cy, const WallGrid& grid);
  // Rows below swoosh s's crest at (cx, cy), or -1 outside it; integer, so both painters agree.
  int swooshDepth(const AquaSwoosh& s, int cx, int cy);
  QString wallColorAt(int cx, int cy, const WallGrid& grid);
  // Same-colour runs along every row of the wallpaper.
  QVector<CellRun> wallRuns(const WallGrid& grid);
  // STENCIL as closed, gel-filled lines across the top of a w×h image, one colour each.
  QVector<WordLine> aquaWordLines(int w, int h);

}  // namespace stencil::support
