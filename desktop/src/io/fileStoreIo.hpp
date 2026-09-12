#pragma once
// The state dir, the atomic JSON read/write and the crop-rect codec, private to the
// fileStore*.cpp TUs.
#include "fileStore.hpp"
#include "deferredWrite.hpp"

#include <QFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <QLocale>
#include <QStandardPaths>
#include <QString>

namespace stencil::gui {


  // Default display unit from the system locale (defined in fileStore.cpp).
  QString localeDefaultUnit();

  // Baked at build time to <repo>/desktop/.stencil (see CMakeLists), else the per-user
  // config dir. The env var wins over both: ctest points it at an isolated dir.
#ifdef STENCIL_STATE_DIR
  inline QString baseDir() {
    const QString env = qEnvironmentVariable("STENCIL_STATE_DIR");
    return env.isEmpty() ? QString(STENCIL_STATE_DIR) : env;
  }
#else
  inline QString baseDir() {
    const QString env = qEnvironmentVariable("STENCIL_STATE_DIR");
    return env.isEmpty()
               ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
               : env;
  }
#endif

  // Atomic (temp + rename); `ownerOnly` narrows it to 0600 (settings holds the key).
  inline bool writeJson(const QString& path, const QJsonDocument& doc, bool ownerOnly = false) {
    return deferredWrite::atomic(path, doc.toJson(QJsonDocument::Indented), ownerOnly);
  }

  inline QJsonDocument readJson(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll());
  }

  inline QJsonObject cropRectToJson(const core::CropRect& r) {
    QJsonObject o;
    o["x"] = r.x;
    o["y"] = r.y;
    o["w"] = r.width;
    o["h"] = r.height;
    return o;
  }

  inline core::CropRect cropRectFromJson(const QJsonObject& o) {
    const double w = o.contains("w") ? o.value("w").toDouble() : o.value("width").toDouble();
    const double h = o.contains("h") ? o.value("h").toDouble() : o.value("height").toDouble();
    return {o.value("x").toDouble(), o.value("y").toDouble(), w, h};
  }

}  // namespace stencil::gui


