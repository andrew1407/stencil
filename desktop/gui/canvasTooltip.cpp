#include "canvasTooltip.hpp"
#include <QApplication>
#include <QLabel>
#include <QScreen>
#include <QVBoxLayout>

namespace stencil::gui {

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
  }

  void CanvasTooltip::setRows(
      const std::vector<std::pair<QString, QString>>& rows) {
    if (rows.empty()) {
      hide();
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

  // Port of tooltip.js position(): cursor + 15, flip when it would overflow the
  // screen, clamp to a 10 px minimum.
  void CanvasTooltip::showAt(const QPoint& globalCursor) {
    adjustSize();
    const QRect scr = QApplication::primaryScreen()->availableGeometry();
    int left = globalCursor.x() + 15;
    int top = globalCursor.y() + 15;
    if (left + width() > scr.right()) left = globalCursor.x() - width() - 15;
    if (top + height() > scr.bottom()) top = globalCursor.y() - height() - 15;
    if (left < scr.left() + 10) left = scr.left() + 10;
    if (top < scr.top() + 10) top = scr.top() + 10;
    move(left, top);
    show();
    raise();
  }

}
