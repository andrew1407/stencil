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


  // Browser modalShell.css: `stencil-links-modal { --vs-label-w: 118px }`.
  static constexpr int LINKS_LABEL_W = 118;
  // A label column AND two chips beside the field, so these rows ask for the popover cap
  // (MainWindow::execMaybePopover): at the compact 420 the field clipped its placeholder.
  static constexpr int LINKS_FIELD_MIN_W = 470 - 2 * 18;   // modalChromeShared PAD_X

  LinksDialog::LinksDialog(const QString& source, const QString& resource,
                           bool hasImage, const QString& pageSeed,
                           const QString& units, QWidget* parent)
      : QDialog(parent), pageSeed(pageSeed) {
    setWindowTitle("Image links");
    setMinimumWidth(540);
    const QColor txt = palette().color(QPalette::WindowText);
    // Theme muted tone for hint/secondary text (browser --text-muted).
    const QString mutedCss =
        QString("color: %1;").arg(palette().color(QPalette::PlaceholderText).name());

    // Browser linksModal.js parity: the shared modal shell, not framed group boxes.
    ModalChrome chrome = installModalChrome(this, "link", tr("Image links"));
    QVBoxLayout* layout = chrome.body;

    // Only the links live here (browser linksModal parity). Each row is modalRow, the browser .vs-row
    // - so spacing 0, the hairline under each row doing the separating.
    auto* linksBox = new QWidget(this);
    linksBox->setMinimumWidth(LINKS_FIELD_MIN_W);   // or the compact shape clips the field
    auto* linksCol = new QVBoxLayout(linksBox);
    linksCol->setContentsMargins(0, 0, 0, 0);
    linksCol->setSpacing(0);

    // One editable link row (Source / Resource): the field plus a compact open-in-browser
    // chip (browser .btn-icon) and a solid danger clear ✕ (browser .links-clear.danger).
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
      QWidget* row = modalRow(linksBox, label, linkRow(edit, open, clear), LINKS_LABEL_W);
      row->setToolTip(labelTip);
      linksCol->addWidget(row);
    };
    sourceEdit = new QLineEdit(source, this);
    sourceEdit->setPlaceholderText("(empty — local upload)");
    addLinkRow(sourceEdit, tr("Source"), tr("The image/video’s own URL"),
               "Open source in the default browser", "Remove source link");

    resourceEdit = new QLineEdit(resource, this);
    resourceEdit->setPlaceholderText("(empty)");
    addLinkRow(resourceEdit, tr("Resource"), tr("The web page the image was found on"),
               "Open resource page in the default browser", "Remove resource link");
    layout->addWidget(linksBox);

    // Add image by URL: preview first, then load the previewed pixels
    auto* addBox = new QWidget(this);
    addBox->setMinimumWidth(LINKS_FIELD_MIN_W);
    auto* addCol = new QVBoxLayout(addBox);
    addCol->setContentsMargins(0, 0, 0, 0);
    addCol->setSpacing(8);
    addCol->addWidget(modalSectionLabel(tr("Add image by URL"), this));
    auto* addForm = new QFormLayout;
    addForm->setContentsMargins(0, 0, 0, 0);
    alignModalForm(addForm, /*growFields=*/true);
    addCol->addLayout(addForm);

    // URL + an inline Preview button (mirrors the browser modal's 👁 Preview).
    urlEdit = new QLineEdit(this);
    urlEdit->setPlaceholderText("https://… (image or video)");
    auto* previewBtn = new QPushButton("Preview", this);
    makeModalCta(previewBtn, "eye");
    previewBtn->setToolTip("Fetch and show the image / first video frame");
    auto* urlRow = new QHBoxLayout;
    urlRow->addWidget(urlEdit, 1);
    urlRow->addWidget(previewBtn);
    addForm->addRow("Image / video URL:", urlRow);

    urlResourceEdit = new QLineEdit(this);
    urlResourceEdit->setPlaceholderText("(optional — page the image is on)");
    addForm->addRow("Resource URL:", urlResourceEdit);

    previewLabel = new QLabel(this);
    previewLabel->setAlignment(Qt::AlignCenter);
    previewLabel->setMinimumHeight(120);
    previewLabel->setMaximumSize(PREVIEW_MAX_W, PREVIEW_MAX_H);
    previewLabel->setFrameShape(QFrame::StyledPanel);
    // Hidden until a preview lands (openImageDialog parity) — an empty bordered box
    // held a 120px void open in the middle of the dialog.
    previewLabel->setVisible(false);
    addForm->addRow(previewLabel);

    // "Video frame" controls, UNDER the preview like a player's scrubber; hidden until a preview
    // resolves the URL as a video. Slider and spin box stay mirrored on the persistent scrub player.
    frameRow = new QWidget(this);
    auto* frameV = new QVBoxLayout(frameRow);
    frameV->setContentsMargins(0, 0, 0, 0);
    auto* frameH = new QHBoxLayout;
    frameSlider = new QSlider(Qt::Horizontal, frameRow);
    frameSlider->setRange(0, 0);
    frameH->addWidget(frameSlider, 1);
    frameH->addWidget(new QLabel("Frame", frameRow));
    frame = new QSpinBox(frameRow);
    frame->setRange(0, 0);
    frameH->addWidget(frame);
    frameTotal = new QLabel(frameRow);
    frameTotal->setStyleSheet(mutedCss);
    frameH->addWidget(frameTotal);
    frameV->addLayout(frameH);
    usePreview = new QCheckBox("Use the video's preview image instead of a frame", frameRow);
    usePreview->setEnabled(false);  // off + disabled until a preview image is found
    frameV->addWidget(usePreview);
    frameRow->setVisible(false);  // shown only for videos
    addForm->addRow(frameRow);

    previewHint = new QLabel(this);
    previewHint->setStyleSheet(mutedCss + " font-size: 11px;");
    previewHint->setWordWrap(true);
    addForm->addRow(previewHint);

    buildQuickCrop(addForm, units);
    layout->addWidget(addBox);

    wirePreview(previewBtn);

    // With an image loaded, only its links can be edited; with no image, only the
    // add-by-URL loader is offered. Mirrors the browser modal's two modes.
    linksBox->setVisible(hasImage);
    addBox->setVisible(!hasImage);
    layout->addStretch(1);

    // Footer (browser settings-footer): add-by-URL alone gets one. Neither mode has a Cancel/Save
    // pair - edits apply when the dialog closes, however dismissed (browser linksModal parity).
    if (!hasImage)
      addModalFooter(chrome, tr("Downloads bypass page CORS, so any reachable "
                                "image/video URL works."));
  }
}
