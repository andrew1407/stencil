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

  void MediaLoader::startVideo(const QUrl& url) {
    if (done) return;
    isVideo = true;  // every video path (incl. the image-decode fallback) funnels here
    cleanupVideo();  // defensive: never run two pipelines at once
    player = new QMediaPlayer(this);
    audio = new QAudioOutput(this);
    audio->setMuted(true);  // a silent frame grab — never play sound
    player->setAudioOutput(audio);
    sink = new QVideoSink(this);
    player->setVideoSink(sink);

    connect(sink, &QVideoSink::videoFrameChanged, this,
            &MediaLoader::onVideoFrame);
    connect(player, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus s) {
              if (s == QMediaPlayer::LoadedMedia ||
                  s == QMediaPlayer::BufferedMedia)
                tryStartVideoSeek();
              else if (s == QMediaPlayer::InvalidMedia)
                fail(QStringLiteral("Not a readable image or video: %1").arg(src));
            });
    connect(player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString& msg) {
              fail(QStringLiteral("Media error: %1").arg(msg));
            });

    timeout = new QTimer(this);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, this, [this] {
      fail(QStringLiteral("Timed out reading --src: %1").arg(src));
    });
    timeout->start(VIDEO_TIMEOUT_MS);

    player->setSource(url);
  }

  // Once the media is loaded, convert the requested frame index to a position and
  // seek there, then start playback so the sink renders a frame at that position.
  void MediaLoader::tryStartVideoSeek() {
    if (done || !player || seekIssued) return;
    seekIssued = true;

    double fps = player->metaData().value(QMediaMetaData::VideoFrameRate).toDouble();
    if (fps <= 0.0) fps = ASSUMED_FPS;
    this->fps = fps;                       // expose for the dialog's slider / frame count
    targetMs = static_cast<qint64>(frame / fps * 1000.0 + 0.5);
    const qint64 dur = player->duration();
    if (dur > 0) {
      durationMs = dur;              // expose for the dialog's slider bounds
      targetMs = std::min(targetMs, std::max<qint64>(0, dur - 1));
    }

    captureThumbnail();  // metadata is available now (LoadedMedia) — grab any cover art

    if (targetMs > 0) player->setPosition(targetMs);
    // Playback is required for the sink to emit frames; we stop on the first
    // usable one in onVideoFrame().
    player->play();
  }

  // Read the container's embedded preview/cover image, if any. Best-effort: many
  // files (and most network streams) carry none, leaving thumbnail null.
  void MediaLoader::captureThumbnail() {
    if (!player || !thumbnail.isNull()) return;
    const QMediaMetaData md = player->metaData();
    QImage t = md.value(QMediaMetaData::ThumbnailImage).value<QImage>();
    if (t.isNull()) t = md.value(QMediaMetaData::CoverArtImage).value<QImage>();
    if (!t.isNull()) thumbnail = t;
  }

  void MediaLoader::onVideoFrame(const QVideoFrame& frame) {
    if (done || !seekIssued || !frame.isValid()) return;
    // The decoder may stream frames from the start before the seek lands, so wait until playback has
    // reached the position. 60 ms tolerance ~ a couple of frames; the timeout backstops a lost seek.
    if (targetMs > 0 && player && player->position() + 60 < targetMs) return;
    const QImage img = frame.toImage();
    if (img.isNull()) return;  // wait for the next, decodable frame
    captureThumbnail();  // last chance to read cover art (metadata may arrive late)
    done = true;
    if (player) player->stop();
    if (timeout) timeout->stop();
    // A video frame has no on-disk original, so localPath is empty.
    emit loaded(img.copy(), QString());
    cleanupVideo();
  }

  void MediaLoader::fail(const QString& message) {
    if (done) return;
    done = true;
    cleanupVideo();
    emit failed(message);
  }

  void MediaLoader::cleanupVideo() {
    if (timeout) {
      timeout->stop();
      timeout->deleteLater();
      timeout = nullptr;
    }
    if (player) {
      player->stop();
      player->setVideoSink(nullptr);
      player->deleteLater();
      player = nullptr;
    }
    if (sink) {
      sink->deleteLater();
      sink = nullptr;
    }
    if (audio) {
      audio->deleteLater();
      audio = nullptr;
    }
    seekIssued = false;
  }
}

