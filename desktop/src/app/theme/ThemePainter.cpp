#include "MainWindow.hpp"
#include "windowSheets.hpp"
#include "ThemePainter.hpp"
#include "../../support/webcore/icons.hpp"
#include "ToastStack.hpp"
#include "iconSet.hpp"
#include "../../support/skinPrefs.hpp"
#include "theme.hpp"
#include "tipContent.hpp"

#include <QCheckBox>
#include <QLayout>
#include <QDockWidget>
#include <QPainter>

// The theme's small restyling passes — the drop hint, the view toggles, the Image Size bar and the
// logo pixmap — and the manual light/dark toggle.

namespace stencil::gui {

  // The paste combo as KEYCAPS in this platform's glyphs (browser twin: mainContent.js pasteKeys).
  // The caps are painted pictures holding literal colours, so this re-runs on every theme change.
  void ThemePainter::refreshDropHint() {
    if (!w.tools.dropHintText) return;
    const QString combo =
        QKeySequence(w.keys.value("paste", "Ctrl+V")).toString(QKeySequence::NativeText);
    // A keycap is taller than the type beside it and an inline image inflates the line box downwards.
    // One middle-aligned table row centres prose and caps, as a rich tooltip's row does (tipContent).
    w.tools.dropHintText->setText(
        QString("<table cellspacing=\"0\" cellpadding=\"0\"><tr>"
                "<td style=\"vertical-align: middle;\">Drag &amp; drop an <b>image</b> or "
                "<b>.json</b> anywhere on the window — or paste an image with&nbsp;</td>"
                "<td style=\"vertical-align: middle;\">%1</td></tr></table>")
            .arg(comboKeycapsHtml(combo, currentPalette())));
  }

  // theme.cpp's QSS draws a wider indicator than Qt's default, and applying it re-polishes the
  // floor away; numbers are the QSS's (16px + 1px border a side + 7px spacing) — keep in step.
  void ThemePainter::sizeViewToggles() {
    for (QCheckBox* box : {w.tools.showPointsCheck, w.tools.showLinesCheck}) {
      if (!box) continue;
      // Through the stylesheet so it reaches sizeHint (setMinimumWidth does not).
      box->setStyleSheet(support::viewToggleSheet());
      for (QWidget* p = box->parentWidget(); p && p != &w; p = p->parentWidget()) {
        if (!p->layout()) continue;
        p->layout()->invalidate();
        p->layout()->activate();
      }
    }
  }

  // A muted grey reads as disabled on the skin's face, so there the line takes the window's ink.
  void ThemePainter::restyleImageSizeInfo() {
    if (!w.tools.imageSizeInfo) return;
    const QString ink = support::isWebcore()
                            ? themePalette(w.painted.dark, w.settings.accentColor).textMain.name()
                            : QStringLiteral("#9aa0a8");
    w.tools.imageSizeInfo->setStyleSheet(support::imageSizeInfoSheet(ink));
  }

  void ThemePainter::syncImageInfoDockHeight() {
    if (!w.tools.imageInfoDock || !w.tools.imageInfoHost) return;
    // The host's cached item hint for the bar predates the label height reserveImageInfoHeight
    // just set, and a bar that is not yet shown never re-announces it on its own.
    if (w.tools.imageInfoBar) w.tools.imageInfoBar->updateGeometry();
    w.tools.imageInfoDock->setFixedHeight(w.tools.imageInfoHost->sizeHint().height());
  }

  // Mini S-mark logo, a QPainter port of the browser's app-logo SVG (toolbar.js); only the frame tracks the accent.
  QPixmap ThemePainter::makeLogoPixmap(int size) const {
    // Under a skin the mark is the skin's own (support/webcore); only its ring takes the accent,
    // as the frame below does.
    if (support::isWebcore()) {
      const qreal r = qMax(w.devicePixelRatioF(), 2.0);
      QColor ring = accentPrimary(w.settings.accentColor);
      if (!ring.isValid()) ring = QColor(DEFAULT_ACCENT_HEX);
      return iconFromMarkup(support::pixelLogoSvg(ring), ring, size, r).pixmap(QSize(size, size), r);
    }
    // At LEAST 2x: devicePixelRatioF() often reports 1 before the window is on its Retina screen, and a 1x pixmap in a 2x button draws at HALF size.
    const qreal dpr = qMax(w.devicePixelRatioF(), 2.0);
    QPixmap pm(qRound(size * dpr), qRound(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const double u = size / 64.0;   // browser viewBox is 0..64
    QColor accent = accentPrimary(w.settings.accentColor);
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

  // A manual toggle stops following the OS (browser behaviour).
  void ThemePainter::toggleTheme() {
    // One flip at a time: a second press mid-wipe tears the two palettes across each other. The
    // press is dropped until the wipe ends.
    if (w.painted.swapping()) return;
    // Relative to what is PAINTED: a skin may be forcing a theme the stored mode does not say.
    support::clearForcedDark();
    w.settings.themeMode = w.painted.dark ? "light" : "dark";
    w.applySettings(w.settings, true);
  }

  bool ThemePainter::paintingDark() const {
    return support::forcedDark().value_or(resolveDark(w.settings.themeMode));
  }

}  // namespace stencil::gui
