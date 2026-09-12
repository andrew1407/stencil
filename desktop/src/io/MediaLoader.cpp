#include "MediaLoader.hpp"
#include "mediaLoaderParts.hpp"
#include "fetchGuard.hpp"
#include <QAudioOutput>
#include <QFileInfo>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>
#include <algorithm>

namespace stencil::gui {

  namespace guard = stencil::net::fetchGuard;



  MediaLoader::MediaLoader(QObject* parent) : QObject(parent) {}

  MediaLoader::~MediaLoader() { cleanupVideo(); }

  void MediaLoader::load(const QString& src, int frame) {
    cleanupVideo();
    src_ = src;
    frame_ = std::max(0, frame);
    done_ = false;
    isVideo_ = false;
    thumbnail_ = QImage();
    fps_ = 0;
    durationMs_ = 0;
    seekIssued_ = false;

    // Resolve to a URL: an existing local file wins (so relative paths and odd
    // names aren't misread as URLs); otherwise fromUserInput turns a bare
    // "example.com/x.png" into a proper http URL.
    const QFileInfo fi(src);
    if (fi.exists()) {
      localPath_ = fi.absoluteFilePath();
      url_ = QUrl::fromLocalFile(localPath_);
    } else {
      localPath_.clear();
      url_ = QUrl::fromUserInput(src);
    }
    resolve();
  }

  void MediaLoader::extractFrames(const QString& src, const QList<int>& indices,
                                  std::function<void(QList<QImage>, QString)> done) {
    if (indices.isEmpty()) {
      done({}, QString());
      return;
    }
    // Sequential driver over load(): one loaded()/failed() per call, so chain
    // the next seek from the completion. Connections are severed on finish so a
    // later plain load() doesn't re-enter this collector.
    struct St {
      QString src;
      QList<int> indices;
      int i = 0;
      QList<QImage> frames;
      QMetaObject::Connection ok, fail;
      std::function<void(QList<QImage>, QString)> done;
    };
    auto st = std::make_shared<St>();
    st->src = src;
    st->indices = indices;
    st->done = std::move(done);
    const auto finish = [st](const QString& error) {
      QObject::disconnect(st->ok);
      QObject::disconnect(st->fail);
      st->done(st->frames, error);
    };
    st->ok = connect(this, &MediaLoader::loaded, this,
                     [this, st, finish](const QImage& img, const QString&) {
                       st->frames.append(img);
                       if (++st->i >= st->indices.size()) {
                         finish(QString());
                         return;
                       }
                       load(st->src, st->indices.at(st->i));
                     });
    st->fail = connect(this, &MediaLoader::failed, this,
                       [finish](const QString& message) { finish(message); });
    load(src, st->indices.first());
  }

  void MediaLoader::resolve() {
    if (!url_.isValid()) {
      fail(QStringLiteral("Invalid --src: %1").arg(src_));
      return;
    }
    const bool video = looksLikeVideo(src_, url_);

    if (!isHttp(url_)) {
      if (video) {
        startVideo(url_);
        return;
      }
      QImage img(localPath_.isEmpty() ? url_.toLocalFile() : localPath_);
      if (!img.isNull()) {
        const QString path = localPath_;
        done_ = true;
        emit loaded(img, path);
        return;
      }
      // Extensionless or misdetected — give the media decoder a chance.
      startVideo(url_);
      return;
    }

    // Remote URL — refuse an internal target before EITHER branch reaches it.
    const QString why = guard::blockedReason(url_, /*strict=*/false);
    if (!why.isEmpty()) {
      fail(QStringLiteral("Could not fetch --src: %1").arg(why));
      return;
    }
    if (video) {
      startVideo(url_);  // QMediaPlayer streams a direct media URL itself
      return;
    }
    // Unknown remote: download and try to decode as an image; if that fails,
    // fall back to treating the URL as streamable video.
    const QUrl u = url_;
    guard::get(this, u, /*strict=*/false, [this, u](const QByteArray& bytes, const QString& err) {
      if (done_) return;
      QImage img;
      if (err.isEmpty() && img.loadFromData(bytes)) {
        done_ = true;
        emit loaded(img, QString());
        return;
      }
      startVideo(u);  // not an image (or no bytes): a media stream may still work
      if (!player_ && !err.isEmpty()) fail(QStringLiteral("Could not fetch --src: %1").arg(err));
    });
  }
}

