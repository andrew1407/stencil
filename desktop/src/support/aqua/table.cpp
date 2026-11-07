#include "table.hpp"

#include "gradient.hpp"
#include "theme.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <iterator>

namespace stencil::support {

  namespace {
    AquaConfig parseConfig() {
      AquaConfig c;
      QFile f(":/config/aqua.json");
      if (!f.open(QIODevice::ReadOnly)) return c;
      const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
      const QJsonObject tokens = root.value("tokens").toObject();
      readTokens(tokens.value("light").toObject(), c.tokens[0]);
      readTokens(tokens.value("dark").toObject(), c.tokens[1]);
      const QJsonObject aliases = root.value("aliases").toObject();
      for (auto it = aliases.begin(); it != aliases.end(); ++it) c.aliases.insert(it.key(), it.value().toString());
      const QJsonObject tint = root.value("tint").toObject();
      c.tintBase = tint.value("base").toString();
      for (const int dark : {0, 1}) {
        const QJsonObject face = tint.value(dark ? "dark" : "light").toObject();
        for (auto it = face.begin(); it != face.end(); ++it) {
          const QJsonArray pair = it.value().toArray();
          c.tint[dark].push_back({it.key(), pair.at(0).toString(), pair.at(1).toDouble()});
        }
      }
      const QJsonObject font = root.value("font").toObject();
      c.fontFamilies = jsonStrings(font.value("families").toArray());
      c.fontPx = font.value("px").toInt(c.fontPx);
      c.fontPt = font.value("pt").toInt(c.fontPt);
      const QJsonObject img = root.value("image").toObject();
      c.width = img.value("width").toInt(c.width);
      c.height = img.value("height").toInt(c.height);
      c.cell = img.value("cell").toInt(c.cell);
      c.ramp = jsonStrings(img.value("ramp").toArray());
      for (const QJsonValue& v : img.value("swooshes").toArray()) {
        const QJsonObject o = v.toObject();
        AquaSwoosh s;
        s.x0 = o.value("x0").toInt();
        s.span = std::max(1, o.value("span").toInt());
        s.base = o.value("base").toInt();
        s.amp = o.value("amp").toInt();
        s.width = o.value("width").toInt();
        s.edge = o.value("edge").toInt();
        s.color = o.value("color").toString();
        s.edgeColor = o.value("edgeColor").toString();
        c.swooshes.push_back(s);
      }
      const QJsonObject word = root.value("word").toObject();
      c.stroke = word.value("stroke").toString();
      c.thickness = word.value("thickness").toDouble(c.thickness);
      c.colors = jsonStrings(word.value("colors").toArray());
      c.widthShare = word.value("widthShare").toDouble(c.widthShare);
      c.centerYShare = word.value("centerYShare").toDouble(c.centerYShare);
      const QJsonObject s = root.value("strings").toObject();
      c.offToast = s.value("off").toString();
      c.projectName = s.value("project").toString();
      c.imageName = s.value("imageName").toString();
      return c;
    }
  }  // namespace

  const AquaConfig& aquaConfig() {
    static const AquaConfig cfg = parseConfig();
    return cfg;
  }

  const QString& aquaLogoSvg() {
    static const QString svg = [] {
      QFile f(":/config/svgArt.json");
      if (!f.open(QIODevice::ReadOnly)) return QString();
      return QJsonDocument::fromJson(f.readAll()).object().value("aquaLogo").toObject().value("svg").toString();
    }();
    return svg;
  }

  QString aquaLogoArt(const QColor& accent, bool dark) {
    const QHash<QString, QColor> gel = gelTokens(accent, dark);
    QString svg = aquaLogoSvg();
    static constexpr const char* NAMES[] = {"--aq-gel-hi", "--aq-gel-top", "--aq-gel-bottom", "--aq-gel-edge"};
    for (std::size_t i = 0; i < std::size(NAMES); ++i) svg.replace('%' + QString::number(i + 1), gel.value(QLatin1String(NAMES[i])).name());
    return svg;
  }

  QColor mixHex(const QColor& a, const QColor& b, double share) { return aquaMix(b, share, a); }

  QHash<QString, QColor> gelTokens(const QColor& accent, bool dark) {
    const AquaConfig& c = aquaConfig();
    const QColor tint = accent.isValid() ? accent : QColor(c.tintBase);
    QHash<QString, QColor> out;
    for (const AquaTint& t : c.tint[dark ? 1 : 0]) out.insert(t.name, mixHex(tint, QColor(t.toward), t.share));
    out.insert(QStringLiteral("--aq-on-select"), stencil::gui::onAccentInk(out.value(QStringLiteral("--aq-select"))));
    out.insert(QStringLiteral("--aq-on-gel"),   // the word on a gel capsule, read against its deep half
               stencil::gui::onAccentInk(out.value(QStringLiteral("--aq-gel-bottom"))));
    return out;
  }

  WallGrid wallGrid() {
    const AquaConfig& c = aquaConfig();
    return WallGrid{c.cell, c.width / c.cell, c.height / c.cell};
  }

  QString rampAt(int cy, const WallGrid& grid) {
    const QStringList& ramp = aquaConfig().ramp;
    const int bands = static_cast<int>(ramp.size());
    return ramp.value(std::min(bands - 1, (cy * bands) / grid.rows));
  }

  int swooshDepth(const AquaSwoosh& s, int cx, int cy) {
    const int u = cx - s.x0;
    if (u < 0 || u > s.span) return -1;
    const long long lift = (static_cast<long long>(s.amp) * 4 * u * (s.span - u)) / (static_cast<long long>(s.span) * s.span);
    const int d = cy - (s.base - static_cast<int>(lift));
    return d >= 0 && d < s.width ? d : -1;
  }

  QString wallColorAt(int cx, int cy, const WallGrid& grid) {
    QString color = rampAt(cy, grid);
    for (const AquaSwoosh& s : aquaConfig().swooshes) {
      const int d = swooshDepth(s, cx, cy);
      if (d < 0) continue;
      const bool isRim = d < s.edge || d >= s.width - s.edge;
      if (!isRim) color = s.color;
      else if ((cx + cy) & 1) color = s.edgeColor;
    }
    return color;
  }

  QVector<CellRun> wallRuns(const WallGrid& grid) {
    return runsOf(grid.cols, grid.rows, [&grid](int cx, int cy) { return wallColorAt(cx, cy, grid); });
  }

  QVector<WordLine> aquaWordLines(int w, int h) {
    const AquaConfig& c = aquaConfig();
    return wordLinesOf(w, h, webcoreWordGlyphs(), WordStyle{c.widthShare, c.centerYShare, c.colors, c.stroke, c.thickness});
  }

}  // namespace stencil::support
