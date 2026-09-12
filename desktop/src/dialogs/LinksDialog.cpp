#include "../support/SearchCombo.hpp"
#include "linksDialogParts.hpp"
#include "LinksDialog.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "fetchGuard.hpp"
#include "MediaLoader.hpp"
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


  LinksDialog::LinksDialog(const QString& source, const QString& resource,
                           bool hasImage, const QString& pageSeed,
                           const QString& units, QWidget* parent)
      : QDialog(parent), pageSeed_(pageSeed) {
    setWindowTitle("Image links");
    setMinimumWidth(540);
    const QColor txt = palette().color(QPalette::WindowText);
    // Theme muted tone for hint/secondary text (browser --text-muted).
    const QString mutedCss =
        QString("color: %1;").arg(palette().color(QPalette::PlaceholderText).name());

    // Browser linksModal.js parity: the shared modal shell + LINKS / ADD IMAGE BY URL
    // sections instead of framed group boxes.
    ModalChrome chrome = installModalChrome(this, "link", tr("Image links"));
    QVBoxLayout* layout = chrome.body;

    // Current project + links: edit / open / remove (only with an image loaded)
    auto* linksBox = new QWidget(this);
    auto* linksCol = new QVBoxLayout(linksBox);
    linksCol->setContentsMargins(0, 0, 0, 0);
    linksCol->setSpacing(8);
    // Only the links live here (browser linksModal parity): the project's name is edited
    // in the projects list / the title, so no PROJECT section and no "Links" caption.
    auto* linksForm = new QFormLayout;
    linksForm->setContentsMargins(0, 0, 0, 0);
    alignModalForm(linksForm, /*growFields=*/true);   // labels left, fields span the row
    linksCol->addLayout(linksForm);

    // One editable link row (Source / Resource): the field plus a compact open-in-
    // browser chip (browser .btn-icon size) and a solid danger clear ✕
    // (browser .links-clear.danger).
    const auto addLinkRow = [&](QLineEdit* edit, const QString& label, const QString& labelTip,
                                const QString& openTip, const QString& clearTip) {
      const auto miniBtn = [](QPushButton* b) {
        b->setProperty("miniChip", true);   // theme.cpp: tight padding, or the glyph clips
        b->setFixedSize(34, 29);
        b->setIconSize(QSize(14, 14));
      };
      auto* open = new QPushButton(this);
      open->setIcon(themedIcon("external", txt, 14));
      miniBtn(open);
      open->setToolTip(openTip);
      auto* clear = new QPushButton(this);
      clear->setProperty("modalDangerGhost", true);
      clear->setIcon(themedIcon("x", QColor("#ffffff"), 14));
      miniBtn(clear);
      clear->setToolTip(clearTip);
      connect(open, &QPushButton::clicked, this, [this, edit] { openInBrowser(edit); });
      connect(clear, &QPushButton::clicked, edit, &QLineEdit::clear);
      auto* lbl = new QLabel(label, this);
      lbl->setToolTip(labelTip);
      linksForm->addRow(lbl, linkRow(edit, open, clear));
    };
    sourceEdit_ = new QLineEdit(source, this);
    sourceEdit_->setPlaceholderText("(empty — local upload)");
    addLinkRow(sourceEdit_, tr("Source:"), tr("The image/video’s own URL"),
               "Open source in the default browser", "Remove source link");

    resourceEdit_ = new QLineEdit(resource, this);
    resourceEdit_->setPlaceholderText("(empty)");
    addLinkRow(resourceEdit_, tr("Resource:"), tr("The web page the image was found on"),
               "Open resource page in the default browser", "Remove resource link");
    layout->addWidget(linksBox);

    // Add image by URL: preview first, then load the previewed pixels
    auto* addBox = new QWidget(this);
    auto* addCol = new QVBoxLayout(addBox);
    addCol->setContentsMargins(0, 0, 0, 0);
    addCol->setSpacing(8);
    addCol->addWidget(modalSectionLabel(tr("Add image by URL"), this));
    auto* addForm = new QFormLayout;
    addForm->setContentsMargins(0, 0, 0, 0);
    alignModalForm(addForm, /*growFields=*/true);
    addCol->addLayout(addForm);

    // URL + an inline Preview button (mirrors the browser modal's 👁 Preview).
    urlEdit_ = new QLineEdit(this);
    urlEdit_->setPlaceholderText("https://… (image or video)");
    auto* previewBtn = new QPushButton("Preview", this);
    makeModalCta(previewBtn, "eye");
    previewBtn->setToolTip("Fetch and show the image / first video frame");
    auto* urlRow = new QHBoxLayout;
    urlRow->addWidget(urlEdit_, 1);
    urlRow->addWidget(previewBtn);
    addForm->addRow("Image / video URL:", urlRow);

    urlResourceEdit_ = new QLineEdit(this);
    urlResourceEdit_->setPlaceholderText("(optional — page the image is on)");
    addForm->addRow("Resource URL:", urlResourceEdit_);

    previewLabel_ = new QLabel(this);
    previewLabel_->setAlignment(Qt::AlignCenter);
    previewLabel_->setMinimumHeight(120);
    previewLabel_->setMaximumSize(PREVIEW_MAX_W, PREVIEW_MAX_H);
    previewLabel_->setFrameShape(QFrame::StyledPanel);
    // Hidden until a preview lands (openImageDialog parity) — an empty bordered box
    // held a 120px void open in the middle of the dialog.
    previewLabel_->setVisible(false);
    addForm->addRow(previewLabel_);

    // "Video frame" controls, UNDER the preview like a player's scrubber; hidden
    // until a preview resolves the URL as a video. Slider scrubs, spin box edits the
    // exact frame; both stay mirrored and seek the persistent scrub player.
    frameRow_ = new QWidget(this);
    auto* frameV = new QVBoxLayout(frameRow_);
    frameV->setContentsMargins(0, 0, 0, 0);
    auto* frameH = new QHBoxLayout;
    frameSlider_ = new QSlider(Qt::Horizontal, frameRow_);
    frameSlider_->setRange(0, 0);
    frameH->addWidget(frameSlider_, 1);
    frameH->addWidget(new QLabel("Frame", frameRow_));
    frame_ = new QSpinBox(frameRow_);
    frame_->setRange(0, 0);
    frameH->addWidget(frame_);
    frameTotal_ = new QLabel(frameRow_);
    frameTotal_->setStyleSheet(mutedCss);
    frameH->addWidget(frameTotal_);
    frameV->addLayout(frameH);
    usePreview_ = new QCheckBox("Use the video's preview image instead of a frame", frameRow_);
    usePreview_->setEnabled(false);  // off + disabled until a preview image is found
    frameV->addWidget(usePreview_);
    frameRow_->setVisible(false);  // shown only for videos
    addForm->addRow(frameRow_);

    previewHint_ = new QLabel(this);
    previewHint_->setStyleSheet(mutedCss + " font-size: 11px;");
    previewHint_->setWordWrap(true);
    addForm->addRow(previewHint_);

    // Quick pre-load edits (mirrors browser linksModal quick-crop): open the
    // editor already cropped to a page aspect/orientation, or uncropped. Shown only
    // once a preview resolves an image/frame.
    quickcropRow_ = new QWidget(this);
    {
      auto* qc = new QHBoxLayout(quickcropRow_);
      qc->setContentsMargins(0, 0, 0, 0);
      qc->addWidget(new QLabel("Quick edits:", quickcropRow_));
      cropPage_ = new QCheckBox("Crop to page", quickcropRow_);
      cropPage_->setChecked(true);
      cropPage_->setToolTip("Crop the image to the page aspect on load");
      cropAlbum_ = new QCheckBox("Album", quickcropRow_);
      cropAlbum_->setToolTip("Landscape orientation (off = portrait)");
      cropPageSize_ = new SearchComboBox(quickcropRow_);
      // Every named ISO format (labels with sizes, data = the canonical name).
      // No "custom" here — the quick crop needs a fixed page aspect.
      fillPageSizeCombo(cropPageSize_, /*includeCustom=*/false, units);
      qc->addWidget(cropPage_);
      qc->addWidget(cropAlbum_);
      qc->addWidget(cropPageSize_);
      qc->addStretch(1);
    }
    quickcropRow_->setVisible(false);  // shown once a preview succeeds
    addForm->addRow(quickcropRow_);
    // Album / page only matter when cropping to page; grey them out otherwise.
    connect(cropPage_, &QCheckBox::toggled, this, &LinksDialog::syncQuickcropEnabled);

    loadBtn_ = new QPushButton("Load into editor", this);
    // The real call-to-action in add-by-URL mode → accent CTA (white glyph on the
    // accent fill, like the Connect dialog's Connect button).
    makeModalCta(loadBtn_, "download");
    loadBtn_->setEnabled(false);  // enabled once a preview succeeds
    loadBtn_->setToolTip("Preview an image or video URL first");
    connect(loadBtn_, &QPushButton::clicked, this, &LinksDialog::requestLoad);
    addForm->addRow(QString(), loadBtn_);
    layout->addWidget(addBox);

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
      previewLabel_->setVisible(false);
      frameRow_->setVisible(false);
      quickcropRow_->setVisible(false);
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
    layout->addStretch(1);

    // Footer (browser settings-footer): the hint alone, in both modes — like the
    // browser, there is no Cancel/Save pair; edits apply when the dialog closes (the
    // caller reads the fields whatever way it was dismissed). Add-by-URL's CTA is
    // "Load into editor" up in the body, so its hint says what the loader can reach.
    addModalFooter(chrome, hasImage
                               ? tr("Editing the current image’s links.")
                               : tr("Downloads bypass page CORS, so any reachable "
                                    "image/video URL works."));
  }
}

