#include "../../../support/menu/SearchCombo.hpp"
#include "linksDialogParts.hpp"
#include "LinksDialog.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "fetchGuard.hpp"
#include "MediaLoader.hpp"
#include "../../../support/modal/modalChrome.hpp"
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
    if ((obj == urlEdit || obj == urlResourceEdit) &&
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
    else if (u.isValid()) previewHint->setText("Only http(s) links can be opened.");
  }

  void LinksDialog::doPreview() {
    const QString url = urlEdit->text().trimmed();
    if (url.isEmpty()) {
      previewHint->setText("Enter an image or video URL first.");
      return;
    }
    previewHint->setText("Loading…");
    loadBtn->setEnabled(false);
    preview->load(url, frame->value());
  }

  void LinksDialog::resetPreviewState() {
    if (fetchTimer) fetchTimer->stop();
    teardownScrubPlayer();
    previewImage = QImage();
    frameImage = QImage();
    thumbImage = QImage();
    previewIsVideo = false;
    previewLabel->clear();
    previewLabel->setVisible(false);
    previewHint->clear();
    frameRow->setVisible(false);
    quickcropRow->setVisible(false);
    usePreview->setEnabled(false);
    frame->setEnabled(true);
    frameTotal->clear();
    loadBtn->setEnabled(false);
  }

  // Mirror a chosen frame to BOTH the slider and the spin box, then schedule a debounced seek
  // (QSignalBlocker keeps the set from echoing back, so both land on the same final value).
  void LinksDialog::setFrame(int n) {
    n = std::clamp(n, frame->minimum(), frame->maximum());
    {
      const QSignalBlocker bs(frameSlider);
      frameSlider->setValue(n);
    }
    {
      const QSignalBlocker bf(frame);
      frame->setValue(n);
    }
    if (previewIsVideo && !usePreview->isChecked()) fetchTimer->start();
  }

  // Bound the slider + spin box to the video's frame count (best-effort: a stream
  // with no known duration leaves a generous open range so any frame can be typed).
  void LinksDialog::applyFrameBounds() {
    const int count = preview->frameCount();
    const int maxFrame = count > 0 ? count - 1 : 1'000'000;
    const QSignalBlocker bs(frameSlider);
    const QSignalBlocker bf(frame);
    frameSlider->setMaximum(maxFrame);
    frame->setMaximum(maxFrame);
    const int cur = std::min(frame->value(), maxFrame);
    frameSlider->setValue(cur);
    frame->setValue(cur);
    frameTotal->setText(count > 0 ? QString("/ %1").arg(maxFrame) : QString());
  }

  // Load the video once into a persistent player + sink so scrubbing seeks a ready
  // stream (fast and accurate) instead of re-streaming a fresh player each time.
  void LinksDialog::setupScrubPlayer(const QUrl& url) {
    teardownScrubPlayer();
    if (url.isEmpty()) return;
    scrubPlayer = new QMediaPlayer(this);
    scrubAudio = new QAudioOutput(this);
    scrubAudio->setMuted(true);
    scrubPlayer->setAudioOutput(scrubAudio);
    scrubSink = new QVideoSink(this);
    scrubPlayer->setVideoSink(scrubSink);
    connect(scrubSink, &QVideoSink::videoFrameChanged, this, &LinksDialog::onScrubFrame);
    connect(scrubPlayer, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus s) {
              if (s == QMediaPlayer::LoadedMedia || s == QMediaPlayer::BufferedMedia)
                seekScrub(frame->value());  // render the current frame once ready
            });
    scrubPlayer->setSource(url);
  }

  void LinksDialog::teardownScrubPlayer() {
    scrubPending = false;
    if (scrubPlayer) {
      scrubPlayer->stop();
      scrubPlayer->setVideoSink(nullptr);
      scrubPlayer->deleteLater();
      scrubPlayer = nullptr;
    }
    if (scrubSink) {
      scrubSink->deleteLater();
      scrubSink = nullptr;
    }
    if (scrubAudio) {
      scrubAudio->deleteLater();
      scrubAudio = nullptr;
    }
  }

  // Seek the persistent player to a frame. Playback is briefly required for the sink
  // to emit a frame at the new position; onScrubFrame() grabs it and pauses.
  void LinksDialog::seekScrub(int frame) {
    if (!scrubPlayer) return;
    const double fps = scrubFps > 0 ? scrubFps : 30.0;
    scrubTargetMs = static_cast<qint64>(frame / fps * 1000.0 + 0.5);
    if (scrubDurationMs > 0)
      scrubTargetMs = std::min(scrubTargetMs, std::max<qint64>(0, scrubDurationMs - 1));
    scrubPending = true;
    scrubPlayer->setPosition(scrubTargetMs);
    scrubPlayer->play();
  }

  // A frame rendered by the scrub player: once playback reaches the seek target, grab
  // it, pause, and show it (unless the embedded preview image is the chosen source).
  void LinksDialog::onScrubFrame(const QVideoFrame& frame) {
    if (!scrubPending || !frame.isValid()) return;
    if (scrubTargetMs > 0 && scrubPlayer &&
        scrubPlayer->position() + 60 < scrubTargetMs)
      return;  // still streaming up to the seek point — wait for the target frame
    const QImage img = frame.toImage();
    if (img.isNull()) return;
    scrubPending = false;
    if (scrubPlayer) scrubPlayer->pause();
    frameImage = img.copy();
    if (previewIsVideo && !usePreview->isChecked()) updateVideoPreview();
  }
}

