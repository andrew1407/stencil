// OpenImageDialog — the VIDEO SCRUB: one persistent player, loaded once per source, that
// the Frame field and the bar under the picture seek. Kept apart from the preview itself
// (OpenImageDialogPreview.cpp) because a still never touches any of it.
#include "MediaLoader.hpp"
#include "OpenImageDialog.hpp"
#include "openImageDialogParts.hpp"

#include <QAudioOutput>
#include <QLabel>
#include <QMediaPlayer>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>

#include <algorithm>
#include <cmath>

namespace stencil::gui {

  // Mirror a chosen frame to BOTH the slider and the spin box, validated against the
  // range, then schedule a debounced seek (QSignalBlocker prevents the set echoing).
  void OpenImageDialog::setFrame(int n) {
    n = std::clamp(n, frame_->minimum(), frame_->maximum());
    {
      const QSignalBlocker bs(frameSlider_);
      frameSlider_->setValue(n);
    }
    {
      const QSignalBlocker bf(frame_);
      frame_->setValue(n);
    }
    if (previewIsVideo_) fetchTimer_->start();
  }

  // Bound the slider + spin box to the video's frame count (best-effort: a stream
  // with no known duration leaves a generous open range so any frame can be typed).
  void OpenImageDialog::applyFrameBounds() {
    const int count = preview_->frameCount();
    const int maxFrame = count > 0 ? count - 1 : 1'000'000;
    const QSignalBlocker bs(frameSlider_);
    const QSignalBlocker bf(frame_);
    frameSlider_->setMaximum(maxFrame);
    frame_->setMaximum(maxFrame);
    const int cur = std::min(frame_->value(), maxFrame);
    frameSlider_->setValue(cur);
    frame_->setValue(cur);
  }

  // Load the video once into a persistent player + sink so scrubbing seeks a ready
  // stream (fast + accurate) instead of re-streaming a fresh player each time.
  void OpenImageDialog::setupScrubPlayer(const QUrl& url) {
    teardownScrubPlayer();
    if (url.isEmpty()) return;
    scrub_.player = new QMediaPlayer(this);
    scrub_.audio = new QAudioOutput(this);
    scrub_.audio->setMuted(true);
    scrub_.player->setAudioOutput(scrub_.audio);
    scrub_.sink = new QVideoSink(this);
    scrub_.player->setVideoSink(scrub_.sink);
    connect(scrub_.sink, &QVideoSink::videoFrameChanged, this, &OpenImageDialog::onScrubFrame);
    connect(scrub_.player, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus s) {
              if (s == QMediaPlayer::LoadedMedia || s == QMediaPlayer::BufferedMedia)
                seekScrub(frame_->value());  // render the current frame once ready
            });
    scrub_.player->setSource(url);
  }

  void OpenImageDialog::teardownScrubPlayer() {
    scrub_.pending = false;
    if (scrub_.player) {
      scrub_.player->stop();
      scrub_.player->setVideoSink(nullptr);
      scrub_.player->deleteLater();
      scrub_.player = nullptr;
    }
    if (scrub_.sink) {
      scrub_.sink->deleteLater();
      scrub_.sink = nullptr;
    }
    if (scrub_.audio) {
      scrub_.audio->deleteLater();
      scrub_.audio = nullptr;
    }
  }

  // Seek the persistent player to a frame. Playback is briefly required for the sink
  // to emit a frame at the new position; onScrubFrame() grabs it and pauses.
  void OpenImageDialog::seekScrub(int frame) {
    if (!scrub_.player) return;
    const double fps = scrub_.fps > 0 ? scrub_.fps : 30.0;
    scrub_.targetMs = static_cast<qint64>(frame / fps * 1000.0 + 0.5);
    if (scrub_.durationMs > 0)
      scrub_.targetMs = std::min(scrub_.targetMs, std::max<qint64>(0, scrub_.durationMs - 1));
    scrub_.pending = true;
    scrub_.player->setPosition(scrub_.targetMs);
    scrub_.player->play();
  }

  // A frame rendered by the scrub player: once playback reaches the seek target, grab
  // it, pause, and show it (unless the embedded preview image is the chosen source).
  void OpenImageDialog::onScrubFrame(const QVideoFrame& frame) {
    if (!scrub_.pending || !frame.isValid()) return;
    if (scrub_.targetMs > 0 && scrub_.player &&
        scrub_.player->position() + 60 < scrub_.targetMs)
      return;  // still streaming up to the seek point — wait for the target frame
    const QImage img = frame.toImage();
    if (img.isNull()) return;
    scrub_.pending = false;
    if (scrub_.player) scrub_.player->pause();
    frameImage_ = img.copy();
    if (previewIsVideo_) updateVideoPreview();
  }

}  // namespace stencil::gui
