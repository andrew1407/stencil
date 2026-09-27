// The co-edit smoke suite's first section: editor A creates the shared project and uploads its
// original; editor B opens it and loads the original — openServerProject's data path.
#include "coEditParts.hpp"

#include <QBuffer>
#include <QImage>

namespace coedit {

  bool openShared(ServerClient* A, ServerClient* B, QString& id, qint64& v0, qint64& bVersion) {
    // A tiny PNG original so the open/load path has bytes to download + decode.
    QByteArray png;
    {
      QImage img(20, 10, QImage::Format_ARGB32);
      img.fill(Qt::red);
      QBuffer buf(&png);
      buf.open(QIODevice::WriteOnly);
      check(img.save(&buf, "PNG"), "encoded a source PNG");
    }

    const QString name = QStringLiteral("e2e-coedit-%1")
                             .arg(QString::number(qHash(png) ^ 0x5715));  // clock-free unique-ish

    // ── Editor A: create the shared project + upload its original ──
    {
      auto ready = std::make_shared<bool>(false);
      A->createProjectAsync(name, QString(), QString(), /*hasImage=*/true, 20, 10,
                            [&](bool ok, QString newId, qint64 ver) {
                              check(ok && !newId.isEmpty(), "A: created the shared project");
                              id = newId;
                              v0 = ver;
                              *ready = true;
                            });
      pump(ready);
    }
    if (id.isEmpty()) return false;
    {
      auto ready = std::make_shared<bool>(false);
      A->uploadFileAsync(id, "original", png, "png", 20, 10, [&](bool ok) {
        check(ok, "A: uploaded the original image");
        *ready = true;
      });
      pump(ready);
    }

    // ── Editor B: open/load the project (openServerProject's data path) ──
    {
      auto ready = std::make_shared<bool>(false);
      B->getProjectAsync(id, [&](bool ok, ServerProject meta, QJsonObject /*layout*/) {
        check(ok && meta.name == name, "B: opened the project (getProject sees A's project)");
        bVersion = meta.version;
        *ready = true;
      });
      pump(ready);
    }
    {
      auto ready = std::make_shared<bool>(false);
      B->downloadFileAsync(id, "original", [&](bool ok, QByteArray bytes) {
        QImage img;
        check(ok && !bytes.isEmpty() && img.loadFromData(bytes),
              "B: downloaded + decoded the original (open/load path)");
        *ready = true;
      });
      pump(ready);
    }
    return true;
  }

}  // namespace coedit
