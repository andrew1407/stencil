#include "../support/searchCombo.hpp"
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

  namespace {
    // Tab order (QTabWidget indices).
    enum { TabFile = 0, TabUrl = 1, TabBlank = 2 };

    constexpr int kPreviewMaxW = 440;  // preview scaled to fit this box,
    constexpr int kPreviewMaxH = 300;  // keeping aspect ratio (browser parity).

    // One browser .vs-row (components.css): a hairline-underlined form row with a
    // fixed label column (stencil-open-image-modal label min-width: 88px, row
    // padding 7px 4px). The QSS half ([vsRow]/[vsLabel]) lives in theme.cpp.
    QWidget* vsRow(QWidget* parent, const QString& label, QLayout* content) {
      return modalRow(parent, label, content, /*labelMinW=*/88);
    }
    QWidget* vsRow(QWidget* parent, const QString& label, QWidget* field, int stretch = 1) {
      auto* h = new QHBoxLayout;
      h->setContentsMargins(0, 0, 0, 0);
      h->addWidget(field, stretch);
      if (!stretch) h->addStretch(1);
      return vsRow(parent, label, h);
    }

    // QTabWidget::setTabBar is protected — this shim installs the browser-parity
    // underline tab strip (support/underlineTabBar.hpp) before any tab is added.
    struct OiTabWidget : QTabWidget {
      explicit OiTabWidget(QWidget* parent) : QTabWidget(parent) {
        setTabBar(new UnderlineTabBar(this));
      }
    };

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
    // The browser's shared modal width. The four-button footer a replaceable project
    // adds (Cancel / Replace image / Open here / Open in new window) paints tighter
    // than the layout's minimum reports, so that shape gets a little more room.
    setMinimumWidth(canReplace ? 610 : kModalWidth);
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
    previewLabel_->setMaximumSize(kPreviewMaxW, kPreviewMaxH);
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

  // One dialog height across the source tabs, sized for the tallest (browser parity).
  // Measured on FIRST SHOW: updateGeometry() is a no-op on a hidden widget, so pre-show
  // every tab reports the same stale sizeHint. No explicit minimum is pinned — that would
  // override the layout's own and let the content squeeze.
  void OpenImageDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    if (measured_) return;
    measured_ = true;
    measuring_ = true;   // silent switches — no cross-tab fade for a measurement
    const int keep = tabs_->currentIndex();
    int tallest = 0;
    for (int i = 0; i < tabs_->count(); ++i) {
      tabs_->setCurrentIndex(i);
      if (QLayout* l = layout()) l->activate();
      tallest = std::max(tallest, sizeHint().height());
    }
    tabs_->setCurrentIndex(keep);
    measuring_ = false;
    if (QLayout* l = layout()) l->activate();
    if (tallest > height()) resize(width(), tallest);
  }

  // The arriving tab page eases in (the strip's underline slides in step — see
  // underlineTabBar.hpp), so switching Local file / URL link / Blank is not a hard
  // cut. The veil is dropped when the play ends, so nothing is ever left dimmed.
  void OpenImageDialog::fadeInCurrentPage() {
    if (!constructed_ || measuring_ || !isVisible() || support::motionReduced()) return;
    QWidget* page = tabs_->currentWidget();
    if (!page) return;
    auto* veil = new QGraphicsOpacityEffect(page);
    veil->setOpacity(0.0);
    page->setGraphicsEffect(veil);
    auto* fade = new QPropertyAnimation(veil, "opacity", veil);
    fade->setDuration(180);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    QPointer<QWidget> guard(page);
    QPointer<QGraphicsOpacityEffect> veilGuard(veil);
    connect(fade, &QPropertyAnimation::finished, page, [guard, veilGuard] {
      // however it ended, never left dimmed — but only OUR veil is removed
      if (guard && veilGuard && guard->graphicsEffect() == veilGuard)
        guard->setGraphicsEffect(nullptr);
    });
    fade->start(QAbstractAnimation::DeleteWhenStopped);
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
    // Anchored on the custom-fill swatch that was clicked.
    const QColor c =
        support::pickColorAnimated(customColor_, this, "Fill color", customSwatch_);
    if (!c.isValid()) return;
    customColor_ = c;
    setColorSwatch(customSwatch_, customColor_);
  }

  // A QTabWidget's pane is as tall as its TALLEST page, so the one-row File/URL
  // tabs would carry the Blank tab's empty rows under them — give only the page on
  // show its height (the browser's tab panel is content-height too).
  void OpenImageDialog::fitTabsToCurrentPage() {
    QWidget* page = tabs_->currentWidget();
    if (!page) return;
    int tallest = 0;   // QTabWidget::sizeHint() asks every page, not just the one on show
    for (int i = 0; i < tabs_->count(); i++)
      tallest = std::max(tallest, tabs_->widget(i)->sizeHint().height());
    const int chrome = tabs_->sizeHint().height() - tallest;   // tab bar + pane frame
    tabs_->setFixedHeight(chrome + page->sizeHint().height());
  }

  void OpenImageDialog::applyMode() {
    const bool blank = tabs_->currentIndex() == TabBlank;
    fitTabsToCurrentPage();
    incogRow_->setVisible(!blank);  // incognito has no effect on a blank
    here_->setVisible(!blank);
    newWindow_->setVisible(!blank);
    if (replace_) replace_->setVisible(!blank && tabs_->currentIndex() == TabFile);
    replaceRow_->setVisible(!blank && canReplace_ && tabs_->currentIndex() == TabFile);
    createBlank_->setVisible(blank);
    refreshTargetRow();
    if (blank) clearPreviewImage();   // the blank tab has no source to preview
    // Switching source tabs invalidates any preview built for the other tab.
    resetPreviewState();
    if (!blank) refreshButtons();
    else frameRow_->setVisible(false);
    fadeInCurrentPage();
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
        kPreviewMaxW, kPreviewMaxH, Qt::KeepAspectRatio, Qt::SmoothTransformation));
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

  // Album / page size are only meaningful while cropping to page — shown only then, the
  // toggle wearing the orientation it holds (browser #open-image-crop-orientation).
  void OpenImageDialog::syncQuickcropEnabled() {
    const bool on = cropPage_->isChecked();
    cropAlbum_->setText(cropAlbum_->isChecked() ? tr("Album") : tr("Portrait"));
    cropAlbum_->setVisible(on);
    cropPageSize_->setVisible(on);
  }

  void OpenImageDialog::setServerTargets(const QStringList& urls) {
    serverUrls_ = urls;
    target_->clear();
    target_->addItem(tr("Local (this computer)"), QString());
    for (const QString& u : urls) target_->addItem(u, u);
    refreshTargetRow();
  }

  // The row shows only with a server to pick, for a file/URL open that is not incognito.
  void OpenImageDialog::refreshTargetRow() {
    if (!targetRow_) return;
    const bool blank = tabs_->currentIndex() == TabBlank;
    targetRow_->setVisible(!serverUrls_.isEmpty() && !blank && !incognito_->isChecked());
  }

  QString OpenImageDialog::serverTarget() const {
    // isHidden, not isVisible: read after exec() returns, when the dialog is down.
    if (!targetRow_ || targetRow_->isHidden()) return QString();
    return target_->currentData().toString();
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

  // The White/Black presets and the picker all write customColor_ (browser parity:
  // the presets set the same fill the custom swatch holds).
  QColor OpenImageDialog::blankColor() const { return customColor_; }
  int OpenImageDialog::blankWidth() const { return blankWidth_->value(); }
  int OpenImageDialog::blankHeight() const { return blankHeight_->value(); }

  bool OpenImageDialog::cropToPage() const { return cropPage_ && cropPage_->isChecked(); }
  bool OpenImageDialog::cropAlbum() const { return cropAlbum_ && cropAlbum_->isChecked(); }
  QString OpenImageDialog::cropPageSize() const {
    return cropPageSize_ ? cropPageSize_->currentData().toString() : QString();
  }

}
