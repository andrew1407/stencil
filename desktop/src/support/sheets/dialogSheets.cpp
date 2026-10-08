#include "dialogSheets.hpp"

// The dialogs' sheet text; the widgets wearing each live under src/dialogs/.

namespace stencil::support {

  QString mutedTextSheet(const QColor& muted) { return QString("color: %1;").arg(muted.name()); }

  QString mutedHintSheet(const QColor& muted) { return mutedTextSheet(muted) + " font-size: 11px;"; }

  // browser .oi-status / .oi-crop-dims: the muted line under the picture.
  QString openImageMutedSheet() { return QString("color: gray; font-size: 11px;"); }

  QString paletteMidTextSheet() { return QString("color: palette(mid);"); }

  QString projectsHoverPreviewSheet() {
    return QString(
        "QLabel{background:#1e1e1e;border:2px solid #d4a017;"
        "border-radius:8px;padding:4px;}");
  }

  QString dropIndicatorSheet(const QColor& highlight) {
    return QStringLiteral("background:%1; border-radius:2px;").arg(highlight.name());
  }

  // Scoped by objectName: bare property rules bleed into the widget's own QToolTip.
  QString connGripSheet() { return QString("#connGrip { color: palette(mid); letter-spacing: -3px; }"); }

  QString connAdminTextSheet(const QColor& gold) { return QStringLiteral("color:%1;").arg(gold.name()); }

  // Browser .connect-batch-bar, value for value. min-height is the 14px line box: a QSS min-height IS
  // the minimumSizeHint, and at 0 a squeezed dialog crushes the buttons to their padding.
  QString connBatchBarSheet(const QColor& info, const QColor& border, const QColor& link) {
    return QStringLiteral("QWidget#connBatchBar{background:%1;border:1px solid %2;"
                          "border-radius:8px;}"
                          "QLabel#connBatchCount{font-weight:600;color:%3;}"
                          "QWidget#connBatchBar QPushButton{border-radius:4px;"
                          "padding:6px 10px;min-height:17px;}")
        .arg(info.name(), border.name(), link.name());
  }

  // Browser .connect-row, value for value (css/components/). The transient states come AFTER the
  // admin gold so they still win the border (its ring folded into a 2px border — box-shadow has no Qt spelling).
  QString connRowSheet(const ConnRowColors& c) {
    return QString(
               // The app-wide sheet pads every QListWidget::item by 4px with its own fill; the slot is nothing, the card everything.
               "QListWidget#connList{border:none;background:transparent;}"
               "QListWidget::item{padding:0;background:transparent;border:none;}"
               "QListWidget::item:hover,QListWidget::item:selected{background:transparent;}"
               // With no label Qt still reserves the app-wide `spacing: 7px` to the box's right.
               "QCheckBox#connRowSelect{spacing:0px;}"
               "QWidget#connRow,QWidget#connRowAdmin,QWidget#connRowExpired{"
               "border:1px solid %1;border-radius:8px;background:%2;}"
               "QWidget#connRowAdmin{border:2px solid %3;}"
               "QWidget#connRowExpired{border:1px solid %4;background:%5;}"
               "QWidget#connRow[selected=\"true\"],QWidget#connRowAdmin[selected=\"true\"],"
               "QWidget#connRowExpired[selected=\"true\"]{border-color:%6;background:%7;}"
               "QWidget#connRow[hovered=\"true\"],QWidget#connRowAdmin[hovered=\"true\"],"
               "QWidget#connRowExpired[hovered=\"true\"]{background:%7;}"
               // Compact filled chrome: 31x25 matches the browser only BECAUSE the border is none; the content box
               // is pinned to the 15px glyph so the expired row's action is the same size.
               "QPushButton[rowAction=\"true\"]{background:%6;border:none;"
               "border-radius:4px;color:#ffffff;padding:5px 8px;"
               "min-height:15px;max-height:15px;}"
               "QPushButton#rowReconnect,QPushButton#expiredReconnect,"
               "QPushButton#rowDisconnect,QPushButton#inviteBtn{"
               "min-width:15px;max-width:15px;}"
               "QPushButton[rowAction=\"true\"]:hover{background:%8;}"
               "QPushButton[rowAction=\"true\"]:pressed{background:%9;}"
               "QPushButton#rowDisconnect{background:%10;}"
               "QPushButton#rowDisconnect:hover{background:%11;}"
               "QPushButton#expiredReconnect{background:%4;color:#1f1f1f;}"
               "QPushButton#expiredReconnect:hover{background:%12;}")
        .arg(c.border.name(), c.input.name(), c.gold.name(), c.amber.name(),
             c.amberWash.name(), c.accent.name(), c.info.name(),
             c.accentHover.name(), c.accentPressed.name())
        .arg(c.danger.name(), c.dangerHover.name(), c.amberHover.name());
  }

}  // namespace stencil::support
