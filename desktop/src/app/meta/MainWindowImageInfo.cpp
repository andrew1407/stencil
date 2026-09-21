#include "MainWindow.hpp"
#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "ChatPlanTarget.hpp"
#include "ChatMenuPanel.hpp"
#include "planExecutor.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "CanvasTooltip.hpp"
#include "CanvasWidget.hpp"
#include <QScrollArea>
#include "guiHelpers.hpp"
#include "SearchCombo.hpp"
#include "ControlsPill.hpp"
#include "iconSet.hpp"
#include "InfoDialog.hpp"
#include "LinksDialog.hpp"
#include "MediaLoader.hpp"
#include "ProjectsDialog.hpp"
#include "ConnectDialog.hpp"
#include "SelectionPanel.hpp"
#include "SelectedLineBar.hpp"
#include "SettingsDialog.hpp"
#include "ShortcutsDialog.hpp"
#include "theme.hpp"
#include "../../support/control/WrapRow.hpp"
#include "../../support/modal/modalChrome.hpp"

#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QToolBar>
#include <algorithm>

// The "Image Size" bar, the fullscreen glyph and the header logo mark.

namespace stencil::gui {

  // Hold the row at its TALLER state's height (the incognito glyph is ~2px taller). Measured on a throwaway twin; cached until font/theme changes.
  void MainWindow::reserveImageInfoHeight() {
    if (!imageSizeInfo) return;
    const QString key = imageSizeInfo->font().key() + QLatin1Char('|') +
                        QString::number(imageSizeInfo->font().pointSizeF()) +
                        QLatin1Char('|') + settings.themeMode + QLatin1Char('|') +
                        settings.accentColor;
    if (key != imageInfoHeightKey || imageSizeInfo->minimumHeight() <= 0) {
      QLabel twin;
      twin.setFont(imageSizeInfo->font());
      twin.setStyleSheet(imageSizeInfo->styleSheet());
      twin.setContentsMargins(imageSizeInfo->contentsMargins());   // sizeHint() honours these
      twin.ensurePolished();
      twin.setTextFormat(Qt::PlainText);
      twin.setText(QStringLiteral("No image loaded"));
      int h = twin.sizeHint().height();
      twin.setTextFormat(Qt::RichText);
      twin.setText(QStringLiteral("Image Size: 8888 × 8888 px") + incognitoTagHtml());
      h = std::max(h, twin.sizeHint().height());
      imageInfoHeightKey = key;
      imageSizeInfo->setFixedHeight(h);
    }
    // The panel dock sits below the Image Size dock in the same stack, so no faked header gap.
    syncImageInfoDockHeight();
  }

  // Pins the dock's own height too, or QMainWindow draws a drag grip above the canvas. Brackets OUT to enter, IN to leave (browser fullscreen/layer.js).
  void MainWindow::syncFullscreenGlyph() {
    if (!actFullscreen) return;
    const QString name = fs.active ? QStringLiteral("minimize") : QStringLiteral("maximize");
    actionIconNames.insert(actFullscreen, name);
    const QColor ink = toolButtonIconColor(actFullscreen, iconColor);
    actFullscreen->setIcon(themedIcon(name, ink, TOOL_ICON));
  }

  // Qt matches property selectors at polish time, so the flag needs a re-polish.
  void MainWindow::markFullscreenBars(bool on) {
    if (selPanel) selPanel->setCollapseChevronVisible(!on);
    for (QToolBar* b : findChildren<QToolBar*>()) {
      if (b == headerToolbar) continue;
      b->setProperty("fsBar", on);
      b->style()->unpolish(b);
      b->style()->polish(b);
    }
    // …and the canvas frame with them: in fullscreen it wears the browser's fullscreen-panel
    // hairline (app.qss [fsView]).
    if (scroll) {
      scroll->setProperty("fsView", on);
      scroll->style()->unpolish(scroll);
      scroll->style()->polish(scroll);
    }
  }

  void MainWindow::syncImageInfoDockHeight() {
    if (!imageInfoDock || !imageInfoHost) return;
    imageInfoDock->setFixedHeight(imageInfoHost->sizeHint().height());
  }

  void MainWindow::updateImageSizeInfo() {
    QString size;
    if (canvas && canvas->hasImage()) {
      const bool isBlank = !blankColor.isEmpty();
      size = QString("Image Size: %1 × %2 px%3")
                 .arg(canvas->imageWidth())
                 .arg(canvas->imageHeight())
                 .arg(isBlank ? QStringLiteral("  ·  blank") : QString());
    } else {
      size = QStringLiteral("No image loaded");
    }
    if (imageSizeInfo) {
      // The row's height is RESERVED for the taller state, so switching cannot resize the bar.
      reserveImageInfoHeight();
      // Browser parity (drawingApp.js updateInfo + .info-incognito): incognito rides INLINE. Our own text, never model output, so rich text is safe.
      if (incognito) {
        imageSizeInfo->setTextFormat(Qt::RichText);
        imageSizeInfo->setText(size.toHtmlEscaped() + incognitoTagHtml());
      } else {
        imageSizeInfo->setTextFormat(Qt::PlainText);
        imageSizeInfo->setText(size);
      }
    }
    // The "?" carries the SAME size plus the incognito line — the collapsed state's only readout.
    if (statusHint) {
      QString tip = size;
      if (incognito) tip += QStringLiteral("\nIncognito — not saved");
      statusHint->setToolTip(tip);
      refreshStatusHintVisibility();
    }
  }

  // Mini S-mark logo, a QPainter port of the browser's app-logo SVG (toolbar.js); only the frame tracks the accent.
  QPixmap MainWindow::makeLogoPixmap(int size) const {
    // At LEAST 2x: devicePixelRatioF() often reports 1 before the window is on its Retina screen, and a 1x pixmap in a 2x button draws at HALF size.
    const qreal dpr = qMax(devicePixelRatioF(), 2.0);
    QPixmap pm(qRound(size * dpr), qRound(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const double u = size / 64.0;   // browser viewBox is 0..64
    QColor accent = accentPrimary(settings.accentColor);
    if (!accent.isValid()) accent = QColor(DEFAULT_ACCENT_HEX);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#2b2f3a"));
    p.drawRoundedRect(QRectF(2 * u, 2 * u, 60 * u, 60 * u), 13 * u, 13 * u);
    QPen frame(accent);
    frame.setWidthF(2.5 * u);
    p.setPen(frame);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(2.75 * u, 2.75 * u, 58.5 * u, 58.5 * u), 12.25 * u, 12.25 * u);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#3a3f4b"));
    p.drawRoundedRect(QRectF(12 * u, 12 * u, 40 * u, 40 * u), 4 * u, 4 * u);
    const QPointF pts[7] = {QPointF(44 * u, 20 * u), QPointF(32 * u, 16 * u), QPointF(20 * u, 24 * u),
                            QPointF(32 * u, 32 * u), QPointF(44 * u, 40 * u), QPointF(32 * u, 48 * u),
                            QPointF(20 * u, 44 * u)};
    QPen line(QColor("#FFFF00"));
    line.setWidthF(3.5 * u);
    line.setCapStyle(Qt::RoundCap);
    line.setJoinStyle(Qt::RoundJoin);
    p.setPen(line);
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(pts, 7);
    p.setBrush(QColor("#FFFF00"));
    p.setPen(QPen(QColor("#000000"), 1.25 * u));
    for (const auto& pt : pts) p.drawEllipse(pt, 2.6 * u, 2.6 * u);
    return pm;
  }

}  // namespace stencil::gui
