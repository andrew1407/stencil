// The quick pre-load edits of the add-by-URL section: crop to a page aspect and orientation, or
// not at all, plus the accent CTA that loads the previewed pixels. Shown once a preview resolves.
#include "../support/SearchCombo.hpp"
#include "linksDialogParts.hpp"
#include "LinksDialog.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "fetchGuard.hpp"
#include "MediaLoader.hpp"
#include "../support/modalChrome.hpp"
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
    quickcropRow_ = new QWidget(this);
    {
      auto* qc = new QHBoxLayout(quickcropRow_);
      qc->setContentsMargins(0, 0, 0, 0);
      qc->addWidget(new QLabel("Quick edits:", quickcropRow_));
      cropPage_ = new QCheckBox("Crop to page", quickcropRow_);
      cropPage_->setChecked(true);
      cropPage_->setToolTip("Crop the image to the page aspect on load");
      cropAlbum_ = new QCheckBox("Album", quickcropRow_);
      cropAlbum_->setToolTip("Landscape orientation (off = portrait)");
      cropPageSize_ = new SearchComboBox(quickcropRow_);
      // Every named ISO format (labels with sizes, data = the canonical name).
      // No "custom" here — the quick crop needs a fixed page aspect.
      fillPageSizeCombo(cropPageSize_, /*includeCustom=*/false, units);
      qc->addWidget(cropPage_);
      qc->addWidget(cropAlbum_);
      qc->addWidget(cropPageSize_);
      qc->addStretch(1);
    }
    quickcropRow_->setVisible(false);  // shown once a preview succeeds
    addForm->addRow(quickcropRow_);
    // Album / page only matter when cropping to page; grey them out otherwise.
    connect(cropPage_, &QCheckBox::toggled, this, &LinksDialog::syncQuickcropEnabled);

    loadBtn_ = new QPushButton("Load into editor", this);
    // The real call-to-action in add-by-URL mode → accent CTA (white glyph on the
    // accent fill, like the Connect dialog's Connect button).
    makeModalCta(loadBtn_, "download");
    loadBtn_->setEnabled(false);  // enabled once a preview succeeds
    loadBtn_->setToolTip("Preview an image or video URL first");
    connect(loadBtn_, &QPushButton::clicked, this, &LinksDialog::requestLoad);
    addForm->addRow(QString(), loadBtn_);
  }

}  // namespace stencil::gui
