#include "fileStore.hpp"
#include "core/localeUnit.hpp"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QStandardPaths>

namespace stencil::gui {

  namespace {

    // Seed the default display unit from the system locale: US customary →
    // inches, everything else (incl. metric and the UK) → cm. Maps QLocale's
    // measurement system onto the STL-only core policy. Only used as a default;
    // a saved "units" preference always overrides it (see loadSettings).
    QString localeDefaultUnit() {
      using MS = core::localeUnit::MeasurementSystem;
      const auto qsys = QLocale::system().measurementSystem();
      const MS sys = (qsys == QLocale::ImperialUSSystem) ? MS::ImperialUS
                   : (qsys == QLocale::ImperialUKSystem) ? MS::ImperialUK
                                                         : MS::Metric;
      return QString::fromStdString(core::localeUnit::defaultUnit(sys));
    }

    // Baked at build time to <repo>/desktop/.stencil (see CMakeLists). Falls back
    // to the per-user config dir if the define is somehow absent.
#ifdef STENCIL_STATE_DIR
    QString baseDir() { return QString(STENCIL_STATE_DIR); }
#else
    QString baseDir() {
      return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    }
#endif

    bool writeJson(const QString& path, const QJsonDocument& doc) {
      QFile f(path);
      if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
      f.write(doc.toJson(QJsonDocument::Indented));
      return true;
    }

    QJsonDocument readJson(const QString& path) {
      QFile f(path);
      if (!f.open(QIODevice::ReadOnly)) return {};
      return QJsonDocument::fromJson(f.readAll());
    }

  }  // namespace

  // ── Line <-> JSON (mirrors the browser line object fields). Promoted to
  // fileStore:: so the layout data actions can reuse them. ──
  QJsonObject fileStore::lineToJson(const core::Line& line) {
    QJsonArray pts;
    for (const auto& p : line.points) {
      QJsonObject o;
      o["x"] = p.x;
      o["y"] = p.y;
      pts.append(o);
    }
    QJsonObject o;
    o["points"] = pts;
    o["color"] = QString::fromStdString(line.color);
    o["thickness"] = line.thickness;
    o["markerSize"] = line.markerSize;
    o["style"] = QString::fromStdString(line.style);
    o["locked"] = line.locked;
    o["fillColor"] = QString::fromStdString(line.fillColor);
    return o;
  }

  core::Line fileStore::lineFromJson(const QJsonObject& o) {
    core::Line line;
    for (const auto& v : o["points"].toArray()) {
      const QJsonObject po = v.toObject();
      line.points.push_back({po["x"].toDouble(), po["y"].toDouble()});
    }
    line.color = o.value("color").toString("#FFFF00").toStdString();
    line.thickness = o.value("thickness").toDouble(2.0);
    line.markerSize = o.value("markerSize").toDouble(4.0);
    line.style = o.value("style").toString("solid").toStdString();
    line.locked = o.value("locked").toBool(false);
    line.fillColor = o.value("fillColor").toString("transparent").toStdString();
    return line;
  }

  QJsonArray fileStore::linesToJson(const core::Lines& lines) {
    QJsonArray arr;
    for (const auto& l : lines) arr.append(lineToJson(l));
    return arr;
  }

  core::Lines fileStore::linesFromJson(const QJsonArray& arr) {
    core::Lines lines;
    for (const auto& v : arr) lines.push_back(lineFromJson(v.toObject()));
    return lines;
  }

  // Build the layout export envelope (browser drawingApp.js:2078-2079).
  QJsonObject fileStore::buildLayoutJson(int w, int h, const core::Lines& lines) {
    QJsonObject o;
    o["imageWidth"] = w;
    o["imageHeight"] = h;
    o["lines"] = linesToJson(lines);
    return o;
  }

  // Read the layout envelope back, reporting stored image size (browser
  // drawingApp.js:2111 dimension check).
  core::Lines fileStore::parseLayoutJson(const QJsonObject& o, int& wOut, int& hOut) {
    wOut = o.value("imageWidth").toInt(0);
    hOut = o.value("imageHeight").toInt(0);
    return linesFromJson(o.value("lines").toArray());
  }

  QString fileStore::stateDir() {
    const QString dir = baseDir();
    QDir().mkpath(dir);
    return dir;
  }
  QString fileStore::settingsPath() { return stateDir() + "/settings.json"; }
  QString fileStore::sessionPath() { return stateDir() + "/session.autosave"; }
  QString fileStore::projectsPath() { return stateDir() + "/projects.json"; }

  Settings fileStore::loadSettings() {
    Settings s;
    // Seed the locale-based default before reading the file, so a brand-new
    // user (no settings yet) or an older config without a "units" key still
    // gets a sensible default; a stored "units" value below overrides it.
    s.units = localeDefaultUnit();
    const QJsonObject o = readJson(settingsPath()).object();
    if (o.isEmpty()) return s;
    // themeMode is the new key; migrate the legacy `theme` ("dark"->dark,
    // "light"->light, anything else / missing -> system).
    if (o.contains("themeMode")) {
      s.themeMode = o.value("themeMode").toString(s.themeMode);
    } else if (o.contains("theme")) {
      const QString legacy = o.value("theme").toString();
      s.themeMode = (legacy == "dark" || legacy == "light") ? legacy : "system";
    }
    s.autosave = o.value("autosave").toBool(s.autosave);
    s.showPoints = o.value("showPoints").toBool(s.showPoints);
    s.showLines = o.value("showLines").toBool(s.showLines);
    s.defaultColor = o.value("defaultColor").toString(s.defaultColor);
    s.defaultThickness = o.value("defaultThickness").toDouble(s.defaultThickness);
    s.defaultMarkerSize = o.value("defaultMarkerSize").toDouble(s.defaultMarkerSize);
    s.defaultStyle = o.value("defaultStyle").toString(s.defaultStyle);
    s.pageSize = o.value("pageSize").toString(s.pageSize);
    s.customPageWidth = o.value("customPageWidth").toDouble(s.customPageWidth);
    s.customPageHeight = o.value("customPageHeight").toDouble(s.customPageHeight);
    s.units = o.value("units").toString(s.units);
    s.allowFormulas = o.value("allowFormulas").toBool(s.allowFormulas);
    s.formulaX = o.value("formulaX").toString(s.formulaX);
    s.formulaY = o.value("formulaY").toString(s.formulaY);
    s.tooltipEnabled = o.value("tooltipEnabled").toBool(s.tooltipEnabled);
    // Image filter + custom tint (browser storage.js:309-311).
    s.imageFilter = o.value("imageFilter").toString(s.imageFilter);
    s.filterColor = o.value("filterColor").toString(s.filterColor);
    return s;
  }

  void fileStore::saveSettings(const Settings& s) {
    QJsonObject o;
    o["themeMode"] = s.themeMode;
    o["autosave"] = s.autosave;
    o["showPoints"] = s.showPoints;
    o["showLines"] = s.showLines;
    o["defaultColor"] = s.defaultColor;
    o["defaultThickness"] = s.defaultThickness;
    o["defaultMarkerSize"] = s.defaultMarkerSize;
    o["defaultStyle"] = s.defaultStyle;
    o["pageSize"] = s.pageSize;
    o["customPageWidth"] = s.customPageWidth;
    o["customPageHeight"] = s.customPageHeight;
    o["units"] = s.units;
    o["allowFormulas"] = s.allowFormulas;
    o["formulaX"] = s.formulaX;
    o["formulaY"] = s.formulaY;
    o["tooltipEnabled"] = s.tooltipEnabled;
    o["imageFilter"] = s.imageFilter;
    o["filterColor"] = s.filterColor;
    writeJson(settingsPath(), QJsonDocument(o));
  }

  std::optional<Session> fileStore::loadSession() {
    const QJsonObject o = readJson(sessionPath()).object();
    if (o.isEmpty()) return std::nullopt;
    Session s;
    s.imagePath = o.value("imagePath").toString();
    s.pageSize = o.value("pageSize").toString("A3");
    s.scale = o.value("scale").toDouble(1.0);
    s.customPageWidth = o.value("customPageWidth").toDouble(21.0);
    s.customPageHeight = o.value("customPageHeight").toDouble(29.7);
    // Image filter with the legacy blackAndWhite migration, then tint + draw
    // mode (browser storage.js:309,359).
    s.imageFilter = o.value("imageFilter")
                        .toString(o.value("blackAndWhite").toBool(false) ? "bw" : "none");
    s.filterColor = o.value("filterColor").toString("#7c3aed");
    s.drawMode = o.value("drawMode").toString("line");
    s.lines = linesFromJson(o.value("lines").toArray());
    return s;
  }

  void fileStore::saveSession(const Session& s) {
    QJsonObject o;
    o["imagePath"] = s.imagePath;
    o["pageSize"] = s.pageSize;
    o["scale"] = s.scale;
    o["customPageWidth"] = s.customPageWidth;
    o["customPageHeight"] = s.customPageHeight;
    o["imageFilter"] = s.imageFilter;
    o["filterColor"] = s.filterColor;
    o["drawMode"] = s.drawMode;
    o["lines"] = linesToJson(s.lines);
    writeJson(sessionPath(), QJsonDocument(o));
  }

  void fileStore::clearSession() { QFile::remove(sessionPath()); }

  std::vector<Project> fileStore::loadProjects() {
    std::vector<Project> out;
    for (const auto& v : readJson(projectsPath()).array()) {
      const QJsonObject o = v.toObject();
      Project pr;
      pr.meta.id = o.value("id").toString().toStdString();
      pr.meta.name = o.value("name").toString().toStdString();
      pr.meta.createdAt = o.value("createdAt").toVariant().toLongLong();
      pr.meta.updatedAt = o.value("updatedAt").toVariant().toLongLong();
      pr.imagePath = o.value("imagePath").toString();
      pr.meta.hasImage = !pr.imagePath.isEmpty();
      pr.lines = linesFromJson(o.value("lines").toArray());
      out.push_back(std::move(pr));
    }
    return out;
  }

  QString fileStore::hotkeysPath() { return stateDir() + "/hotkeys.json"; }

  QHash<QString, QString> fileStore::loadHotkeys() {
    QHash<QString, QString> out;
    const QJsonObject o = readJson(hotkeysPath()).object();
    for (auto it = o.begin(); it != o.end(); ++it)
      out.insert(it.key(), it.value().toString());
    return out;
  }

  void fileStore::saveHotkeys(const QHash<QString, QString>& overrides) {
    QJsonObject o;
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
      o[it.key()] = it.value();
    writeJson(hotkeysPath(), QJsonDocument(o));
  }

  void fileStore::saveProjects(const std::vector<Project>& projects) {
    QJsonArray arr;
    for (const auto& pr : projects) {
      QJsonObject o;
      o["id"] = QString::fromStdString(pr.meta.id);
      o["name"] = QString::fromStdString(pr.meta.name);
      o["createdAt"] = QString::number(pr.meta.createdAt).toLongLong();
      o["updatedAt"] = QString::number(pr.meta.updatedAt).toLongLong();
      o["imagePath"] = pr.imagePath;
      o["lines"] = linesToJson(pr.lines);
      arr.append(o);
    }
    writeJson(projectsPath(), QJsonDocument(arr));
  }

}
