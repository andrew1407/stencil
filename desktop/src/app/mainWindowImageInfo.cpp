#include "mainWindow.hpp"
#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "chatPlanTarget.hpp"
#include "chatMenuPanel.hpp"
#include "planExecutor.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "canvasTooltip.hpp"
#include "canvasWidget.hpp"
#include "guiHelpers.hpp"
#include "searchCombo.hpp"
#include "controlsPill.hpp"
#include "iconSet.hpp"
#include "infoDialog.hpp"
#include "linksDialog.hpp"
#include "mediaLoader.hpp"
#include "projectsDialog.hpp"
#include "connectDialog.hpp"
#include "selectionPanel.hpp"
#include "selectedLineBar.hpp"
#include "settingsDialog.hpp"
#include "shortcutsDialog.hpp"
#include "theme.hpp"
#include "../support/wrapRow.hpp"
#include "../support/modalChrome.hpp"

#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QToolBar>
#include <algorithm>

// The "Image Size" bar, the fullscreen glyph and the header logo mark.

namespace stencil::gui {

  // Hold the info row at its TALLER state's height: the incognito glyph is ~2px
  // taller than plain text, and it must not shift the rows below. Measured on a
  // throwaway twin (the live label would flicker); cached until font/theme changes.
  void MainWindow::reserveImageInfoHeight() {
    if (!imageSizeInfo_) return;
    const QString key = imageSizeInfo_->font().key() + QLatin1Char('|') +
                        QString::number(imageSizeInfo_->font().pointSizeF()) +
                        QLatin1Char('|') + settings_.themeMode + QLatin1Char('|') +
                        settings_.accentColor;
    if (key != imageInfoHeightKey_ || imageSizeInfo_->minimumHeight() <= 0) {
      QLabel twin;
      twin.setFont(imageSizeInfo_->font());
      twin.setStyleSheet(imageSizeInfo_->styleSheet());
      twin.setContentsMargins(imageSizeInfo_->contentsMargins());   // sizeHint() honours these
      twin.ensurePolished();
      twin.setTextFormat(Qt::PlainText);
      twin.setText(QStringLiteral("No image loaded"));
      int h = twin.sizeHint().height();
      twin.setTextFormat(Qt::RichText);
      twin.setText(QStringLiteral("Image Size: 8888 × 8888 px") + incognitoTagHtml());
      h = std::max(h, twin.sizeHint().height());
      imageInfoHeightKey_ = key;
      imageSizeInfo_->setFixedHeight(h);
    }
    // The Image Size bar has its own dock (buildImageInfoBar) and the panel dock sits below
    // that same stack, so the panel lands at the right height with no faked header gap.
    syncImageInfoDockHeight();
  }

  // Pins imageInfoDock_'s own height too, not just its content's — otherwise QMainWindow
  // still treats it as resizable and draws a drag grip above the canvas for nothing.
  // Brackets OUT to enter fullscreen, IN to leave it (browser twin: fullscreenLayer.js).
  // The button repaints off actionIconNames_ on the icon change, keeping its glyph white.
  void MainWindow::syncFullscreenGlyph() {
    if (!actFullscreen_) return;
    const QString name = fsActive_ ? QStringLiteral("minimize") : QStringLiteral("maximize");
    actionIconNames_.insert(actFullscreen_, name);
    const QColor ink = toolButtonIconColor(actFullscreen_, iconColor_);
    actFullscreen_->setIcon(themedIcon(name, ink, kToolIcon));
  }

  // A deeper bottom band while the rows hang over the canvas (theme.cpp QToolBar[fsBar]).
  // Qt matches property selectors at polish time, so the flag needs a re-polish.
  void MainWindow::markFullscreenBars(bool on) {
    for (QToolBar* b : findChildren<QToolBar*>()) {
      if (b == headerToolbar_) continue;
      b->setProperty("fsBar", on);
      b->style()->unpolish(b);
      b->style()->polish(b);
    }
  }

  void MainWindow::syncImageInfoDockHeight() {
    if (!imageInfoDock_ || !imageInfoHost_) return;
    imageInfoDock_->setFixedHeight(imageInfoHost_->sizeHint().height());
  }

  void MainWindow::updateImageSizeInfo() {
    QString size;
    if (canvas_ && canvas_->hasImage()) {
      const bool isBlank = !blankColor_.isEmpty();
      size = QString("Image Size: %1 × %2 px%3")
                 .arg(canvas_->imageWidth())
                 .arg(canvas_->imageHeight())
                 .arg(isBlank ? QStringLiteral("  ·  blank") : QString());
    } else {
      size = QStringLiteral("No image loaded");
    }
    if (imageSizeInfo_) {
      // The row's height is RESERVED for the taller of its two states before either is
      // shown, so switching between them cannot resize the info bar (see below).
      reserveImageInfoHeight();
      // Browser parity (drawingApp.js updateInfo + layout.css .info-incognito):
      // the incognito state rides INLINE on this line, accent-coloured and bold,
      // in both the loaded and the empty state. It is our own text, never model
      // output, so rich text is safe here.
      if (incognito_) {
        imageSizeInfo_->setTextFormat(Qt::RichText);
        imageSizeInfo_->setText(size.toHtmlEscaped() + incognitoTagHtml());
      } else {
        imageSizeInfo_->setTextFormat(Qt::PlainText);
        imageSizeInfo_->setText(size);
      }
    }
    // The "?" beside the project name carries the SAME size plus the incognito
    // line — and only those two facts. It is the collapsed state's only readout,
    // so it is refreshed from here (every incognito change ends in this call via
    // updateProjectTitle).
    if (statusHint_) {
      QString tip = size;
      if (incognito_) tip += QStringLiteral("\nIncognito — not saved");
      statusHint_->setToolTip(tip);
      refreshStatusHintVisibility();
    }
  }

  // Mini S-mark logo — a QPainter port of the browser's app-logo SVG (toolbar.js): a dark rounded
  // square with an ACCENT-coloured frame, an inner darker square, and a yellow polyline whose seven
  // dots trace an S. Only the frame tracks the accent (like the browser), so it never looks garish.
  // Repainted on theme/accent change from applyTheme.
  QPixmap MainWindow::makeLogoPixmap(int size) const {
    // At LEAST 2x, whatever devicePixelRatioF() says: it often reports 1 here (before the
    // window is on its Retina screen), and a 1x pixmap in a 2x button draws at HALF size
    // (a tiny resting logo). A 1x screen just downscales it, crisp.
    const qreal dpr = qMax(devicePixelRatioF(), 2.0);
    QPixmap pm(qRound(size * dpr), qRound(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const double u = size / 64.0;   // browser viewBox is 0..64
    QColor accent = accentPrimary(settings_.accentColor);
    if (!accent.isValid()) accent = QColor("#7c3aed");
    // Outer rounded square (dark), then the accent frame stroke on top.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#2b2f3a"));
    p.drawRoundedRect(QRectF(2 * u, 2 * u, 60 * u, 60 * u), 13 * u, 13 * u);
    QPen frame(accent);
    frame.setWidthF(2.5 * u);
    p.setPen(frame);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(2.75 * u, 2.75 * u, 58.5 * u, 58.5 * u), 12.25 * u, 12.25 * u);
    // Inner darker square.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#3a3f4b"));
    p.drawRoundedRect(QRectF(12 * u, 12 * u, 40 * u, 40 * u), 4 * u, 4 * u);
    // Yellow polyline + dots.
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
