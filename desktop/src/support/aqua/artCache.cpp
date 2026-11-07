#include "artCache.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QStandardPaths>

#include <algorithm>

namespace stencil::support {

  void pruneArtFamily(const QString& dir, const QString& family) {
    QHash<QString, QDateTime> newest;   // key (its 1x and 2x as one) → the later of its two files
    for (const QFileInfo& f : QDir(dir).entryInfoList({family + "-*.png"}, QDir::Files)) {
      QString key = f.completeBaseName();
      if (key.endsWith(QLatin1String("@2x"))) key.chop(3);
      if (!newest.contains(key) || newest[key] < f.lastModified()) newest[key] = f.lastModified();
    }
    QStringList keys = newest.keys();
    std::sort(keys.begin(), keys.end(), [&newest](const QString& a, const QString& b) { return newest[a] > newest[b]; });
    for (qsizetype i = ART_CACHE_CAP; i < keys.size(); ++i)
      for (const char* suffix : {".png", "@2x.png"}) QFile::remove(dir + '/' + keys[i] + suffix);
  }

  QString cachedArtPath(const QString& key, const QSize& size, const std::function<void(QPainter&)>& paint, bool isRetina) {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString base = dir + '/' + key;
    const QString proof = base + (isRetina ? "@2x.png" : ".png");
    if (QFile::exists(proof)) return base + ".png";
    QDir().mkpath(dir);
    for (const int k : {1, 2}) {
      if (k == 2 && !isRetina) break;
      QImage img(size * k, QImage::Format_ARGB32_Premultiplied);
      img.fill(Qt::transparent);
      QPainter p(&img);
      p.setRenderHint(QPainter::Antialiasing);
      p.scale(k, k);
      paint(p);
      p.end();
      if (!img.save(base + (k == 1 ? ".png" : "@2x.png"))) return QString();
    }
    pruneArtFamily(dir, key.section('-', 0, 1));
    return base + ".png";
  }

}  // namespace stencil::support
