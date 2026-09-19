// The quick-crop row and the crop's own page-ratio row, with its Custom W×H group.
#include "OpenImageDialog.hpp"
#include "openImageDialogParts.hpp"
#include "../support/SearchCombo.hpp"
#include "../support/iconSpin.hpp"
#include "guiHelpers.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace stencil::gui {

  void OpenImageDialog::buildCropRows(QVBoxLayout* layout) {
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
      // when arriving already equals motion_.albumShown's false default, so the widget must
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
  }

}
