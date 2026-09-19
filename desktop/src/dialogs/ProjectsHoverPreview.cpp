#include "ProjectsDialog.hpp"
#include <QListWidget>

#include "ProjectRowDelegate.hpp"
#include "projectsRowChrome.hpp"
#include "ProjectsDialog.hpp"
#include "../support/FlowLayout.hpp"
#include "../support/DisintegrateOverlay.hpp"
#include "AppTooltip.hpp"

#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QSize>
#include <QVariant>
#include <QVariantAnimation>

// The floating hover-magnify preview over a row's thumbnail.

namespace stencil::gui {

  bool ProjectsDialog::dustHoverPreview(QListWidgetItem* it, bool gather) {
    if (!hover_.hoverPreview || !list_ || !it) return false;
    // The SAME thumbnail rect the hover hit test uses — the flight starts and
    // ends on the picture, never on the checkbox beside it.
    const auto* del = static_cast<ProjectRowDelegate*>(list_->itemDelegate());
    QRect iconCell = del ? del->iconRectFor(list_->row(it)) : QRect();
    if (!iconCell.isValid()) iconCell = list_->visualItemRect(it);
    const QPoint origin = list_->viewport()->mapToGlobal(iconCell.center());
    // alwaysEscape: the preview is its own ToolTip window ABOVE the dialog - a child layer's motes
    // played underneath it; paintNow on a close, so it never blinks out before any mote shows.
    return gui::flyTipDust(hover_.hoverPreview, window(), origin, gather,
                           gather ? gui::TIP_DUST_IN_MS : gui::TIP_DUST_OUT_MS,
                           /*escapeHost=*/true, /*paintNow=*/!gather,
                           /*alwaysEscape=*/true) != nullptr;
  }

  QVariantAnimation* ProjectsDialog::hoverFade() {
    if (!hover_.hoverFade) {
      hover_.hoverFade = new QVariantAnimation(this);
      connect(hover_.hoverFade, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        if (hover_.hoverPreview) hover_.hoverPreview->setWindowOpacity(v.toDouble());
      });
      connect(hover_.hoverFade, &QVariantAnimation::finished, this, [this] {
        if (!hover_.hoverClosing) return;
        hover_.hoverClosing = false;
        if (hover_.hoverPreview) hover_.hoverPreview->hide();
      });
    }
    return hover_.hoverFade;
  }

  void ProjectsDialog::placeHoverPreview(const QPoint& globalCursor) {
    if (!hover_.hoverPreview) return;
    // Down-right of the cursor, flipped/clamped to stay on-screen (browser positionZoom).
    QScreen* s = QGuiApplication::screenAt(globalCursor);
    const QRect scr = (s ? s : QGuiApplication::primaryScreen())->availableGeometry();
    const QSize sz = hover_.hoverPreview->size();
    QPoint gp = globalCursor + QPoint(18, 18);
    if (gp.x() + sz.width() > scr.right()) gp.setX(globalCursor.x() - 18 - sz.width());
    if (gp.y() + sz.height() > scr.bottom()) gp.setY(scr.bottom() - sz.height());
    if (gp.x() < scr.left()) gp.setX(scr.left());
    if (gp.y() < scr.top()) gp.setY(scr.top());
    hover_.hoverPreview->move(gp);
  }

  void ProjectsDialog::revealHoverPreview(QListWidgetItem* it) {
    if (!hover_.hoverPreview) return;
    hover_.hoverClosing = false;   // BEFORE stop(): stop() emits finished, which would hide()
    auto* fade = hoverFade();
    fade->stop();
    fade->setKeyValues({});
    if (support::motionReduced()) {  // the end state, at once
      hover_.hoverPreview->setWindowOpacity(1.0);
      return;
    }
    if (dustHoverPreview(it, /*gather=*/true)) {
      // The preview waits behind its own motes and fades up as the last of them land
      // (the shared surfaceForm ramp).
      hover_.hoverPreview->setWindowOpacity(0.0);
      gui::holdFadeKeys(fade, gui::TIP_DUST_IN_MS);
    } else {
      fade->setDuration(HOVER_FADE_MS);
      fade->setStartValue(hover_.hoverPreview->windowOpacity());
      fade->setEndValue(1.0);
    }
    fade->start();
  }

  void ProjectsDialog::hideHoverPreview() {
    if (!hover_.hoverPreview || !hover_.hoverPreview->isVisible() || hover_.hoverClosing) return;
    // Photographed and dusted while it is still the box on screen, so the label fades out BEHIND the
    // leaving motes instead of blinking off under them.
    const bool dusted = !support::motionReduced() && dustHoverPreview(hover_.hoverItem, /*gather=*/false);
    hover_.hoverItem = nullptr;
    if (!dusted) {
      if (hover_.hoverFade) { hover_.hoverClosing = false; hover_.hoverFade->stop(); }
      hover_.hoverPreview->hide();
      return;
    }
    auto* fade = hoverFade();
    fade->stop();
    fade->setKeyValues({});
    hover_.hoverClosing = true;
    fade->setDuration(gui::DUST_HAND_OVER_MS);
    fade->setStartValue(hover_.hoverPreview->windowOpacity());
    fade->setEndValue(0.0);
    fade->start();
  }

  bool ProjectsDialog::pointerOverPreviewedIcon() const {
    if (!list_ || !hover_.hoverItem) return false;
    const QPoint vpos = list_->viewport()->mapFromGlobal(QCursor::pos());
    if (!list_->viewport()->rect().contains(vpos)) return false;
    if (list_->itemAt(vpos) != hover_.hoverItem) return false;
    const auto* del = static_cast<ProjectRowDelegate*>(list_->itemDelegate());
    const QRect dec = del ? del->iconRectFor(list_->row(hover_.hoverItem)) : QRect();
    return dec.isValid() && dec.adjusted(-2, -2, 2, 2).contains(vpos);
  }

}  // namespace stencil::gui
