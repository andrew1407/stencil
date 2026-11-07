#include "CanvasTooltip.hpp"
#include "windowSheets.hpp"
#include "../support/motion/DisintegrateOverlay.hpp"
#include "AppTooltip.hpp"
#include "../support/tip/tipFlight.hpp"
#include "../support/motionPrefs.hpp"
#include "../support/menu/popupSlide.hpp"
#include <QApplication>
#include <QLabel>
#include <QVariantAnimation>
#include <QVBoxLayout>

namespace stencil::gui {

  namespace {
    // Fade fallback; the dust flights ride the shared tip clock (DisintegrateOverlay.hpp
    // TOOLTIP_DUST_IN_MS/TOOLTIP_DUST_OUT_MS/DUST_HOLD), same as AppTooltip's.
    constexpr int FADE_MS = TOOLTIP_FADE_MS;
  }

  CanvasTooltip::CanvasTooltip(QWidget* parent) : QFrame(parent) {
    // Frameless overlay that doesn't steal focus; styled via the app stylesheet's
    // QToolTip-like look (we reuse QFrame so the theme QSS can target it).
    setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setObjectName("canvasTooltip");
    setProperty(TIP_WINDOW_PROPERTY, true);
    setStyleSheet(support::canvasTooltipSheet());
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    body = new QLabel(this);
    body->setTextFormat(Qt::RichText);
    lay->addWidget(body);
    hide();

    fade = new QVariantAnimation(this);
    fade->setDuration(FADE_MS);
    connect(fade, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& v) { setWindowOpacity(v.toDouble()); });
    connect(fade, &QVariantAnimation::finished, this, [this] {
      if (closing) { closing = false; QFrame::hide(); }
    });
  }

  void CanvasTooltip::setRows(
      const std::vector<std::pair<QString, QString>>& rows) {
    if (rows.empty()) {
      hideTip();
      return;
    }
    QString html = "<table cellspacing='2'>";
    for (const auto& r : rows) {
      html += QString("<tr><td><b>%1</b></td><td>&nbsp;&nbsp;%2</td></tr>")
                  .arg(r.first.toHtmlEscaped(), r.second.toHtmlEscaped());
    }
    html += "</table>";
    body->setText(html);
    adjustSize();
  }

  bool CanvasTooltip::dust(bool shouldGather) {
    // Not window(): this widget's OWN Qt::ToolTip flag makes it a top-level in its own
    // right, so that would just return itself. The real host is the app window above it.
    QWidget* host = parentWidget() ? parentWidget()->window() : nullptr;
    return flyTipDust(this, host, lastCursor, shouldGather,
                      shouldGather ? TOOLTIP_DUST_IN_MS : TOOLTIP_DUST_OUT_MS, /*escapeHost=*/true)
           != nullptr;
  }

  // Port of tooltip.js position(): cursor + 15, flip when it would overflow the
  // screen, clamp to a 10 px minimum.
  void CanvasTooltip::showAt(const QPoint& globalCursor) {
    lastCursor = globalCursor;
    adjustSize();
    // Mid-entrance it stays where the flight aims: moved under it, it jumped as it landed (browser tooltip.js).
    if (isVisible() && !closing && !moveHold.hasExpired()) return;
    const QRect scr = QApplication::primaryScreen()->availableGeometry();
    int left = globalCursor.x() + 15;
    int top = globalCursor.y() + 15;
    if (left + width() > scr.right()) left = globalCursor.x() - width() - 15;
    if (top + height() > scr.bottom()) top = globalCursor.y() - height() - 15;
    if (left < scr.left() + 10) left = scr.left() + 10;
    if (top < scr.top() + 10) top = scr.top() + 10;
    move(left, top);

    // An entrance only on hidden → visible (a follow is a bare move); mid fade-out counts as hidden, or the
    // fade's end would hide the tip just re-shown.
    const bool wasHidden = !isVisible() || closing;
    if (wasHidden) {
      closing = false;
      fade->stop();
      setWindowOpacity(support::motionReduced() ? 1.0 : 0.0);
    }
    show();
    raise();
    if (!wasHidden || support::motionReduced()) return;

    if (dust(true)) {
      holdFadeKeys(fade, TOOLTIP_DUST_IN_MS);
      moveHold.setRemainingTime(support::motionMs(TOOLTIP_DUST_IN_MS));
    } else if (support::isSlideMotionOk()) {
      setWindowOpacity(1.0);   // Sliding: poured out of the pointer (browser tooltip.js tipFallbackIn)
      support::slidePopupIn(*this, lastCursor, support::tipSlideInMs());
      moveHold.setRemainingTime(support::motionMs(support::tipSlideInMs()));
      return;
    } else if (support::growTipAt(*this, lastCursor)) {
      moveHold.setRemainingTime(support::motionMs(support::tipGrowMs()));
      return;   // Transition: it grows out of the pointer, and shows itself as the picture lands
    } else {
      fade->setKeyValues({});
      fade->setDuration(support::motionMs(FADE_MS));
      fade->setKeyValueAt(0.0, 0.0);
      fade->setKeyValueAt(1.0, 1.0);
    }
    fade->start();
  }

  void CanvasTooltip::hideTip() {
    if (!isVisible()) return;
    fade->stop();
    if (support::motionReduced()) { closing = false; QFrame::hide(); return; }
    // Photographed and dusted while still on screen — the cloud is what the tip
    // leaves behind, so it hands over in one beat rather than blinking out.
    const bool wasDusted = dust(false);
    const bool wasPoured = !wasDusted && support::isSlideMotionOk()
                        && support::slidePopupOut(*this, parentWidget(), lastCursor, support::tipSlideOutMs());
    if (wasPoured || (!wasDusted && support::shrinkTipAt(*this, lastCursor))) { closing = false; QFrame::hide(); return; }
    closing = true;
    fade->setKeyValues({});
    fade->setDuration(support::motionMs(wasDusted ? gui::TOOLTIP_HAND_OVER_MS : FADE_MS));
    fade->setKeyValueAt(0.0, windowOpacity());
    fade->setKeyValueAt(1.0, 0.0);
    fade->start();
  }

}
