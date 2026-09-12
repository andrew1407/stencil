#include "../support/searchCombo.hpp"
#include "openImageDialogParts.hpp"
#include "openImageDialog.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "../support/modalChrome.hpp"
#include "../support/modalReveal.hpp"
#include "../support/underlineTabBar.hpp"
#include "mediaLoader.hpp"
#include <algorithm>
#include <QAudioOutput>
#include <QGraphicsOpacityEffect>
#include <QPointer>
#include <QPropertyAnimation>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMediaPlayer>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <QVideoSink>

namespace stencil::gui {

  // Decode the current source (image, or the chosen video frame) into the preview.
  void OpenImageDialog::doPreview() {
    const QString src = source();
    if (src.isEmpty()) {
      setHint("Choose a file or paste a URL first.");
      return;
    }
    setHint("Loading…");
    previewedSource_ = src;
    preview_->load(src, frame_->value());
  }

  // The typed source has moved on from the one that was previewed: keep the picture up (it
  // is still what the user asked to see) but drop everything derived from it, so nothing
  // downstream mistakes it for a preview of the CURRENT source.
  void OpenImageDialog::stalePreview() {
    if (fetchTimer_) fetchTimer_->stop();
    teardownScrubPlayer();
    previewImage_ = QImage();
    frameImage_ = QImage();
    thumbImage_ = QImage();
    previewIsVideo_ = false;
    frameRow_->setVisible(false);
    quickcropRow_->setVisible(false);
    usePreview_->setEnabled(false);
    if (previewLabel_->isVisible()) setHint("Preview of the previous URL — press Preview to load this one.");
  }

  void OpenImageDialog::resetPreviewState() {
    previewedSource_.clear();
    if (fetchTimer_) fetchTimer_->stop();
    teardownScrubPlayer();
    previewImage_ = QImage();
    frameImage_ = QImage();
    thumbImage_ = QImage();
    previewIsVideo_ = false;
    clearPreviewImage();
    setHint({});
    frameRow_->setVisible(false);
    quickcropRow_->setVisible(false);
    usePreview_->setEnabled(false);
    frame_->setEnabled(true);
    frameTotal_->clear();
  }

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
    if (previewIsVideo_ && !usePreview_->isChecked()) fetchTimer_->start();
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
    frameTotal_->setText(count > 0 ? QString("/ %1").arg(maxFrame) : QString());
  }

  // Load the video once into a persistent player + sink so scrubbing seeks a ready
  // stream (fast + accurate) instead of re-streaming a fresh player each time.
  void OpenImageDialog::setupScrubPlayer(const QUrl& url) {
    teardownScrubPlayer();
    if (url.isEmpty()) return;
    scrubPlayer_ = new QMediaPlayer(this);
    scrubAudio_ = new QAudioOutput(this);
    scrubAudio_->setMuted(true);
    scrubPlayer_->setAudioOutput(scrubAudio_);
    scrubSink_ = new QVideoSink(this);
    scrubPlayer_->setVideoSink(scrubSink_);
    connect(scrubSink_, &QVideoSink::videoFrameChanged, this, &OpenImageDialog::onScrubFrame);
    connect(scrubPlayer_, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus s) {
              if (s == QMediaPlayer::LoadedMedia || s == QMediaPlayer::BufferedMedia)
                seekScrub(frame_->value());  // render the current frame once ready
            });
    scrubPlayer_->setSource(url);
  }

  void OpenImageDialog::teardownScrubPlayer() {
    scrubPending_ = false;
    if (scrubPlayer_) {
      scrubPlayer_->stop();
      scrubPlayer_->setVideoSink(nullptr);
      scrubPlayer_->deleteLater();
      scrubPlayer_ = nullptr;
    }
    if (scrubSink_) {
      scrubSink_->deleteLater();
      scrubSink_ = nullptr;
    }
    if (scrubAudio_) {
      scrubAudio_->deleteLater();
      scrubAudio_ = nullptr;
    }
  }

  // Seek the persistent player to a frame. Playback is briefly required for the sink
  // to emit a frame at the new position; onScrubFrame() grabs it and pauses.
  void OpenImageDialog::seekScrub(int frame) {
    if (!scrubPlayer_) return;
    const double fps = scrubFps_ > 0 ? scrubFps_ : 30.0;
    scrubTargetMs_ = static_cast<qint64>(frame / fps * 1000.0 + 0.5);
    if (scrubDurationMs_ > 0)
      scrubTargetMs_ = std::min(scrubTargetMs_, std::max<qint64>(0, scrubDurationMs_ - 1));
    scrubPending_ = true;
    scrubPlayer_->setPosition(scrubTargetMs_);
    scrubPlayer_->play();
  }

  // A frame rendered by the scrub player: once playback reaches the seek target, grab
  // it, pause, and show it (unless the embedded preview image is the chosen source).
  void OpenImageDialog::onScrubFrame(const QVideoFrame& frame) {
    if (!scrubPending_ || !frame.isValid()) return;
    if (scrubTargetMs_ > 0 && scrubPlayer_ &&
        scrubPlayer_->position() + 60 < scrubTargetMs_)
      return;  // still streaming up to the seek point — wait for the target frame
    const QImage img = frame.toImage();
    if (img.isNull()) return;
    scrubPending_ = false;
    if (scrubPlayer_) scrubPlayer_->pause();
    frameImage_ = img.copy();
    if (previewIsVideo_ && !usePreview_->isChecked()) updateVideoPreview();
  }

  // The muted status line under the preview: shown only when it has something to
  // say, so an untouched dialog keeps no blank line for it.
  void OpenImageDialog::setHint(const QString& text) {
    previewHint_->setText(text);
    previewHint_->setVisible(!text.isEmpty());
  }

  // Drop the rendered preview AND its box — an empty bordered panel is not a preview.
  void OpenImageDialog::clearPreviewImage() {
    previewLabel_->clear();
    previewLabel_->setVisible(false);
  }

  void OpenImageDialog::showPreview(const QImage& img, const QString& hint) {
    previewImage_ = img;
    if (img.isNull()) {
      clearPreviewImage();
      return;
    }
    previewLabel_->setPixmap(QPixmap::fromImage(img).scaled(
        PREVIEW_MAX_W, PREVIEW_MAX_H, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    previewLabel_->setVisible(true);
    setHint(hint);
  }

  // For a video, show either the embedded preview image (when chosen + available) or
  // the seeked frame. The frame spinbox is irrelevant while the preview is used.
  void OpenImageDialog::updateVideoPreview() {
    const bool usePrev = usePreview_->isChecked() && !thumbImage_.isNull();
    frame_->setEnabled(!usePrev);
    frameSlider_->setEnabled(!usePrev);
    const QImage& shown = usePrev ? thumbImage_ : frameImage_;
    showPreview(shown,
                usePrev
                    ? QString("Using the video's embedded preview image (%1×%2).")
                          .arg(shown.width()).arg(shown.height())
                    : QString("Video %1×%2 — drag the slider or type a frame, then open.")
                          .arg(shown.width()).arg(shown.height()));
  }
}

