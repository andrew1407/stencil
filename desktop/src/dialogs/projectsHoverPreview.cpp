#include "projectsDialog.hpp"
#include <QListWidget>

#include "projectRowDelegate.hpp"
#include "projectsRowChrome.hpp"
#include "projectsDialog.hpp"
#include "../support/flowLayout.hpp"
#include "../support/disintegrateOverlay.hpp"
#include "appTooltip.hpp"

#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QSize>
#include <QVariant>
#include <QVariantAnimation>

// The floating hover-magnify preview over a row's thumbnail.

namespace stencil::gui {

  bool ProjectsDialog::dustHoverPreview(QListWidgetItem* it, bool gather) {
    if (!hoverPreview_ || !list_ || !it) return false;
    // The SAME thumbnail rect the hover hit test uses — the flight starts and
    // ends on the picture, never on the checkbox beside it.
    const auto* del = static_cast<ProjectRowDelegate*>(list_->itemDelegate());
    QRect iconCell = del ? del->iconRectFor(list_->row(it)) : QRect();
    if (!iconCell.isValid()) iconCell = list_->visualItemRect(it);
    const QPoint origin = list_->viewport()->mapToGlobal(iconCell.center());
    // alwaysEscape: the preview is its own ToolTip window ABOVE the dialog — a child
    // layer's motes played underneath it; paintNow on a close, so the
    // preview never blinks out before any mote shows.
    return gui::flyTipDust(hoverPreview_, window(), origin, gather,
                           gather ? gui::TIP_DUST_IN_MS : gui::TIP_DUST_OUT_MS,
                           /*escapeHost=*/true, /*paintNow=*/!gather,
                           /*alwaysEscape=*/true) != nullptr;
  }

  QVariantAnimation* ProjectsDialog::hoverFade() {
    if (!hoverFade_) {
      hoverFade_ = new QVariantAnimation(this);
      connect(hoverFade_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        if (hoverPreview_) hoverPreview_->setWindowOpacity(v.toDouble());
      });
      connect(hoverFade_, &QVariantAnimation::finished, this, [this] {
        if (!hoverClosing_) return;
        hoverClosing_ = false;
        if (hoverPreview_) hoverPreview_->hide();
      });
    }
    return hoverFade_;
  }

  void ProjectsDialog::placeHoverPreview(const QPoint& globalCursor) {
    if (!hoverPreview_) return;
    // Down-right of the cursor, flipped/clamped to stay on-screen (browser positionZoom).
    QScreen* s = QGuiApplication::screenAt(globalCursor);
    const QRect scr = (s ? s : QGuiApplication::primaryScreen())->availableGeometry();
    const QSize sz = hoverPreview_->size();
    QPoint gp = globalCursor + QPoint(18, 18);
    if (gp.x() + sz.width() > scr.right()) gp.setX(globalCursor.x() - 18 - sz.width());
    if (gp.y() + sz.height() > scr.bottom()) gp.setY(scr.bottom() - sz.height());
    if (gp.x() < scr.left()) gp.setX(scr.left());
    if (gp.y() < scr.top()) gp.setY(scr.top());
    hoverPreview_->move(gp);
  }

  void ProjectsDialog::revealHoverPreview(QListWidgetItem* it) {
    if (!hoverPreview_) return;
    hoverClosing_ = false;   // BEFORE stop(): stop() emits finished, which would hide()
    auto* fade = hoverFade();
    fade->stop();
    fade->setKeyValues({});
    if (support::motionReduced()) {  // the end state, at once
      hoverPreview_->setWindowOpacity(1.0);
      return;
    }
    if (dustHoverPreview(it, /*gather=*/true)) {
      // The preview waits behind its own motes and fades up as the last of them land
      // (the shared surfaceForm ramp).
      hoverPreview_->setWindowOpacity(0.0);
      gui::holdFadeKeys(fade, gui::TIP_DUST_IN_MS);
    } else {
      fade->setDuration(HOVER_FADE_MS);
      fade->setStartValue(hoverPreview_->windowOpacity());
      fade->setEndValue(1.0);
    }
    fade->start();
  }

  void ProjectsDialog::hideHoverPreview() {
    if (!hoverPreview_ || !hoverPreview_->isVisible() || hoverClosing_) return;
    // Photographed and dusted while it is still the box on screen — the cloud is what
    // it leaves behind, so the hand-over is one beat, not a cut: the label itself
    // fades out BEHIND the leaving motes instead of blinking off under them.
    const bool dusted = !support::motionReduced() && dustHoverPreview(hoverItem_, /*gather=*/false);
    hoverItem_ = nullptr;
    if (!dusted) {
      if (hoverFade_) { hoverClosing_ = false; hoverFade_->stop(); }
      hoverPreview_->hide();
      return;
    }
    auto* fade = hoverFade();
    fade->stop();
    fade->setKeyValues({});
    hoverClosing_ = true;
    fade->setDuration(gui::DUST_HAND_OVER_MS);
    fade->setStartValue(hoverPreview_->windowOpacity());
    fade->setEndValue(0.0);
    fade->start();
  }

  bool ProjectsDialog::pointerOverPreviewedIcon() const {
    if (!list_ || !hoverItem_) return false;
    const QPoint vpos = list_->viewport()->mapFromGlobal(QCursor::pos());
    if (!list_->viewport()->rect().contains(vpos)) return false;
    if (list_->itemAt(vpos) != hoverItem_) return false;
    const auto* del = static_cast<ProjectRowDelegate*>(list_->itemDelegate());
    const QRect dec = del ? del->iconRectFor(list_->row(hoverItem_)) : QRect();
    return dec.isValid() && dec.adjusted(-2, -2, 2, 2).contains(vpos);
  }

}  // namespace stencil::gui
