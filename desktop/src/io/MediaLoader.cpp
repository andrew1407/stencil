#include "MediaLoader.hpp"
#include "mediaLoaderParts.hpp"
#include "fetchGuard.hpp"
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPromise>
#include <QThreadPool>
#include <algorithm>
#include <memory>

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
    ++loadSerial;
    isVideo = false;
    thumbnail = QImage();
    fps = 0;
    durationMs = 0;
    seekIssued = false;

    if (src.startsWith(QLatin1String("data:"), Qt::CaseInsensitive)) {
      localPath.clear();
      url.clear();
      decodeThen([src] {
        QImage img;
        return decodeDataUri(src, img) ? img : QImage();
      }, [this](const QImage& img) {
        // A malformed payload fails like any other candidate, so the walk carries on past it.
        if (img.isNull()) return fail(QStringLiteral("Could not decode the inline image"));
        done = true;
        emit loaded(img, QString());
      });
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
      const QString file = localPath.isEmpty() ? url.toLocalFile() : localPath;
      auto still = std::make_shared<bool>(false);   // written on the pool, read once it has answered
      decodeThen([file, still] {
        QFile f(file);
        *still = f.open(QIODevice::ReadOnly) && isStillImage(f.read(SIGNATURE_BYTES));
        return QImage(file);
      }, [this, still](const QImage& img) {
        if (img.isNull() && *still) return fail(unreadableMessage(src));
        if (img.isNull()) return startVideo(url);   // extensionless or misdetected: try the media decoder
        done = true;
        emit loaded(img, localPath);
      });
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
    // Unknown remote: download and decode it as an image; bytes that fail with no still-image
    // signature may yet be a video, so the URL is streamed.
    const QUrl u = url;
    guard::get(this, u, /*strict=*/false, [this, u](const QByteArray& bytes, const QString& err) {
      if (done) return;
      auto notImage = [this, u, err] {
        // A candidate still waiting makes the video probe a stall: the answer has already
        // settled this one (the browser refuses an unaccepted content type and moves on).
        if (hasMoreCandidates()) {
          fail(QStringLiteral("Could not fetch --src: %1")
                   .arg(err.isEmpty() ? QStringLiteral("that URL is not an image") : err));
          return;
        }
        startVideo(u);  // not an image (or no bytes): a media stream may still work
        if (!player && !err.isEmpty()) fail(QStringLiteral("Could not fetch --src: %1").arg(err));
      };
      if (!err.isEmpty() || bytes.isEmpty()) return notImage();
      const bool still = isStillImage(bytes.left(SIGNATURE_BYTES));
      decodeThen([bytes] { return QImage::fromData(bytes); }, [this, notImage, still](const QImage& img) {
        if (img.isNull() && still && !hasMoreCandidates()) return fail(unreadableMessage(src));
        if (img.isNull()) return notImage();
        done = true;
        emit loaded(img, QString());
      });
    });
  }

  void MediaLoader::decodeThen(std::function<QImage()> work,
                               std::function<void(const QImage&)> then) {
    const quint64 serial = loadSerial;
    auto* watcher = new QFutureWatcher<QImage>(this);
    auto promise = std::make_shared<QPromise<QImage>>();
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, serial, then] {
      watcher->deleteLater();
      if (serial == loadSerial && !done) then(watcher->future().resultCount() ? watcher->result() : QImage());
    });
    watcher->setFuture(promise->future());
    promise->start();
    QThreadPool::globalInstance()->start([promise, work] {
      promise->addResult(work());
      promise->finish();
    });
  }
}

