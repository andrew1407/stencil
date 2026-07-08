#include "openImageDialog.hpp"
#include "guiHelpers.hpp"
#include "mediaLoader.hpp"
#include <algorithm>
#include <QAudioOutput>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
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

  namespace {
    // Tab order (QTabWidget indices).
    enum { TabFile = 0, TabUrl = 1, TabBlank = 2 };

    constexpr int kPreviewMaxW = 440;  // preview scaled to fit this box,
    constexpr int kPreviewMaxH = 300;  // keeping aspect ratio (browser parity).

    // Video extensions the loader (MediaLoader) can seek + grab a frame from. A
    // source with one of these — local or in a URL — reveals the frame control.
    bool looksLikeVideo(const QString& src) {
      static const QStringList kExt = {"mp4", "mov", "webm", "mkv", "avi", "m4v", "mpg", "mpeg"};
      const QString s = src.trimmed();
      if (s.isEmpty()) return false;
      // Strip a URL query/fragment before reading the extension.
      QString tail = s.section('/', -1).section('?', 0, 0).section('#', 0, 0);
      return kExt.contains(QFileInfo(tail).suffix().toLower());
    }
  }  // namespace

  OpenImageDialog::OpenImageDialog(QWidget* parent, bool canReplace,
                                   int blankW, int blankH, bool startBlank,
                                   const QString& pageSeed, const QString& units)
      : QDialog(parent), pageSeed_(pageSeed), units_(units), canReplace_(canReplace) {
    setWindowTitle("Open Image");
    setMinimumWidth(480);
    const QString mutedCss = "color: gray; font-size: 11px;";

    auto* layout = new QVBoxLayout(this);

    // ── Source tabs: Local file / URL link / Blank. ──
    tabs_ = new QTabWidget(this);

    // Tab: Local file. A read-only field showing the chosen path + a Browse button
    // (images AND videos, mirroring the browser modal).
    auto* fileTab = new QWidget(this);
    auto* fileForm = new QFormLayout(fileTab);
    auto* fileRow = new QHBoxLayout;
    path_ = new QLineEdit(this);
    path_->setReadOnly(true);
    path_->setPlaceholderText("No file chosen");
    path_->setToolTip("The chosen image or video file (use Choose File… to pick one)");
    auto* browse = new QPushButton("Choose File…", this);
    browse->setToolTip("Browse for an image or video file to open");
    connect(browse, &QPushButton::clicked, this, &OpenImageDialog::browse);
    fileRow->addWidget(path_, 1);
    fileRow->addWidget(browse);
    fileForm->addRow("Image / video:", fileRow);
    tabs_->addTab(fileTab, "Local file");

    // Tab: URL link. Load an image or video straight from the web (resolved via
    // MediaLoader, which handles CORS-free fetch + video-frame grab).
    auto* urlTab = new QWidget(this);
    auto* urlForm = new QFormLayout(urlTab);
    url_ = new QLineEdit(this);
    url_->setPlaceholderText("https://… (image or video)");
    url_->setToolTip("Load an image or video directly from a web URL");
    urlForm->addRow("URL:", url_);
    tabs_->addTab(urlTab, "URL link");

    // Tab: Blank. Solid-color canvas (folded in from the retired blank-image dialog).
    auto* blankTab = new QWidget(this);
    auto* blankForm = new QFormLayout(blankTab);
    auto* colorRow = new QHBoxLayout;
    white_ = new QRadioButton("White", this);
    white_->setToolTip("Fill the blank image with white");
    black_ = new QRadioButton("Black", this);
    black_->setToolTip("Fill the blank image with black");
    customColorRadio_ = new QRadioButton("Custom:", this);
    customColorRadio_->setToolTip("Fill the blank image with the picked custom color");
    white_->setChecked(true);
    auto* colorGroup = new QButtonGroup(this);
    colorGroup->addButton(white_);
    colorGroup->addButton(black_);
    colorGroup->addButton(customColorRadio_);
    customSwatch_ = new QToolButton(this);
    customSwatch_->setToolTip("Pick a custom fill color");
    setColorSwatch(customSwatch_, customColor_);
    connect(customSwatch_, &QToolButton::clicked, this, &OpenImageDialog::pickCustomColor);
    colorRow->addWidget(white_);
    colorRow->addWidget(black_);
    colorRow->addWidget(customColorRadio_);
    colorRow->addWidget(customSwatch_);
    colorRow->addStretch(1);
    blankForm->addRow("Fill color:", colorRow);
    blankWidth_ = new QSpinBox(this);
    blankWidth_->setRange(1, 8192);
    blankWidth_->setSuffix(" px");
    blankWidth_->setValue(blankW);
    blankWidth_->setToolTip("Blank image width in pixels (1–8192)");
    blankHeight_ = new QSpinBox(this);
    blankHeight_->setRange(1, 8192);
    blankHeight_->setSuffix(" px");
    blankHeight_->setValue(blankH);
    blankHeight_->setToolTip("Blank image height in pixels (1–8192)");
    blankForm->addRow("Width:", blankWidth_);
    blankForm->addRow("Height:", blankHeight_);
    tabs_->addTab(blankTab, "Blank");
    layout->addWidget(tabs_);

    // Preview button: fetch/decode the chosen source and show it before committing
    // (mirrors the browser modal's 👁 Preview). A local file auto-previews on browse.
    auto* previewRow = new QHBoxLayout;
    previewRow->addStretch(1);
    previewBtn_ = new QPushButton("Preview", this);
    previewBtn_->setToolTip("Show the image / first video frame before opening");
    connect(previewBtn_, &QPushButton::clicked, this, &OpenImageDialog::doPreview);
    previewRow->addWidget(previewBtn_);
    layout->addLayout(previewRow);

    // Rendered preview image / frame.
    previewLabel_ = new QLabel(this);
    previewLabel_->setAlignment(Qt::AlignCenter);
    previewLabel_->setMinimumHeight(120);
    previewLabel_->setMaximumSize(kPreviewMaxW, kPreviewMaxH);
    previewLabel_->setFrameShape(QFrame::StyledPanel);
    auto* previewCenter = new QHBoxLayout;
    previewCenter->addStretch(1);
    previewCenter->addWidget(previewLabel_);
    previewCenter->addStretch(1);
    layout->addLayout(previewCenter);

    // "Video frame" controls (mirrors LinksDialog): the slider scrubs; the spin box
    // shows/edits the exact frame; both stay mirrored and seek the persistent scrub
    // player. A checkbox can switch to the container's embedded preview image instead.
    frame_ = new QSpinBox(this);
    frame_->setRange(0, 0);
    frame_->setToolTip("Exact frame number (validated against the video length)");
    frameSlider_ = new QSlider(Qt::Horizontal, this);
    frameSlider_->setRange(0, 0);
    frameSlider_->setToolTip("Scrub to a frame");
    frameTotal_ = new QLabel(this);
    frameTotal_->setStyleSheet(mutedCss);
    frameRow_ = new QWidget(this);
    auto* frameV = new QVBoxLayout(frameRow_);
    frameV->setContentsMargins(0, 0, 0, 0);
    auto* frameH = new QHBoxLayout;
    frameH->addWidget(frameSlider_, 1);
    frameH->addWidget(new QLabel("Frame", frameRow_));
    frameH->addWidget(frame_);
    frameH->addWidget(frameTotal_);
    frameV->addLayout(frameH);
    usePreview_ = new QCheckBox("Use the video's preview image instead of a frame", frameRow_);
    usePreview_->setToolTip(
        "Some videos embed a preview/cover image, unrelated to their frames. "
        "Enabled only when this video carries one.");
    usePreview_->setEnabled(false);
    frameV->addWidget(usePreview_);
    frameRow_->setVisible(false);  // shown only for videos
    layout->addWidget(frameRow_);

    previewHint_ = new QLabel(this);
    previewHint_->setStyleSheet(mutedCss);
    previewHint_->setWordWrap(true);
    layout->addWidget(previewHint_);

    // ── Quick pre-load crop (mirrors LinksDialog quick-crop): open the editor already
    // cropped to a page aspect/orientation, or uncropped. Crop is OFF by default;
    // shown only once a preview resolves an image/frame. ──
    quickcropRow_ = new QWidget(this);
    {
      auto* qc = new QHBoxLayout(quickcropRow_);
      qc->setContentsMargins(0, 0, 0, 0);
      cropPage_ = new QCheckBox("Crop", quickcropRow_);
      cropPage_->setChecked(false);  // UNCHECKED by default → open the whole image
      cropPage_->setToolTip("Crop the image to the page aspect on open");
      cropAlbum_ = new QCheckBox("Album", quickcropRow_);
      cropAlbum_->setToolTip("Landscape orientation (off = portrait)");
      cropPageSize_ = new QComboBox(quickcropRow_);
      // Every named ISO format (labels with sizes, data = the canonical name). No
      // "custom" here — the crop needs a fixed page aspect.
      fillPageSizeCombo(cropPageSize_, /*includeCustom=*/false, units_);
      cropPageSize_->setToolTip("Page size to crop to");
      qc->addWidget(cropPage_);
      qc->addWidget(cropAlbum_);
      qc->addWidget(cropPageSize_);
      qc->addStretch(1);
    }
    quickcropRow_->setVisible(false);  // shown once a preview succeeds
    layout->addWidget(quickcropRow_);
    // Album / page only matter when cropping to page; grey them out otherwise.
    connect(cropPage_, &QCheckBox::toggled, this, &OpenImageDialog::syncQuickcropEnabled);

    // Incognito: edit without saving (mirrors the browser checkbox). Applies to a
    // file/URL open; hidden on the Blank tab (blank creation never honored it).
    commonForm_ = new QFormLayout;
    commonForm_->setContentsMargins(0, 0, 0, 0);
    incognito_ = new QCheckBox("Edit without saving", this);
    incognito_->setToolTip("Open the image in incognito mode — edits are not saved");
    commonForm_->addRow("Incognito:", incognito_);
    layout->addLayout(commonForm_);

    // Replace options: only shown on the Local file tab over a replaceable project.
    replaceRow_ = new QWidget(this);
    if (canReplace_) {
      rename_ = new QCheckBox("Rename project to the new image", this);
      rename_->setToolTip("Adopt the new file's name for this project");
      keep_ = new QCheckBox("Keep existing annotations", this);
      keep_->setToolTip("Keep the current lines over the replacement image");
      keep_->setChecked(true);
      auto* opts = new QHBoxLayout(replaceRow_);
      opts->setContentsMargins(0, 0, 0, 0);
      opts->setSpacing(18);
      opts->addWidget(new QLabel("Replace:", this));
      opts->addWidget(rename_);
      opts->addWidget(keep_);
      opts->addStretch(1);
    }
    layout->addWidget(replaceRow_);

    auto* hint = new QLabel(
        "“Open here” loads the source in this editor; “Open in new window” leaves it "
        "untouched. A URL/video always makes a new project. “Blank” makes a solid-color "
        "canvas.",
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(mutedCss);
    layout->addWidget(hint);

    // ── Footer actions ── file/URL: Cancel / Replace? / Open here / Open in new window.
    // blank: Cancel / Create blank.
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    auto* cancel = new QPushButton("Cancel", this);
    cancel->setToolTip("Close without opening an image");
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    here_ = new QPushButton("Open here", this);
    connect(here_, &QPushButton::clicked, this, [this] { outcome_ = Outcome::Here; accept(); });
    newWindow_ = new QPushButton("Open in new window", this);
    connect(newWindow_, &QPushButton::clicked, this, [this] { outcome_ = Outcome::NewWindow; accept(); });
    createBlank_ = new QPushButton("Create blank", this);
    connect(createBlank_, &QPushButton::clicked, this, [this] { outcome_ = Outcome::Blank; accept(); });
    btnRow->addWidget(cancel);
    if (canReplace_) {
      replace_ = new QPushButton("Replace image", this);
      connect(replace_, &QPushButton::clicked, this, [this] { outcome_ = Outcome::Replace; accept(); });
      btnRow->addWidget(replace_);
    }
    btnRow->addWidget(here_);
    btnRow->addWidget(newWindow_);
    btnRow->addWidget(createBlank_);
    layout->addLayout(btnRow);

    // ── Preview wiring (mirrors LinksDialog) ──
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
                showQuickcrop(frameImage_.width(), frameImage_.height());
                // Load the video ONCE into a persistent player for live scrubbing
                // (re-streaming per frame, as the detector does, never seeks reliably).
                setupScrubPlayer(preview_->resolvedUrl());
              } else {
                teardownScrubPlayer();
                frameImage_ = QImage();
                thumbImage_ = QImage();
                frameRow_->setVisible(false);
                showPreview(img, QString("Image %1×%2").arg(img.width()).arg(img.height()));
                showQuickcrop(img.width(), img.height());
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
      quickcropRow_->setVisible(false);
      usePreview_->setEnabled(false);
      previewHint_->setText("Could not load that source — " + msg);
    });

    // Debounce seeks lightly so a fast drag coalesces into the latest position.
    fetchTimer_ = new QTimer(this);
    fetchTimer_->setSingleShot(true);
    fetchTimer_->setInterval(80);
    connect(fetchTimer_, &QTimer::timeout, this, [this] {
      if (previewIsVideo_ && !usePreview_->isChecked()) seekScrub(frame_->value());
    });
    // Slider ↔ spin box stay mirrored; either changing schedules a debounced seek.
    connect(frameSlider_, &QSlider::valueChanged, this, [this](int v) { setFrame(v); });
    connect(frame_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) { setFrame(v); });
    connect(frameSlider_, &QSlider::sliderReleased, this, [this] {
      fetchTimer_->stop();
      if (previewIsVideo_ && !usePreview_->isChecked()) seekScrub(frame_->value());
    });
    // Toggling "use preview image" swaps between the cached frame and embedded image.
    connect(usePreview_, &QCheckBox::toggled, this, [this] {
      if (previewIsVideo_) updateVideoPreview();
    });

    // A URL edit invalidates the current preview (and any video frame state); Enter
    // previews it. Typing a URL blanks the (mutually exclusive) file selection.
    connect(url_, &QLineEdit::textEdited, this, [this] { resetPreviewState(); refreshButtons(); });
    url_->installEventFilter(this);
    connect(tabs_, &QTabWidget::currentChanged, this, [this] { applyMode(); });
    tabs_->setCurrentIndex(startBlank ? TabBlank : TabFile);
    applyMode();
  }

  bool OpenImageDialog::eventFilter(QObject* obj, QEvent* event) {
    if (obj == url_ && event->type() == QEvent::KeyPress) {
      const auto* k = static_cast<QKeyEvent*>(event);
      if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
        doPreview();  // Enter previews the URL rather than accepting the dialog
        return true;
      }
    }
    return QDialog::eventFilter(obj, event);
  }

  void OpenImageDialog::browse() {
    const QString p = QFileDialog::getOpenFileName(
        this, "Open image or video", QString(),
        "Images and video (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.mp4 *.mov *.webm "
        "*.mkv *.avi *.m4v *.mpg *.mpeg);;All files (*)");
    if (p.isEmpty()) return;
    path_->setText(p);
    resetPreviewState();
    refreshButtons();
    doPreview();  // a local file previews immediately
  }

  void OpenImageDialog::pickCustomColor() {
    const QColor c = QColorDialog::getColor(customColor_, this, "Fill color",
                                            QColorDialog::DontUseNativeDialog);
    if (!c.isValid()) return;
    customColor_ = c;
    customColorRadio_->setChecked(true);
    setColorSwatch(customSwatch_, customColor_);
  }

  // Swap the footer actions to match the active tab. Replace only ever applies to a
  // local-file source over a replaceable project. Preview + crop are file/URL only.
  void OpenImageDialog::applyMode() {
    const bool blank = tabs_->currentIndex() == TabBlank;
    commonForm_->setRowVisible(incognito_, !blank);  // incognito has no effect on a blank
    here_->setVisible(!blank);
    newWindow_->setVisible(!blank);
    if (replace_) replace_->setVisible(!blank && tabs_->currentIndex() == TabFile);
    replaceRow_->setVisible(!blank && canReplace_ && tabs_->currentIndex() == TabFile);
    createBlank_->setVisible(blank);
    previewBtn_->setVisible(!blank);
    previewLabel_->setVisible(!blank);
    // Switching source tabs invalidates any preview built for the other tab.
    resetPreviewState();
    if (!blank) refreshButtons();
    else frameRow_->setVisible(false);
  }

  // Action buttons stay disabled until a source (file or URL) is chosen; Replace is
  // only for a local-image file (a URL/video opens as a new project).
  void OpenImageDialog::refreshButtons() {
    const bool has = !source().isEmpty();
    const bool video = looksLikeVideo(source());
    previewBtn_->setEnabled(has);
    if (replace_) {
      const bool canReplaceNow = has && !isUrl() && !video;
      replace_->setEnabled(canReplaceNow);
      replace_->setToolTip(
          !has ? "Choose an image file first"
               : (isUrl() || video
                      ? "A URL or video opens as a new project (no in-place replace)"
                      : "Swap this project's image in place (same project)"));
    }
    refreshOpenEnabled();
  }

  // Gate the open buttons. A source is enough to open (an un-previewed source falls
  // back to the async resolve in MainWindow); with a preview taken, opening adopts the
  // exact previewed pixels. Tooltips explain the state.
  void OpenImageDialog::refreshOpenEnabled() {
    const bool has = !source().isEmpty();
    here_->setEnabled(has);
    newWindow_->setEnabled(has);
    const QString reason = "Choose an image/video file or paste a URL first";
    here_->setToolTip(has ? "Open the chosen source in this editor (makes a new project)"
                          : reason);
    newWindow_->setToolTip(has ? "Open the chosen source in a new window (this editor stays)"
                               : reason);
  }

  // Decode the current source (image, or the chosen video frame) into the preview.
  void OpenImageDialog::doPreview() {
    const QString src = source();
    if (src.isEmpty()) {
      previewHint_->setText("Choose a file or paste a URL first.");
      return;
    }
    previewHint_->setText("Loading…");
    preview_->load(src, frame_->value());
  }

  void OpenImageDialog::resetPreviewState() {
    if (fetchTimer_) fetchTimer_->stop();
    teardownScrubPlayer();
    previewImage_ = QImage();
    frameImage_ = QImage();
    thumbImage_ = QImage();
    previewIsVideo_ = false;
    previewLabel_->clear();
    previewHint_->clear();
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

  // Render `img` into the preview area + set the status hint (adopted on open).
  void OpenImageDialog::showPreview(const QImage& img, const QString& hint) {
    previewImage_ = img;
    if (img.isNull()) {
      previewLabel_->clear();
      return;
    }
    previewLabel_->setPixmap(QPixmap::fromImage(img).scaled(
        kPreviewMaxW, kPreviewMaxH, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    previewHint_->setText(hint);
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

  // Reveal the quick-crop row for a previewed image/frame, defaulting the album toggle
  // to the media's orientation (wider-than-tall ⇒ album) and the page size to the app's
  // current page (mirrors LinksDialog's showQuickcrop). Crop itself stays OFF.
  void OpenImageDialog::showQuickcrop(int w, int h) {
    cropAlbum_->setChecked((w >= h) && (w > 0));
    const int idx = cropPageSize_->findData(pageSeed_);
    cropPageSize_->setCurrentIndex(idx < 0 ? cropPageSize_->findData("A3") : idx);
    syncQuickcropEnabled();
    quickcropRow_->setVisible(true);
    refreshOpenEnabled();
  }

  // Album / page size are only meaningful while cropping to page.
  void OpenImageDialog::syncQuickcropEnabled() {
    const bool on = cropPage_->isChecked();
    cropAlbum_->setEnabled(on);
    cropPageSize_->setEnabled(on);
  }

  QString OpenImageDialog::source() const {
    if (tabs_->currentIndex() == TabUrl) return url_->text().trimmed();
    if (tabs_->currentIndex() == TabFile) return path_->text();
    return QString();  // blank tab has no source
  }
  bool OpenImageDialog::isUrl() const { return tabs_->currentIndex() == TabUrl; }
  bool OpenImageDialog::isVideo() const { return looksLikeVideo(source()); }
  int OpenImageDialog::frame() const { return frame_->value(); }
  bool OpenImageDialog::incognito() const { return incognito_->isChecked(); }
  bool OpenImageDialog::rename() const { return rename_ && rename_->isChecked(); }
  bool OpenImageDialog::keepAnnotations() const { return !keep_ || keep_->isChecked(); }

  QColor OpenImageDialog::blankColor() const {
    if (white_->isChecked()) return QColor(Qt::white);
    if (black_->isChecked()) return QColor(Qt::black);
    return customColor_;
  }
  int OpenImageDialog::blankWidth() const { return blankWidth_->value(); }
  int OpenImageDialog::blankHeight() const { return blankHeight_->value(); }

  bool OpenImageDialog::cropToPage() const { return cropPage_ && cropPage_->isChecked(); }
  bool OpenImageDialog::cropAlbum() const { return cropAlbum_ && cropAlbum_->isChecked(); }
  QString OpenImageDialog::cropPageSize() const {
    return cropPageSize_ ? cropPageSize_->currentData().toString() : QString();
  }

}
