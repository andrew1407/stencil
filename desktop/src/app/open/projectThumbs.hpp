#pragma once
#include "fileStore.hpp"

#include <QColor>
#include <QImage>
#include <QPixmap>
#include <QString>
#include <functional>
#include <vector>

class QObject;

// The Projects dialog's local thumbnails: decoded on the pool at thumbnail scale, composed with
// their lines at that scale, cached per project state and look, and handed to the rows lazily.
namespace stencil::gui::projectThumbs {

  // px; larger than the 56px row icon so the hover-magnify preview stays crisp.
  inline constexpr int THUMB = 320;

  // What a thumbnail varies with besides the project itself.
  struct Look {
    bool dark = false;
    QString accent;
    bool showPoints = true;
    bool showLines = true;
    QString filter;
    QColor tint;
    double pageWidthCm = 0;
    double pageHeightCm = 0;
  };

  // One picture to compose. `image` set = in memory (the open canvas), else read from `path`.
  // An empty `key` is never cached.
  struct Source {
    QString id;
    QString path;
    QImage image;
    core::Lines lines;
    core::CropRect crop;
    int rotation = 0;
    QString key;
    bool mirrored = false;
  };

  // The cache key of a stored project: its id, update stamp, image file, geometry and lines.
  QString keyOf(const Project& pr, const Look& look);
  QPixmap cached(const QString& key);

  // Each source: decoded or scaled on the pool, composed on `ctx`'s thread, then `ready`. Nothing
  // runs once `ctx` is gone.
  void build(QObject* ctx, std::vector<Source> sources, const Look& look,
             std::function<void(const QString& id, const QPixmap& thumb)> ready);

}  // namespace stencil::gui::projectThumbs
