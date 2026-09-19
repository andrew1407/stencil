#include "../support/SearchCombo.hpp"
#include "openImageDialogParts.hpp"
#include "OpenImageDialog.hpp"
#include <QScrollArea>
#include "cropGeometry.hpp"
#include "pageMetrics.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "../support/modalChrome.hpp"
#include "../support/modalReveal.hpp"
#include "../support/UnderlineTabBar.hpp"
#include "MediaLoader.hpp"
#include <algorithm>
#include <cmath>
#include <QAudioOutput>
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
  QString OpenImageDialog::cropPageSize() const { return pageSeed_; }

  // What the user actually dragged. Empty unless a stage is up, so a caller with no rect
  // falls back to the centred crop it always computed.
  core::CropRect OpenImageDialog::cropRect() const {
    return cropStage_ ? cropStage_->cropRect() : core::CropRect{};
  }

  // Reveal the quick-crop row for a previewed image/frame: the orientation follows the
  // media (wider-than-tall => album) and the page size the app's current one. Crop stays OFF.
  void OpenImageDialog::showQuickcrop(int w, int h) {
    cropAlbum_->setChecked((w >= h) && (w > 0));
    // SHOWN before the sync: syncQuickcropEnabled ends in the refit, and a row still hidden
    // then is left out of the hint — the window came up one row short of its own content.
    quickcropRow_->setVisible(true);
    syncQuickcropEnabled();
    refreshOpenEnabled();
  }

  // The page and its crop only matter while cropping. The read-out is the crop editor's own
  // line (CropDialogDrag.cpp) over the same core geometry, so the two cannot drift apart.
  void OpenImageDialog::syncQuickcropEnabled() {
    const bool on = cropPage_->isChecked();
    const int tab = tabs_->currentIndex();   // the choice belongs to the tab that made it
    if (tab == TabFile || tab == TabUrl) tabCrop_[tab] = on;
    cropAlbum_->setText(cropAlbum_->isChecked() ? tr("Album") : tr("Portrait"));
    cropAlbumDust(on);
    cropSizeRowDust(on);
    cropSizeCustomDust(on && cropPageSize_->currentData().toString() == QLatin1String("custom"));
    syncCropStage();
  }

  // cropPageSize_/cropSizeW_/H_ resolved — the CROP's own ratio, never pageSeed_.
  // Browser twin: openImageModal.js pageDims()/CROP_RATIOS.
  core::PageSize OpenImageDialog::cropPageDims() const {
    if (!cropPageSize_) return core::PageSize{29.7, 42.0};
    const QString key = cropPageSize_->currentData().toString();
    if (key == QLatin1String("custom"))
      return core::PageSize{cropSizeW_->value(), cropSizeH_->value()};
    if (key == QLatin1String("1:1")) return core::PageSize{1.0, 1.0};
    if (key == QLatin1String("2:3")) return core::PageSize{2.0, 3.0};
    const core::PageSize page = core::namedPageSize(pageSeed_.toStdString());
    return page.width > 0 ? page : core::PageSize{29.7, 42.0};
  }

  // A different ratio (or Custom width/height) picked while the stage is already up.
  void OpenImageDialog::syncCropPageChoice() {
    if (!cropStage_) return;
    const core::PageSize page = cropPageDims();
    cropStage_->setPageSize(page.width, page.height);
  }

  // The stage TAKES THE PICTURE'S PLACE while Crop is on — a draggable box over the image,
  // its size read out underneath (the browser's cropStage); a VIDEO's frame is no different,
  // so nothing arrives or leaves. Rebuilt per image: the rect lives in that image's pixels.
  void OpenImageDialog::syncCropStage() {
    const bool on = cropPage_->isChecked() && !previewImage_.isNull();
    if (cropStage_) {
      // HIDDEN first: deleteLater leaves it a live child past the measure below, and a
      // flip would count both stages — one extra picture of height.
      cropStage_->hide();
      cropStage_->deleteLater();
      cropStage_ = nullptr;
    }
    cropStageHost_->setVisible(on);
    // What the LABEL has, not what we still hold pixels for: a URL edited after a preview
    // keeps its picture on screen (stalePreview) though previewImage_ has gone.
    previewLabel_->setVisible(!previewLabel_->pixmap().isNull() && !on);
    if (!on) {
      // The fall is photographed and raised while the line still stands, and only then does
      // the window take its room back — a resize first drags the cloud with it.
      cropDimsDust(false);
      cropSizeRowDust(false);
      cropAlbumDust(false);
      cropSizeCustomDust(false);
      // The rows' room comes back to the picture, if the stage had to give any up.
      if (previewCapH_ > 0) { previewCapH_ = 0; applyPreviewFit(); }
      if (previewIsVideo_) frameSlider_->setFixedWidth(previewLabel_->pixmap().width());
      refitWindowHeight();
      return;
    }
    const core::PageSize page = cropPageDims();
    const double pw = page.width;
    const double ph = page.height;
    // The tab's OWN last-dragged rect, if this is the same decode it was dragged on — a
    // fresh centeredCrop() otherwise threw the drag away on every tab switch, even a round
    // trip back to the exact same picture. Its own shape says Album or Portrait; the
    // checkbox follows THAT, not the other way around, or restoring it would be overwritten
    // by whatever the LAST tab happened to leave the toggle on.
    const int tab = tabs_->currentIndex();
    const TabPreviewCache* cache = (tab == TabFile || tab == TabUrl) ? &tabCache_[tab] : nullptr;
    const bool hasSaved = cache && cache->cropRectValid && cache->previewImage.size() == previewImage_.size();
    // The FIRST crop on this decode starts from ITS OWN orientation (showQuickcrop's own
    // w>=h rule) — never cropAlbum_'s current state, which is a SINGLE checkbox shared by
    // every tab and is whatever the LAST tab happened to leave it at. A restore reaching
    // here via applyMode()'s quiet resync — not the fresh-load path that calls
    // showQuickcrop — never gets that recompute otherwise.
    const int iw = previewImage_.width(), ih = previewImage_.height();
    const bool defaultAlbum = (iw >= ih) && (iw > 0);
    const core::CropRect initial = hasSaved
        ? cache->cropRect
        : core::centeredCrop(previewImage_.width(), previewImage_.height(),
                             core::cropAspect(pw, ph, defaultAlbum));
    // autoFitScreen=false: CropPreview's OWN screen-relative sizing is for the standalone
    // Crop tool, not this stage — this one's box comes from previewFitBox() alone (grows
    // with the window, resizeEvent), never a fraction of the screen, even transiently.
    cropStage_ = new CropPreview(previewImage_, pw, ph, initial, cropStageHost_,
                                 /*autoFitScreen=*/false);
    // The constructor already derived album_ from `initial`'s own shape — no second,
    // stale-checkbox opinion on top of it.
    {
      const QSignalBlocker block(cropAlbum_);
      cropAlbum_->setChecked(cropStage_->album());
      cropAlbum_->setText(cropStage_->album() ? tr("Album") : tr("Portrait"));
    }
    cropStage_->setFitBox(previewFitBox());
    cropStageHost_->layout()->addWidget(cropStage_);
    // A child born into an already-visible parent stays hidden, and a hidden item is left
    // out of the layout's hint — the window would shrink instead of making room.
    cropStage_->show();
    connect(cropStage_, &CropPreview::cropChanged, this, &OpenImageDialog::refreshCropDims);
    connect(cropStage_, &CropPreview::cropChanged, this, &OpenImageDialog::persistCropRect);
    // The scrub bar keeps the picture's width, which is now the STAGE's painted picture.
    if (previewIsVideo_) frameSlider_->setFixedWidth(cropStage_->paintedRect().width());
    refreshCropDims();     // the words first: the cloud is a photograph of them
    cropDimsDust(true);    // …the line takes its final place in the column, then slides in
    refitWindowHeight();   // …so the window is measured against the shape this lands on
  }

  // The tab's own copy of the drag, so a switch away and back restores it instead of a
  // fresh centeredCrop(). Keyed to the decode's own size — a later, differently-sized
  // picture on the same tab must never reuse a rect fitted to the one before it.
  void OpenImageDialog::persistCropRect() {
    const int tab = tabs_->currentIndex();
    if (!cropStage_ || (tab != TabFile && tab != TabUrl)) return;
    TabPreviewCache& cache = tabCache_[tab];
    cache.cropRect = cropStage_->cropRect();
    cache.cropRectValid = true;
    cache.previewImage = previewImage_;   // the size stamp the restore checks against
  }

  // The stage's own line, in the crop editor's words (CropDialogDrag.cpp).
  void OpenImageDialog::refreshCropDims() {
    if (!cropStage_) return;
    const core::CropRect r = cropStage_->cropRect();
    cropDims_->setText(QStringLiteral("%1 \u00d7 %2 px \u00b7 %3")
                           .arg(qRound(r.width)).arg(qRound(r.height))
                           .arg(cropStage_->album() ? tr("Album (landscape)") : tr("Portrait")));
  }
}
