#include "fileStore.hpp"
#include "fileStoreIo.hpp"
#include "deferredWrite.hpp"
#include "layoutCanon.hpp"
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

  // US customary → inches, everything else (incl. the UK) → cm; a saved preference wins.
  QString localeDefaultUnit() {
    typedef core::localeUnit::MeasurementSystem MS;
    const auto qsys = QLocale::system().measurementSystem();
    const MS sys = (qsys == QLocale::ImperialUSSystem) ? MS::IMPERIAL_US
                 : (qsys == QLocale::ImperialUKSystem) ? MS::IMPERIAL_UK
                                                       : MS::METRIC;
    return QString::fromStdString(core::localeUnit::defaultUnit(sys));
  }


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
    // Written ONLY when set: an absent key is how "inherit" round-trips without changing old files' bytes.
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

  // Crop keys are the browser's {x,y,w,h}; legacy {width,height} still reads. Filter + tint ride
  // along (browser storage.js #buildLayout) so a reopened project restores the same look.
  QJsonObject fileStore::buildLayoutJson(int w, int h, const core::Lines& lines,
                                         const QString& imageFilter,
                                         const QString& filterColor,
                                         const core::CropRect& cropRect,
                                         int rotationQuarters,
                                         const LayoutMeta& meta) {
    QMap<QString, QJsonValue> vals;
    vals["imageWidth"] = w;
    vals["imageHeight"] = h;
    vals["lines"] = linesToJson(lines);
    vals["imageFilter"] = imageFilter;
    vals["filterColor"] = filterColor;
    if (cropRect.width > 0 && cropRect.height > 0) vals["cropRect"] = cropRectToJson(cropRect);
    if (rotationQuarters != 0) vals["rotationQuarters"] = rotationQuarters;
    if (!meta.pageSize.isEmpty()) vals["pageSize"] = meta.pageSize;
    if (meta.customPageWidth != 0) vals["customPageWidth"] = meta.customPageWidth;
    if (meta.customPageHeight != 0) vals["customPageHeight"] = meta.customPageHeight;
    if (meta.allowFormulas) vals["allowFormulas"] = true;
    if (!meta.formulaX.isEmpty()) vals["formulaX"] = meta.formulaX;
    if (!meta.formulaY.isEmpty()) vals["formulaY"] = meta.formulaY;
    return layoutCanon::emitExport(vals);  // the canon's key set, not this function's
  }

  // Reports the stored image size (browser dimension check) and, when requested, crop/rotation.
  core::Lines fileStore::parseLayoutJson(const QJsonObject& o, int& wOut, int& hOut,
                                         core::CropRect* cropOut, int* rotOut) {
    wOut = o.value("imageWidth").toInt(0);
    hOut = o.value("imageHeight").toInt(0);
    if (cropOut && o.contains("cropRect")) *cropOut = cropRectFromJson(o.value("cropRect").toObject());
    if (rotOut) *rotOut = o.value("rotationQuarters").toInt(0);
    return linesFromJson(o.value("lines").toArray());
  }

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
    root["version"] = STENCIL_FILE_VERSION;
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
    // llm-contract.md §12.3; omitted otherwise so plain files are unchanged.
    if (!pf.chat.isEmpty()) root["chat"] = pf.chat;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
  }
}


