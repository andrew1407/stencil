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
    previewImage = img;
    if (img.isNull()) {
      previewLabel->clear();
      previewLabel->setVisible(false);
      loadBtn->setEnabled(false);
      return;
    }
    previewLabel->setPixmap(QPixmap::fromImage(img).scaled(
        PREVIEW_MAX_W, PREVIEW_MAX_H, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    previewLabel->setVisible(true);
    previewHint->setText(hint);
    loadBtn->setEnabled(true);
    loadBtn->setToolTip("Load the previewed image into the editor");
  }

  // For a video, show either the embedded preview image (when chosen and available)
  // or the seeked frame. The frame spinbox is irrelevant while the preview is used.
  void LinksDialog::updateVideoPreview() {
    const bool usePrev = usePreview->isChecked() && !thumbImage.isNull();
    frame->setEnabled(!usePrev);          // frame controls are moot while using the
    frameSlider->setEnabled(!usePrev);    // embedded preview image
    const QImage& shown = usePrev ? thumbImage : frameImage;
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
    cropPage->setChecked(true);
    cropAlbum->setChecked((w >= h) && (w > 0));
    const int idx = cropPageSize->findData(pageSeed);
    cropPageSize->setCurrentIndex(idx < 0 ? cropPageSize->findData("A3") : idx);
    syncQuickcropEnabled();
    quickcropRow->setVisible(true);
  }

  // Album / page size are only meaningful while cropping to page.
  void LinksDialog::syncQuickcropEnabled() {
    const bool on = cropPage->isChecked();
    cropAlbum->setEnabled(on);
    cropPageSize->setEnabled(on);
  }

  void LinksDialog::requestLoad() {
    if (previewImage.isNull()) return;  // Load is gated on a successful preview
    loadRequested = true;
    accept();
  }

  QString LinksDialog::source() const { return sourceEdit->text().trimmed(); }
  QString LinksDialog::resource() const { return resourceEdit->text().trimmed(); }
  QString LinksDialog::urlSource() const { return urlEdit->text().trimmed(); }
  QString LinksDialog::urlResource() const { return urlResourceEdit->text().trimmed(); }
  int LinksDialog::urlFrame() const { return frame->value(); }

  bool LinksDialog::cropToPage() const { return cropPage->isChecked(); }
  bool LinksDialog::getCropAlbum() const { return cropAlbum->isChecked(); }
  QString LinksDialog::getCropPageSize() const {
    return cropPageSize->currentData().toString();
  }
}

