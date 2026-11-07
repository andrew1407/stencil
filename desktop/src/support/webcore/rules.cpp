#include "rules.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>

namespace stencil::support {

  namespace {
    QVector<QRect> rects(const QJsonArray& a) {
      QVector<QRect> out;
      for (const QJsonValue& v : a) {
        const QJsonArray r = v.toArray();
        if (r.size() == 4) out.push_back(QRect(r.at(0).toInt(), r.at(1).toInt(), r.at(2).toInt(), r.at(3).toInt()));
      }
      return out;
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
      c.fontFamilies = jsonStrings(font.value("families").toArray());
      c.fontPx = font.value("px").toInt(c.fontPx);
      c.fontPt = font.value("pt").toInt(c.fontPt);
      const QJsonObject img = root.value("image").toObject();
      c.width = img.value("width").toInt(c.width);
      c.height = img.value("height").toInt(c.height);
      c.cell = img.value("cell").toInt(c.cell);
      c.horizonShare = img.value("horizonShare").toDouble(c.horizonShare);
      c.skyBands = jsonStrings(img.value("skyBands").toArray());
      const QJsonObject hill = img.value("hill").toObject();
      c.ampCells = hill.value("ampCells").toInt(c.ampCells);
      c.spanCells = hill.value("spanCells").toInt(c.spanCells);
      c.offsetCells = hill.value("offsetCells").toInt(c.offsetCells);
      c.grass = jsonStrings(img.value("grass").toArray());
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
      c.colors = jsonStrings(word.value("colors").toArray());
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
    return runsOf(grid.cols, grid.rows, [&grid](int cx, int cy) { return cellColorAt(cx, cy, grid); });
  }

  QString pixelColorAt(int px, int py) {
    const CellGrid grid = cellGrid();
    const int cx = px / grid.cell, cy = py / grid.cell;
    QString color = cellColorAt(cx, cy, grid);
    for (const CellRun& b : cloudBlocks())
      if (cx >= b.x && cx < b.x + b.w && cy >= b.y && cy < b.y + b.h) color = b.color;
    return color;
  }

  WordGlyphs webcoreWordGlyphs() {
    const WebcoreConfig& c = webcoreConfig();
    return WordGlyphs{c.text, c.glyphs, c.gridW, c.gridH, c.gapCells};
  }

  QVector<WordLine> wordLines(int w, int h) {
    const WebcoreConfig& c = webcoreConfig();
    return wordLinesOf(w, h, webcoreWordGlyphs(), WordStyle{c.widthShare, c.centerYShare, c.colors, c.stroke, c.thickness});
  }

}  // namespace stencil::support
