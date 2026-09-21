// The quick pre-load edits of the add-by-URL section: crop to a page aspect and orientation, or
// not at all, plus the accent CTA that loads the previewed pixels. Shown once a preview resolves.
#include "../../support/menu/SearchCombo.hpp"
#include "linksDialogParts.hpp"
#include "LinksDialog.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "fetchGuard.hpp"
#include "MediaLoader.hpp"
#include "../../support/modal/modalChrome.hpp"
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

  void LinksDialog::buildQuickCrop(QFormLayout* addForm, const QString& units) {
    // Quick pre-load edits (mirrors browser linksModal quick-crop): open the editor already cropped
    // to a page aspect/orientation, or uncropped. Shown only once a preview resolves.
    quickcropRow = new QWidget(this);
    {
      auto* qc = new QHBoxLayout(quickcropRow);
      qc->setContentsMargins(0, 0, 0, 0);
      qc->addWidget(new QLabel("Quick edits:", quickcropRow));
      cropPage = new QCheckBox("Crop to page", quickcropRow);
      cropPage->setChecked(true);
      cropPage->setToolTip("Crop the image to the page aspect on load");
      cropAlbum = new QCheckBox("Album", quickcropRow);
      cropAlbum->setToolTip("Landscape orientation (off = portrait)");
      cropPageSize = new SearchComboBox(quickcropRow);
      // Every named ISO format (labels with sizes, data = the canonical name).
      // No "custom" here — the quick crop needs a fixed page aspect.
      fillPageSizeCombo(cropPageSize, /*includeCustom=*/false, units);
      qc->addWidget(cropPage);
      qc->addWidget(cropAlbum);
      qc->addWidget(cropPageSize);
      qc->addStretch(1);
    }
    quickcropRow->setVisible(false);  // shown once a preview succeeds
    addForm->addRow(quickcropRow);
    // Album / page only matter when cropping to page; grey them out otherwise.
    connect(cropPage, &QCheckBox::toggled, this, &LinksDialog::syncQuickcropEnabled);

    loadBtn = new QPushButton("Load into editor", this);
    // The real call-to-action in add-by-URL mode → accent CTA (white glyph on the
    // accent fill, like the Connect dialog's Connect button).
    makeModalCta(loadBtn, "download");
    loadBtn->setEnabled(false);  // enabled once a preview succeeds
    loadBtn->setToolTip("Preview an image or video URL first");
    connect(loadBtn, &QPushButton::clicked, this, &LinksDialog::requestLoad);
    addForm->addRow(QString(), loadBtn);
  }

}  // namespace stencil::gui
