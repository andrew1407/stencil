#include "../support/searchCombo.hpp"
#include "linksDialogParts.hpp"
#include "linksDialog.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "fetchGuard.hpp"
#include "mediaLoader.hpp"
#include "../support/modalChrome.hpp"
#include <algorithm>
#include <QPalette>
#include <QAudioOutput>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMediaPlayer>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <QVideoSink>

namespace stencil::gui {

  bool LinksDialog::eventFilter(QObject* obj, QEvent* event) {
    if ((obj == urlEdit_ || obj == urlResourceEdit_) &&
        event->type() == QEvent::KeyPress) {
      const auto* k = static_cast<QKeyEvent*>(event);
      if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
        doPreview();    // Enter previews the URL instead of accepting the dialog
        return true;    // consume, so the default (OK) button doesn't fire
      }
    }
    return QDialog::eventFilter(obj, event);
  }

  void LinksDialog::openInBrowser(const QLineEdit* field) const {
    const QUrl u = QUrl::fromUserInput(field->text().trimmed());
    // http(s) only — a typed file:/smb: URL must never reach the OS handler.
    if (stencil::net::fetchGuard::isWebScheme(u)) QDesktopServices::openUrl(u);
    else if (u.isValid()) previewHint_->setText("Only http(s) links can be opened.");
  }

  void LinksDialog::doPreview() {
    const QString url = urlEdit_->text().trimmed();
    if (url.isEmpty()) {
      previewHint_->setText("Enter an image or video URL first.");
      return;
    }
    previewHint_->setText("Loading…");
    loadBtn_->setEnabled(false);
    preview_->load(url, frame_->value());
  }

  void LinksDialog::resetPreviewState() {
    if (fetchTimer_) fetchTimer_->stop();
    teardownScrubPlayer();
    previewImage_ = QImage();
    frameImage_ = QImage();
    thumbImage_ = QImage();
    previewIsVideo_ = false;
    previewLabel_->clear();
    previewLabel_->setVisible(false);
    previewHint_->clear();
    frameRow_->setVisible(false);
    quickcropRow_->setVisible(false);
    usePreview_->setEnabled(false);
    frame_->setEnabled(true);
    frameTotal_->clear();
    loadBtn_->setEnabled(false);
  }

  // Mirror a chosen frame to BOTH the slider and the spin box, validated against
  // the range, then schedule a debounced seek (QSignalBlocker keeps the set from
  // echoing back, so both land on the same final value).
  void LinksDialog::setFrame(int n) {
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
  void LinksDialog::applyFrameBounds() {
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
  // stream (fast and accurate) instead of re-streaming a fresh player each time.
  void LinksDialog::setupScrubPlayer(const QUrl& url) {
    teardownScrubPlayer();
    if (url.isEmpty()) return;
    scrubPlayer_ = new QMediaPlayer(this);
    scrubAudio_ = new QAudioOutput(this);
    scrubAudio_->setMuted(true);
    scrubPlayer_->setAudioOutput(scrubAudio_);
    scrubSink_ = new QVideoSink(this);
    scrubPlayer_->setVideoSink(scrubSink_);
    connect(scrubSink_, &QVideoSink::videoFrameChanged, this, &LinksDialog::onScrubFrame);
    connect(scrubPlayer_, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus s) {
              if (s == QMediaPlayer::LoadedMedia || s == QMediaPlayer::BufferedMedia)
                seekScrub(frame_->value());  // render the current frame once ready
            });
    scrubPlayer_->setSource(url);
  }

  void LinksDialog::teardownScrubPlayer() {
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
  void LinksDialog::seekScrub(int frame) {
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
  void LinksDialog::onScrubFrame(const QVideoFrame& frame) {
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
}

