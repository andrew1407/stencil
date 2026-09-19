#include "../support/SearchCombo.hpp"
#include "openImageDialogParts.hpp"
#include "OpenImageDialog.hpp"
#include "../support/iconSpin.hpp"
#include <QScrollArea>
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "../support/modalChrome.hpp"
#include "../support/modalReveal.hpp"
#include "../support/UnderlineTabBar.hpp"
#include "MediaLoader.hpp"
#include <algorithm>
#include <QAudioOutput>
#include <QGraphicsOpacityEffect>
#include <QPointer>
#include <QPropertyAnimation>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
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
                                   const QString& pageSeed)
      : QDialog(parent), pageSeed_(pageSeed), canReplace_(canReplace) {
    setWindowTitle("Open Image");
    // The browser's shared modal width; the four-button footer a replaceable project
    // adds paints tighter than the layout's minimum reports, so it gets more room.
    setMinimumWidth(canReplace ? 610 : MODAL_WIDTH);
    const QString mutedCss = "color: gray; font-size: 11px;";

    // Browser openImageModal.js parity: the shared modal shell around the tabbed body.
    ModalChrome chrome = installModalChrome(this, "image", tr("Open Image"));
    // A crop stage over a tall picture outgrows the screen, so the body SCROLLS rather
    // than the window running off the bottom (the browser's .app-modal does the same).
    ModalScrollBody body = makeModalScrollBody(chrome, /*topPad=*/0);
    bodyScroll_ = body.scroll;
    bodyContent_ = body.content;   // a QScrollArea's own sizeHint is a fixed default, so
    QVBoxLayout* layout = body.layout;

    // Source tabs: Local file / URL link / Blank — the browser .oi-tab strip
    // (UnderlineTabBar.hpp): hover, sliding underline, and an accent-tinted glyph.
    tabs_ = new OiTabWidget(this);
    // No pane box: the .vs-rows carry their own hairlines, so a rounded pane doubled
    // up — only the strip's own full-width hairline remains (theme.cpp).
    tabs_->setObjectName("oiTabs");
    // Hug the tab page. QTabWidget expands by default, so the pane stretched into a tall
    // empty box under a two-field form (the browser's tab panel is content-height).
    tabs_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    // Tab: Local file (browser: one .vs-row "Choose" + the file input) — a read-only
    // path field + Choose (images AND videos). It auto-previews, so it needs no button.
    auto* fileTab = new QWidget(this);
    auto* fileV = new QVBoxLayout(fileTab);
    fileV->setContentsMargins(0, 14, 0, 0);   // browser .oi-tabs margin-bottom: 14px
    fileV->setSpacing(0);
    // ONE control, not a button beside a field: the accent CTA on the left butted straight
    // against the path readout, both inside a single outlined box — the browser's .oi-file
    // (css/components/openImage.css). The box carries the outline, so the halves carry none.
    auto* fileBox = new QFrame(this);
    fileBox->setObjectName(QStringLiteral("oiFileBox"));
    auto* fileRow = new QHBoxLayout(fileBox);
    fileRow->setContentsMargins(0, 0, 0, 0);
    fileRow->setSpacing(0);
    path_ = new QLineEdit(this);
    path_->setReadOnly(true);
    path_->setObjectName(QStringLiteral("oiPathField"));
    path_->setPlaceholderText("No file chosen");
    auto* browse = new QPushButton("Choose File", this);
    browse->setObjectName(QStringLiteral("oiChooseBtn"));
    makeModalCta(browse, "folder");
    connect(browse, &QPushButton::clicked, this, &OpenImageDialog::browse);
    // The whole box opens the chooser, not just the button — the browser's is one control
    // end to end, and a read-only field that ignores a click reads as broken.
    support::clickActivates(path_, browse);
    path_->setToolTip(tr("Click to choose an image or video"));
    fileRow->addWidget(browse);
    fileRow->addWidget(path_, 1);
    fileV->addWidget(vsRow(fileTab, tr("Choose"), fileBox));
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

    // Tab: Blank (browser FILL COLOR / SIZE (PX)): White and Black are swatch BUTTONS
    // with the custom swatch beside them, then plain px fields — no radios, no suffix.
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
    // A QToolButton is icon-ONLY by default, which would drop the hex setColorSwatch writes.
    customSwatch_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setColorSwatch(customSwatch_, customColor_, SWATCH_SIZE, /*withHex=*/true);
    connect(customSwatch_, &QToolButton::clicked, this, &OpenImageDialog::pickCustomColor);
    connect(whiteBtn, &QPushButton::clicked, this, [this] {
      customColor_ = QColor(Qt::white);
      setColorSwatch(customSwatch_, customColor_, SWATCH_SIZE, /*withHex=*/true);
    });
    connect(blackBtn, &QPushButton::clicked, this, [this] {
      customColor_ = QColor(Qt::black);
      setColorSwatch(customSwatch_, customColor_, SWATCH_SIZE, /*withHex=*/true);
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
    previewLabel_->setMaximumSize(previewFitBox());
    previewLabel_->setFrameShape(QFrame::StyledPanel);
    // Hidden until a preview lands (the browser shows no preview area until there is one).
    previewLabel_->setVisible(false);
    // The scrub bar rides UNDER the picture at its exact width, the way a player's
    // progress bar does (browser parity) — whatever is above it sizes it.
    frameSlider_ = new QSlider(Qt::Horizontal, this);
    makeScrubBar(frameSlider_);
    frameSlider_->setRange(0, 0);
    frameSlider_->setContentsMargins(0, 0, 0, PREVIEW_COL_GAP);
    frameSlider_->setVisible(false);
    auto* previewCol = new QVBoxLayout;
    previewCol->setContentsMargins(0, 0, 0, 0);
    // No layout spacing: each row carries the gap BELOW it as its own margin, so the
    // read-out arriving or leaving changes the column by exactly its own height — a
    // layout gap would pop in whole while the line was still sliding.
    previewCol->setSpacing(0);
    previewLabel_->setContentsMargins(0, PREVIEW_COL_GAP, 0, PREVIEW_COL_GAP);
    previewCol->addWidget(previewLabel_, 0, Qt::AlignHCenter);
    // The crop stage TAKES THE PICTURE'S PLACE while Crop is on (syncCropStage), a video's
    // frame included — the rect is drawn on the picture already there, never on a second
    // copy below it. The scrub bar stays under whichever of the two is up.
    cropStageHost_ = new QWidget(this);
    auto* stageBox = new QVBoxLayout(cropStageHost_);
    stageBox->setContentsMargins(0, 0, 0, 0);
    stageBox->setSpacing(6);
    cropStageHost_->setContentsMargins(0, PREVIEW_COL_GAP, 0, PREVIEW_COL_GAP);
    cropStageHost_->setVisible(false);
    previewCol->addWidget(cropStageHost_, 0, Qt::AlignHCenter);
    previewCol->addWidget(frameSlider_, 0, Qt::AlignHCenter);
    cropDims_ = new QLabel(this);
    cropDims_->setStyleSheet(mutedCss);
    cropDims_->setVisible(false);
    previewCol->addWidget(cropDims_, 0, Qt::AlignHCenter);
    auto* previewCenter = new QHBoxLayout;
    previewCenter->addStretch(1);
    previewCenter->addLayout(previewCol);
    previewCenter->addStretch(1);
    layout->addLayout(previewCenter);

    // The frame the scrub lands on, typed or stepped; the slider above is its twin.
    frame_ = new QSpinBox(this);
    frame_->setRange(0, 0);
    frameRow_ = vsRow(this, tr("Frame"), frame_);
    frameRow_->setVisible(false);  // shown only for videos
    layout->addWidget(frameRow_);

    previewHint_ = new QLabel(this);
    previewHint_->setStyleSheet(mutedCss);
    previewHint_->setWordWrap(true);
    previewHint_->setContentsMargins(4, 10, 0, 0);   // browser .oi-status margin: 10px 0 0 4px
    previewHint_->setVisible(false);   // an empty hint keeps no line of its own
    layout->addWidget(previewHint_);

    // Quick pre-load crop (mirrors LinksDialog quick-crop) as the browser's Crop
    // .vs-row (#open-image-crop-row): the toggle, its caption, then — only while
    // cropping — the Album/Portrait toggle and the page the aspect comes from. Crop is
    // OFF by default; shown only once a preview resolves an image/frame.
    {
      // No tooltip on the box (browser parity): the caption beside it says what it does.
      auto* qc = checkCaptionRow(this, cropPage_,
                                 tr("Trim to the page aspect before opening."));
      cropPage_->setChecked(false);  // UNCHECKED by default → open the whole image
      cropAlbum_ = new QPushButton(this);
      cropAlbum_->setObjectName(QStringLiteral("cropAlbumBtn"));   // app.qss: left, not centred
      cropAlbum_->setCheckable(true);
      makeModalCta(cropAlbum_, "swap");
      cropAlbum_->setToolTip("Swap album / portrait — flips the crop orientation");
      cropAlbum_->setAutoDefault(false);
      // Fixed to its longer face: an auto-width button jumped on every press, since
      // "Album" and "Portrait" measure differently (browser twin: openImage.css
      // #open-image-crop-orientation).
      cropAlbum_->setText(tr("Portrait"));
      const int portraitW = cropAlbum_->sizeHint().width();
      cropAlbum_->setText(tr("Album"));
      cropAlbum_->setFixedWidth(std::max(portraitW, cropAlbum_->sizeHint().width()));
      // Explicit, matching cropDims_'s own: cropAlbumDust's guard skips the FIRST call
      // when arriving already equals cropAlbumShown_'s false default, so the widget must
      // already be hidden going in, not rely on that call to make it so.
      cropAlbum_->setVisible(false);
      qc->addSpacing(8);
      qc->addWidget(cropAlbum_);
      quickcropRow_ = vsRow(this, tr("Crop"), qc);
      quickcropRow_->setObjectName(QStringLiteral("oiNoDivider"));
    }
    quickcropRow_->setVisible(false);  // shown once a preview succeeds
    layout->addWidget(quickcropRow_);

    // The crop's own ASPECT RATIO — its own row (browser twin: #open-image-crop-size-row),
    // shown/hidden with the same particle sweep as the read-out below the stage, only while
    // cropping. A handful of plain ratios beside the project's own page: every named ISO
    // page (A/B/C) shares one ratio, so listing the whole series here said nothing a
    // single "Page" entry doesn't already say.
    {
      auto* sc = new QHBoxLayout;
      sc->setContentsMargins(0, 0, 0, 0);
      sc->setSpacing(8);
      cropPageSize_ = new SearchComboBox(this, /*searchable=*/false);   // four entries: nothing to search
      cropPageSize_->addItem(tr("Page — Default"), QStringLiteral("page"));
      cropPageSize_->addItem(tr("1:1 (Square)"), QStringLiteral("1:1"));
      cropPageSize_->addItem(tr("2:3"), QStringLiteral("2:3"));
      cropPageSize_->addItem(tr("Custom"), QStringLiteral("custom"));
      cropPageSize_->setCurrentIndex(0);   // starts on the project's own page
      cropPageSize_->setToolTip("The crop's own aspect ratio");
      sc->addWidget(cropPageSize_);   // its own content width (browser twin: .oi-crop-size, never stretched)
      // The W/H pair takes the row's slack, out to the Album/Portrait button's own edge above.
      cropSizeCustomGroup_ = new QWidget(this);
      {
        auto* cg = new QHBoxLayout(cropSizeCustomGroup_);
        cg->setContentsMargins(0, 0, 0, 0);
        cg->setSpacing(6);
        cropSizeW_ = new QDoubleSpinBox(cropSizeCustomGroup_);
        cropSizeW_->setRange(0.1, 500.0);
        cropSizeW_->setSingleStep(0.1);
        cropSizeW_->setDecimals(1);
        cropSizeW_->setValue(21.0);   // SettingsDialog's own Custom default (page_/customW_)
        cropSizeH_ = new QDoubleSpinBox(cropSizeCustomGroup_);
        cropSizeH_->setRange(0.1, 500.0);
        cropSizeH_->setSingleStep(0.1);
        cropSizeH_->setDecimals(1);
        cropSizeH_->setValue(29.7);
        // W/H beside their own field (browser twin: openImageMarkup.js's
        // .oi-crop-size-field) — a plain RATIO pair, so no unit label rides along either
        // (user report).
        cg->addWidget(new QLabel(QStringLiteral("W"), cropSizeCustomGroup_));
        cg->addWidget(cropSizeW_, 1);
        cg->addWidget(new QLabel(QStringLiteral("H"), cropSizeCustomGroup_));
        cg->addWidget(cropSizeH_, 1);
      }
      cropSizeCustomGroup_->setVisible(false);
      sc->addWidget(cropSizeCustomGroup_, 1);
      sc->addStretch(0);   // with the pair hidden the room stays blank, never poured into the combo
      cropSizeRow_ = vsRow(this, tr("Aspect ratio"), sc);
      cropSizeRow_->setObjectName(QStringLiteral("oiNoDivider"));
    }
    cropSizeRow_->setVisible(false);
    layout->addWidget(cropSizeRow_);
    connect(cropPageSize_, &QComboBox::currentIndexChanged, this, [this] {
      cropSizeCustomDust(cropPageSize_->currentData().toString() == QLatin1String("custom"));
      syncCropPageChoice();
      refitWindowHeight();   // the W/H pair is taller than the combo: unrefitted, it overflowed into a scrollbar
    });
    connect(cropSizeW_, &QDoubleSpinBox::valueChanged, this, [this] {
      if (cropPageSize_->currentData().toString() == QLatin1String("custom")) syncCropPageChoice();
    });
    connect(cropSizeH_, &QDoubleSpinBox::valueChanged, this, [this] {
      if (cropPageSize_->currentData().toString() == QLatin1String("custom")) syncCropPageChoice();
    });

    // The page + its read-out only matter while cropping; shown only then (browser parity).
    connect(cropPage_, &QCheckBox::toggled, this, &OpenImageDialog::syncQuickcropEnabled);
    // A user press FLIPS the stage already up — never syncQuickcropEnabled's full rebuild,
    // which starts a fresh stage from the PICTURE's own default orientation and would
    // silently overwrite the very checked state this signal just set (button "did nothing").
    connect(cropAlbum_, &QPushButton::toggled, this, [this] {
      cropAlbum_->setText(cropAlbum_->isChecked() ? tr("Album") : tr("Portrait"));
      if (cropStage_) cropStage_->setAlbum(cropAlbum_->isChecked());
      support::spinIconOnce(cropAlbum_);   // the press turns the glyph it flips
    });

    // Applies to a file/URL open; hidden on the Blank tab (never honored there).
    incogRow_ = vsRow(this, tr("Incognito"),
                      checkCaptionRow(this, incognito_,
                                      tr("Edit without saving — the image is never "
                                         "written to storage.")));
    incogRow_->setObjectName(QStringLiteral("oiNoDivider"));
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
    connect(here_, &QPushButton::clicked, this, [this] { outcome_ = Outcome::HERE; accept(); });
    newWindow_ = new QPushButton("Open in new window", this);
    makeModalCta(newWindow_, "external");
    connect(newWindow_, &QPushButton::clicked, this, [this] { outcome_ = Outcome::NEW_WINDOW; accept(); });
    createBlank_ = new QPushButton("Create blank", this);
    makeModalCta(createBlank_, "image");   // browser #blank-image-create
    connect(createBlank_, &QPushButton::clicked, this, [this] { outcome_ = Outcome::BLANK; accept(); });
    btnRow->addWidget(cancel);
    if (canReplace_) {
      replace_ = new QPushButton("Replace image", this);
      makeModalCta(replace_, "refresh");
      connect(replace_, &QPushButton::clicked, this, [this] { outcome_ = Outcome::REPLACE; accept(); });
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
                scrubFps_ = preview_->frameRate() > 0 ? preview_->frameRate() : 30.0;
                scrubDurationMs_ = preview_->durationMs();
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
                frameRow_->setVisible(false);
                showPreview(img, QString());   // no size line; the browser has none
                showQuickcrop(img.width(), img.height());
              }
            });
    connect(preview_, &MediaLoader::failed, this, [this](const QString& msg) {
      teardownScrubPlayer();
      previewImage_ = QImage();
      frameImage_ = QImage();
      previewIsVideo_ = false;
      clearPreviewImage();
      frameRow_->setVisible(false);
      quickcropRow_->setVisible(false);
      setHint("Could not load that source — " + msg);
    });

    // Debounce seeks lightly so a fast drag coalesces into the latest position.
    fetchTimer_ = new QTimer(this);
    fetchTimer_->setSingleShot(true);
    fetchTimer_->setInterval(80);
    connect(fetchTimer_, &QTimer::timeout, this, [this] {
      if (previewIsVideo_) seekScrub(frame_->value());
    });
    // Slider ↔ spin box stay mirrored; either changing schedules a debounced seek.
    connect(frameSlider_, &QSlider::valueChanged, this, [this](int v) { setFrame(v); });
    connect(frame_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) { setFrame(v); });
    connect(frameSlider_, &QSlider::sliderReleased, this, [this] {
      fetchTimer_->stop();
      if (previewIsVideo_) seekScrub(frame_->value());
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

