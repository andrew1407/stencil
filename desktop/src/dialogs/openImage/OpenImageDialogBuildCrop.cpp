// The quick-crop row and the crop's own page-ratio row, with its Custom W×H group.
#include "OpenImageDialog.hpp"
#include "openImageDialogParts.hpp"
#include "../../support/menu/SearchCombo.hpp"
#include "../../support/icon/iconSpin.hpp"
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
    // Quick pre-load crop (mirrors LinksDialog quick-crop) as the browser's Crop .vs-row
    // (#open-image-crop-row). OFF by default; shown once a preview resolves.
    {
      // No tooltip on the box (browser parity): the caption beside it says what it does.
      auto* qc = checkCaptionRow(this, cropPage,
                                 tr("Trim to the page aspect before opening."));
      cropPage->setChecked(false);  // UNCHECKED by default → open the whole image
      cropAlbum = new QPushButton(this);
      cropAlbum->setObjectName(QStringLiteral("cropAlbumBtn"));   // app.qss: left, not centred
      cropAlbum->setCheckable(true);
      makeModalCta(cropAlbum, "swap");
      cropAlbum->setToolTip("Swap album / portrait — flips the crop orientation");
      cropAlbum->setAutoDefault(false);
      // Fixed to its longer face: an auto-width button jumped on every press, since "Album" and
      // "Portrait" measure differently (browser twin: openImage.css #open-image-crop-orientation).
      cropAlbum->setText(tr("Portrait"));
      const int portraitW = cropAlbum->sizeHint().width();
      cropAlbum->setText(tr("Album"));
      cropAlbum->setFixedWidth(std::max(portraitW, cropAlbum->sizeHint().width()));
      // Explicit, matching cropDims's own: cropAlbumDust's guard skips the FIRST call, so the widget
      // must already be hidden going in, not rely on that call to make it so.
      cropAlbum->setVisible(false);
      qc->addSpacing(8);
      qc->addWidget(cropAlbum);
      quickcropRow = vsRow(this, tr("Crop"), qc);
      quickcropRow->setObjectName(QStringLiteral("oiNoDivider"));
    }
    quickcropRow->setVisible(false);  // shown once a preview succeeds
    layout->addWidget(quickcropRow);

    // The crop's own ASPECT RATIO on its own row (browser #open-image-crop-size-row). Plain ratios
    // beside the project's page: every named ISO page shares one ratio, so the series said nothing.
    {
      auto* sc = new QHBoxLayout;
      sc->setContentsMargins(0, 0, 0, 0);
      sc->setSpacing(8);
      cropPageSize = new SearchComboBox(this, /*searchable=*/false);   // four entries: nothing to search
      cropPageSize->addItem(tr("Page — Default"), QStringLiteral("page"));
      cropPageSize->addItem(tr("1:1 (Square)"), QStringLiteral("1:1"));
      cropPageSize->addItem(tr("2:3"), QStringLiteral("2:3"));
      cropPageSize->addItem(tr("Custom"), QStringLiteral("custom"));
      cropPageSize->setCurrentIndex(0);   // starts on the project's own page
      cropPageSize->setToolTip("The crop's own aspect ratio");
      sc->addWidget(cropPageSize);   // its own content width (browser twin: .oi-crop-size, never stretched)
      // The W/H pair takes the row's slack, out to the Album/Portrait button's own edge above.
      cropSizeCustomGroup = new QWidget(this);
      {
        auto* cg = new QHBoxLayout(cropSizeCustomGroup);
        cg->setContentsMargins(0, 0, 0, 0);
        cg->setSpacing(6);
        cropSizeW = new QDoubleSpinBox(cropSizeCustomGroup);
        cropSizeW->setRange(0.1, 500.0);
        cropSizeW->setSingleStep(0.1);
        cropSizeW->setDecimals(1);
        cropSizeW->setValue(21.0);   // SettingsDialog's own Custom default (page/customW)
        cropSizeH = new QDoubleSpinBox(cropSizeCustomGroup);
        cropSizeH->setRange(0.1, 500.0);
        cropSizeH->setSingleStep(0.1);
        cropSizeH->setDecimals(1);
        cropSizeH->setValue(29.7);
        // W/H beside their own field (browser twin: openImageMarkup.js .oi-crop-size-field) - a plain
        // RATIO pair, so no unit label rides along either (user report).
        cg->addWidget(new QLabel(QStringLiteral("W"), cropSizeCustomGroup));
        cg->addWidget(cropSizeW, 1);
        cg->addWidget(new QLabel(QStringLiteral("H"), cropSizeCustomGroup));
        cg->addWidget(cropSizeH, 1);
      }
      cropSizeCustomGroup->setVisible(false);
      sc->addWidget(cropSizeCustomGroup, 1);
      sc->addStretch(0);   // with the pair hidden the room stays blank, never poured into the combo
      cropSizeRow = vsRow(this, tr("Aspect ratio"), sc);
      cropSizeRow->setObjectName(QStringLiteral("oiNoDivider"));
    }
    cropSizeRow->setVisible(false);
    layout->addWidget(cropSizeRow);
    connect(cropPageSize, &QComboBox::currentIndexChanged, this, [this] {
      cropSizeCustomDust(cropPageSize->currentData().toString() == QLatin1String("custom"));
      syncCropPageChoice();
      refitWindowHeight();   // the W/H pair is taller than the combo: unrefitted, it overflowed into a scrollbar
    });
    connect(cropSizeW, &QDoubleSpinBox::valueChanged, this, [this] {
      if (cropPageSize->currentData().toString() == QLatin1String("custom")) syncCropPageChoice();
    });
    connect(cropSizeH, &QDoubleSpinBox::valueChanged, this, [this] {
      if (cropPageSize->currentData().toString() == QLatin1String("custom")) syncCropPageChoice();
    });

    // The page + its read-out only matter while cropping; shown only then (browser parity).
    connect(cropPage, &QCheckBox::toggled, this, &OpenImageDialog::syncQuickcropEnabled);
    // A user press FLIPS the stage already up - never syncQuickcropEnabled's full rebuild, which
    // starts from the PICTURE's own default orientation and would overwrite this signal's state.
    connect(cropAlbum, &QPushButton::toggled, this, [this] {
      cropAlbum->setText(cropAlbum->isChecked() ? tr("Album") : tr("Portrait"));
      if (cropStage) cropStage->setAlbum(cropAlbum->isChecked());
      support::spinIconOnce(cropAlbum);   // the press turns the glyph it flips
    });
  }

}
