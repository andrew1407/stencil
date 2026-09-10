#include "mediaLoader.hpp"
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
    constexpr int kVideoTimeoutMs = 20000;  // give the decoder time to seek+render
    constexpr double kAssumedFps = 30.0;    // fallback when fps metadata is absent

    bool isHttp(const QUrl& u) {
      const QString s = u.scheme();
      return s == "http" || s == "https";
    }

    // Video by container extension (the launch arg's suffix, or the URL path's).
    bool looksLikeVideo(const QString& src, const QUrl& url) {
      return isVideoFileName(src) || isVideoFileName(url.path());
    }
  }  // namespace

  bool isVideoFileName(const QString& path) {
    static const QStringList kVideoExt = {
        "mp4", "m4v", "mov", "webm", "mkv", "avi", "wmv",
        "flv", "mpg", "mpeg", "ogv", "3gp", "ts"};
    return kVideoExt.contains(QFileInfo(path).suffix().toLower());
  }

  bool isImageFileName(const QString& path) {
    static const QStringList kImageExt = {"png", "jpg", "jpeg", "webp", "gif", "bmp"};
    return kImageExt.contains(QFileInfo(path).suffix().toLower());
  }

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
      // ── Local file ──
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

    // ── Remote URL ── refuse an internal target before EITHER branch reaches it.
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

  void MediaLoader::startVideo(const QUrl& url) {
    if (done_) return;
    isVideo_ = true;  // every video path (incl. the image-decode fallback) funnels here
    cleanupVideo();  // defensive: never run two pipelines at once
    player_ = new QMediaPlayer(this);
    audio_ = new QAudioOutput(this);
    audio_->setMuted(true);  // a silent frame grab — never play sound
    player_->setAudioOutput(audio_);
    sink_ = new QVideoSink(this);
    player_->setVideoSink(sink_);

    connect(sink_, &QVideoSink::videoFrameChanged, this,
            &MediaLoader::onVideoFrame);
    connect(player_, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus s) {
              if (s == QMediaPlayer::LoadedMedia ||
                  s == QMediaPlayer::BufferedMedia)
                tryStartVideoSeek();
              else if (s == QMediaPlayer::InvalidMedia)
                fail(QStringLiteral("Not a readable image or video: %1").arg(src_));
            });
    connect(player_, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString& msg) {
              fail(QStringLiteral("Media error: %1").arg(msg));
            });

    timeout_ = new QTimer(this);
    timeout_->setSingleShot(true);
    connect(timeout_, &QTimer::timeout, this, [this] {
      fail(QStringLiteral("Timed out reading --src: %1").arg(src_));
    });
    timeout_->start(kVideoTimeoutMs);

    player_->setSource(url);
  }

  // Once the media is loaded, convert the requested frame index to a position and
  // seek there, then start playback so the sink renders a frame at that position.
  void MediaLoader::tryStartVideoSeek() {
    if (done_ || !player_ || seekIssued_) return;
    seekIssued_ = true;

    double fps = player_->metaData().value(QMediaMetaData::VideoFrameRate).toDouble();
    if (fps <= 0.0) fps = kAssumedFps;
    fps_ = fps;                       // expose for the dialog's slider / frame count
    targetMs_ = static_cast<qint64>(frame_ / fps * 1000.0 + 0.5);
    const qint64 dur = player_->duration();
    if (dur > 0) {
      durationMs_ = dur;              // expose for the dialog's slider bounds
      targetMs_ = std::min(targetMs_, std::max<qint64>(0, dur - 1));
    }

    captureThumbnail();  // metadata is available now (LoadedMedia) — grab any cover art

    if (targetMs_ > 0) player_->setPosition(targetMs_);
    // Playback is required for the sink to emit frames; we stop on the first
    // usable one in onVideoFrame().
    player_->play();
  }

  // Read the container's embedded preview/cover image, if any. Best-effort: many
  // files (and most network streams) carry none, leaving thumbnail_ null.
  void MediaLoader::captureThumbnail() {
    if (!player_ || !thumbnail_.isNull()) return;
    const QMediaMetaData md = player_->metaData();
    QImage t = md.value(QMediaMetaData::ThumbnailImage).value<QImage>();
    if (t.isNull()) t = md.value(QMediaMetaData::CoverArtImage).value<QImage>();
    if (!t.isNull()) thumbnail_ = t;
  }

  void MediaLoader::onVideoFrame(const QVideoFrame& frame) {
    if (done_ || !seekIssued_ || !frame.isValid()) return;
    // For a non-first frame, the decoder may stream frames from the start before
    // the seek lands — wait until playback has reached the requested position so
    // we grab the intended frame, not frame 0. (60 ms tolerance ≈ a couple of
    // frames; the timeout backstops a seek that never arrives.)
    if (targetMs_ > 0 && player_ && player_->position() + 60 < targetMs_) return;
    const QImage img = frame.toImage();
    if (img.isNull()) return;  // wait for the next, decodable frame
    captureThumbnail();  // last chance to read cover art (metadata may arrive late)
    done_ = true;
    if (player_) player_->stop();
    if (timeout_) timeout_->stop();
    // A video frame has no on-disk original, so localPath is empty.
    emit loaded(img.copy(), QString());
    cleanupVideo();
  }

  void MediaLoader::fail(const QString& message) {
    if (done_) return;
    done_ = true;
    cleanupVideo();
    emit failed(message);
  }

  void MediaLoader::cleanupVideo() {
    if (timeout_) {
      timeout_->stop();
      timeout_->deleteLater();
      timeout_ = nullptr;
    }
    if (player_) {
      player_->stop();
      player_->setVideoSink(nullptr);
      player_->deleteLater();
      player_ = nullptr;
    }
    if (sink_) {
      sink_->deleteLater();
      sink_ = nullptr;
    }
    if (audio_) {
      audio_->deleteLater();
      audio_ = nullptr;
    }
    seekIssued_ = false;
  }

}
