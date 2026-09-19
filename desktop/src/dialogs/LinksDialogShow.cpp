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

  // Render `img` into the preview area, set the status hint, and arm Load.
  void LinksDialog::showPreview(const QImage& img, const QString& hint) {
    previewImage_ = img;
    if (img.isNull()) {
      previewLabel_->clear();
      previewLabel_->setVisible(false);
      loadBtn_->setEnabled(false);
      return;
    }
    previewLabel_->setPixmap(QPixmap::fromImage(img).scaled(
        PREVIEW_MAX_W, PREVIEW_MAX_H, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    previewLabel_->setVisible(true);
    previewHint_->setText(hint);
    loadBtn_->setEnabled(true);
    loadBtn_->setToolTip("Load the previewed image into the editor");
  }

  // For a video, show either the embedded preview image (when chosen and available)
  // or the seeked frame. The frame spinbox is irrelevant while the preview is used.
  void LinksDialog::updateVideoPreview() {
    const bool usePrev = usePreview_->isChecked() && !thumbImage_.isNull();
    frame_->setEnabled(!usePrev);          // frame controls are moot while using the
    frameSlider_->setEnabled(!usePrev);    // embedded preview image
    const QImage& shown = usePrev ? thumbImage_ : frameImage_;
    showPreview(shown,
                usePrev
                    ? QString("Using the video's embedded preview image (%1×%2).")
                          .arg(shown.width()).arg(shown.height())
                    : QString("Video %1×%2 — drag the slider or type a frame, then Load.")
                          .arg(shown.width()).arg(shown.height()));
  }

  // Reveal the quick-crop row, defaulting the album toggle to the media's orientation
  // (wider-than-tall => album) and the page to the app's current one (browser showQuickcrop).
  void LinksDialog::showQuickcrop(int w, int h) {
    cropPage_->setChecked(true);
    cropAlbum_->setChecked((w >= h) && (w > 0));
    const int idx = cropPageSize_->findData(pageSeed_);
    cropPageSize_->setCurrentIndex(idx < 0 ? cropPageSize_->findData("A3") : idx);
    syncQuickcropEnabled();
    quickcropRow_->setVisible(true);
  }

  // Album / page size are only meaningful while cropping to page.
  void LinksDialog::syncQuickcropEnabled() {
    const bool on = cropPage_->isChecked();
    cropAlbum_->setEnabled(on);
    cropPageSize_->setEnabled(on);
  }

  void LinksDialog::requestLoad() {
    if (previewImage_.isNull()) return;  // Load is gated on a successful preview
    loadRequested_ = true;
    accept();
  }

  QString LinksDialog::source() const { return sourceEdit_->text().trimmed(); }
  QString LinksDialog::resource() const { return resourceEdit_->text().trimmed(); }
  QString LinksDialog::urlSource() const { return urlEdit_->text().trimmed(); }
  QString LinksDialog::urlResource() const { return urlResourceEdit_->text().trimmed(); }
  int LinksDialog::urlFrame() const { return frame_->value(); }

  bool LinksDialog::cropToPage() const { return cropPage_->isChecked(); }
  bool LinksDialog::cropAlbum() const { return cropAlbum_->isChecked(); }
  QString LinksDialog::cropPageSize() const {
    return cropPageSize_->currentData().toString();
  }
}

