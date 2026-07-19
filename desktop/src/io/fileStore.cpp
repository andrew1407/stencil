#include "fileStore.hpp"
#include "localeUnit.hpp"
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

  // Crop rectangle <-> JSON (original-image pixels). File-local: only the session
  // / project (de)serializers need it.
  static QJsonObject cropRectToJson(const core::CropRect& r) {
    QJsonObject o;
    o["x"] = r.x;
    o["y"] = r.y;
    o["width"] = r.width;
    o["height"] = r.height;
    return o;
  }

  static core::CropRect cropRectFromJson(const QJsonObject& o) {
    return {o.value("x").toDouble(), o.value("y").toDouble(),
            o.value("width").toDouble(), o.value("height").toDouble()};
  }

  // Build the layout export envelope (browser drawingApp.js:2078-2079). The
  // image filter + custom tint ride along (browser storage.js #buildLayout) so a
  // reopened project restores the same b&w/sepia/invert/contour/tint result,
  // not just the lines.
  QJsonObject fileStore::buildLayoutJson(int w, int h, const core::Lines& lines,
                                         const QString& imageFilter,
                                         const QString& filterColor,
                                         const core::CropRect& cropRect,
                                         int rotationQuarters,
                                         const LayoutMeta& meta) {
    QJsonObject o;
    o["imageWidth"] = w;
    o["imageHeight"] = h;
    o["lines"] = linesToJson(lines);
    o["imageFilter"] = imageFilter;
    o["filterColor"] = filterColor;
    // Geometry is optional on the wire: omit a non-crop and a zero rotation so callers
    // that don't pass them (file export) stay byte-identical to the old envelope.
    if (cropRect.width > 0 && cropRect.height > 0) o["cropRect"] = cropRectToJson(cropRect);
    if (rotationQuarters != 0) o["rotationQuarters"] = rotationQuarters;
    // Page format + formulas (server save only): omit-when-default so file exports stay stable.
    if (!meta.pageSize.isEmpty()) o["pageSize"] = meta.pageSize;
    if (meta.customPageWidth != 0) o["customPageWidth"] = meta.customPageWidth;
    if (meta.customPageHeight != 0) o["customPageHeight"] = meta.customPageHeight;
    if (meta.allowFormulas) o["allowFormulas"] = true;
    if (!meta.formulaX.isEmpty()) o["formulaX"] = meta.formulaX;
    if (!meta.formulaY.isEmpty()) o["formulaY"] = meta.formulaY;
    return o;
  }

  // Read the layout envelope back, reporting stored image size (browser
  // drawingApp.js:2111 dimension check) and, when requested, crop/rotation.
  core::Lines fileStore::parseLayoutJson(const QJsonObject& o, int& wOut, int& hOut,
                                         core::CropRect* cropOut, int* rotOut) {
    wOut = o.value("imageWidth").toInt(0);
    hOut = o.value("imageHeight").toInt(0);
    if (cropOut && o.contains("cropRect")) *cropOut = cropRectFromJson(o.value("cropRect").toObject());
    if (rotOut) *rotOut = o.value("rotationQuarters").toInt(0);
    return linesFromJson(o.value("lines").toArray());
  }

  // Read the page format + x/y formulas out of a layout (absent fields stay at defaults).
  fileStore::LayoutMeta fileStore::parseLayoutMeta(const QJsonObject& o) {
    LayoutMeta m;
    m.pageSize = o.value("pageSize").toString();
    m.customPageWidth = o.value("customPageWidth").toDouble(0);
    m.customPageHeight = o.value("customPageHeight").toDouble(0);
    m.allowFormulas = o.value("allowFormulas").toBool(false);
    m.formulaX = o.value("formulaX").toString();
    m.formulaY = o.value("formulaY").toString();
    return m;
  }

  // ── .stencil portable project files ──────────────────────────────────────────
  namespace {
    QString stencilMimeForExt(const QString& ext) {
      const QString e = ext.toLower();
      if (e == "png") return "image/png";
      if (e == "jpg" || e == "jpeg") return "image/jpeg";
      if (e == "bmp") return "image/bmp";
      if (e == "webp") return "image/webp";
      if (e == "gif") return "image/gif";
      return "application/octet-stream";
    }
  }

  QByteArray fileStore::buildProjectFile(const ProjectFileData& pf) {
    QJsonObject root;
    root["format"] = "stencil-project";
    root["version"] = kStencilFileVersion;
    root["name"] = pf.name.isEmpty() ? QStringLiteral("Untitled") : pf.name;
    if (!pf.color.isEmpty()) root["color"] = pf.color;
    if (!pf.description.isEmpty()) root["description"] = pf.description;
    if (!pf.keywords.isEmpty()) root["keywords"] = QJsonArray::fromStringList(pf.keywords);
    if (!pf.source.isEmpty()) root["source"] = pf.source;
    if (!pf.resource.isEmpty()) root["resource"] = pf.resource;
    if (pf.blank) {
      root["blank"] = true;
      if (!pf.blankColor.isEmpty()) root["blankColor"] = pf.blankColor;
    }
    QJsonObject img;
    img["dataUrl"] = "data:" + stencilMimeForExt(pf.imageExt) + ";base64,"
                     + QString::fromLatin1(pf.imageBytes.toBase64());
    img["ext"] = pf.imageExt;
    img["w"] = pf.imageWidth;
    img["h"] = pf.imageHeight;
    root["image"] = img;
    root["layout"] = pf.layout;
    if (pf.hasTheme) {
      QJsonObject theme;
      if (!pf.themeMode.isEmpty()) theme["mode"] = pf.themeMode;
      if (!pf.themeAccent.isEmpty()) theme["accent"] = pf.themeAccent;
      if (!theme.isEmpty()) root["theme"] = theme;
    }
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
  }

  bool fileStore::parseProjectFile(const QByteArray& bytes, ProjectFileData& out, QString* err) {
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &pe);
    if (doc.isNull() || !doc.isObject()) {
      if (err) *err = QStringLiteral("Not valid JSON: ") + pe.errorString();
      return false;
    }
    const QJsonObject o = doc.object();
    if (o.value("format").toString() != "stencil-project") {
      if (err) *err = QStringLiteral("Not a Stencil project file.");
      return false;
    }
    const int ver = o.value("version").toInt(0);
    if (ver < 1) {
      if (err) *err = QStringLiteral("Unrecognized project-file version.");
      return false;
    }
    if (ver > kStencilFileVersion) {
      if (err) *err = QStringLiteral("This project needs a newer Stencil.");
      return false;
    }
    const QJsonObject img = o.value("image").toObject();
    const QString dataUrl = img.value("dataUrl").toString();
    const int marker = dataUrl.indexOf("base64,");
    if (marker < 0) {
      if (err) *err = QStringLiteral("Project file has no embedded image.");
      return false;
    }
    out.imageBytes = QByteArray::fromBase64(dataUrl.mid(marker + 7).toLatin1());
    if (out.imageBytes.isEmpty()) {
      if (err) *err = QStringLiteral("Project image could not be decoded.");
      return false;
    }
    out.imageExt = img.value("ext").toString("png");
    out.imageWidth = img.value("w").toInt(0);
    out.imageHeight = img.value("h").toInt(0);
    out.layout = o.value("layout").toObject();
    out.name = o.value("name").toString("Untitled");
    out.color = o.value("color").toString();
    out.description = o.value("description").toString();
    out.keywords.clear();
    const QJsonArray kws = o.value("keywords").toArray();
    for (const auto& v : kws) out.keywords << v.toString();
    out.source = o.value("source").toString();
    out.resource = o.value("resource").toString();
    out.blank = o.value("blank").toBool(false);
    out.blankColor = o.value("blankColor").toString();
    const QJsonObject theme = o.value("theme").toObject();
    if (!theme.isEmpty()) {
      out.hasTheme = true;
      out.themeMode = theme.value("mode").toString();
      out.themeAccent = theme.value("accent").toString();
    }
    return true;
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
    s.accentColor = o.value("accentColor").toString(s.accentColor);
    s.autosave = o.value("autosave").toBool(s.autosave);
    s.syncToServer = o.value("syncToServer").toBool(s.syncToServer);
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
    s.tooltipShowPage = o.value("tooltipShowPage").toBool(s.tooltipShowPage);
    s.tooltipShowScreen = o.value("tooltipShowScreen").toBool(s.tooltipShowScreen);
    s.tooltipShowCoords = o.value("tooltipShowCoords").toBool(s.tooltipShowCoords);
    // Image filter + custom tint (browser storage.js:309-311).
    s.imageFilter = o.value("imageFilter").toString(s.imageFilter);
    s.filterColor = o.value("filterColor").toString(s.filterColor);
    s.holdDrawDelay = o.value("holdDrawDelay").toInt(s.holdDrawDelay);
    s.browserBaseUrl = o.value("browserBaseUrl").toString(s.browserBaseUrl);
    s.telegramBotUsername = o.value("telegramBotUsername").toString(s.telegramBotUsername);
    return s;
  }

  void fileStore::saveSettings(const Settings& s) {
    QJsonObject o;
    o["themeMode"] = s.themeMode;
    o["accentColor"] = s.accentColor;
    o["autosave"] = s.autosave;
    o["syncToServer"] = s.syncToServer;
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
    o["tooltipShowPage"] = s.tooltipShowPage;
    o["tooltipShowScreen"] = s.tooltipShowScreen;
    o["tooltipShowCoords"] = s.tooltipShowCoords;
    o["imageFilter"] = s.imageFilter;
    o["filterColor"] = s.filterColor;
    o["holdDrawDelay"] = s.holdDrawDelay;
    o["browserBaseUrl"] = s.browserBaseUrl;
    o["telegramBotUsername"] = s.telegramBotUsername;
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
    s.cropRect = cropRectFromJson(o.value("cropRect").toObject());
    s.rotationQuarters = o.value("rotationQuarters").toInt(0);
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
    if (s.cropRect.width > 0) o["cropRect"] = cropRectToJson(s.cropRect);
    if (s.rotationQuarters) o["rotationQuarters"] = s.rotationQuarters;
    writeJson(sessionPath(), QJsonDocument(o));
  }

  void fileStore::clearSession() { QFile::remove(sessionPath()); }

  Project fileStore::projectFromJson(const QJsonObject& o) {
    Project pr;
    pr.meta.id = o.value("id").toString().toStdString();
    pr.meta.name = o.value("name").toString().toStdString();
    pr.meta.createdAt = o.value("createdAt").toVariant().toLongLong();
    pr.meta.updatedAt = o.value("updatedAt").toVariant().toLongLong();
    // Explicit expiration (0 = keep forever). Legacy projects predate these keys:
    // default-fill to the old derived rule (updatedAt + one week) so behaviour
    // doesn't jump on upgrade. Mirrors browser ProjectsStore #normalizeMeta.
    if (o.contains("expiresAt"))
      pr.meta.expiresAt = o.value("expiresAt").toVariant().toLongLong();
    else
      pr.meta.expiresAt = pr.meta.updatedAt + core::ProjectsStore::EXPIRY_MS;
    pr.meta.refreshPeriod = o.contains("refreshPeriod")
      ? o.value("refreshPeriod").toString().toStdString()
      : std::string(core::ProjectsStore::DEFAULT_PERIOD);
    pr.meta.autoRefresh = o.value("autoRefresh").toBool(true);
    pr.imagePath = o.value("imagePath").toString();
    pr.meta.hasImage = !pr.imagePath.isEmpty();
    pr.meta.source = o.value("source").toString().toStdString();
    pr.meta.resource = o.value("resource").toString().toStdString();
    // Per-project accent color (empty = theme default). Mirrors the browser record.
    pr.meta.color = o.value("color").toString().toStdString();
    // Per-project free-text description (empty = none). Mirrors the browser record.
    pr.meta.description = o.value("description").toString().toStdString();
    // Per-project search keywords (empty = none). Mirrors the browser record.
    pr.meta.keywords.clear();
    for (const auto& kv : o.value("keywords").toArray()) {
      const std::string k = kv.toString().toStdString();
      if (!k.empty()) pr.meta.keywords.push_back(k);
    }
    // Blank-image fill colour (empty = not a blank project). `blank` is derived. Mirrors browser.
    pr.meta.blankColor = o.value("blankColor").toString().toStdString();
    pr.meta.blank = !pr.meta.blankColor.empty();
    // Provenance: opened from a .stencil file (drives the bronze projects-list outline).
    pr.meta.fromFile = o.value("fromFile").toBool(false);
    // Cached image px dimensions + total drawn-line length (cm); 0/absent for legacy
    // projects (re-stamped from live state on the next save). Mirrors the browser record.
    pr.meta.imageW = o.value("imageW").toInt(0);
    pr.meta.imageH = o.value("imageH").toInt(0);
    pr.meta.lineLengthCm = o.value("lineLengthCm").toDouble(0);
    pr.lines = linesFromJson(o.value("lines").toArray());
    pr.cropRect = cropRectFromJson(o.value("cropRect").toObject());
    pr.rotationQuarters = o.value("rotationQuarters").toInt(0);
    return pr;
  }

  std::vector<Project> fileStore::loadProjects() {
    std::vector<Project> out;
    for (const auto& v : readJson(projectsPath()).array())
      out.push_back(projectFromJson(v.toObject()));
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

  QJsonObject fileStore::projectToJson(const Project& pr) {
    QJsonObject o;
    o["id"] = QString::fromStdString(pr.meta.id);
    o["name"] = QString::fromStdString(pr.meta.name);
    o["createdAt"] = QString::number(pr.meta.createdAt).toLongLong();
    o["updatedAt"] = QString::number(pr.meta.updatedAt).toLongLong();
    // Expiration fields (mirrors the browser record). Written always so the
    // stored 0 = "keep forever" is unambiguous on the next load.
    o["expiresAt"] = QString::number(pr.meta.expiresAt).toLongLong();
    o["refreshPeriod"] = QString::fromStdString(pr.meta.refreshPeriod);
    o["autoRefresh"] = pr.meta.autoRefresh;
    o["imagePath"] = pr.imagePath;
    if (!pr.meta.source.empty()) o["source"] = QString::fromStdString(pr.meta.source);
    if (!pr.meta.resource.empty()) o["resource"] = QString::fromStdString(pr.meta.resource);
    // Per-project accent color: omit when empty so a plain project's bytes are unchanged.
    if (!pr.meta.color.empty()) o["color"] = QString::fromStdString(pr.meta.color);
    // Per-project description: omit when empty so a plain project's bytes are unchanged.
    if (!pr.meta.description.empty()) o["description"] = QString::fromStdString(pr.meta.description);
    // Per-project search keywords: omit when empty so a plain project's bytes are unchanged.
    if (!pr.meta.keywords.empty()) {
      QJsonArray kw;
      for (const auto& k : pr.meta.keywords) kw.append(QString::fromStdString(k));
      o["keywords"] = kw;
    }
    // Blank-image fill colour: omit when empty so an ordinary project's bytes are unchanged.
    if (!pr.meta.blankColor.empty()) o["blankColor"] = QString::fromStdString(pr.meta.blankColor);
    // Provenance: omit unless set, so a plain project's bytes stay unchanged.
    if (pr.meta.fromFile) o["fromFile"] = true;
    // Cached image px dimensions + total drawn-line length (cm), display-only tooltip
    // data: each omitted when 0/empty so a plain project's bytes stay unchanged. Not
    // synced to the server (mirrors the browser project's imageW/imageH/lineLengthCm).
    if (pr.meta.imageW > 0) o["imageW"] = pr.meta.imageW;
    if (pr.meta.imageH > 0) o["imageH"] = pr.meta.imageH;
    if (pr.meta.lineLengthCm > 0) o["lineLengthCm"] = pr.meta.lineLengthCm;
    o["lines"] = linesToJson(pr.lines);
    if (pr.cropRect.width > 0) o["cropRect"] = cropRectToJson(pr.cropRect);
    if (pr.rotationQuarters) o["rotationQuarters"] = pr.rotationQuarters;
    return o;
  }

  void fileStore::saveProjects(const std::vector<Project>& projects) {
    QJsonArray arr;
    for (const auto& pr : projects) arr.append(projectToJson(pr));
    writeJson(projectsPath(), QJsonDocument(arr));
  }

}
