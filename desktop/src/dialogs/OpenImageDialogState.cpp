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
#include <QVariantAnimation>
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

  // One dialog height across the tabs, sized for the tallest, measured on FIRST SHOW —
  // pre-show every tab reports the same stale sizeHint.
  void OpenImageDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    // A popover owns neither its size nor its place: the overlay caps it, its body scrolls.
    if (measured_ || !isWindow()) return;
    measured_ = true;
    measuring_ = true;   // silent switches — no cross-tab fade for a measurement
    const int keep = tabs_->currentIndex();
    int tallest = 0;
    for (int i = 0; i < tabs_->count(); ++i) {
      tabs_->setCurrentIndex(i);
      if (QLayout* l = layout()) l->activate();
      tallest = std::max(tallest, wantedHeight());
    }
    tabs_->setCurrentIndex(keep);
    measuring_ = false;
    if (QLayout* l = layout()) { l->invalidate(); l->activate(); }
    // The floor is the browser's OI_MIN_H, not what the emptiest tab measured (barely
    // 280px) — never dropped under afterwards either.
    const int want = std::max({tallest, OI_MIN_H, height()});
    // An ease armed by the last pre-show refit would land after this and overwrite it.
    if (heightAnim_) heightAnim_->stop();
    if (want != height()) resize(width(), want);
    floorH_ = shownH_ = want;
    // …re-centred for that height: Qt centred it at its pre-measurement size, so growing
    // this much left it sitting low.
    if (QWidget* p = parentWidget())
      move(p->geometry().center().x() - width() / 2, p->geometry().center().y() - want / 2);
    clampToScreen();
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
    if (p != previewedSource_) scatterPreviewDust();
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
    setColorSwatch(customSwatch_, customColor_, SWATCH_SIZE, /*withHex=*/true);
  }

  void OpenImageDialog::applyMode() {
    cancelPreviewDust();   // the outgoing tab's flourish does not play over the arriving one
    previewCapH_ = 0;      // the arriving tab's picture is fitted afresh
    // The OUTGOING tab's stage goes NOW: left standing, showPreview() below would call
    // setOriginal() on it for the ARRIVING (differently shaped) picture, recomputing —
    // and persisting — a rect from the OLD stage's stale aspect. syncQuickcropEnabled()
    // rebuilds a correct one once the new picture has actually landed.
    if (cropStage_) {
      cropStage_->hide();
      cropStage_->deleteLater();
      cropStage_ = nullptr;
    }
    const bool blank = tabs_->currentIndex() == TabBlank;
    incogRow_->setVisible(!blank);  // incognito has no effect on a blank
    here_->setVisible(!blank);
    newWindow_->setVisible(!blank);
    if (replace_) replace_->setVisible(!blank && tabs_->currentIndex() == TabFile);
    replaceRow_->setVisible(!blank && canReplace_ && tabs_->currentIndex() == TabFile);
    createBlank_->setVisible(blank);
    refreshTargetRow();
    // File and URL each keep their own chosen source AND their own crop choice: ticking
    // Crop on the URL tab says nothing about the local file (browser twin: tabCrop).
    const int tab = tabs_->currentIndex();
    if (tab == TabFile || tab == TabUrl) {
      const QSignalBlocker block(cropPage_);
      cropPage_->setChecked(tabCrop_[tab]);
    }
    // Quiet from here: resetPreviewState()/stalePreview() below rebuild the crop stage on
    // their own, ahead of schedule — ungated, that pass played the outgoing rows' closing
    // flourish on every ordinary switch (a tab switch is not a toggle).
    quietCrop_ = true;
    const QString src = source();
    if (blank) {
      clearPreviewImage();
      resetPreviewState();
    } else if (src.isEmpty()) {
      resetPreviewState();
    } else if (src != previewedSource_) {
      TabPreviewCache& cache = tabCache_[tab];
      if (cache.valid && cache.source == src) {
        restoreTabPreview(cache);
      } else if (tab == TabUrl && cache.valid && restoreTabPreview(cache)) {
        // The typed URL moved on while this tab was away: coming back still shows what it
        // was showing — losing the picture to a half-typed address is not a tab switch's
        // doing. Preview is what replaces it, exactly as stalePreview leaves it.
        stalePreview();
      } else {
        resetPreviewState();
        doPreview();
      }
    }
    // The Crop choice came back signals-blocked, so its stage did not: rebuild it here,
    // still quietly.
    if (!blank) {
      syncQuickcropEnabled();
      quietCrop_ = false;
      refreshButtons();
    } else {
      quietCrop_ = false;
      frameRow_->setVisible(false);
    }
    fitTabsToCurrentPage();  // after the preview settles — not the tab it's leaving
    fadeInCurrentPage();
    // The tab's own control takes the keyboard (browser setTab) — Local its Choose button,
    // its path field being read-only. Never mid-measurement: that walks every tab.
    if (measuring_) return;
    if (tabs_->currentIndex() == TabUrl) {
      url_->setFocus(Qt::TabFocusReason);
    } else if (tabs_->currentIndex() == TabFile) {
      if (QWidget* page = tabs_->currentWidget())
        if (auto* choose = page->findChild<QPushButton*>()) choose->setFocus(Qt::TabFocusReason);
    }
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

}

