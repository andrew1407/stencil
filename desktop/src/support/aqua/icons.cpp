#include "icons.hpp"

#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSvgRenderer>

namespace stencil::support {

  namespace {
    struct AquaArt {
      int grid = 32;
      QHash<QString, QString> icons;
    };

    const AquaArt& art() {
      static const AquaArt a = [] {
        AquaArt out;
        QFile f(QStringLiteral(":/config/iconsAqua.json"));
        if (!f.open(QIODevice::ReadOnly)) return out;
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        out.grid = root.value("size").toInt(out.grid);
        const QJsonObject icons = root.value("icons").toObject();
        for (auto it = icons.begin(); it != icons.end(); ++it) out.icons.insert(it.key(), it.value().toString());
        return out;
      }();
      return a;
    }
  }  // namespace

  bool hasAquaIcon(const QString& name) { return art().icons.contains(name); }

  // Each document is rendered on its own, so one fixed gradient-id token never collides.
  QString aquaIconSvg(const QString& name) {
    const auto it = art().icons.constFind(name);
    if (it == art().icons.cend()) return QString();
    QString inner = *it;
    inner.replace(QLatin1String("%u"), QLatin1String("aq"));
    const QString side = QString::number(art().grid);
    return QStringLiteral(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %1 %1" width="%1" height="%1">%2</svg>)")
        .arg(side, inner);
  }

  // Rasterised once per name and scale: the idle card asks for its glyph every paint.
  QImage aquaIconImage(const QString& name, bool, int scale) {
    static QHash<QString, QImage> rasters;
    const QString key = name + '@' + QString::number(scale);
    const auto hit = rasters.constFind(key);
    if (hit != rasters.constEnd()) return *hit;
    const QString doc = aquaIconSvg(name);
    if (doc.isEmpty() || scale <= 0) return QImage();
    QSvgRenderer renderer(doc.toUtf8());
    QImage img(QSize(16, 16) * scale, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&p);
    rasters.insert(key, img);
    return img;
  }

}  // namespace stencil::support
