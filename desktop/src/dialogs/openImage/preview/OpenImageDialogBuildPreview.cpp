// The preview column: the picture, the scrub bar riding under it, the frame row and the hint.
#include "OpenImageDialog.hpp"
#include "openImageDialogParts.hpp"
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace stencil::gui {

  namespace {
    // browser .oi-status / .oi-crop-dims: the muted line under the picture.
    const QString MUTED_CSS = "color: gray; font-size: 11px;";
  }

  void OpenImageDialog::buildPreviewColumn(QVBoxLayout* layout) {
    // Rendered preview image / frame.
    previewLabel = new QLabel(this);
    previewLabel->setAlignment(Qt::AlignCenter);
    previewLabel->setMaximumSize(previewFitBox());
    previewLabel->setFrameShape(QFrame::StyledPanel);
    // Hidden until a preview lands (the browser shows no preview area until there is one).
    previewLabel->setVisible(false);
    // The scrub bar rides UNDER the picture at its exact width, the way a player's
    // progress bar does (browser parity) — whatever is above it sizes it.
    frameSlider = new QSlider(Qt::Horizontal, this);
    makeScrubBar(frameSlider);
    frameSlider->setRange(0, 0);
    frameSlider->setContentsMargins(0, 0, 0, PREVIEW_COL_GAP);
    frameSlider->setVisible(false);
    auto* previewCol = new QVBoxLayout;
    previewCol->setContentsMargins(0, 0, 0, 0);
    // No layout spacing: each row carries the gap BELOW it as its own margin, so a row arriving or
    // leaving changes the column by exactly its own height - a layout gap would pop in whole.
    previewCol->setSpacing(0);
    previewLabel->setContentsMargins(0, PREVIEW_COL_GAP, 0, PREVIEW_COL_GAP);
    previewCol->addWidget(previewLabel, 0, Qt::AlignHCenter);
    // The crop stage TAKES THE PICTURE'S PLACE while Crop is on (syncCropStage), a video's frame
    // included - never a second copy below it. The scrub bar stays under whichever of the two is up.
    cropStageHost = new QWidget(this);
    auto* stageBox = new QVBoxLayout(cropStageHost);
    stageBox->setContentsMargins(0, 0, 0, 0);
    stageBox->setSpacing(6);
    cropStageHost->setContentsMargins(0, PREVIEW_COL_GAP, 0, PREVIEW_COL_GAP);
    cropStageHost->setVisible(false);
    previewCol->addWidget(cropStageHost, 0, Qt::AlignHCenter);
    previewCol->addWidget(frameSlider, 0, Qt::AlignHCenter);
    cropDims = new QLabel(this);
    cropDims->setStyleSheet(MUTED_CSS);
    cropDims->setVisible(false);
    previewCol->addWidget(cropDims, 0, Qt::AlignHCenter);
    auto* previewCenter = new QHBoxLayout;
    previewCenter->addStretch(1);
    previewCenter->addLayout(previewCol);
    previewCenter->addStretch(1);
    layout->addLayout(previewCenter);

    // The frame the scrub lands on, typed or stepped; the slider above is its twin.
    frame = new QSpinBox(this);
    frame->setRange(0, 0);
    frameRow = vsRow(this, tr("Frame"), frame);
    frameRow->setVisible(false);  // shown only for videos
    layout->addWidget(frameRow);

    previewHint = new QLabel(this);
    previewHint->setStyleSheet(MUTED_CSS);
    previewHint->setWordWrap(true);
    previewHint->setContentsMargins(4, 10, 0, 0);   // browser .oi-status margin: 10px 0 0 4px
    previewHint->setVisible(false);   // an empty hint keeps no line of its own
    layout->addWidget(previewHint);
  }

}
