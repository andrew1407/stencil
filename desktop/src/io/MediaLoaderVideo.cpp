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
    timeout_->start(VIDEO_TIMEOUT_MS);

    player_->setSource(url);
  }

  // Once the media is loaded, convert the requested frame index to a position and
  // seek there, then start playback so the sink renders a frame at that position.
  void MediaLoader::tryStartVideoSeek() {
    if (done_ || !player_ || seekIssued_) return;
    seekIssued_ = true;

    double fps = player_->metaData().value(QMediaMetaData::VideoFrameRate).toDouble();
    if (fps <= 0.0) fps = ASSUMED_FPS;
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

