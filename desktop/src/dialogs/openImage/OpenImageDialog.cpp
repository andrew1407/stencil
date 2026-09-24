#include "../../support/control/dblReset.hpp"
#include "../../support/menu/SearchCombo.hpp"
#include "openImageDialogParts.hpp"
#include "OpenImageDialog.hpp"
#include "../../support/icon/iconSpin.hpp"
#include <QScrollArea>
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "../../support/modal/modalChrome.hpp"
#include "../../support/modal/modalReveal.hpp"
#include "../../support/control/UnderlineTabBar.hpp"
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
      : QDialog(parent), pageSeed(pageSeed), canReplace(canReplace) {
    setWindowTitle("Open Image");
    // The browser's shared modal width; the four-button footer a replaceable project
    // adds paints tighter than the layout's minimum reports, so it gets more room.
    setMinimumWidth(canReplace ? 610 : MODAL_WIDTH);

    // Browser openImageModal.js parity: the shared modal shell around the tabbed body.
    ModalChrome chrome = installModalChrome(this, "image", tr("Open Image"));
    // A crop stage over a tall picture outgrows the screen, so the body SCROLLS rather
    // than the window running off the bottom (the browser's .app-modal does the same).
    ModalScrollBody body = makeModalScrollBody(chrome, /*topPad=*/0);
    size.bodyScroll = body.scroll;
    size.bodyContent = body.content;   // a QScrollArea's own sizeHint is a fixed default, so
    QVBoxLayout* layout = body.layout;


    buildTabs(layout, blankW, blankH);
    buildPreviewColumn(layout);
    buildCropRows(layout);

    // Applies to a file/URL open; hidden on the Blank tab (never honored there).
    incogRow = vsRow(this, tr("Incognito"),
                      checkCaptionRow(this, incognito,
                                      tr("Edit without saving — the image is never "
                                         "written to storage.")));
    incogRow->setObjectName(QStringLiteral("oiNoDivider"));
    layout->addWidget(incogRow);
    // Incognito never offers a server target (browser: fillTargetSelect(!incog)).
    connect(incognito, &QCheckBox::toggled, this, &OpenImageDialog::refreshTargetRow);

    // Save target (browser #open-image-target-row): only shown when at least one server
    // is connected — setServerTargets fills it.
    target = new SearchComboBox(this, /*searchable=*/false);
    target->setObjectName(QStringLiteral("openImageTarget"));
    support::setResetDefault(target, 0);   // "here, locally"
    target->setToolTip("Open here locally or create on a connected server");
    targetRow = vsRow(this, tr("Save to"), target);
    targetRow->setVisible(false);
    layout->addWidget(targetRow);

    // What a double-click restores (browser openImage/modal.js onOpen, cropRows.js).
    support::setResetDefault(incognito, false);
    support::setResetDefault(cropPage, false);
    support::setResetDefault(cropPageSize, QStringLiteral("page"));

    // Replace options: only shown on the Local file tab over a replaceable project.
    // The two checks stack (browser .oi-replace wraps them onto their own lines).
    act.replaceRow = new QWidget(this);
    if (this->canReplace) {
      rename = new QCheckBox("Rename project to the new image", this);
      keep = new QCheckBox("Keep existing annotations", this);
      support::setResetDefault(rename, false);
      support::setResetDefault(keep, true);
      keep->setChecked(true);
      auto* checks = new QVBoxLayout;
      checks->setContentsMargins(0, 0, 0, 0);
      checks->setSpacing(7);
      checks->addWidget(rename);
      checks->addWidget(keep);
      auto* wrap = new QVBoxLayout(act.replaceRow);
      wrap->setContentsMargins(0, 0, 0, 0);
      wrap->addWidget(vsRow(act.replaceRow, tr("Replace"), checks));
    }
    layout->addWidget(act.replaceRow);
    // Slack at the BOTTOM (browser: rows stack at the top of the body) — mid-body it
    // split the URL row from the Incognito row with a band of empty space.
    layout->addStretch(1);

    // Footer actions (browser settings-footer: every enabled button accent-filled, Cancel included;
    // a disabled one drops to the grey chip). blank mode: Cancel / Create blank.
    QHBoxLayout* btnRow = addModalFooter(chrome);
    auto* cancel = new QPushButton("Cancel", this);
    makeModalCta(cancel, "x");
    cancel->setToolTip("Close without opening an image");
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    act.here = new QPushButton("Open here", this);
    makeModalCta(act.here, "image");
    connect(act.here, &QPushButton::clicked, this, [this] { outcome = Outcome::HERE; accept(); });
    act.newWindow = new QPushButton("Open in new window", this);
    makeModalCta(act.newWindow, "external");
    connect(act.newWindow, &QPushButton::clicked, this, [this] { outcome = Outcome::NEW_WINDOW; accept(); });
    act.createBlank = new QPushButton("Create blank", this);
    makeModalCta(act.createBlank, "image");   // browser #blank-image-create
    connect(act.createBlank, &QPushButton::clicked, this, [this] { outcome = Outcome::BLANK; accept(); });
    btnRow->addWidget(cancel);
    if (this->canReplace) {
      act.replace = new QPushButton("Replace image", this);
      makeModalCta(act.replace, "refresh");
      connect(act.replace, &QPushButton::clicked, this, [this] { outcome = Outcome::REPLACE; accept(); });
      btnRow->addWidget(act.replace);
    }
    btnRow->addWidget(act.here);
    btnRow->addWidget(act.newWindow);
    btnRow->addWidget(act.createBlank);

    // Preview wiring (mirrors LinksDialog)
    preview = new MediaLoader(this);
    connect(preview, &MediaLoader::loaded, this,
            [this](const QImage& img, const QString&) {
              previewIsVideo = preview->isVideoSource();
              if (previewIsVideo) {
                frameImage = img;
                scrub.fps = preview->frameRate() > 0 ? preview->frameRate() : 30.0;
                scrub.durationMs = preview->getDurationMs();
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
                frameRow->setVisible(false);
                showPreview(img, QString());   // no size line; the browser has none
                showQuickcrop(img.width(), img.height());
              }
            });
    connect(preview, &MediaLoader::failed, this, [this](const QString& msg) {
      teardownScrubPlayer();
      previewImage = QImage();
      frameImage = QImage();
      previewIsVideo = false;
      clearPreviewImage();
      frameRow->setVisible(false);
      quickcropRow->setVisible(false);
      setHint("Could not load that source — " + msg);
    });

    // Debounce seeks lightly so a fast drag coalesces into the latest position.
    fetchTimer = new QTimer(this);
    fetchTimer->setSingleShot(true);
    fetchTimer->setInterval(80);
    connect(fetchTimer, &QTimer::timeout, this, [this] {
      if (previewIsVideo) seekScrub(frame->value());
    });
    // Slider ↔ spin box stay mirrored; either changing schedules a debounced seek.
    connect(frameSlider, &QSlider::valueChanged, this, [this](int v) { setFrame(v); });
    connect(frame, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) { setFrame(v); });
    connect(frameSlider, &QSlider::sliderReleased, this, [this] {
      fetchTimer->stop();
      if (previewIsVideo) seekScrub(frame->value());
    });
    // A URL edit keeps the picture on screen while the text is corrected - it only stops counting as
    // THIS url's preview, and opening re-resolves the typed url. Enter previews it again.
    connect(url, &QLineEdit::textEdited, this, [this] {
      if (source() != previewedSource) stalePreview();
      refreshButtons();
    });
    url->installEventFilter(this);
    connect(tabs, &QTabWidget::currentChanged, this, [this] { applyMode(); });
    tabs->setCurrentIndex(startBlank ? TabBlank : TabFile);
    applyMode();
    constructed = true;   // tab switches from here on are USER switches — they fade
  }
}

