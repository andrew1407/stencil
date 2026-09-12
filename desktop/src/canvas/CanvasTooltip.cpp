#include "CanvasTooltip.hpp"
#include "../support/DisintegrateOverlay.hpp"
#include "../support/modalReveal.hpp"
#include <QApplication>
#include <QLabel>
#include <QPalette>
#include <QScreen>
#include <QVariantAnimation>
#include <QVBoxLayout>

namespace stencil::gui {

  namespace {
    // Fade fallback; the dust flights ride the shared tip clock (DisintegrateOverlay.hpp
    // TIP_DUST_IN_MS/TIP_DUST_OUT_MS/DUST_HOLD/DUST_HAND_OVER_MS), same as AppTooltip's.
    constexpr int FADE_MS = 90;
  }

  CanvasTooltip::CanvasTooltip(QWidget* parent) : QFrame(parent) {
    // Frameless overlay that doesn't steal focus; styled via the app stylesheet's
    // QToolTip-like look (we reuse QFrame so the theme QSS can target it).
    setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setObjectName("canvasTooltip");
    setStyleSheet(
        "#canvasTooltip { background:#222; color:#eee; border:1px solid #555;"
        " border-radius:4px; }"
        " #canvasTooltip QLabel { color:#eee; padding:4px 8px; }");
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    body_ = new QLabel(this);
    body_->setTextFormat(Qt::RichText);
    lay->addWidget(body_);
    hide();

    fade_ = new QVariantAnimation(this);
    fade_->setDuration(FADE_MS);
    connect(fade_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& v) { setWindowOpacity(v.toDouble()); });
    connect(fade_, &QVariantAnimation::finished, this, [this] {
      if (closing_) { closing_ = false; QFrame::hide(); }
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
    body_->setText(html);
    adjustSize();
  }

  bool CanvasTooltip::dust(bool gather) {
    // Not window(): this widget's OWN Qt::ToolTip flag makes it a top-level in its own
    // right, so that would just return itself. The real host is the app window above it.
    QWidget* host = parentWidget() ? parentWidget()->window() : nullptr;
    return flyTipDust(this, host, lastCursor_, gather,
                      gather ? TIP_DUST_IN_MS : TIP_DUST_OUT_MS, /*escapeHost=*/true)
           != nullptr;
  }

  // Port of tooltip.js position(): cursor + 15, flip when it would overflow the
  // screen, clamp to a 10 px minimum.
  void CanvasTooltip::showAt(const QPoint& globalCursor) {
    lastCursor_ = globalCursor;
    adjustSize();
    const QRect scr = QApplication::primaryScreen()->availableGeometry();
    int left = globalCursor.x() + 15;
    int top = globalCursor.y() + 15;
    if (left + width() > scr.right()) left = globalCursor.x() - width() - 15;
    if (top + height() > scr.bottom()) top = globalCursor.y() - height() - 15;
    if (left < scr.left() + 10) left = scr.left() + 10;
    if (top < scr.top() + 10) top = scr.top() + 10;
    move(left, top);

    // Dust/fade in only on the hidden -> visible transition; while it merely follows
    // the cursor (already visible) we just move(), so there's no per-frame flicker.
    const bool wasHidden = !isVisible();
    if (wasHidden) {
      closing_ = false;
      fade_->stop();
      setWindowOpacity(support::motionReduced() ? 1.0 : 0.0);
    }
    show();
    raise();
    if (!wasHidden || support::motionReduced()) return;

    if (dust(true)) {
      holdFadeKeys(fade_, TIP_DUST_IN_MS);
    } else {
      fade_->setKeyValues({});
      fade_->setDuration(FADE_MS);
      fade_->setKeyValueAt(0.0, 0.0);
      fade_->setKeyValueAt(1.0, 1.0);
    }
    fade_->start();
  }

  void CanvasTooltip::hideTip() {
    if (!isVisible()) return;
    fade_->stop();
    if (support::motionReduced()) { closing_ = false; QFrame::hide(); return; }
    // Photographed and dusted while still on screen — the cloud is what the tip
    // leaves behind, so it hands over in one beat rather than blinking out.
    const bool dusted = dust(false);
    closing_ = true;
    fade_->setKeyValues({});
    fade_->setDuration(dusted ? gui::DUST_HAND_OVER_MS : FADE_MS);
    fade_->setKeyValueAt(0.0, windowOpacity());
    fade_->setKeyValueAt(1.0, 0.0);
    fade_->start();
  }

}
