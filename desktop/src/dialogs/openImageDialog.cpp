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


  OpenImageDialog::OpenImageDialog(QWidget* parent, bool canReplace,
                                   int blankW, int blankH, bool startBlank,
                                   const QString& pageSeed, const QString& units)
      : QDialog(parent), pageSeed_(pageSeed), units_(units), canReplace_(canReplace) {
    setWindowTitle("Open Image");
    // The browser's shared modal width. The four-button footer a replaceable project
    // adds (Cancel / Replace image / Open here / Open in new window) paints tighter
    // than the layout's minimum reports, so that shape gets a little more room.
    setMinimumWidth(canReplace ? 610 : MODAL_WIDTH);
    const QString mutedCss = "color: gray; font-size: 11px;";

    // Browser openImageModal.js parity: the shared modal shell around the tabbed body.
    ModalChrome chrome = installModalChrome(this, "image", tr("Open Image"));
    QVBoxLayout* layout = chrome.body;

    // Source tabs: Local file / URL link / Blank — the browser .oi-tab strip
    // (underlineTabBar.hpp): animated hover, sliding accent underline, and the
    // selected tab's GLYPH tinted accent along with its label.
    tabs_ = new OiTabWidget(this);
    // No pane box (browser .oi-tabs: an underlined tab strip over plain rows — the
    // .vs-rows carry their own hairlines, so the generic rounded pane doubled up).
    // The pane keeps only the strip's own full-width hairline (theme.cpp).
    tabs_->setObjectName("oiTabs");
    // Hug the tab page. QTabWidget expands by default, so the pane stretched into a tall
    // empty box under a two-field form (the browser's tab panel is content-height).
    tabs_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    // Tab: Local file (browser: one .vs-row "Choose" + the file input). A read-only
    // field showing the chosen path + a Choose File button (images AND videos); the
    // chosen file auto-previews, so this tab carries no Preview button of its own.
    auto* fileTab = new QWidget(this);
    auto* fileV = new QVBoxLayout(fileTab);
    fileV->setContentsMargins(0, 14, 0, 0);   // browser .oi-tabs margin-bottom: 14px
    fileV->setSpacing(0);
    auto* fileRow = new QHBoxLayout;
    fileRow->setContentsMargins(0, 0, 0, 0);
    path_ = new QLineEdit(this);
    path_->setReadOnly(true);
    path_->setPlaceholderText("No file chosen");
    auto* browse = new QPushButton("Choose File…", this);
    makeModalCta(browse, "folder");
    connect(browse, &QPushButton::clicked, this, &OpenImageDialog::browse);
    fileRow->addWidget(path_, 1);
    fileRow->addWidget(browse);
    fileV->addWidget(vsRow(fileTab, tr("Choose"), fileRow));
    tabs_->addTab(fileTab, "Local file");

    // Tab: URL link (browser: one .vs-row "URL" with the field AND the Preview button
    // inline — never on a row of its own). Resolved via MediaLoader (CORS-free fetch +
    // video-frame grab); preview is explicit so a half-typed URL never spins a fetch.
    auto* urlTab = new QWidget(this);
    auto* urlV = new QVBoxLayout(urlTab);
    urlV->setContentsMargins(0, 14, 0, 0);
    urlV->setSpacing(0);
    auto* urlRow = new QHBoxLayout;
    urlRow->setContentsMargins(0, 0, 0, 0);
    url_ = new QLineEdit(this);
    url_->setPlaceholderText("https://… (image or video)");
    previewBtn_ = new QPushButton("Preview", this);
    makeModalCta(previewBtn_, "image");   // the browser's inline Preview button
    previewBtn_->setToolTip("Show the image / first video frame before opening");
    connect(previewBtn_, &QPushButton::clicked, this, &OpenImageDialog::doPreview);
    urlRow->addWidget(url_, 1);
    urlRow->addWidget(previewBtn_);
    urlV->addWidget(vsRow(urlTab, tr("URL"), urlRow));
    tabs_->addTab(urlTab, "URL link");

    // Tab: Blank (browser: FILL COLOR / SIZE (PX) sections of .vs-rows — the White and
    // Black presets are swatch BUTTONS that pick the fill, the custom swatch beside
    // them; plain px number fields, no radios and no unit suffix).
    auto* blankTab = new QWidget(this);
    auto* blankV = new QVBoxLayout(blankTab);
    blankV->setContentsMargins(0, 14, 0, 0);
    blankV->setSpacing(0);
    // Browser .vs-section spacing: 14px above (none on the first), 6px below.
    auto* fillSection = modalSectionLabel(tr("Fill color"), blankTab);
    fillSection->setContentsMargins(0, 0, 0, 6);
    blankV->addWidget(fillSection);
    auto* presetRow = new QHBoxLayout;
    presetRow->setContentsMargins(0, 0, 0, 0);
    presetRow->setSpacing(8);
    auto* whiteBtn = new QPushButton(tr("White"), this);
    whiteBtn->setObjectName("biPresetWhite");
    whiteBtn->setToolTip("Fill with white");   // browser bi-preset titles
    auto* blackBtn = new QPushButton(tr("Black"), this);
    blackBtn->setObjectName("biPresetBlack");
    blackBtn->setToolTip("Fill with black");
    customSwatch_ = new QToolButton(this);
    setColorSwatch(customSwatch_, customColor_);
    connect(customSwatch_, &QToolButton::clicked, this, &OpenImageDialog::pickCustomColor);
    connect(whiteBtn, &QPushButton::clicked, this, [this] {
      customColor_ = QColor(Qt::white);
      setColorSwatch(customSwatch_, customColor_);
    });
    connect(blackBtn, &QPushButton::clicked, this, [this] {
      customColor_ = QColor(Qt::black);
      setColorSwatch(customSwatch_, customColor_);
    });
    presetRow->addWidget(whiteBtn);
    presetRow->addWidget(blackBtn);
    presetRow->addStretch(1);
    blankV->addWidget(vsRow(blankTab, tr("Presets"), presetRow));
    blankV->addWidget(vsRow(blankTab, tr("Custom color"), customSwatch_, /*stretch=*/0));
    auto* sizeSection = modalSectionLabel(tr("Size (px)"), blankTab);
    sizeSection->setContentsMargins(0, 14, 0, 6);
    blankV->addWidget(sizeSection);
    blankWidth_ = new QSpinBox(this);
    blankWidth_->setRange(1, 8192);
    blankWidth_->setValue(blankW);
    blankHeight_ = new QSpinBox(this);
    blankHeight_->setRange(1, 8192);
    blankHeight_->setValue(blankH);
    blankV->addWidget(vsRow(blankTab, tr("Width"), blankWidth_));
    blankV->addWidget(vsRow(blankTab, tr("Height"), blankHeight_));
    tabs_->addTab(blankTab, "Blank");
    // Browser tab glyphs, named so the strip re-tints them per state (muted / hover /
    // accent-selected) instead of a fixed-colour QIcon.
    auto* tabStrip = static_cast<UnderlineTabBar*>(tabs_->tabBar());
    tabStrip->setTabGlyph(TabFile, "file-text");
    tabStrip->setTabGlyph(TabUrl, "link");
    tabStrip->setTabGlyph(TabBlank, "plus-circle");
    layout->addWidget(tabs_);

    // Rendered preview image / frame.
    previewLabel_ = new QLabel(this);
    previewLabel_->setAlignment(Qt::AlignCenter);
    previewLabel_->setMaximumSize(PREVIEW_MAX_W, PREVIEW_MAX_H);
    previewLabel_->setFrameShape(QFrame::StyledPanel);
    // Hidden until a preview lands (the browser shows no preview area until there is one).
    previewLabel_->setVisible(false);
    auto* previewCenter = new QHBoxLayout;
    previewCenter->addStretch(1);
    previewCenter->addWidget(previewLabel_);
    previewCenter->addStretch(1);
    layout->addLayout(previewCenter);

    // "Video frame" controls (mirrors LinksDialog) as the browser's Frame .vs-row: the
    // slider scrubs; the spin box shows/edits the exact frame; both stay mirrored and
    // seek the persistent scrub player. A checkbox under them can switch to the
    // container's embedded preview image instead.
    frame_ = new QSpinBox(this);
    frame_->setRange(0, 0);
    frameSlider_ = new QSlider(Qt::Horizontal, this);
    frameSlider_->setRange(0, 0);
    frameTotal_ = new QLabel(this);
    frameTotal_->setStyleSheet(mutedCss);
    auto* frameV = new QVBoxLayout;
    frameV->setContentsMargins(0, 0, 0, 0);
    frameV->setSpacing(7);
    auto* frameH = new QHBoxLayout;
    frameH->setContentsMargins(0, 0, 0, 0);
    frameH->addWidget(frameSlider_, 1);
    frameH->addWidget(frame_);
    frameH->addWidget(frameTotal_);
    frameV->addLayout(frameH);
    usePreview_ = new QCheckBox("Use the video's preview image instead of a frame", this);
    usePreview_->setEnabled(false);
    frameV->addWidget(usePreview_);
    frameRow_ = vsRow(this, tr("Frame"), frameV);
    frameRow_->setVisible(false);  // shown only for videos
    layout->addWidget(frameRow_);

    previewHint_ = new QLabel(this);
    previewHint_->setStyleSheet(mutedCss);
    previewHint_->setWordWrap(true);
    previewHint_->setVisible(false);   // an empty hint keeps no line of its own
    layout->addWidget(previewHint_);

    // Quick pre-load crop (mirrors LinksDialog quick-crop) as the browser's Crop
    // .vs-row (#open-image-crop-row): the toggle, its caption, then — only while
    // cropping — the Album/Portrait toggle and the page the aspect comes from. Crop is
    // OFF by default; shown only once a preview resolves an image/frame.
    {
      auto* qc = new QHBoxLayout;
      qc->setContentsMargins(0, 0, 0, 0);
      qc->setSpacing(8);
      cropPage_ = new QCheckBox(this);
      cropPage_->setChecked(false);  // UNCHECKED by default → open the whole image
      cropPage_->setToolTip("Crop the image to the page aspect before opening");
      auto* cropHint = new QLabel(tr("Trim to the page aspect before opening."), this);
      cropHint->setObjectName(QStringLiteral("modalFooterHint"));   // browser .footer-hint
      cropAlbum_ = new QPushButton(this);
      cropAlbum_->setCheckable(true);
      makeModalCta(cropAlbum_, "swap");
      cropAlbum_->setToolTip("Swap album / portrait — flips the crop orientation");
      cropAlbum_->setAutoDefault(false);
      cropPageSize_ = new SearchComboBox(this);
      // Every named ISO format (labels with sizes, data = the canonical name). No
      // "custom" here — the crop needs a fixed page aspect.
      fillPageSizeCombo(cropPageSize_, /*includeCustom=*/false, units_);
      qc->addWidget(cropPage_);
      qc->addWidget(cropHint, 1);
      qc->addWidget(cropAlbum_);
      qc->addWidget(cropPageSize_);
      quickcropRow_ = vsRow(this, tr("Crop"), qc);
    }
    quickcropRow_->setVisible(false);  // shown once a preview succeeds
    layout->addWidget(quickcropRow_);
    // Album / page only matter when cropping to page; shown only then (browser parity).
    connect(cropPage_, &QCheckBox::toggled, this, &OpenImageDialog::syncQuickcropEnabled);
    connect(cropAlbum_, &QPushButton::toggled, this, &OpenImageDialog::syncQuickcropEnabled);

    // Incognito: a .vs-row with the browser's full caption (openImageModal.js).
    // Applies to a file/URL open; hidden on the Blank tab (never honored there).
    incognito_ = new QCheckBox(
        "Edit without saving — the image is never written to storage.", this);
    incogRow_ = vsRow(this, tr("Incognito"), incognito_, /*stretch=*/0);
    layout->addWidget(incogRow_);
    // Incognito never offers a server target (browser: fillTargetSelect(!incog)).
    connect(incognito_, &QCheckBox::toggled, this, &OpenImageDialog::refreshTargetRow);

    // Save target (browser #open-image-target-row): only shown when at least one server
    // is connected — setServerTargets fills it.
    target_ = new SearchComboBox(this, /*searchable=*/false);
    target_->setObjectName(QStringLiteral("openImageTarget"));
    target_->setToolTip("Open here locally or create on a connected server");
    targetRow_ = vsRow(this, tr("Save to"), target_);
    targetRow_->setVisible(false);
    layout->addWidget(targetRow_);

    // Replace options: only shown on the Local file tab over a replaceable project.
    // The two checks stack (browser .oi-replace wraps them onto their own lines).
    replaceRow_ = new QWidget(this);
    if (canReplace_) {
      rename_ = new QCheckBox("Rename project to the new image", this);
      keep_ = new QCheckBox("Keep existing annotations", this);
      keep_->setChecked(true);
      auto* checks = new QVBoxLayout;
      checks->setContentsMargins(0, 0, 0, 0);
      checks->setSpacing(7);
      checks->addWidget(rename_);
      checks->addWidget(keep_);
      auto* wrap = new QVBoxLayout(replaceRow_);
      wrap->setContentsMargins(0, 0, 0, 0);
      wrap->addWidget(vsRow(replaceRow_, tr("Replace"), checks));
    }
    layout->addWidget(replaceRow_);
    // Slack at the BOTTOM (browser: rows stack at the top of the body) — mid-body it
    // split the URL row from the Incognito row with a band of empty space.
    layout->addStretch(1);

    // Footer actions (browser settings-footer: every enabled button accent-filled,
    // Cancel included; a disabled one drops to the grey chip) — file/URL: Cancel /
    // Replace? / Open here / Open in new window. blank: Cancel / Create blank.
    QHBoxLayout* btnRow = addModalFooter(chrome);
    auto* cancel = new QPushButton("Cancel", this);
    makeModalCta(cancel, "x");
    cancel->setToolTip("Close without opening an image");
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    here_ = new QPushButton("Open here", this);
    makeModalCta(here_, "image");
    connect(here_, &QPushButton::clicked, this, [this] { outcome_ = Outcome::Here; accept(); });
    newWindow_ = new QPushButton("Open in new window", this);
    makeModalCta(newWindow_, "external");
    connect(newWindow_, &QPushButton::clicked, this, [this] { outcome_ = Outcome::NewWindow; accept(); });
    createBlank_ = new QPushButton("Create blank", this);
    makeModalCta(createBlank_, "image");   // browser #blank-image-create
    connect(createBlank_, &QPushButton::clicked, this, [this] { outcome_ = Outcome::Blank; accept(); });
    btnRow->addWidget(cancel);
    if (canReplace_) {
      replace_ = new QPushButton("Replace image", this);
      makeModalCta(replace_, "refresh");
      connect(replace_, &QPushButton::clicked, this, [this] { outcome_ = Outcome::Replace; accept(); });
      btnRow->addWidget(replace_);
    }
    btnRow->addWidget(here_);
    btnRow->addWidget(newWindow_);
    btnRow->addWidget(createBlank_);

    // Preview wiring (mirrors LinksDialog)
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
      clearPreviewImage();
      frameRow_->setVisible(false);
      quickcropRow_->setVisible(false);
      usePreview_->setEnabled(false);
      setHint("Could not load that source — " + msg);
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

    // A URL edit keeps the picture on screen while the text is corrected — it only
    // stops counting as THIS url's preview (the frame/crop controls it sized go with
    // it), and opening re-resolves the typed url. Enter previews it again.
    connect(url_, &QLineEdit::textEdited, this, [this] {
      if (source() != previewedSource_) stalePreview();
      refreshButtons();
    });
    url_->installEventFilter(this);
    connect(tabs_, &QTabWidget::currentChanged, this, [this] { applyMode(); });
    tabs_->setCurrentIndex(startBlank ? TabBlank : TabFile);
    applyMode();
    constructed_ = true;   // tab switches from here on are USER switches — they fade
  }
}

