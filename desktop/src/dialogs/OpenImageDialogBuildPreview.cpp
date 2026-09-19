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
    // No layout spacing: each row carries the gap BELOW it as its own margin, so a row arriving or
    // leaving changes the column by exactly its own height - a layout gap would pop in whole.
    previewCol->setSpacing(0);
    previewLabel_->setContentsMargins(0, PREVIEW_COL_GAP, 0, PREVIEW_COL_GAP);
    previewCol->addWidget(previewLabel_, 0, Qt::AlignHCenter);
    // The crop stage TAKES THE PICTURE'S PLACE while Crop is on (syncCropStage), a video's frame
    // included - never a second copy below it. The scrub bar stays under whichever of the two is up.
    cropStageHost_ = new QWidget(this);
    auto* stageBox = new QVBoxLayout(cropStageHost_);
    stageBox->setContentsMargins(0, 0, 0, 0);
    stageBox->setSpacing(6);
    cropStageHost_->setContentsMargins(0, PREVIEW_COL_GAP, 0, PREVIEW_COL_GAP);
    cropStageHost_->setVisible(false);
    previewCol->addWidget(cropStageHost_, 0, Qt::AlignHCenter);
    previewCol->addWidget(frameSlider_, 0, Qt::AlignHCenter);
    cropDims_ = new QLabel(this);
    cropDims_->setStyleSheet(MUTED_CSS);
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
    previewHint_->setStyleSheet(MUTED_CSS);
    previewHint_->setWordWrap(true);
    previewHint_->setContentsMargins(4, 10, 0, 0);   // browser .oi-status margin: 10px 0 0 4px
    previewHint_->setVisible(false);   // an empty hint keeps no line of its own
    layout->addWidget(previewHint_);
  }

}
