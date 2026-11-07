#pragma once
// The rules both skins' tables share (browser js/ui/skin/rules.js): the JSON readers, a picture as
// same-colour runs along its rows, and the word as closed, filled lines across the top of an image.
#include <QColor>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace stencil::support {

  struct CellRun { int x = 0, y = 0, w = 0, h = 0; QString color; };

  struct WordLine {
    QVector<QPointF> points;
    QString color, fillColor;
    double thickness = 2;
  };

  // The glyph table a word is set in (webcore.json `word`), and the lettering one skin gives it.
  struct WordGlyphs {
    QString text;
    QHash<QChar, QVector<QPoint>> glyphs;
    int gridW = 5, gridH = 7, gapCells = 2;
  };
  struct WordStyle {
    double widthShare = 0.8, centerYShare = 0.28;
    QStringList colors;
    QString stroke;
    double thickness = 2;
  };

  QStringList jsonStrings(const QJsonArray& a);
  void readTokens(const QJsonObject& o, QHash<QString, QColor>& into);
  double round2(double v);

  // Same-colour runs along every row of a cols×rows grid, `colorAt(cx, cy)` naming each cell's colour.
  QVector<CellRun> runsOf(int cols, int rows, const std::function<QString(int, int)>& colorAt);
  // The word as closed, filled lines across the top of a w×h image, one colour each.
  QVector<WordLine> wordLinesOf(int w, int h, const WordGlyphs& word, const WordStyle& style);

}  // namespace stencil::support
