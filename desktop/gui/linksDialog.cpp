#include "linksDialog.hpp"
#include "guiHelpers.hpp"
#include "mediaLoader.hpp"
#include <algorithm>
#include <QAudioOutput>
#include <QCheckBox>
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

  namespace {
    // One editable link row: a line edit plus "open in browser" (↗) and clear (✕).
    QHBoxLayout* linkRow(QLineEdit* edit, QPushButton* openBtn, QPushButton* clearBtn) {
      auto* row = new QHBoxLayout;
      row->addWidget(edit, 1);
      row->addWidget(openBtn);
      row->addWidget(clearBtn);
      return row;
    }

    constexpr int kPreviewMaxW = 440;  // preview is scaled to fit this box,
    constexpr int kPreviewMaxH = 300;  // keeping aspect ratio (browser parity).
  }  // namespace

  LinksDialog::LinksDialog(const QString& source, const QString& resource,
                           bool hasImage, QWidget* parent)
      : QDialog(parent) {
    setWindowTitle("Image links");
    setMinimumWidth(480);

    auto* layout = new QVBoxLayout(this);

    // ── Current links: edit / open / remove (only with an image loaded) ──
    auto* linksBox = new QGroupBox("Links", this);
    auto* linksForm = new QFormLayout(linksBox);

    sourceEdit_ = new QLineEdit(source, this);
    sourceEdit_->setPlaceholderText("(empty — local upload)");
    auto* srcOpen = new QPushButton("↗", this);
    srcOpen->setToolTip("Open source in the default browser");
    auto* srcClear = new QPushButton("✕", this);
    srcClear->setToolTip("Remove source link");
    connect(srcOpen, &QPushButton::clicked, this, [this] { openInBrowser(sourceEdit_); });
    connect(srcClear, &QPushButton::clicked, this, [this] { sourceEdit_->clear(); });
    linksForm->addRow("Source:", linkRow(sourceEdit_, srcOpen, srcClear));

    resourceEdit_ = new QLineEdit(resource, this);
    resourceEdit_->setPlaceholderText("(empty)");
    auto* resOpen = new QPushButton("↗", this);
    resOpen->setToolTip("Open resource page in the default browser");
    auto* resClear = new QPushButton("✕", this);
    resClear->setToolTip("Remove resource link");
    connect(resOpen, &QPushButton::clicked, this, [this] { openInBrowser(resourceEdit_); });
    connect(resClear, &QPushButton::clicked, this, [this] { resourceEdit_->clear(); });
    linksForm->addRow("Resource:", linkRow(resourceEdit_, resOpen, resClear));
    layout->addWidget(linksBox);

    // ── Add image by URL: preview first, then load the previewed pixels ──
    auto* addBox = new QGroupBox("Add image by URL", this);
    auto* addForm = new QFormLayout(addBox);

    // URL + an inline Preview button (mirrors the browser modal's 👁 Preview).
    urlEdit_ = new QLineEdit(this);
    urlEdit_->setPlaceholderText("https://… (image or video)");
    auto* previewBtn = new QPushButton("👁 Preview", this);
    previewBtn->setToolTip("Fetch and show the image / first video frame");
    auto* urlRow = new QHBoxLayout;
    urlRow->addWidget(urlEdit_, 1);
    urlRow->addWidget(previewBtn);
    addForm->addRow("Image / video URL:", urlRow);

    urlResourceEdit_ = new QLineEdit(this);
    urlResourceEdit_->setPlaceholderText("(optional — page the image is on)");
    addForm->addRow("Resource URL:", urlResourceEdit_);

    // Preview area: the decoded image/frame plus a status line.
    previewLabel_ = new QLabel(this);
    previewLabel_->setAlignment(Qt::AlignCenter);
    previewLabel_->setMinimumHeight(120);
    previewLabel_->setMaximumSize(kPreviewMaxW, kPreviewMaxH);
    previewLabel_->setFrameShape(QFrame::StyledPanel);
    addForm->addRow(previewLabel_);

    // "Video frame" controls live in their own row widget, placed UNDER the preview
    // (like a video player's scrubber). Hidden until a preview resolves the URL as a
    // video. The slider scrubs; the spin box shows/edits the exact frame number; both
    // stay mirrored and seek the persistent scrub player. A checkbox can switch to the
    // embedded preview image instead.
    frameRow_ = new QWidget(this);
    auto* frameV = new QVBoxLayout(frameRow_);
    frameV->setContentsMargins(0, 0, 0, 0);
    auto* frameH = new QHBoxLayout;
    frameSlider_ = new QSlider(Qt::Horizontal, frameRow_);
    frameSlider_->setRange(0, 0);
    frameSlider_->setToolTip("Scrub to a frame");
    frameH->addWidget(frameSlider_, 1);
    frameH->addWidget(new QLabel("Frame", frameRow_));
    frame_ = new QSpinBox(frameRow_);
    frame_->setRange(0, 0);
    frame_->setToolTip("Exact frame number (validated against the video length)");
    frameH->addWidget(frame_);
    frameTotal_ = new QLabel(frameRow_);
    frameTotal_->setStyleSheet("color: gray;");
    frameH->addWidget(frameTotal_);
    frameV->addLayout(frameH);
    usePreview_ = new QCheckBox("Use the video's preview image instead of a frame", frameRow_);
    usePreview_->setToolTip(
        "Some videos embed a preview/cover image, unrelated to their frames. "
        "Enabled only when this video carries one.");
    usePreview_->setEnabled(false);  // off + disabled until a preview image is found
    frameV->addWidget(usePreview_);
    frameRow_->setVisible(false);  // shown only for videos
    addForm->addRow(frameRow_);

    previewHint_ = new QLabel(this);
    previewHint_->setStyleSheet("color: gray; font-size: 11px;");
    previewHint_->setWordWrap(true);
    addForm->addRow(previewHint_);

    loadBtn_ = new QPushButton("⬇ Load into editor", this);
    loadBtn_->setEnabled(false);  // enabled once a preview succeeds
    connect(loadBtn_, &QPushButton::clicked, this, &LinksDialog::requestLoad);
    addForm->addRow(QString(), loadBtn_);
    layout->addWidget(addBox);

    auto* hint = new QLabel(
        "Downloads bypass page CORS, so any reachable image/video URL works.", this);
    hint->setStyleSheet("color: gray; font-size: 11px;");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    // ── Preview wiring ──
    preview_ = new MediaLoader(this);
    connect(preview_, &MediaLoader::loaded, this,
            [this](const QImage& img, const QString&) {
              previewIsVideo_ = preview_->isVideoSource();
              if (previewIsVideo_) {
                frameImage_ = img;
                thumbImage_ = preview_->embeddedThumbnail();
                scrubFps_ = preview_->frameRate() > 0 ? preview_->frameRate() : 30.0;
                scrubDurationMs_ = preview_->durationMs();
                const bool hasThumb = !thumbImage_.isNull();
                usePreview_->setEnabled(hasThumb);
                if (!hasThumb && usePreview_->isChecked())
                  usePreview_->setChecked(false);  // (re-renders via toggled)
                frameRow_->setVisible(true);
                applyFrameBounds();  // size the slider / spin box to this video
                updateVideoPreview();
                // Load the video ONCE into a persistent player for live scrubbing
                // (re-streaming per frame, as the detector does, never seeks reliably).
                setupScrubPlayer(preview_->resolvedUrl());
              } else {
                teardownScrubPlayer();
                frameImage_ = QImage();
                thumbImage_ = QImage();
                frameRow_->setVisible(false);
                showPreview(img, QString("Image %1×%2").arg(img.width()).arg(img.height()));
              }
            });
    connect(preview_, &MediaLoader::failed, this, [this](const QString& msg) {
      teardownScrubPlayer();
      previewImage_ = QImage();
      frameImage_ = QImage();
      thumbImage_ = QImage();
      previewIsVideo_ = false;
      previewLabel_->clear();
      frameRow_->setVisible(false);
      usePreview_->setEnabled(false);
      previewHint_->setText("Could not load that URL — " + msg);
      loadBtn_->setEnabled(false);
    });
    connect(previewBtn, &QPushButton::clicked, this, &LinksDialog::doPreview);
    // Enter in the URL fields triggers Preview rather than closing the dialog.
    urlEdit_->installEventFilter(this);
    urlResourceEdit_->installEventFilter(this);
    // Editing the URL invalidates the current preview (and any video frame state).
    connect(urlEdit_, &QLineEdit::textEdited, this,
            [this] { resetPreviewState(); });
    // Toggling "use preview image" swaps between the cached frame and embedded image
    // (no re-fetch needed — both are already in hand).
    connect(usePreview_, &QCheckBox::toggled, this, [this] {
      if (previewIsVideo_) updateVideoPreview();
    });
    // Debounce seeks lightly so a fast drag coalesces into the latest position
    // rather than firing a seek per pixel (the player is already loaded, so seeks
    // are cheap — just smoother).
    fetchTimer_ = new QTimer(this);
    fetchTimer_->setSingleShot(true);
    fetchTimer_->setInterval(80);
    connect(fetchTimer_, &QTimer::timeout, this, [this] {
      if (previewIsVideo_ && !usePreview_->isChecked()) seekScrub(frame_->value());
    });
    // Slider ↔ spin box stay mirrored (syncing_ guards the echo); either one
    // changing schedules a debounced seek. Releasing the slider seeks at once.
    connect(frameSlider_, &QSlider::valueChanged, this, [this](int v) { setFrame(v); });
    connect(frame_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) { setFrame(v); });
    connect(frameSlider_, &QSlider::sliderReleased, this, [this] {
      fetchTimer_->stop();
      if (previewIsVideo_ && !usePreview_->isChecked()) seekScrub(frame_->value());
    });

    // With an image loaded, only its links can be edited; with no image, only the
    // add-by-URL loader is offered. Mirrors the browser modal's two modes.
    linksBox->setVisible(hasImage);
    addBox->setVisible(!hasImage);
    hint->setVisible(!hasImage);

    auto* box = makeButtonBox(this, QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    box->button(QDialogButtonBox::Ok)->setText(hasImage ? "Save links" : "Close");
    layout->addWidget(box);
  }

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
    const QString url = field->text().trimmed();
    if (url.isEmpty()) return;
    QDesktopServices::openUrl(QUrl::fromUserInput(url));
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
    previewHint_->clear();
    frameRow_->setVisible(false);
    usePreview_->setEnabled(false);
    frame_->setEnabled(true);
    frameTotal_->clear();
    loadBtn_->setEnabled(false);
  }

  // Mirror a chosen frame to BOTH the slider and the spin box, validated against the
  // range, then schedule a debounced seek. QSignalBlocker prevents the set from
  // echoing back (each control's change is connected here), so both always end up on
  // the same, final value — no missed updates.
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

  // Render `img` into the preview area, set the status hint, and arm Load.
  void LinksDialog::showPreview(const QImage& img, const QString& hint) {
    previewImage_ = img;
    if (img.isNull()) {
      previewLabel_->clear();
      loadBtn_->setEnabled(false);
      return;
    }
    previewLabel_->setPixmap(QPixmap::fromImage(img).scaled(
        kPreviewMaxW, kPreviewMaxH, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    previewHint_->setText(hint);
    loadBtn_->setEnabled(true);
  }

  // For a video, show either the embedded preview image (when chosen and available)
  // or the seeked frame. The frame spinbox is irrelevant while the preview is used.
  void LinksDialog::updateVideoPreview() {
    const bool usePrev = usePreview_->isChecked() && !thumbImage_.isNull();
    frame_->setEnabled(!usePrev);          // frame controls are moot while using the
    frameSlider_->setEnabled(!usePrev);    // embedded preview image
    const QImage& shown = usePrev ? thumbImage_ : frameImage_;
    showPreview(shown,
                usePrev
                    ? QString("Using the video's embedded preview image (%1×%2).")
                          .arg(shown.width()).arg(shown.height())
                    : QString("Video %1×%2 — drag the slider or type a frame, then Load.")
                          .arg(shown.width()).arg(shown.height()));
  }

  void LinksDialog::requestLoad() {
    if (previewImage_.isNull()) return;  // Load is gated on a successful preview
    loadRequested_ = true;
    accept();
  }

  QString LinksDialog::source() const { return sourceEdit_->text().trimmed(); }
  QString LinksDialog::resource() const { return resourceEdit_->text().trimmed(); }
  QString LinksDialog::urlSource() const { return urlEdit_->text().trimmed(); }
  QString LinksDialog::urlResource() const { return urlResourceEdit_->text().trimmed(); }
  int LinksDialog::urlFrame() const { return frame_->value(); }

}
