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

  namespace {
    // A data: uri carries its own bytes, so there is no url to resolve: the browser→desktop
    // hand-off and the bitmap a web drag renders both arrive this way.
    bool decodeDataUri(const QString& src, QImage& out) {
      const int comma = src.indexOf(QLatin1Char(','));
      if (comma < 0) return false;
      const QByteArray payload = src.mid(comma + 1).toUtf8();
      return out.loadFromData(
          src.left(comma).contains(QLatin1String(";base64"), Qt::CaseInsensitive)
              ? QByteArray::fromBase64(payload)
              : QByteArray::fromPercentEncoding(payload));
    }
  }  // namespace

  MediaLoader::MediaLoader(QObject* parent) : QObject(parent) {}

  MediaLoader::~MediaLoader() { cleanupVideo(); }

  void MediaLoader::load(const QString& src, int frame) {
    candidates.clear();
    candidateIndex = 0;
    firstError.clear();
    beginLoad(src, frame);
  }

  void MediaLoader::loadFirstOf(const QStringList& sources, int frame) {
    QStringList ranked;
    for (const QString& s : sources)
      if (!s.trimmed().isEmpty()) ranked << s;
    if (ranked.size() < 2) {
      load(ranked.value(0), frame);
      return;
    }
    candidates = ranked;
    candidateIndex = 0;
    firstError.clear();
    beginLoad(candidates.first(), frame);
  }

  void MediaLoader::beginLoad(const QString& src, int frame) {
    cleanupVideo();
    this->src = src;
    this->frame = std::max(0, frame);
    done = false;
    isVideo = false;
    thumbnail = QImage();
    fps = 0;
    durationMs = 0;
    seekIssued = false;

    if (src.startsWith(QLatin1String("data:"), Qt::CaseInsensitive)) {
      localPath.clear();
      url.clear();
      QImage img;
      // A malformed payload fails like any other candidate, so the walk carries on past it.
      if (!decodeDataUri(src, img)) {
        fail(QStringLiteral("Could not decode the inline image"));
        return;
      }
      done = true;
      emit loaded(img, QString());
      return;
    }

    // Resolve to a URL: an existing local file wins (so relative paths and odd names are not misread
    // as URLs); otherwise fromUserInput turns a bare "example.com/x.png" into a proper http URL.
    const QFileInfo fi(src);
    if (fi.exists()) {
      localPath = fi.absoluteFilePath();
      url = QUrl::fromLocalFile(localPath);
    } else {
      localPath.clear();
      url = QUrl::fromUserInput(src);
    }
    resolve();
  }

  void MediaLoader::extractFrames(const QString& src, const QList<int>& indices,
                                  std::function<void(QList<QImage>, QString)> done) {
    if (indices.isEmpty()) {
      done({}, QString());
      return;
    }
    // Sequential driver over load(): one loaded()/failed() per call, so chain the next seek from the
    // completion. Connections are severed on finish so a later plain load() cannot re-enter this.
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
    if (!url.isValid()) {
      fail(QStringLiteral("Invalid --src: %1").arg(src));
      return;
    }
    const bool video = looksLikeVideo(src, url);

    if (!isHttp(url)) {
      if (video) {
        startVideo(url);
        return;
      }
      QImage img(localPath.isEmpty() ? url.toLocalFile() : localPath);
      if (!img.isNull()) {
        const QString path = localPath;
        done = true;
        emit loaded(img, path);
        return;
      }
      // Extensionless or misdetected — give the media decoder a chance.
      startVideo(url);
      return;
    }

    // Remote URL — refuse an internal target before EITHER branch reaches it.
    const QString why = guard::blockedReason(url, /*strict=*/false);
    if (!why.isEmpty()) {
      fail(QStringLiteral("Could not fetch --src: %1").arg(why));
      return;
    }
    if (video) {
      startVideo(url);  // QMediaPlayer streams a direct media URL itself
      return;
    }
    // Unknown remote: download and try to decode as an image; if that fails,
    // fall back to treating the URL as streamable video.
    const QUrl u = url;
    guard::get(this, u, /*strict=*/false, [this, u](const QByteArray& bytes, const QString& err) {
      if (done) return;
      QImage img;
      if (err.isEmpty() && img.loadFromData(bytes)) {
        done = true;
        emit loaded(img, QString());
        return;
      }
      // A candidate still waiting makes the 20 s video probe a stall: the answer has already
      // settled this one (the browser refuses an unaccepted content type and moves on).
      if (hasMoreCandidates()) {
        fail(QStringLiteral("Could not fetch --src: %1")
                 .arg(err.isEmpty() ? QStringLiteral("that URL is not an image") : err));
        return;
      }
      startVideo(u);  // not an image (or no bytes): a media stream may still work
      if (!player && !err.isEmpty()) fail(QStringLiteral("Could not fetch --src: %1").arg(err));
    });
  }
}

