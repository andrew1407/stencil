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

    // Browser openImageModal.js parity: the shared modal shell around the tabbed body.
    ModalChrome chrome = installModalChrome(this, "image", tr("Open Image"));
    // A crop stage over a tall picture outgrows the screen, so the body SCROLLS rather
    // than the window running off the bottom (the browser's .app-modal does the same).
    ModalScrollBody body = makeModalScrollBody(chrome, /*topPad=*/0);
    size_.bodyScroll = body.scroll;
    size_.bodyContent = body.content;   // a QScrollArea's own sizeHint is a fixed default, so
    QVBoxLayout* layout = body.layout;


    buildTabs(layout, blankW, blankH);
    buildPreviewColumn(layout);
    buildCropRows(layout);

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
    act_.replaceRow = new QWidget(this);
    if (canReplace_) {
      rename_ = new QCheckBox("Rename project to the new image", this);
      keep_ = new QCheckBox("Keep existing annotations", this);
      keep_->setChecked(true);
      auto* checks = new QVBoxLayout;
      checks->setContentsMargins(0, 0, 0, 0);
      checks->setSpacing(7);
      checks->addWidget(rename_);
      checks->addWidget(keep_);
      auto* wrap = new QVBoxLayout(act_.replaceRow);
      wrap->setContentsMargins(0, 0, 0, 0);
      wrap->addWidget(vsRow(act_.replaceRow, tr("Replace"), checks));
    }
    layout->addWidget(act_.replaceRow);
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
    act_.here = new QPushButton("Open here", this);
    makeModalCta(act_.here, "image");
    connect(act_.here, &QPushButton::clicked, this, [this] { outcome_ = Outcome::HERE; accept(); });
    act_.newWindow = new QPushButton("Open in new window", this);
    makeModalCta(act_.newWindow, "external");
    connect(act_.newWindow, &QPushButton::clicked, this, [this] { outcome_ = Outcome::NEW_WINDOW; accept(); });
    act_.createBlank = new QPushButton("Create blank", this);
    makeModalCta(act_.createBlank, "image");   // browser #blank-image-create
    connect(act_.createBlank, &QPushButton::clicked, this, [this] { outcome_ = Outcome::BLANK; accept(); });
    btnRow->addWidget(cancel);
    if (canReplace_) {
      act_.replace = new QPushButton("Replace image", this);
      makeModalCta(act_.replace, "refresh");
      connect(act_.replace, &QPushButton::clicked, this, [this] { outcome_ = Outcome::REPLACE; accept(); });
      btnRow->addWidget(act_.replace);
    }
    btnRow->addWidget(act_.here);
    btnRow->addWidget(act_.newWindow);
    btnRow->addWidget(act_.createBlank);

    // Preview wiring (mirrors LinksDialog)
    preview_ = new MediaLoader(this);
    connect(preview_, &MediaLoader::loaded, this,
            [this](const QImage& img, const QString&) {
              previewIsVideo_ = preview_->isVideoSource();
              if (previewIsVideo_) {
                frameImage_ = img;
                scrub_.fps = preview_->frameRate() > 0 ? preview_->frameRate() : 30.0;
                scrub_.durationMs = preview_->durationMs();
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

