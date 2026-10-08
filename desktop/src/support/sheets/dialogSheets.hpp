#pragma once
// The dialogs' per-widget style sheets: the muted hint lines, the projects list's hover glance and
// drop line, and the connections window's rows and batch bar (browser .connect-row / .connect-batch-bar).
#include <QColor>
#include <QString>

namespace stencil::support {

  QString mutedTextSheet(const QColor& muted);
  QString mutedHintSheet(const QColor& muted);
  QString openImageMutedSheet();
  QString paletteMidTextSheet();
  QString projectsHoverPreviewSheet();
  QString dropIndicatorSheet(const QColor& highlight);
  QString connGripSheet();
  QString connAdminTextSheet(const QColor& gold);
  QString connBatchBarSheet(const QColor& info, const QColor& border, const QColor& link);

  // The connections list's row colours, by role.
  struct ConnRowColors {
    QColor border, input, gold, amber, amberWash, accent, info, accentHover, accentPressed;
    QColor danger, dangerHover, amberHover;
  };
  QString connRowSheet(const ConnRowColors& c);

}  // namespace stencil::support
