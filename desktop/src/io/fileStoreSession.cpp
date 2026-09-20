#include "fileStore.hpp"
#include "fileStoreIo.hpp"
#include "accentDefaults.hpp"
#include "deferredWrite.hpp"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QStandardPaths>

namespace stencil::gui {

  std::optional<Session> fileStore::loadSession() {
    const QJsonObject o = readJson(sessionPath()).object();
    if (o.isEmpty()) return std::nullopt;
    Session s;
    s.imagePath = o.value("imagePath").toString();
    s.pageSize = o.value("pageSize").toString("A3");
    s.scale = o.value("scale").toDouble(1.0);
    s.customPageWidth = o.value("customPageWidth").toDouble(21.0);
    s.customPageHeight = o.value("customPageHeight").toDouble(29.7);
    // Legacy blackAndWhite migration (browser storage.js).
    s.imageFilter = o.value("imageFilter")
                        .toString(o.value("blackAndWhite").toBool(false) ? "bw" : "none");
    s.filterColor = o.value("filterColor").toString(DEFAULT_ACCENT_HEX);
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
    // Legacy projects default to the old derived rule (updatedAt + one week); browser #normalizeMeta twin.
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
    pr.meta.color = o.value("color").toString().toStdString();
    pr.meta.description = o.value("description").toString().toStdString();
    pr.meta.keywords.clear();
    for (const auto& kv : o.value("keywords").toArray()) {
      const std::string k = kv.toString().toStdString();
      if (!k.empty()) pr.meta.keywords.push_back(k);
    }
    pr.meta.blankColor = o.value("blankColor").toString().toStdString();
    pr.meta.blank = !pr.meta.blankColor.empty();
    pr.meta.fromFile = o.value("fromFile").toBool(false);
    // 0/absent for legacy projects, re-stamped on the next save.
    pr.meta.imageW = o.value("imageW").toInt(0);
    pr.meta.imageH = o.value("imageH").toInt(0);
    pr.meta.lineLengthCm = o.value("lineLengthCm").toDouble(0);
    pr.lines = linesFromJson(o.value("lines").toArray());
    pr.cropRect = cropRectFromJson(o.value("cropRect").toObject());
    pr.rotationQuarters = o.value("rotationQuarters").toInt(0);
    pr.chat = o.value("chat").toObject();
    // 0.0/0/0 is exactly "never saved".
    pr.zoomScale = o.value("zoom").toDouble(0.0);
    pr.scrollLeft = o.value("scrollLeft").toInt(0);
    pr.scrollTop = o.value("scrollTop").toInt(0);
    return pr;
  }

  std::vector<Project> fileStore::loadProjects() {
    deferredWrite::flush();   // a debounced saveProjects may still be in the air
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
    // Written always so the stored 0 = "keep forever" is unambiguous.
    o["expiresAt"] = QString::number(pr.meta.expiresAt).toLongLong();
    o["refreshPeriod"] = QString::fromStdString(pr.meta.refreshPeriod);
    o["autoRefresh"] = pr.meta.autoRefresh;
    o["imagePath"] = pr.imagePath;
    if (!pr.meta.source.empty()) o["source"] = QString::fromStdString(pr.meta.source);
    if (!pr.meta.resource.empty()) o["resource"] = QString::fromStdString(pr.meta.resource);
    if (!pr.meta.color.empty()) o["color"] = QString::fromStdString(pr.meta.color);
    if (!pr.meta.description.empty()) o["description"] = QString::fromStdString(pr.meta.description);
    if (!pr.meta.keywords.empty()) {
      QJsonArray kw;
      for (const auto& k : pr.meta.keywords) kw.append(QString::fromStdString(k));
      o["keywords"] = kw;
    }
    if (!pr.meta.blankColor.empty()) o["blankColor"] = QString::fromStdString(pr.meta.blankColor);
    if (pr.meta.fromFile) o["fromFile"] = true;
    // Omitted when 0/empty so a plain project's bytes stay unchanged; not synced to the server.
    if (pr.meta.imageW > 0) o["imageW"] = pr.meta.imageW;
    if (pr.meta.imageH > 0) o["imageH"] = pr.meta.imageH;
    if (pr.meta.lineLengthCm > 0) o["lineLengthCm"] = pr.meta.lineLengthCm;
    o["lines"] = linesToJson(pr.lines);
    if (pr.cropRect.width > 0) o["cropRect"] = cropRectToJson(pr.cropRect);
    if (pr.rotationQuarters) o["rotationQuarters"] = pr.rotationQuarters;
    if (!pr.chat.isEmpty()) o["chat"] = pr.chat;
    // Omitted when never saved so 0.0/0/0 keeps reading as "never saved".
    if (pr.zoomScale > 0) o["zoom"] = pr.zoomScale;
    if (pr.scrollLeft) o["scrollLeft"] = pr.scrollLeft;
    if (pr.scrollTop) o["scrollTop"] = pr.scrollTop;
    return o;
  }

  // The WHOLE registry is re-serialised for any change; 600 ms is the house debounce window.
  void fileStore::saveProjects(const std::vector<Project>& projects) {
    deferredWrite::schedule(projectsPath(), 600, [copy = projects] {
      QJsonArray arr;
      for (const auto& pr : copy) arr.append(projectToJson(pr));
      return QJsonDocument(arr).toJson(QJsonDocument::Indented);
    });
  }
  void fileStore::flushWrites() { deferredWrite::flush(); }
}

