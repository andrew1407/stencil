#include "rules.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>

namespace stencil::support {

  namespace {
    QStringList strings(const QJsonArray& a) {
      QStringList out;
      for (const QJsonValue& v : a) out << v.toString();
      return out;
    }

    QVector<QRect> rects(const QJsonArray& a) {
      QVector<QRect> out;
      for (const QJsonValue& v : a) {
        const QJsonArray r = v.toArray();
        if (r.size() == 4) out.push_back(QRect(r.at(0).toInt(), r.at(1).toInt(), r.at(2).toInt(), r.at(3).toInt()));
      }
      return out;
    }

    void readTokens(const QJsonObject& o, QHash<QString, QColor>& into) {
      for (auto it = o.begin(); it != o.end(); ++it) into.insert(it.key(), QColor(it.value().toString()));
    }

    WebcoreConfig parseConfig() {
      WebcoreConfig c;
      QFile f(":/config/webcore.json");
      if (!f.open(QIODevice::ReadOnly)) return c;
      const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
      const QJsonObject tokens = root.value("tokens").toObject();
      readTokens(tokens.value("light").toObject(), c.tokens[0]);
      readTokens(tokens.value("dark").toObject(), c.tokens[1]);
      const QJsonObject font = root.value("font").toObject();
      c.fontFamilies = strings(font.value("families").toArray());
      c.fontPx = font.value("px").toInt(c.fontPx);
      c.fontPt = font.value("pt").toInt(c.fontPt);
      const QJsonObject img = root.value("image").toObject();
      c.width = img.value("width").toInt(c.width);
      c.height = img.value("height").toInt(c.height);
      c.cell = img.value("cell").toInt(c.cell);
      c.horizonShare = img.value("horizonShare").toDouble(c.horizonShare);
      c.skyBands = strings(img.value("skyBands").toArray());
      const QJsonObject hill = img.value("hill").toObject();
      c.ampCells = hill.value("ampCells").toInt(c.ampCells);
      c.spanCells = hill.value("spanCells").toInt(c.spanCells);
      c.offsetCells = hill.value("offsetCells").toInt(c.offsetCells);
      c.grass = strings(img.value("grass").toArray());
      c.cloudInk = img.value("cloudInk").toString();
      c.cloudShade = img.value("cloudShade").toString();
      for (const QJsonValue& v : img.value("clouds").toArray()) {
        const QJsonObject o = v.toObject();
        WebcoreCloud cloud;
        cloud.x = o.value("x").toInt();
        cloud.y = o.value("y").toInt();
        cloud.blocks = rects(o.value("blocks").toArray());
        cloud.shade = rects(o.value("shade").toArray());
        c.clouds.push_back(cloud);
      }
      const QJsonObject word = root.value("word").toObject();
      c.text = word.value("text").toString();
      c.stroke = word.value("stroke").toString();
      c.thickness = word.value("thickness").toDouble(c.thickness);
      c.colors = strings(word.value("colors").toArray());
      const QJsonArray grid = word.value("grid").toArray();
      if (grid.size() == 2) { c.gridW = grid.at(0).toInt(); c.gridH = grid.at(1).toInt(); }
      c.gapCells = word.value("gapCells").toInt(c.gapCells);
      c.widthShare = word.value("widthShare").toDouble(c.widthShare);
      c.centerYShare = word.value("centerYShare").toDouble(c.centerYShare);
      const QJsonObject glyphs = word.value("glyphs").toObject();
      for (auto it = glyphs.begin(); it != glyphs.end(); ++it) {
        QVector<QPoint> poly;
        for (const QJsonValue& p : it.value().toArray()) {
          const QJsonArray xy = p.toArray();
          if (xy.size() == 2) poly.push_back(QPoint(xy.at(0).toInt(), xy.at(1).toInt()));
        }
        if (!it.key().isEmpty()) c.glyphs.insert(it.key().at(0), poly);
      }
      const QJsonObject s = root.value("strings").toObject();
      c.offToast = s.value("off").toString();
      c.projectName = s.value("project").toString();
      c.imageName = s.value("imageName").toString();
      return c;
    }

    double round2(double v) { return std::round(v * 100) / 100; }
  }  // namespace

  const WebcoreConfig& webcoreConfig() {
    static const WebcoreConfig cfg = parseConfig();
    return cfg;
  }

  CellGrid cellGrid() {
    const WebcoreConfig& c = webcoreConfig();
    CellGrid g;
    g.cell = c.cell;
    g.cols = c.width / c.cell;
    g.rows = c.height / c.cell;
    g.horizon = qRound(g.rows * c.horizonShare);
    return g;
  }

  int hillTopCell(int cx, const CellGrid& grid) {
    const WebcoreConfig& c = webcoreConfig();
    const int u = (((cx - c.offsetCells) % c.spanCells) + c.spanCells) % c.spanCells;
    const double lift = std::floor((double(c.ampCells) * 4 * u * (c.spanCells - u)) / (double(c.spanCells) * c.spanCells));
    return grid.horizon - int(lift);
  }

  QString skyBandAt(int cy, const CellGrid& grid) {
    const QStringList& bands = webcoreConfig().skyBands;
    const int i = std::min(int(bands.size()) - 1, (cy * int(bands.size())) / grid.horizon);
    return bands.value(i);
  }

  QString grassShadeAt(int cx, int cy, const CellGrid& grid) {
    const WebcoreConfig& c = webcoreConfig();
    const int bands = int(c.grass.size());
    const int bandCells = std::max(1, (grid.rows - grid.horizon + c.ampCells) / bands);
    const int d = cy - hillTopCell(cx, grid) + ((cx + cy) & 1) * (bandCells >> 1);
    return c.grass.value(std::min(bands - 1, d / bandCells));
  }

  QString cellColorAt(int cx, int cy, const CellGrid& grid) {
    return cy >= hillTopCell(cx, grid) ? grassShadeAt(cx, cy, grid) : skyBandAt(cy, grid);
  }

  QVector<CellRun> cloudBlocks() {
    const WebcoreConfig& c = webcoreConfig();
    QVector<CellRun> out;
    for (const WebcoreCloud& cl : c.clouds) {
      for (const QRect& r : cl.blocks) out.push_back({cl.x + r.x(), cl.y + r.y(), r.width(), r.height(), c.cloudInk});
      for (const QRect& r : cl.shade) out.push_back({cl.x + r.x(), cl.y + r.y(), r.width(), r.height(), c.cloudShade});
    }
    return out;
  }

  QVector<CellRun> cellRuns(const CellGrid& grid) {
    QVector<CellRun> runs;
    for (int cy = 0; cy < grid.rows; ++cy) {
      int start = 0;
      QString color = cellColorAt(0, cy, grid);
      for (int cx = 1; cx <= grid.cols; ++cx) {
        const QString c = cx < grid.cols ? cellColorAt(cx, cy, grid) : QString();
        if (c == color) continue;
        runs.push_back({start, cy, cx - start, 1, color});
        start = cx;
        color = c;
      }
    }
    return runs;
  }

  QString pixelColorAt(int px, int py) {
    const CellGrid grid = cellGrid();
    const int cx = px / grid.cell, cy = py / grid.cell;
    QString color = cellColorAt(cx, cy, grid);
    for (const CellRun& b : cloudBlocks())
      if (cx >= b.x && cx < b.x + b.w && cy >= b.y && cy < b.y + b.h) color = b.color;
    return color;
  }

  QVector<WordLine> wordLines(int w, int h) {
    const WebcoreConfig& c = webcoreConfig();
    const int n = int(c.text.size());
    const int totalCells = n * c.gridW + (n - 1) * c.gapCells;
    const double cellPx = (w * c.widthShare) / totalCells;
    const double left = (w - totalCells * cellPx) / 2;
    const double top = h * c.centerYShare - (c.gridH * cellPx) / 2;
    QVector<WordLine> out;
    for (int i = 0; i < n; ++i) {
      WordLine line;
      for (const QPoint& p : c.glyphs.value(c.text.at(i)))
        line.points.push_back(QPointF(round2(left + (i * (c.gridW + c.gapCells) + p.x()) * cellPx),
                                      round2(top + p.y() * cellPx)));
      line.color = c.stroke;
      line.fillColor = c.colors.value(i % int(c.colors.size()));
      line.thickness = c.thickness;
      out.push_back(line);
    }
    return out;
  }

}  // namespace stencil::support
