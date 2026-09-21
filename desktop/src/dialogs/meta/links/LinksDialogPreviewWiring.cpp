// The add-by-URL preview wiring: what a resolved image or video does to the preview box, the frame
// scrubber and the quick-crop row, what a failure clears, and the debounced seeks behind the slider.
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

  void LinksDialog::wirePreview(QPushButton* previewBtn) {
    preview = new MediaLoader(this);
    connect(preview, &MediaLoader::loaded, this,
            [this](const QImage& img, const QString&) {
              previewIsVideo = preview->isVideoSource();
              if (previewIsVideo) {
                frameImage = img;
                thumbImage = preview->embeddedThumbnail();
                scrubFps = preview->frameRate() > 0 ? preview->frameRate() : 30.0;
                scrubDurationMs = preview->getDurationMs();
                const bool hasThumb = !thumbImage.isNull();
                usePreview->setEnabled(hasThumb);
                if (!hasThumb && usePreview->isChecked())
                  usePreview->setChecked(false);  // (re-renders via toggled)
                frameRow->setVisible(true);
                applyFrameBounds();  // size the slider / spin box to this video
                updateVideoPreview();
                showQuickcrop(frameImage.width(), frameImage.height());
                // Load the video ONCE into a persistent player for live scrubbing
                // (re-streaming per frame, as the detector does, never seeks reliably).
                setupScrubPlayer(preview->resolvedUrl());
              } else {
                teardownScrubPlayer();
                frameImage = QImage();
                thumbImage = QImage();
                frameRow->setVisible(false);
                showPreview(img, QString("Image %1×%2").arg(img.width()).arg(img.height()));
                showQuickcrop(img.width(), img.height());
              }
            });
    connect(preview, &MediaLoader::failed, this, [this](const QString& msg) {
      teardownScrubPlayer();
      previewImage = QImage();
      frameImage = QImage();
      thumbImage = QImage();
      previewIsVideo = false;
      previewLabel->clear();
      previewLabel->setVisible(false);
      frameRow->setVisible(false);
      quickcropRow->setVisible(false);
      usePreview->setEnabled(false);
      previewHint->setText("Could not load that URL — " + msg);
      loadBtn->setEnabled(false);
    });
    connect(previewBtn, &QPushButton::clicked, this, &LinksDialog::doPreview);
    // Enter in the URL fields triggers Preview rather than closing the dialog.
    urlEdit->installEventFilter(this);
    urlResourceEdit->installEventFilter(this);
    // Editing the URL invalidates the current preview (and any video frame state).
    connect(urlEdit, &QLineEdit::textEdited, this,
            [this] { resetPreviewState(); });
    // Toggling "use preview image" swaps between the cached frame and embedded image
    // (no re-fetch needed — both are already in hand).
    connect(usePreview, &QCheckBox::toggled, this, [this] {
      if (previewIsVideo) updateVideoPreview();
    });
    // Debounce seeks lightly so a fast drag coalesces into the latest position rather than firing
    // a seek per pixel.
    fetchTimer = new QTimer(this);
    fetchTimer->setSingleShot(true);
    fetchTimer->setInterval(80);
    connect(fetchTimer, &QTimer::timeout, this, [this] {
      if (previewIsVideo && !usePreview->isChecked()) seekScrub(frame->value());
    });
    // Slider ↔ spin box stay mirrored (syncing guards the echo); either one
    // changing schedules a debounced seek. Releasing the slider seeks at once.
    connect(frameSlider, &QSlider::valueChanged, this, [this](int v) { setFrame(v); });
    connect(frame, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) { setFrame(v); });
    connect(frameSlider, &QSlider::sliderReleased, this, [this] {
      fetchTimer->stop();
      if (previewIsVideo && !usePreview->isChecked()) seekScrub(frame->value());
    });
  }

}  // namespace stencil::gui
