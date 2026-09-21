#include "OpenImageDialog.hpp"
#include "openImageDialogParts.hpp"
#include "MediaLoader.hpp"
#include <algorithm>
#include <QCheckBox>
#include <QLabel>
#include <QLayout>
#include "../support/motionPrefs.hpp"
#include <QEasingCurve>
#include <QScreen>
#include <QVariantAnimation>
#include <QScrollArea>
#include <QTabWidget>
#include <QWidget>

namespace stencil::gui {

  namespace {
    // A layout caches each child's hint in its own item; invalidate() clears the layout,
    // never those items — updateGeometry() is what drops them.
    void invalidateTree(QLayout* l) {
      if (!l) return;
      for (int i = 0; i < l->count(); ++i) {
        QLayoutItem* it = l->itemAt(i);
        if (QLayout* child = it->layout()) invalidateTree(child);
        else if (QWidget* w = it->widget()) { invalidateTree(w->layout()); w->updateGeometry(); }
      }
      l->invalidate();
    }
  }  // namespace

  // A QScrollArea's sizeHint is a fixed default saying nothing about what it holds, so the
  // window is sized from the CONTENT, swapped in for the viewport's height.
  int OpenImageDialog::wantedHeight() const {
    if (!size.bodyScroll || !size.bodyContent) return sizeHint().height();
    // sizeHint() asks a height-for-width child for its height at the width IT would prefer, not the
    // one it gets; heightForWidth() at the REAL width is the number the layout settles at.
    QLayout* cl = size.bodyContent->layout();
    // Before the dialog's REAL width is established, size.bodyContent can report a tiny transient
    // width - heightForWidth() there wraps every caption and poisons size.floorH for good.
    const int contentH = cl && cl->hasHeightForWidth() && size.bodyContent->width() >= PREVIEW_MAX_W
        ? cl->totalHeightForWidth(size.bodyContent->width())
        : size.bodyContent->sizeHint().height();
    return height() - size.bodyScroll->height() + contentH;
  }

  // A QTabWidget's pane is as tall as its TALLEST page, so the one-row File/URL tabs
  // would carry the Blank tab's empty rows — give the page on show its own height.
  void OpenImageDialog::fitTabsToCurrentPage() {
    QWidget* page = tabs->currentWidget();
    if (!page) return;
    int tallest = 0;   // QTabWidget::sizeHint() asks every page, not just the one on show
    for (int i = 0; i < tabs->count(); i++)
      tallest = std::max(tallest, tabs->widget(i)->sizeHint().height());
    const int chrome = tabs->sizeHint().height() - tallest;   // tab bar + pane frame
    tabs->setFixedHeight(chrome + page->sizeHint().height());
    // The WINDOW tracks the page too: a shorter tab or a cleared preview shrinks it back, the
    // first-show height being only a floor. A popover never runs that pass, so `measured` cannot gate it.
    if ((measured || !isWindow()) && !measuring) refitWindowHeight();
  }

  // Qt grows a top-level that no longer fits but never shrinks one back: the shrink is ours.
  void OpenImageDialog::refitWindowHeight() {
    QLayout* l = layout();
    if (!l) return;
    // As a POPOVER this is a child of a capped overlay, so it cannot resize itself as a window - but
    // it must still grow with its content (the browser's .modal-popover). The overlay grows with it.
    if (!isWindow()) {
      growPopoverToContent();
      return;
    }
    // A QScrollArea holds its content through the viewport, NOT as an item of the dialog's layout, so
    // the walk below never reaches the one subtree the window is actually measured from.
    if (size.bodyContent) {
      invalidateTree(size.bodyContent->layout());
      size.bodyContent->updateGeometry();
      if (QLayout* cl = size.bodyContent->layout()) cl->activate();
    }
    invalidateTree(l);
    l->activate();
    int h = std::max(wantedHeight(), size.floorH);
    // Never taller than the screen: past it the picture gives up the excess first
    // (shrinkPreviewToFit), and only a picture already at its floor leaves the body scrolling.
    if (QScreen* scr = screen()) {
      const int cap = int(scr->availableGeometry().height() * 0.92);
      if (h > cap) h = std::min(shrinkPreviewToFit(h - cap), cap);
    }
    animateHeightTo(h);   // it compares against the height last SHOWN, not the current one
    clampToScreen();
  }

  // The popover's height is the OVERLAY's: both are resized together, never past the cap
  // execMaybePopover set, and never past the bottom of the window they sit in.
  void OpenImageDialog::growPopoverToContent() {
    QWidget* overlay = parentWidget();
    if (!overlay || !size.bodyScroll || !size.bodyContent) return;
    // The content's own hint, re-measured: stale, the popover never gives a picture's room
    // back when the tab that showed it is left.
    if (QLayout* cl = size.bodyContent->layout()) { cl->invalidate(); cl->activate(); }
    size.bodyContent->updateGeometry();
    const int cap = maximumHeight();
    int want = std::min(wantedHeight(), cap);
    if (QWidget* host = overlay->parentWidget())
      want = std::min(want, host->height() - overlay->y() - 8);
    if (want <= 0) return;
    animateHeightTo(want);   // eased, like the window's: a flat resize read as a jump
  }

  // One animation, restarted, so a run of changes chases the latest height. The flight starts from
  // the height last SHOWN: Qt has already grown the window to the new minimum.
  void OpenImageDialog::animateHeightTo(int h) {
    const bool flying = size.anim && size.anim->state() == QAbstractAnimation::Running;
    const int start = flying || size.shownH <= 0 ? height() : size.shownH;
    if (start == h && height() == h) return;
    if (support::motionReduced() || !isVisible()) {
      setHeightNow(h);
      return;
    }
    // A resize is clamped by the layout minimum and the one already propagated to the window, so both
    // stand down for the flight. Scrollbars stay off: bodyContent reflows an animation ahead.
    if (QLayout* l = layout()) l->setSizeConstraint(QLayout::SetNoConstraint);
    setMinimumHeight(0);
    if (size.bodyScroll) size.bodyScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (height() != start) setHeightNow(start);
    if (!size.anim) {
      size.anim = new QVariantAnimation(this);
      size.anim->setDuration(OI_RESIZE_MS);
      size.anim->setEasingCurve(QEasingCurve::OutCubic);
      connect(size.anim, &QVariantAnimation::valueChanged, this,
              [this](const QVariant& v) { setHeightNow(v.toInt()); });
      connect(size.anim, &QVariantAnimation::finished, this, [this] {
        if (QLayout* l = layout()) l->setSizeConstraint(QLayout::SetDefaultConstraint);
        // Back on: a picture tall enough to run the dialog past the screen still needs it.
        if (size.bodyScroll) size.bodyScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
      });
    }
    size.anim->stop();
    size.anim->setStartValue(start);
    size.anim->setEndValue(h);
    size.anim->start();
  }

  // As a popover the panel is framed by an overlay of the same height: the two move as one,
  // or the picture is clipped by a frame that stayed behind.
  void OpenImageDialog::setHeightNow(int h) {
    if (!isWindow())
      if (QWidget* overlay = parentWidget()) overlay->resize(overlay->width(), h);
    resize(width(), h);
    size.shownH = h;   // where the next flight starts from
    clampToScreen();
  }

  // A resize can hang the dialog off the monitor, or leave dead space above it — pin it
  // back inside the screen, moving up first and never past its top edge.
  void OpenImageDialog::clampToScreen() {
    QScreen* scr = screen();
    if (!scr || !isWindow()) return;   // a child's x()/y() are its parent's, not the screen's
    const QRect avail = scr->availableGeometry();
    int y = this->y();
    const int bottom = y + height();
    if (bottom > avail.bottom()) y -= bottom - avail.bottom();
    y = std::max(y, avail.top());
    if (y != this->y()) move(this->x(), y);
  }

  // What the CURRENT tab just decoded, so applyMode() can bring it back on a return
  // visit without a second fetch. Blank has no tab slot — nothing to cache.
  void OpenImageDialog::cacheTabPreview(const QString& src, const QString& hint) {
    const int idx = tabs->currentIndex();
    if (idx != TabFile && idx != TabUrl) return;
    TabPreviewCache& c = tabCache[idx];
    c.valid = true;
    c.source = src;
    c.isVideo = previewIsVideo;
    c.previewImage = previewImage;
    c.frameImage = frameImage;
    c.hint = hint;
    c.scrubFps = scrub.fps;
    c.scrubDurationMs = scrub.durationMs;
    c.resolvedUrl = preview->resolvedUrl();
  }

  // Re-adopts a cached decode instantly - no fetch, no "Loading...". The scrub player alone re-inits
  // in the background, since a paused static frame is what the user left the tab looking at.
  bool OpenImageDialog::restoreTabPreview(const TabPreviewCache& cache) {
    if (!cache.valid) return false;
    motion.restoring = true;
    previewedSource = cache.source;
    previewIsVideo = cache.isVideo;
    frameImage = cache.frameImage;
    scrub.fps = cache.scrubFps;
    scrub.durationMs = cache.scrubDurationMs;
    quickcropRow->setVisible(true);   // before the refit below, as showQuickcrop does
    if (previewIsVideo) {
      frameRow->setVisible(true);
      updateVideoPreview();
      setupScrubPlayer(cache.resolvedUrl);
    } else {
      // The OTHER tab may have left this visible (a video's own Frame field) - an image tab has none,
      // and restoring one must not keep it.
      frameRow->setVisible(false);
      showPreview(cache.previewImage, cache.hint);
    }
    motion.restoring = false;
    refreshOpenEnabled();
    return true;
  }

}  // namespace stencil::gui
