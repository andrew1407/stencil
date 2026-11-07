#include "ProjectsDialog.hpp"
#include <QListWidget>

#include "ProjectRowDelegate.hpp"
#include "projectsRowChrome.hpp"
#include "DisintegrateOverlay.hpp"
#include "../../../support/menu/popupSlide.hpp"
#include "../../../support/tip/tipFlight.hpp"

#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QSize>
#include <QVariant>
#include <QVariantAnimation>
#include <QLabel>

// The floating hover-magnify preview over a row's thumbnail.

namespace stencil::gui {

  namespace {
    void endHoverDust(ProjectsHoverParts& hover) {
      if (!hover.hoverDust) return;
      hover.hoverDust->hide();
      hover.hoverDust->deleteLater();
      hover.hoverDust = nullptr;
    }
    // The row's thumbnail centre (GLOBAL), the hover hit test's own rect: what the preview flies out of and back into.
    QPoint iconOrigin(QListWidget* list, QListWidgetItem* it) {
      const auto* del = static_cast<ProjectRowDelegate*>(list->itemDelegate());
      QRect iconCell = del ? del->iconRectFor(list->row(it)) : QRect();
      if (!iconCell.isValid()) iconCell = list->visualItemRect(it);
      return list->viewport()->mapToGlobal(iconCell.center());
    }
    // Where no motes fly: Transition grows it out of the thumbnail and shrinks it back, Sliding pours it; it
    // only faded (user report). False leaves it to the fade.
    bool flyPreview(QWidget* preview, QListWidget* list, QListWidgetItem* it, bool in) {
      if (!preview || !list || !it) return false;
      const QPoint o = iconOrigin(list, it);
      if (support::isTransitionMotionOk()) return in ? support::growTipAt(*preview, o) : support::shrinkTipAt(*preview, o);
      if (!support::isSlideMotionOk()) return false;
      if (in) { support::slidePopupIn(*preview, o); return true; }
      return support::slidePopupOut(*preview, list->viewport(), o);
    }
  }  // namespace

  bool ProjectsDialog::dustHoverPreview(QListWidgetItem* it, bool gather) {
    if (!hover.hoverPreview || !list || !it) return false;
    // The SAME thumbnail rect the hover hit test uses — the flight starts and
    // ends on the picture, never on the checkbox beside it.
    const QPoint origin = iconOrigin(list, it);
    endHoverDust(hover);
    // alwaysEscape: the preview is its own ToolTip window ABOVE the dialog - a child layer's motes
    // played underneath it; paintNow on a close, so it never blinks out before any mote shows.
    hover.hoverDust = gui::flyTipDust(hover.hoverPreview, window(), origin, gather,
                                      gather ? gui::TIP_DUST_IN_MS : gui::TIP_DUST_OUT_MS,
                                      /*escapeHost=*/true, /*paintNow=*/!gather,
                                      /*alwaysEscape=*/true);
    return hover.hoverDust != nullptr;
  }

  QVariantAnimation* ProjectsDialog::hoverFade() {
    if (!hover.hoverFade) {
      hover.hoverFade = new QVariantAnimation(this);
      connect(hover.hoverFade, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        if (hover.hoverPreview) hover.hoverPreview->setWindowOpacity(v.toDouble());
      });
      connect(hover.hoverFade, &QVariantAnimation::finished, this, [this] {
        if (!hover.hoverClosing) return;
        hover.hoverClosing = false;
        if (hover.hoverPreview) hover.hoverPreview->hide();
      });
    }
    return hover.hoverFade;
  }

  void ProjectsDialog::placeHoverPreview(const QPoint& globalCursor) {
    if (!hover.hoverPreview) return;
    // Down-right of the cursor, flipped/clamped to stay on-screen (browser positionZoom).
    QScreen* s = QGuiApplication::screenAt(globalCursor);
    const QRect scr = (s ? s : QGuiApplication::primaryScreen())->availableGeometry();
    const QSize sz = hover.hoverPreview->size();
    QPoint gp = globalCursor + QPoint(18, 18);
    if (gp.x() + sz.width() > scr.right()) gp.setX(globalCursor.x() - 18 - sz.width());
    if (gp.y() + sz.height() > scr.bottom()) gp.setY(scr.bottom() - sz.height());
    if (gp.x() < scr.left()) gp.setX(scr.left());
    if (gp.y() < scr.top()) gp.setY(scr.top());
    const QPoint delta = gp - hover.hoverPreview->pos();
    hover.hoverPreview->move(gp);
    // A gather lands where the box now is, or its photo stays behind beside the moved preview.
    auto* dust = static_cast<DisintegrateOverlay*>(hover.hoverDust.data());
    if (dust && dust->gathering()) dust->retarget(delta);
  }

  void ProjectsDialog::revealHoverPreview(QListWidgetItem* it) {
    if (!hover.hoverPreview) return;
    hover.hoverClosing = false;   // BEFORE stop(): stop() emits finished, which would hide()
    auto* fade = hoverFade();
    fade->stop();
    fade->setKeyValues({});
    if (support::motionReduced()) {  // the end state, at once
      endHoverDust(hover);
      hover.hoverPreview->setWindowOpacity(1.0);
      return;
    }
    if (dustHoverPreview(it, /*gather=*/true)) {
      // The preview waits behind its own motes and fades up as the last of them land
      // (the shared surfaceForm ramp).
      hover.hoverPreview->setWindowOpacity(0.0);
      gui::holdFadeKeys(fade, gui::TIP_DUST_IN_MS);
    } else if (flyPreview(hover.hoverPreview, list, it, /*in=*/true)) {
      return;   // the flight owns the preview's opacity until it lands
    } else {
      fade->setDuration(support::motionMs(HOVER_FADE_MS));
      fade->setStartValue(hover.hoverPreview->windowOpacity());
      fade->setEndValue(1.0);
    }
    fade->start();
  }

  void ProjectsDialog::hideHoverPreview() {
    if (!hover.hoverPreview || !hover.hoverPreview->isVisible() || hover.hoverClosing) return;
    // Photographed and dusted while it is still the box on screen, so the label fades out BEHIND the
    // leaving motes instead of blinking off under them.
    const bool dusted = !support::motionReduced() && dustHoverPreview(hover.hoverItem, /*gather=*/false);
    if (!dusted && !support::motionReduced()) flyPreview(hover.hoverPreview, list, hover.hoverItem, /*in=*/false);
    hover.hoverItem = nullptr;
    if (!dusted) {
      endHoverDust(hover);
      if (hover.hoverFade) { hover.hoverClosing = false; hover.hoverFade->stop(); }
      hover.hoverPreview->hide();
      return;
    }
    auto* fade = hoverFade();
    fade->stop();
    fade->setKeyValues({});
    hover.hoverClosing = true;
    fade->setDuration(support::motionMs(gui::DUST_HAND_OVER_MS));
    fade->setStartValue(hover.hoverPreview->windowOpacity());
    fade->setEndValue(0.0);
    fade->start();
  }

  bool ProjectsDialog::pointerOverPreviewedIcon() const {
    if (!list || !hover.hoverItem) return false;
    const QPoint vpos = list->viewport()->mapFromGlobal(QCursor::pos());
    if (!list->viewport()->rect().contains(vpos)) return false;
    if (list->itemAt(vpos) != hover.hoverItem) return false;
    const auto* del = static_cast<ProjectRowDelegate*>(list->itemDelegate());
    const QRect dec = del ? del->iconRectFor(list->row(hover.hoverItem)) : QRect();
    return dec.isValid() && dec.adjusted(-2, -2, 2, 2).contains(vpos);
  }

}  // namespace stencil::gui
