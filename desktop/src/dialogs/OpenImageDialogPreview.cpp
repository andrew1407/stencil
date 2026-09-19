#include "../support/SearchCombo.hpp"
#include "openImageDialogParts.hpp"
#include "OpenImageDialog.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "../support/modalChrome.hpp"
#include "../support/modalReveal.hpp"
#include "../support/UnderlineTabBar.hpp"
#include "MediaLoader.hpp"
#include <algorithm>
#include <QAudioOutput>
#include <QGraphicsOpacityEffect>
#include <QPointer>
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
#include <QResizeEvent>
#include <QScreen>
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

  // The arriving tab page eases in (the strip's underline slides in step, see
  // UnderlineTabBar.hpp). The veil drops when the play ends, so nothing stays dimmed.
  void OpenImageDialog::fadeInCurrentPage() {
    if (!constructed_ || measuring_ || !isVisible() || support::motionReduced()) return;
    QWidget* page = tabs_->currentWidget();
    if (!page) return;
    auto* veil = new QGraphicsOpacityEffect(page);
    veil->setOpacity(0.0);
    page->setGraphicsEffect(veil);
    auto* fade = new QPropertyAnimation(veil, "opacity", veil);
    fade->setDuration(180);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    QPointer<QWidget> guard(page);
    QPointer<QGraphicsOpacityEffect> veilGuard(veil);
    connect(fade, &QPropertyAnimation::finished, page, [guard, veilGuard] {
      // however it ended, never left dimmed — but only OUR veil is removed
      if (guard && veilGuard && guard->graphicsEffect() == veilGuard)
        guard->setGraphicsEffect(nullptr);
    });
    fade->start(QAbstractAnimation::DeleteWhenStopped);
  }

  // Decode the current source (image, or the chosen video frame) into the preview.
  void OpenImageDialog::doPreview() {
    const QString src = source();
    if (src.isEmpty()) {
      setHint("Choose a file or paste a URL first.");
      return;
    }
    if (src != previewedSource_) scatterPreviewDust();   // out with the old picture
    setHint("Loading…");
    previewedSource_ = src;
    preview_->load(src, frame_->value());
  }

  // Everything DERIVED from a source: the fetch in flight, the decoded pixels, the video
  // it was read as, and the frame row that sized itself to it.
  void OpenImageDialog::dropDerived() {
    if (fetchTimer_) fetchTimer_->stop();
    teardownScrubPlayer();
    previewImage_ = QImage();
    frameImage_ = QImage();
    previewIsVideo_ = false;
    frameRow_->setVisible(false);
  }

  // The typed source has moved on: keep the picture, drop what was derived from it. The Crop CHOICE
  // stays - it belongs to what will be opened - and only its stage goes, with the pixels.
  void OpenImageDialog::stalePreview() {
    dropDerived();
    syncCropStage();
    if (previewLabel_->isVisible()) setHint("Preview of the previous URL — press Preview to load this one.");
  }

  void OpenImageDialog::resetPreviewState() {
    previewedSource_.clear();
    size_.previewCapH = 0;   // a new picture starts from the full box
    dropDerived();
    clearPreviewImage();
    setHint({});
    quickcropRow_->setVisible(false);
    // The STAGE goes with the picture it was cut from: left standing, a tab with no source
    // of its own still showed the other tab's cropped image.
    syncCropStage();
    frame_->setEnabled(true);
  }





  // The muted status line under the preview: shown only when it has something to
  // say, so an untouched dialog keeps no blank line for it.
  void OpenImageDialog::setHint(const QString& text) {
    previewHint_->setText(text);
    previewHint_->setVisible(!text.isEmpty());
  }

  // Drop the preview pixmap and its box, and re-fit (a visibility change stales sizeHint).
  void OpenImageDialog::clearPreviewImage() {
    previewLabel_->clear();
    previewLabel_->setVisible(false);
    frameSlider_->setVisible(false);
    fitTabsToCurrentPage();
  }

  void OpenImageDialog::showPreview(const QImage& img, const QString& hint) {
    previewImage_ = img;
    if (img.isNull()) {
      clearPreviewImage();
      return;
    }
    // source() names the tab's own file/URL — must not replay the arrival on a switch.
    const QString key = source();
    // A departure ALWAYS has an arrival: if the old picture blew away, this one flies in even when
    // the source has been seen before.
    const bool isNew = !key.isEmpty() && (motion_.arrivalDue || !motion_.animatedSources.contains(key));
    const QSize box = previewFitBox();
    const QPixmap shot = QPixmap::fromImage(img).scaled(
        box.width(), box.height(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    previewLabel_->setPixmap(shot);
    // While cropping, the STAGE is the picture — showing the label too would show it twice.
    previewLabel_->setVisible(!cropStage_);
    frameSlider_->setVisible(previewIsVideo_);   // a player's bar, exactly as wide as the frame
    if (previewIsVideo_)
      frameSlider_->setFixedWidth(cropStage_ ? cropStage_->paintedRect().width() : shot.width());
    setHint(hint);
    if (isNew) {
      motion_.animatedSources.insert(key);
      motion_.arrivalDue = false;
      gatherPreviewDust(shot);
    }
    // A scrubbed frame must reach the crop stage too, or it keeps cropping the old one.
    if (cropStage_) cropStage_->setOriginal(previewImage_);
    if (!motion_.restoring) cacheTabPreview(key, hint);   // a restore must not re-key the cache
    fitTabsToCurrentPage();
  }

  // The seeked frame IS the preview for a video — there is no second source to pick.
  void OpenImageDialog::updateVideoPreview() {
    showPreview(frameImage_, QString());   // no size line here; the browser has none
  }

}

