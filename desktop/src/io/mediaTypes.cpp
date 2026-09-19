#include "MediaLoader.hpp"
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// app.qrc's shared-config canon, forced live on first read — a pre-main caller can
// beat the resource's own initializer. Global scope, for Q_INIT_RESOURCE.
static void ensureAppResources() { Q_INIT_RESOURCE(app); }

namespace stencil::gui {

  namespace {
    // browser/js/config/mediaTypes.json `surfaces.desktop`. Deliberately not the wider
    // `video.extensions` contract set - that would accept more files (the asset's `drift` note).
    QStringList canonExtensions(const QString& kind) {
      ensureAppResources();
      QFile f(QStringLiteral(":/config/mediaTypes.json"));
      if (!f.open(QIODevice::ReadOnly)) return {};
      const QJsonObject desktop = QJsonDocument::fromJson(f.readAll())
                                      .object()
                                      .value("surfaces")
                                      .toObject()
                                      .value("desktop")
                                      .toObject();
      QStringList exts;
      for (const QJsonValue& v : desktop.value(kind).toArray()) exts << v.toString();
      return exts;
    }
  }  // namespace

  bool isVideoFileName(const QString& path) {
    static const QStringList VIDEO_EXT = canonExtensions(QStringLiteral("video"));
    return VIDEO_EXT.contains(QFileInfo(path).suffix().toLower());
  }

  bool isImageFileName(const QString& path) {
    static const QStringList IMAGE_EXT = canonExtensions(QStringLiteral("image"));
    return IMAGE_EXT.contains(QFileInfo(path).suffix().toLower());
  }

}
