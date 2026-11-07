#include "skinTable.hpp"

#include <cmath>

namespace stencil::support {

  QStringList jsonStrings(const QJsonArray& a) {
    QStringList out;
    for (const QJsonValue& v : a) out << v.toString();
    return out;
  }

  void readTokens(const QJsonObject& o, QHash<QString, QColor>& into) {
    for (auto it = o.begin(); it != o.end(); ++it) into.insert(it.key(), QColor(it.value().toString()));
  }

  double round2(double v) { return std::round(v * 100) / 100; }

  QVector<CellRun> runsOf(int cols, int rows, const std::function<QString(int, int)>& colorAt) {
    QVector<CellRun> runs;
    for (int cy = 0; cy < rows; ++cy) {
      int start = 0;
      QString color = colorAt(0, cy);
      for (int cx = 1; cx <= cols; ++cx) {
        const QString c = cx < cols ? colorAt(cx, cy) : QString();
        if (c == color) continue;
        runs.push_back({start, cy, cx - start, 1, color});
        start = cx;
        color = c;
      }
    }
    return runs;
  }

  QVector<WordLine> wordLinesOf(int w, int h, const WordGlyphs& word, const WordStyle& style) {
    const int n = static_cast<int>(word.text.size());
    const int totalCells = n * word.gridW + (n - 1) * word.gapCells;
    const double cellPx = (w * style.widthShare) / totalCells;
    const double left = (w - totalCells * cellPx) / 2;
    const double top = h * style.centerYShare - (word.gridH * cellPx) / 2;
    QVector<WordLine> out;
    for (int i = 0; i < n; ++i) {
      WordLine line;
      for (const QPoint& p : word.glyphs.value(word.text.at(i)))
        line.points.push_back(QPointF(round2(left + (i * (word.gridW + word.gapCells) + p.x()) * cellPx),
                                      round2(top + p.y() * cellPx)));
      line.color = style.stroke;
      line.fillColor = style.colors.isEmpty() ? QString() : style.colors.at(i % style.colors.size());
      line.thickness = style.thickness;
      out.push_back(line);
    }
    return out;
  }

}  // namespace stencil::support
