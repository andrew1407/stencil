#include "fileStore.hpp"
#include "localeUnit.hpp"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QStandardPaths>

namespace stencil::gui {

  namespace {

    // Seed the default display unit from the system locale: US customary →
    // inches, everything else (incl. the UK) → cm. Only a default — a saved
    // "units" preference always overrides it (see loadSettings).
    QString localeDefaultUnit() {
      using MS = core::localeUnit::MeasurementSystem;
      const auto qsys = QLocale::system().measurementSystem();
      const MS sys = (qsys == QLocale::ImperialUSSystem) ? MS::ImperialUS
                   : (qsys == QLocale::ImperialUKSystem) ? MS::ImperialUK
                                                         : MS::Metric;
      return QString::fromStdString(core::localeUnit::defaultUnit(sys));
    }

    // Baked at build time to <repo>/desktop/.stencil (see CMakeLists). Falls back
    // to the per-user config dir if the define is somehow absent. The env var wins
    // over both: ctest points it at an isolated dir so tests never touch dev state.
#ifdef STENCIL_STATE_DIR
    QString baseDir() {
      const QString env = qEnvironmentVariable("STENCIL_STATE_DIR");
      return env.isEmpty() ? QString(STENCIL_STATE_DIR) : env;
    }
#else
    QString baseDir() {
      const QString env = qEnvironmentVariable("STENCIL_STATE_DIR");
      return env.isEmpty()
                 ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                 : env;
    }
#endif

    // `ownerOnly` narrows the file to 0600 (settings holds llmApiKey in the clear).
    // Re-applied on every write, so a file from an older build is tightened on save.
    bool writeJson(const QString& path, const QJsonDocument& doc, bool ownerOnly = false) {
      QFile f(path);
      if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
      if (ownerOnly) f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
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
    o["pointSize"] = line.pointSize;
    o["style"] = QString::fromStdString(line.style);
    o["locked"] = line.locked;
    o["fillColor"] = QString::fromStdString(line.fillColor);
    // Point colour. Written ONLY when set: an absent key is how "inherit the line
    // colour" round-trips, and emitting "" for every line would bloat every project file
    // and change the bytes of files that predate the field.
    if (!line.pointColor.empty()) o["pointColor"] = QString::fromStdString(line.pointColor);
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
    line.pointSize = o.value("pointSize").toDouble(4.0);
    line.style = o.value("style").toString("solid").toStdString();
    line.locked = o.value("locked").toBool(false);
    line.fillColor = o.value("fillColor").toString("transparent").toStdString();
    // Absent (every pre-field project) → empty → points follow the line colour.
    line.pointColor = o.value("pointColor").toString("").toStdString();
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

  // Crop rectangle <-> JSON (original-image pixels). Written with the browser's
  // canonical {x,y,w,h} keys; the reader still accepts the legacy {width,height}
  // spelling (old sessions/projects, co-edit peers), canonical wins.
  static QJsonObject cropRectToJson(const core::CropRect& r) {
    QJsonObject o;
    o["x"] = r.x;
    o["y"] = r.y;
    o["w"] = r.width;
    o["h"] = r.height;
    return o;
  }

  static core::CropRect cropRectFromJson(const QJsonObject& o) {
    const double w = o.contains("w") ? o.value("w").toDouble() : o.value("width").toDouble();
    const double h = o.contains("h") ? o.value("h").toDouble() : o.value("height").toDouble();
    return {o.value("x").toDouble(), o.value("y").toDouble(), w, h};
  }

  // Build the layout export envelope (browser drawingApp.js:2078-2079). The
  // image filter + custom tint ride along (browser storage.js #buildLayout) so a
  // reopened project restores the same filter result, not just the lines.
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
    // Persisted chat rides along only when the save-chats opt-in produced one
    // (llm-contract.md §12.3); omitted otherwise so plain files are unchanged.
    if (!pf.chat.isEmpty()) root["chat"] = pf.chat;
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
    const int b64 = dataUrl.indexOf("base64,");
    if (b64 < 0) {
      if (err) *err = QStringLiteral("Project file has no embedded image.");
      return false;
    }
    out.imageBytes = QByteArray::fromBase64(dataUrl.mid(b64 + 7).toLatin1());
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
    out.chat = o.value("chat").toObject();
    return true;
  }

  namespace {
    // §12.1 machinery filter (browser chatStore.js isInternalChatText parity):
    // the §7 continuation note (exact or bracketed variant, any role) and
    // assistant turns that are raw op-/ask-plans never enter the transcript.
    bool isInternalChatText(const QString& role, const QString& text) {
      const QString t = text.trimmed();
      if (t.isEmpty()) return false;
      static const QRegularExpression note(
          QStringLiteral(R"(^\[The working image is now\b[\s\S]*\]$)"));
      if (note.match(t).hasMatch()) return true;
      if (role != QLatin1String("assistant")) return false;
      static const QRegularExpression versionKey(QStringLiteral("\"version\"\\s*:"));
      static const QRegularExpression planKey(
          QStringLiteral("\"(actions|reply|variants|ask)\"\\s*:"));
      return (t.startsWith(QLatin1Char('{')) || t.startsWith(QLatin1Char('['))) &&
             t.contains(versionKey) && t.contains(planKey);
    }

    // Shared §12.1 whitelist for chat documents: rebuild each message so only a
    // valid role + string text survives (no images, no machinery text), then
    // bound the count. Saving (buildChatDoc) additionally drops empty texts.
    QJsonArray sanitizeChatMessages(const QJsonArray& messages, bool dropEmpty) {
      QJsonArray clean;
      for (const auto& v : messages) {
        const QJsonObject m = v.toObject();
        const QString role = m.value("role").toString();
        const QJsonValue textVal = m.value("text");
        if ((role != "user" && role != "assistant") || !textVal.isString()) continue;
        const QString text = textVal.toString();
        if (dropEmpty && text.isEmpty()) continue;
        if (isInternalChatText(role, text)) continue;
        QJsonObject out;
        out["role"] = role;
        out["text"] = text;
        clean.append(out);
      }
      while (clean.size() > fileStore::kChatDocMessageLimit) clean.removeFirst();
      return clean;
    }
  }  // namespace

  QJsonObject fileStore::buildChatDoc(const QJsonArray& messages, qint64 savedAt) {
    QJsonObject doc;
    doc["version"] = kChatDocVersion;
    doc["savedAt"] = savedAt;
    doc["messages"] = sanitizeChatMessages(messages, /*dropEmpty=*/true);
    return doc;
  }

  QJsonArray fileStore::parseChatDoc(const QJsonObject& doc) {
    if (doc.value("version").toInt(0) != kChatDocVersion) return {};
    return sanitizeChatMessages(doc.value("messages").toArray(), /*dropEmpty=*/false);
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
    return settingsFromJson(o, s);
  }

  Settings fileStore::settingsFromJson(const QJsonObject& o, const Settings& base) {
    Settings s = base;
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
    s.defaultPointColor = o.value("defaultPointColor").toString(s.defaultPointColor);
    s.defaultThickness = o.value("defaultThickness").toDouble(s.defaultThickness);
    s.defaultPointSize = o.value("defaultPointSize").toDouble(s.defaultPointSize);
    s.defaultStyle = o.value("defaultStyle").toString(s.defaultStyle);
    s.defaultFillColor = o.value("defaultFillColor").toString(s.defaultFillColor);
    s.selGlowColor = o.value("selGlowColor").toString(s.selGlowColor);
    s.hoverRingColor = o.value("hoverRingColor").toString(s.hoverRingColor);
    s.focusRingColor = o.value("focusRingColor").toString(s.focusRingColor);
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
    s.drawingAnimations = o.value("drawingAnimations").toBool(s.drawingAnimations);
    s.motionMode = o.value("motionMode").toString(s.motionMode);
    s.browserBaseUrl = o.value("browserBaseUrl").toString(s.browserBaseUrl);
    s.telegramBotUsername = o.value("telegramBotUsername").toString(s.telegramBotUsername);
    // AI assistant (llm-contract.md §5 persistence keys) + the saved dock state.
    s.llmProvider = o.value("llmProvider").toString(s.llmProvider);
    s.llmBaseUrl = o.value("llmBaseUrl").toString(s.llmBaseUrl);
    s.llmModel = o.value("llmModel").toString(s.llmModel);
    s.llmApiKey = o.value("llmApiKey").toString(s.llmApiKey);
    s.llmServerUrl = o.value("llmServerUrl").toString(s.llmServerUrl);
    s.saveChatsWithProject = o.value("saveChatsWithProject").toBool(s.saveChatsWithProject);
    s.chatSwapSides = o.value("chatSwapSides").toBool(s.chatSwapSides);
    s.nativeMenuBar = o.value("nativeMenuBar").toBool(s.nativeMenuBar);
    s.windowState = o.value("windowState").toString(s.windowState);
    return s;
  }

  void fileStore::saveSettings(const Settings& s) {
    // Owner-only: this file holds llmApiKey in plaintext (contract §5).
    writeJson(settingsPath(), QJsonDocument(settingsToJson(s)), /*ownerOnly=*/true);
  }

  QJsonObject fileStore::settingsToJson(const Settings& s) {
    QJsonObject o;
    o["themeMode"] = s.themeMode;
    o["accentColor"] = s.accentColor;
    o["autosave"] = s.autosave;
    o["syncToServer"] = s.syncToServer;
    o["showPoints"] = s.showPoints;
    o["showLines"] = s.showLines;
    o["defaultColor"] = s.defaultColor;
    o["defaultPointColor"] = s.defaultPointColor;
    o["defaultThickness"] = s.defaultThickness;
    o["defaultPointSize"] = s.defaultPointSize;
    o["defaultStyle"] = s.defaultStyle;
    o["defaultFillColor"] = s.defaultFillColor;
    o["selGlowColor"] = s.selGlowColor;
    o["hoverRingColor"] = s.hoverRingColor;
    o["focusRingColor"] = s.focusRingColor;
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
    o["drawingAnimations"] = s.drawingAnimations;
    o["motionMode"] = s.motionMode;
    o["browserBaseUrl"] = s.browserBaseUrl;
    o["telegramBotUsername"] = s.telegramBotUsername;
    o["llmProvider"] = s.llmProvider;
    o["llmBaseUrl"] = s.llmBaseUrl;
    o["llmModel"] = s.llmModel;
    o["llmApiKey"] = s.llmApiKey;
    o["llmServerUrl"] = s.llmServerUrl;
    o["saveChatsWithProject"] = s.saveChatsWithProject;
    o["chatSwapSides"] = s.chatSwapSides;
    o["nativeMenuBar"] = s.nativeMenuBar;
    o["windowState"] = s.windowState;
    return o;
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
    s.activeProjectId = o.value("activeProjectId").toString();
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
    if (!s.activeProjectId.isEmpty()) o["activeProjectId"] = s.activeProjectId;
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
    // Persisted chat (llm-contract.md §12); absent for most projects.
    pr.chat = o.value("chat").toObject();
    // Pan/zoom position (browser parity: storage.js zoom/scrollLeft/scrollTop). Absent for
    // projects saved before this existed — 0.0/0/0 is exactly "never saved" (see the field
    // comments in fileStore.hpp).
    pr.zoomScale = o.value("zoom").toDouble(0.0);
    pr.scrollLeft = o.value("scrollLeft").toInt(0);
    pr.scrollTop = o.value("scrollTop").toInt(0);
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
    // Persisted chat: omit when empty so a plain project's bytes stay unchanged.
    if (!pr.chat.isEmpty()) o["chat"] = pr.chat;
    // Pan/zoom position: omit when never saved, so a plain project's bytes stay unchanged
    // (and projectFromJson's 0.0/0/0 default keeps reading as "never saved").
    if (pr.zoomScale > 0) o["zoom"] = pr.zoomScale;
    if (pr.scrollLeft) o["scrollLeft"] = pr.scrollLeft;
    if (pr.scrollTop) o["scrollTop"] = pr.scrollTop;
    return o;
  }

  void fileStore::saveProjects(const std::vector<Project>& projects) {
    QJsonArray arr;
    for (const auto& pr : projects) arr.append(projectToJson(pr));
    writeJson(projectsPath(), QJsonDocument(arr));
  }

}
