#include "../../support/menu/SearchCombo.hpp"
#include "openImageDialogParts.hpp"
#include "OpenImageDialog.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "../../support/modal/modalChrome.hpp"
#include "../../support/modal/modalReveal.hpp"
#include "../../support/control/UnderlineTabBar.hpp"
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
    if (measured || !isWindow()) return;
    measured = true;
    measuring = true;   // silent switches — no cross-tab fade for a measurement
    const int keep = tabs->currentIndex();
    int tallest = 0;
    for (int i = 0; i < tabs->count(); ++i) {
      tabs->setCurrentIndex(i);
      if (QLayout* l = layout()) l->activate();
      tallest = std::max(tallest, wantedHeight());
    }
    tabs->setCurrentIndex(keep);
    measuring = false;
    if (QLayout* l = layout()) { l->invalidate(); l->activate(); }
    // The floor is the browser's OI_MIN_H, not what the emptiest tab measured (barely 280px) —
    // never dropped under afterwards. Not height(): pre-show it is a stale guess a start on the
    // Blank tab leaves near the screen's height.
    const int want = std::max(tallest, OI_MIN_H);
    // An ease armed by the last pre-show refit would land after this and overwrite it.
    if (size.anim) size.anim->stop();
    if (want != height()) resize(width(), want);
    size.floorH = size.shownH = want;
    // …re-centred for that height: Qt centred it at its pre-measurement size, so growing
    // this much left it sitting low.
    if (QWidget* p = parentWidget())
      move(p->geometry().center().x() - width() / 2, p->geometry().center().y() - want / 2);
    clampToScreen();
  }

  bool OpenImageDialog::eventFilter(QObject* obj, QEvent* event) {
    if (obj == url && event->type() == QEvent::KeyPress) {
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
    if (p != previewedSource) scatterPreviewDust();
    path->setText(p);
    resetPreviewState();
    refreshButtons();
    doPreview();  // a local file previews immediately
  }

  void OpenImageDialog::pickCustomColor() {
    // Anchored on the custom-fill swatch that was clicked.
    const QColor c =
        support::pickColorAnimated(blank.color, this, "Fill color", blank.swatch);
    if (!c.isValid()) return;
    blank.color = c;
    setColorSwatch(blank.swatch, blank.color, SWATCH_SIZE, /*withHex=*/true);
  }

  void OpenImageDialog::applyMode() {
    cancelPreviewDust();   // the outgoing tab's flourish does not play over the arriving one
    size.previewCapH = 0;      // the arriving tab's picture is fitted afresh
    // The OUTGOING tab's stage goes NOW: left standing, showPreview() would setOriginal() on it for
    // the arriving picture and persist a rect from the OLD stage's stale aspect.
    if (cropStage) {
      cropStage->hide();
      cropStage->deleteLater();
      cropStage = nullptr;
    }
    const bool blank = tabs->currentIndex() == TabBlank;
    incogRow->setVisible(!blank);  // incognito has no effect on a blank
    act.here->setVisible(!blank);
    act.newWindow->setVisible(!blank);
    if (act.replace) act.replace->setVisible(!blank && tabs->currentIndex() == TabFile);
    act.replaceRow->setVisible(!blank && canReplace && tabs->currentIndex() == TabFile);
    act.createBlank->setVisible(blank);
    refreshTargetRow();
    // File and URL each keep their own chosen source AND their own crop choice: ticking
    // Crop on the URL tab says nothing about the local file (browser twin: tabCrop).
    const int tab = tabs->currentIndex();
    if (tab == TabFile || tab == TabUrl) {
      const QSignalBlocker block(cropPage);
      cropPage->setChecked(tabCrop[tab]);
    }
    // Quiet from here: resetPreviewState()/stalePreview() rebuild the crop stage themselves, and
    // ungated that pass played the outgoing rows' closing flourish on every ordinary switch.
    motion.quietCrop = true;
    const QString src = source();
    if (blank) {
      clearPreviewImage();
      resetPreviewState();
    } else if (src.isEmpty()) {
      resetPreviewState();
    } else if (src != previewedSource) {
      TabPreviewCache& cache = tabCache[tab];
      if (cache.valid && cache.source == src) {
        restoreTabPreview(cache);
      } else if (tab == TabUrl && cache.valid && restoreTabPreview(cache)) {
        // The typed URL moved on while this tab was away: coming back still shows what it was showing.
        // Preview is what replaces it, exactly as stalePreview leaves it.
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
      motion.quietCrop = false;
      refreshButtons();
    } else {
      motion.quietCrop = false;
      frameRow->setVisible(false);
    }
    fitTabsToCurrentPage();  // after the preview settles — not the tab it's leaving
    fadeInCurrentPage();
    // The tab's own control takes the keyboard (browser setTab) — Local its Choose button,
    // its path field being read-only. Never mid-measurement: that walks every tab.
    if (measuring) return;
    if (tabs->currentIndex() == TabUrl) {
      url->setFocus(Qt::TabFocusReason);
    } else if (tabs->currentIndex() == TabFile) {
      if (QWidget* page = tabs->currentWidget())
        if (auto* choose = page->findChild<QPushButton*>()) choose->setFocus(Qt::TabFocusReason);
    }
  }

  // Action buttons stay disabled until a source (file or URL) is chosen; Replace is
  // only for a local-image file (a URL/video opens as a new project).
  void OpenImageDialog::refreshButtons() {
    const bool has = !source().isEmpty();
    const bool video = looksLikeVideo(source());
    previewBtn->setEnabled(has);
    if (act.replace) {
      const bool canReplaceNow = has && !isUrl() && !video;
      act.replace->setEnabled(canReplaceNow);
      act.replace->setToolTip(
          !has ? "Choose an image file first"
               : (isUrl() || video
                      ? "A URL or video opens as a new project (no in-place replace)"
                      : "Swap this project's image in place (same project)"));
    }
    refreshOpenEnabled();
  }

  // Gate the open buttons. A source alone is enough to open (MainWindow's async resolve backs it);
  // with a preview taken, opening adopts the exact previewed pixels.
  void OpenImageDialog::refreshOpenEnabled() {
    const bool has = !source().isEmpty();
    act.here->setEnabled(has);
    act.newWindow->setEnabled(has);
    const QString reason = "Choose an image/video file or paste a URL first";
    act.here->setToolTip(has ? "Open the chosen source in this editor (makes a new project)"
                          : reason);
    act.newWindow->setToolTip(has ? "Open the chosen source in a new window (this editor stays)"
                               : reason);
  }

}

