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
    if (!size_.bodyScroll || !size_.bodyContent) return sizeHint().height();
    // sizeHint() alone asks a height-for-width child (a wrapping caption, nested two
    // QHBoxLayouts deep under checkCaptionRow) for its height at whatever width IT would
    // prefer, not the one it actually gets here — a pessimistic, too-tall guess (measured
    // 12px, a whole row's worth of blank space at the bottom). heightForWidth(), asked at
    // the REAL width, is the number the layout actually settles at.
    QLayout* cl = size_.bodyContent->layout();
    // Before the dialog's REAL width is ever established (showEvent's own first-show
    // measurement runs before that), size_.bodyContent can still report a tiny transient
    // width — heightForWidth() at THAT width wraps every caption to several lines and
    // wildly overshoots, poisoning size_.floorH for the dialog's whole lifetime.
    const int contentH = cl && cl->hasHeightForWidth() && size_.bodyContent->width() >= PREVIEW_MAX_W
        ? cl->totalHeightForWidth(size_.bodyContent->width())
        : size_.bodyContent->sizeHint().height();
    return height() - size_.bodyScroll->height() + contentH;
  }

  // A QTabWidget's pane is as tall as its TALLEST page, so the one-row File/URL tabs
  // would carry the Blank tab's empty rows — give the page on show its own height.
  void OpenImageDialog::fitTabsToCurrentPage() {
    QWidget* page = tabs_->currentWidget();
    if (!page) return;
    int tallest = 0;   // QTabWidget::sizeHint() asks every page, not just the one on show
    for (int i = 0; i < tabs_->count(); i++)
      tallest = std::max(tallest, tabs_->widget(i)->sizeHint().height());
    const int chrome = tabs_->sizeHint().height() - tallest;   // tab bar + pane frame
    tabs_->setFixedHeight(chrome + page->sizeHint().height());
    // The WINDOW tracks the page too: a shorter tab or a cleared preview shrinks it back,
    // the first-show height being only a floor. Skipped before that pass (stale sizeHint).
    // `measured_` is the WINDOW's first-show pass; a popover never runs it (showEvent
    // returns early for a child), so gating on it alone left the compact shape never
    // refitting — a tab switched away from and back came back clipped.
    if ((measured_ || !isWindow()) && !measuring_) refitWindowHeight();
  }

  // Qt grows a top-level that no longer fits but never shrinks one back: the shrink is ours.
  void OpenImageDialog::refitWindowHeight() {
    QLayout* l = layout();
    if (!l) return;
    // As a POPOVER this is a child of a capped overlay, so it cannot resize itself as a
    // window — but it must still grow with its content, or a preview simply gets clipped
    // (the browser's .modal-popover grows to its own max-height). The overlay grows with it.
    if (!isWindow()) {
      growPopoverToContent();
      return;
    }
    // A QScrollArea holds its content through the viewport, NOT as an item of the dialog's
    // layout — so the walk below never reaches it, and the stale hints of the one subtree
    // the window is actually measured from are the ones left uncleared.
    if (size_.bodyContent) {
      invalidateTree(size_.bodyContent->layout());
      size_.bodyContent->updateGeometry();
      if (QLayout* cl = size_.bodyContent->layout()) cl->activate();
    }
    invalidateTree(l);
    l->activate();
    int h = std::max(wantedHeight(), size_.floorH);
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
    if (!overlay || !size_.bodyScroll || !size_.bodyContent) return;
    // The content's own hint, re-measured: stale, the popover never gives a picture's room
    // back when the tab that showed it is left.
    if (QLayout* cl = size_.bodyContent->layout()) { cl->invalidate(); cl->activate(); }
    size_.bodyContent->updateGeometry();
    const int cap = maximumHeight();
    int want = std::min(wantedHeight(), cap);
    if (QWidget* host = overlay->parentWidget())
      want = std::min(want, host->height() - overlay->y() - 8);
    if (want <= 0) return;
    animateHeightTo(want);   // eased, like the window's: a flat resize read as a jump
  }

  // The window EASES to its new height; snapping reads as a jump. One animation,
  // restarted, so a run of changes chases the latest height instead of queueing.
  // The flight starts from the height last SHOWN, not the current one: the rows that just
  // appeared raised the layout's minimum, and Qt has ALREADY grown the window to it by the
  // time we are asked — starting from height() played nothing at all (browser twin:
  // easeBoxHeight's pinned `shown`; support/easeWindowHeight.hpp carries the same note).
  void OpenImageDialog::animateHeightTo(int h) {
    const bool flying = size_.anim && size_.anim->state() == QAbstractAnimation::Running;
    const int start = flying || size_.shownH <= 0 ? height() : size_.shownH;
    if (start == h && height() == h) return;
    if (support::motionReduced() || !isVisible()) {
      setHeightNow(h);
      return;
    }
    // …and a resize is clamped by that minimum AND by the one already propagated to the
    // window, so both stand down for the flight. Mid-flight the rows are clipped by the
    // shorter window, which IS the reveal (the browser's .app-modal clips the same way —
    // via overflow: hidden, never a scrollbar). size_.bodyContent's LAYOUT, though, reflows to
    // its new natural height at once, a whole animation ahead of the window catching up —
    // for that stretch size_.bodyScroll genuinely holds more than its own (still small) viewport,
    // and AsNeeded's bar popping in, then out, is its own width-jump on top of the ease.
    if (QLayout* l = layout()) l->setSizeConstraint(QLayout::SetNoConstraint);
    setMinimumHeight(0);
    if (size_.bodyScroll) size_.bodyScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (height() != start) setHeightNow(start);
    if (!size_.anim) {
      size_.anim = new QVariantAnimation(this);
      size_.anim->setDuration(OI_RESIZE_MS);
      size_.anim->setEasingCurve(QEasingCurve::OutCubic);
      connect(size_.anim, &QVariantAnimation::valueChanged, this,
              [this](const QVariant& v) { setHeightNow(v.toInt()); });
      connect(size_.anim, &QVariantAnimation::finished, this, [this] {
        if (QLayout* l = layout()) l->setSizeConstraint(QLayout::SetDefaultConstraint);
        // Back on: a picture tall enough to run the dialog past the screen still needs it.
        if (size_.bodyScroll) size_.bodyScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
      });
    }
    size_.anim->stop();
    size_.anim->setStartValue(start);
    size_.anim->setEndValue(h);
    size_.anim->start();
  }

  // As a popover the panel is framed by an overlay of the same height: the two move as one,
  // or the picture is clipped by a frame that stayed behind.
  void OpenImageDialog::setHeightNow(int h) {
    if (!isWindow())
      if (QWidget* overlay = parentWidget()) overlay->resize(overlay->width(), h);
    resize(width(), h);
    size_.shownH = h;   // where the next flight starts from
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
    const int idx = tabs_->currentIndex();
    if (idx != TabFile && idx != TabUrl) return;
    TabPreviewCache& c = tabCache_[idx];
    c.valid = true;
    c.source = src;
    c.isVideo = previewIsVideo_;
    c.previewImage = previewImage_;
    c.frameImage = frameImage_;
    c.hint = hint;
    c.scrubFps = scrub_.fps;
    c.scrubDurationMs = scrub_.durationMs;
    c.resolvedUrl = preview_->resolvedUrl();
  }

  // Re-adopts a cached decode instantly — no network/disk fetch, no "Loading…". The
  // scrub player alone re-inits in the background (setupScrubPlayer), since a paused
  // static frame is what the user actually left the tab looking at.
  bool OpenImageDialog::restoreTabPreview(const TabPreviewCache& cache) {
    if (!cache.valid) return false;
    motion_.restoring = true;
    previewedSource_ = cache.source;
    previewIsVideo_ = cache.isVideo;
    frameImage_ = cache.frameImage;
    scrub_.fps = cache.scrubFps;
    scrub_.durationMs = cache.scrubDurationMs;
    quickcropRow_->setVisible(true);   // before the refit below, as showQuickcrop does
    if (previewIsVideo_) {
      frameRow_->setVisible(true);
      updateVideoPreview();
      setupScrubPlayer(cache.resolvedUrl);
    } else {
      // The OTHER tab may have left this visible (a video's own Frame field) — an image
      // tab has none, and restoring one must not keep it, or the Frame row from a video
      // read as belonging to the picture now on screen.
      frameRow_->setVisible(false);
      showPreview(cache.previewImage, cache.hint);
    }
    motion_.restoring = false;
    refreshOpenEnabled();
    return true;
  }

}  // namespace stencil::gui
