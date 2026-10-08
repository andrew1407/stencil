#include "projectThumbs.hpp"
#include "CanvasScene.hpp"
#include "LruCache.hpp"
#include "../../support/rowWork.hpp"

#include <QFileInfo>
#include <QImageReader>
#include <algorithm>
#include <memory>

namespace stencil::gui::projectThumbs {

  namespace {
    LruCache<QString, QPixmap>& cache() {
      static LruCache<QString, QPixmap> c(256);
      return c;
    }

    size_t linesHash(const core::Lines& lines) {
      const std::hash<std::string> str;
      size_t h = lines.size();
      for (const core::Line& l : lines) {
        h = qHashMulti(h, str(l.color), str(l.pointColor), str(l.style), str(l.fillColor),
                       l.thickness, l.pointSize, int(l.locked));
        for (const core::Point& p : l.points) h = qHashMulti(h, p.x, p.y);
      }
      return h;
    }

    // Pixels decoded (or shrunk) so the crop's long side lands near THUMB; `scale` maps the
    // project's image px onto them.
    struct Decoded {
      QImage image;
      double scale = 1.0;
    };

    double scaleFor(const core::CropRect& crop, QSize full) {
      const double span = crop.width > 0 ? std::max(crop.width, crop.height)
                                         : std::max(full.width(), full.height());
      return span > 0 ? std::min(1.0, THUMB / span) : 1.0;
    }

    Decoded decode(const Source& src) {
      Decoded out;
      if (!src.image.isNull()) {
        out.scale = scaleFor(src.crop, src.image.size());
        out.image = out.scale < 1.0
            ? src.image.scaled((src.image.size() * out.scale).expandedTo(QSize(1, 1)),
                               Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            : src.image;
      } else {
        QImageReader reader(src.path);
        const QSize full = reader.size();
        const double s = full.isValid() ? scaleFor(src.crop, full) : 1.0;
        if (s < 1.0) reader.setScaledSize((QSizeF(full) * s).toSize().expandedTo(QSize(1, 1)));
        out.image = reader.read();
        out.scale = s < 1.0 && !out.image.isNull() ? double(out.image.width()) / full.width() : 1.0;
      }
      return out;
    }

    QPixmap compose(CanvasScene& off, const Source& src, const Decoded& d, const Look& look) {
      const double s = d.scale;
      core::Lines lines = src.lines;
      for (core::Line& l : lines) {
        for (core::Point& p : l.points) { p.x *= s; p.y *= s; }
        l.thickness *= s;
        l.pointSize *= s;
      }
      core::CropRect crop = src.crop;
      if (crop.width > 0) { crop.x *= s; crop.y *= s; crop.width *= s; crop.height *= s; }
      // restore() adopts pixels only under a path; an in-memory picture borrows a placeholder one.
      off.restore(src.path.isEmpty() ? QStringLiteral("thumb") : src.path, lines, crop,
                  src.rotation, d.image, src.mirrored);
      off.setImageFilter(look.filter, look.tint);
      const QImage rendered = off.renderToImage(/*withOverlay=*/true);
      if (rendered.isNull()) return {};
      return QPixmap::fromImage(rendered.scaled(THUMB, THUMB, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
  }  // namespace

  QString keyOf(const Project& pr, const Look& look) {
    const QFileInfo fi(pr.imagePath);
    const core::CropRect& c = pr.cropRect;
    return QStringList{
        QString::fromStdString(pr.meta.id), QString::number(pr.meta.updatedAt), pr.imagePath,
        QString::number(fi.size()), QString::number(fi.lastModified().toMSecsSinceEpoch()),
        QStringLiteral("%1,%2,%3,%4,%5,%6").arg(c.x).arg(c.y).arg(c.width).arg(c.height).arg(pr.rotationQuarters).arg(pr.mirrored),
        QString::number(linesHash(pr.lines)), QString::number(look.dark), look.accent,
        QString::number(look.showPoints), QString::number(look.showLines), look.filter,
        look.tint.name(QColor::HexArgb), QString::number(look.pageWidthCm),
        QString::number(look.pageHeightCm)}
        .join(QLatin1Char('|'));
  }

  QPixmap cached(const QString& key) {
    const QPixmap* hit = cache().find(key);
    return hit ? *hit : QPixmap();
  }

  void build(QObject* ctx, std::vector<Source> sources, const Look& look,
             std::function<void(const QString& id, const QPixmap& thumb)> ready) {
    if (sources.empty()) return;
    // One offscreen scene for the batch, gone once the last composition has run.
    auto off = std::make_shared<CanvasScene>();
    off->setDark(look.dark);
    off->setAccent(look.accent);
    off->setShowPoints(look.showPoints);
    off->setShowLines(look.showLines);
    off->setPageCm(look.pageWidthCm, look.pageHeightCm);
    for (Source& src : sources) {
      auto shared = std::make_shared<const Source>(std::move(src));
      support::runOnPool<Decoded>(
          ctx, [shared] { return decode(*shared); },
          [off, shared, look, ready](Decoded d) {
            if (d.image.isNull()) return;
            const QPixmap pm = compose(*off, *shared, d, look);
            if (pm.isNull()) return;
            if (!shared->key.isEmpty()) cache().insert(shared->key, pm);
            ready(shared->id, pm);
          });
    }
  }

}  // namespace stencil::gui::projectThumbs
