#pragma once
// The webcore skin's table (browser/js/config/webcore.json over the qrc) and the pure rules over
// it: the picture's cells and the word's lines. Twin of browser/js/ui/webcore/rules.js, value
// for value.
#include <QColor>
#include <QHash>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>

namespace stencil::support {

  struct WebcoreCloud {
    int x = 0, y = 0;
    QVector<QRect> blocks, shade;   // cell offsets off the anchor
  };

  struct WebcoreConfig {
    QHash<QString, QColor> tokens[2];   // [dark], by CSS custom-property name
    QStringList fontFamilies;
    int fontPx = 12, fontPt = 9;
    int width = 1024, height = 768, cell = 4;
    double horizonShare = 0.58;
    QStringList skyBands, grass;
    int ampCells = 14, spanCells = 200, offsetCells = -40;
    QString cloudInk, cloudShade;
    QVector<WebcoreCloud> clouds;
    QString text, stroke;
    double thickness = 2;
    QStringList colors;
    int gridW = 5, gridH = 7, gapCells = 2;
    double widthShare = 0.8, centerYShare = 0.28;
    QHash<QChar, QVector<QPoint>> glyphs;
    QString offToast, projectName, imageName;
  };

  // Parsed once from :/config/webcore.json.
  const WebcoreConfig& webcoreConfig();

  struct CellGrid { int cell = 4, cols = 256, rows = 192, horizon = 111; };
  CellGrid cellGrid();

  // The crest at column cx: a repeating parabola in integer arithmetic.
  int hillTopCell(int cx, const CellGrid& grid);
  QString skyBandAt(int cy, const CellGrid& grid);
  // Crest to foot in bands, a checker dither half a band deep at every edge.
  QString grassShadeAt(int cx, int cy, const CellGrid& grid);
  QString cellColorAt(int cx, int cy, const CellGrid& grid);

  struct CellRun { int x = 0, y = 0, w = 0, h = 0; QString color; };
  // Every cloud block in cells: the ink first, the shade over it.
  QVector<CellRun> cloudBlocks();
  // Same-colour runs along every row of the base picture.
  QVector<CellRun> cellRuns(const CellGrid& grid);
  // The finished picture's colour at a pixel.
  QString pixelColorAt(int px, int py);

  struct WordLine {
    QVector<QPointF> points;
    QString color, fillColor;
    double thickness = 2;
  };
  // STENCIL as closed, filled lines across the sky of a w×h image, one colour each.
  QVector<WordLine> wordLines(int w, int h);

}  // namespace stencil::support
