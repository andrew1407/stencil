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
    n = std::clamp(n, frame->minimum(), frame->maximum());
    {
      const QSignalBlocker bs(frameSlider);
      frameSlider->setValue(n);
    }
    {
      const QSignalBlocker bf(frame);
      frame->setValue(n);
    }
    if (previewIsVideo) fetchTimer->start();
  }

  // Bound the slider + spin box to the video's frame count (best-effort: a stream
  // with no known duration leaves a generous open range so any frame can be typed).
  void OpenImageDialog::applyFrameBounds() {
    const int count = preview->frameCount();
    const int maxFrame = count > 0 ? count - 1 : 1'000'000;
    const QSignalBlocker bs(frameSlider);
    const QSignalBlocker bf(frame);
    frameSlider->setMaximum(maxFrame);
    frame->setMaximum(maxFrame);
    const int cur = std::min(frame->value(), maxFrame);
    frameSlider->setValue(cur);
    frame->setValue(cur);
  }

  // Load the video once into a persistent player + sink so scrubbing seeks a ready
  // stream (fast + accurate) instead of re-streaming a fresh player each time.
  void OpenImageDialog::setupScrubPlayer(const QUrl& url) {
    teardownScrubPlayer();
    if (url.isEmpty()) return;
    scrub.player = new QMediaPlayer(this);
    scrub.audio = new QAudioOutput(this);
    scrub.audio->setMuted(true);
    scrub.player->setAudioOutput(scrub.audio);
    scrub.sink = new QVideoSink(this);
    scrub.player->setVideoSink(scrub.sink);
    connect(scrub.sink, &QVideoSink::videoFrameChanged, this, &OpenImageDialog::onScrubFrame);
    connect(scrub.player, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus s) {
              if (s == QMediaPlayer::LoadedMedia || s == QMediaPlayer::BufferedMedia)
                seekScrub(frame->value());  // render the current frame once ready
            });
    scrub.player->setSource(url);
  }

  void OpenImageDialog::teardownScrubPlayer() {
    scrub.pending = false;
    if (scrub.player) {
      scrub.player->stop();
      scrub.player->setVideoSink(nullptr);
      scrub.player->deleteLater();
      scrub.player = nullptr;
    }
    if (scrub.sink) {
      scrub.sink->deleteLater();
      scrub.sink = nullptr;
    }
    if (scrub.audio) {
      scrub.audio->deleteLater();
      scrub.audio = nullptr;
    }
  }

  // Seek the persistent player to a frame. Playback is briefly required for the sink
  // to emit a frame at the new position; onScrubFrame() grabs it and pauses.
  void OpenImageDialog::seekScrub(int frame) {
    if (!scrub.player) return;
    const double fps = scrub.fps > 0 ? scrub.fps : 30.0;
    scrub.targetMs = static_cast<qint64>(frame / fps * 1000.0 + 0.5);
    if (scrub.durationMs > 0)
      scrub.targetMs = std::min(scrub.targetMs, std::max<qint64>(0, scrub.durationMs - 1));
    scrub.pending = true;
    scrub.player->setPosition(scrub.targetMs);
    scrub.player->play();
  }

  // A frame rendered by the scrub player: once playback reaches the seek target, grab
  // it, pause, and show it (unless the embedded preview image is the chosen source).
  void OpenImageDialog::onScrubFrame(const QVideoFrame& frame) {
    if (!scrub.pending || !frame.isValid()) return;
    if (scrub.targetMs > 0 && scrub.player &&
        scrub.player->position() + 60 < scrub.targetMs)
      return;  // still streaming up to the seek point — wait for the target frame
    const QImage img = frame.toImage();
    if (img.isNull()) return;
    scrub.pending = false;
    if (scrub.player) scrub.player->pause();
    frameImage = img.copy();
    if (previewIsVideo) updateVideoPreview();
  }

}  // namespace stencil::gui
