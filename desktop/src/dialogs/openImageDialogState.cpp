#include "../support/searchCombo.hpp"
#include "openImageDialogParts.hpp"
#include "openImageDialog.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "../support/modalChrome.hpp"
#include "../support/modalReveal.hpp"
#include "../support/underlineTabBar.hpp"
#include "mediaLoader.hpp"
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

  // One dialog height across the source tabs, sized for the tallest (browser parity).
  // Measured on FIRST SHOW: updateGeometry() is a no-op on a hidden widget, so pre-show
  // every tab reports the same stale sizeHint. No explicit minimum is pinned — that would
  // override the layout's own and let the content squeeze.
  void OpenImageDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    if (measured_) return;
    measured_ = true;
    measuring_ = true;   // silent switches — no cross-tab fade for a measurement
    const int keep = tabs_->currentIndex();
    int tallest = 0;
    for (int i = 0; i < tabs_->count(); ++i) {
      tabs_->setCurrentIndex(i);
      if (QLayout* l = layout()) l->activate();
      tallest = std::max(tallest, sizeHint().height());
    }
    tabs_->setCurrentIndex(keep);
    measuring_ = false;
    if (QLayout* l = layout()) l->activate();
    if (tallest > height()) resize(width(), tallest);
  }

  // The arriving tab page eases in (the strip's underline slides in step — see
  // underlineTabBar.hpp), so switching Local file / URL link / Blank is not a hard
  // cut. The veil is dropped when the play ends, so nothing is ever left dimmed.
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

  bool OpenImageDialog::eventFilter(QObject* obj, QEvent* event) {
    if (obj == url_ && event->type() == QEvent::KeyPress) {
      const auto* k = static_cast<QKeyEvent*>(event);
      if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
        doPreview();  // Enter previews the URL rather than accepting the dialog
        return true;
      }
    }
    return QDialog::eventFilter(obj, event);
  }

  void OpenImageDialog::browse() {
    const QString p = QFileDialog::getOpenFileName(
        this, "Open image or video", QString(),
        "Images and video (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.mp4 *.mov *.webm "
        "*.mkv *.avi *.m4v *.mpg *.mpeg);;All files (*)");
    if (p.isEmpty()) return;
    path_->setText(p);
    resetPreviewState();
    refreshButtons();
    doPreview();  // a local file previews immediately
  }

  void OpenImageDialog::pickCustomColor() {
    // Anchored on the custom-fill swatch that was clicked.
    const QColor c =
        support::pickColorAnimated(customColor_, this, "Fill color", customSwatch_);
    if (!c.isValid()) return;
    customColor_ = c;
    setColorSwatch(customSwatch_, customColor_);
  }

  // A QTabWidget's pane is as tall as its TALLEST page, so the one-row File/URL
  // tabs would carry the Blank tab's empty rows under them — give only the page on
  // show its height (the browser's tab panel is content-height too).
  void OpenImageDialog::fitTabsToCurrentPage() {
    QWidget* page = tabs_->currentWidget();
    if (!page) return;
    int tallest = 0;   // QTabWidget::sizeHint() asks every page, not just the one on show
    for (int i = 0; i < tabs_->count(); i++)
      tallest = std::max(tallest, tabs_->widget(i)->sizeHint().height());
    const int chrome = tabs_->sizeHint().height() - tallest;   // tab bar + pane frame
    tabs_->setFixedHeight(chrome + page->sizeHint().height());
  }

  void OpenImageDialog::applyMode() {
    const bool blank = tabs_->currentIndex() == TabBlank;
    fitTabsToCurrentPage();
    incogRow_->setVisible(!blank);  // incognito has no effect on a blank
    here_->setVisible(!blank);
    newWindow_->setVisible(!blank);
    if (replace_) replace_->setVisible(!blank && tabs_->currentIndex() == TabFile);
    replaceRow_->setVisible(!blank && canReplace_ && tabs_->currentIndex() == TabFile);
    createBlank_->setVisible(blank);
    refreshTargetRow();
    if (blank) clearPreviewImage();   // the blank tab has no source to preview
    // Switching source tabs invalidates any preview built for the other tab.
    resetPreviewState();
    if (!blank) refreshButtons();
    else frameRow_->setVisible(false);
    fadeInCurrentPage();
  }

  // Action buttons stay disabled until a source (file or URL) is chosen; Replace is
  // only for a local-image file (a URL/video opens as a new project).
  void OpenImageDialog::refreshButtons() {
    const bool has = !source().isEmpty();
    const bool video = looksLikeVideo(source());
    previewBtn_->setEnabled(has);
    if (replace_) {
      const bool canReplaceNow = has && !isUrl() && !video;
      replace_->setEnabled(canReplaceNow);
      replace_->setToolTip(
          !has ? "Choose an image file first"
               : (isUrl() || video
                      ? "A URL or video opens as a new project (no in-place replace)"
                      : "Swap this project's image in place (same project)"));
    }
    refreshOpenEnabled();
  }

  // Gate the open buttons. A source is enough to open (an un-previewed source falls
  // back to the async resolve in MainWindow); with a preview taken, opening adopts the
  // exact previewed pixels. Tooltips explain the state.
  void OpenImageDialog::refreshOpenEnabled() {
    const bool has = !source().isEmpty();
    here_->setEnabled(has);
    newWindow_->setEnabled(has);
    const QString reason = "Choose an image/video file or paste a URL first";
    here_->setToolTip(has ? "Open the chosen source in this editor (makes a new project)"
                          : reason);
    newWindow_->setToolTip(has ? "Open the chosen source in a new window (this editor stays)"
                               : reason);
  }

  // Reveal the quick-crop row for a previewed image/frame, defaulting the album toggle
  // to the media's orientation (wider-than-tall ⇒ album) and the page size to the app's
  // current page (mirrors LinksDialog's showQuickcrop). Crop itself stays OFF.
  void OpenImageDialog::showQuickcrop(int w, int h) {
    cropAlbum_->setChecked((w >= h) && (w > 0));
    const int idx = cropPageSize_->findData(pageSeed_);
    cropPageSize_->setCurrentIndex(idx < 0 ? cropPageSize_->findData("A3") : idx);
    syncQuickcropEnabled();
    quickcropRow_->setVisible(true);
    refreshOpenEnabled();
  }

  // Album / page size are only meaningful while cropping to page — shown only then, the
  // toggle wearing the orientation it holds (browser #open-image-crop-orientation).
  void OpenImageDialog::syncQuickcropEnabled() {
    const bool on = cropPage_->isChecked();
    cropAlbum_->setText(cropAlbum_->isChecked() ? tr("Album") : tr("Portrait"));
    cropAlbum_->setVisible(on);
    cropPageSize_->setVisible(on);
  }
}

