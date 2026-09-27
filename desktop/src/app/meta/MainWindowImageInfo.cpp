#include "MainWindow.hpp"
#include "CanvasWidget.hpp"

#include <QLabel>
#include <algorithm>

// The "Image Size" bar, the fullscreen glyph and the header logo mark.

namespace stencil::gui {


  // Hold the row at the TALLER of its two labels (the badge's glyph line runs ~2px over the size
  // text), so which of them is up never decides the row. Twin-measured; cached until font/theme changes.
  void MainWindow::reserveImageInfoHeight() {
    if (!tools.imageSizeInfo) return;
    const QString key = tools.imageSizeInfo->font().key() + QLatin1Char('|') +
                        QString::number(tools.imageSizeInfo->font().pointSizeF()) +
                        QLatin1Char('|') + settings.themeMode + QLatin1Char('|') +
                        settings.accentColor;
    if (key != tools.imageInfoHeightKey || tools.imageSizeInfo->minimumHeight() <= 0) {
      const auto twinHeight = [](const QLabel& like, Qt::TextFormat format, const QString& text) {
        QLabel twin;
        twin.setFont(like.font());
        twin.setStyleSheet(like.styleSheet());
        twin.setContentsMargins(like.contentsMargins());   // sizeHint() honours these
        twin.ensurePolished();
        twin.setTextFormat(format);
        twin.setText(text);
        return twin.sizeHint().height();
      };
      int h = twinHeight(*tools.imageSizeInfo, Qt::PlainText,
                         QStringLiteral("Image Size: 8888 × 8888 px"));
      if (tools.incognitoTag) h = std::max(h, twinHeight(*tools.incognitoTag, Qt::RichText, incognitoTagHtml()));
      tools.imageInfoHeightKey = key;
      tools.imageSizeInfo->setFixedHeight(h);
    }
    // Both labels wear the reserved height whether they are showing or not, so a badge that has
    // never been measured yet cannot arrive taller than the row.
    if (tools.incognitoTag) tools.incognitoTag->setFixedHeight(tools.imageSizeInfo->minimumHeight());
    // The panel dock sits below the Image Size dock in the same stack, so no faked header gap.
    parts.theme.syncImageInfoDockHeight();
  }




  void MainWindow::updateImageSizeInfo() {
    QString size;
    if (canvas && canvas->hasImage()) {
      const bool isBlank = !docSource.blankColor.isEmpty();
      size = QString("Image Size: %1 × %2 px%3")
                 .arg(canvas->imageWidth())
                 .arg(canvas->imageHeight())
                 .arg(isBlank ? QStringLiteral("  ·  blank") : QString());
    } else {
      size = QStringLiteral("No image loaded");
    }
    if (tools.imageSizeInfo) {
      // The row's height is RESERVED for the taller state, so switching cannot resize the bar.
      reserveImageInfoHeight();
      tools.imageSizeInfo->setTextFormat(Qt::PlainText);
      tools.imageSizeInfo->setText(size);
    }
    // Browser parity (projectTitle.js updateInfo + .info-incognito): the badge is a widget of
    // its own beside the size. Our own text, never model output, so rich text is safe.
    if (tools.incognitoTag && incognito) tools.incognitoTag->setText(incognitoTagHtml());
    // The "?" carries the SAME size plus the incognito line — the collapsed state's only readout.
    if (tools.statusHint) {
      QString tip = size;
      if (incognito) tip += QStringLiteral("\nIncognito — not saved");
      tools.statusHint->setToolTip(tip);
    }
    // Last: it is what flies the badge, and it must photograph the text written above.
    parts.view.refreshStatusHintVisibility();
  }


}  // namespace stencil::gui
