#include "windowSheets.hpp"

// The editor window's per-widget sheet text; the widgets wearing each live under src/app/ and src/canvas/.

namespace stencil::support {

  QString statusHintSheet() {
    return QString(
        "QLabel#statusHint{color:rgba(154,160,168,0.75);font-size:11px;font-weight:600;"
        "border:1px solid rgba(154,160,168,0.45);border-radius:9px;background:transparent;}");
  }

  QString sectionLabelSheet(double pt) {
    return QString("font-size:%1pt;font-weight:700;").arg(pt);
  }

  QString logoButtonSheet() {
    return QString("QToolButton{border:none;background:transparent;padding:2px;}");
  }

  QString formulaErrorSheet() { return QString("color:#d9534f;"); }

  QString compareLabelSheet() { return QString("padding-right: 2px;"); }

  QString coordStatusSheet() { return QString("font-family: monospace;"); }

  QString multiSelectLabelSheet() { return QString("color: palette(highlight); font-weight: 600;"); }

  // The padding eats the indicator's overrun instead of the neighbour's label.
  QString viewToggleSheet() { return QStringLiteral("padding-right:12px;"); }

  QString imageSizeInfoSheet(const QString& ink) { return QStringLiteral("color:%1;").arg(ink); }

  QString contextToggleSheet(const QColor& text, const QString& checkPath, const QString& dotPath) {
    return QStringLiteral(
               "QCheckBox::indicator,QRadioButton::indicator{width:15px;height:15px;"
               "border:1px solid %1;background:transparent;}"
               "QCheckBox::indicator{border-radius:4px;}"
               "QRadioButton::indicator{border-radius:8px;}"
               "QCheckBox::indicator:checked{image:url(\"%2\");}"
               "QRadioButton::indicator:checked{image:url(\"%3\");}")
        .arg(text.name(), checkPath, dotPath);
  }

  QString projectNameEditingSheet(const QString& fg, const QColor& accent) {
    return QString("QLineEdit{color:%1;font-weight:600;border:1px solid %2;border-radius:6px;"
                   "background:palette(base);padding:2px 6px;}"
                   "QLineEdit:focus{border:1px solid %2;}")
        .arg(fg, accent.name());
  }

  // The hover ring has to live HERE: this per-widget sheet outranks the app-wide one, so the themed
  // `QLineEdit#projectNameField:hover` never applied. Same ring strength as the browser's two stacked 45% layers.
  QString projectNameRestingSheet(const QString& fg, const QColor& accent) {
    const QString ring = QString("rgba(%1,%2,%3,0.45)")
                             .arg(accent.red())
                             .arg(accent.green())
                             .arg(accent.blue());
    return QString("QLineEdit{color:%1;font-weight:600;border:1px solid transparent;"
                   "border-radius:6px;background:transparent;padding:3px 8px;}"
                   "QLineEdit:hover{border:2px solid %2;padding:2px 7px;}"
                   "QLineEdit:focus{border:1px solid transparent;}")
        .arg(fg, ring);
  }

  QString colorSwatchSheet(const gui::Palette& pal, bool labelled, bool skin) {
    return QStringLiteral(
               "QToolButton{background:%1;border:1px solid %2;border-radius:%6px;"
               "color:%4;padding:0 %5px;}"
               "QToolButton:hover{border-color:%3;}")
        .arg(pal.inputBg.name(), pal.borderMain.name(), pal.accent.name(),
             pal.textMain.name(), labelled ? QStringLiteral("6") : QStringLiteral("0"),
             skin ? QStringLiteral("0") : QStringLiteral("7"));
  }

  QString faceLeftPadSheet(int px) { return QStringLiteral("QToolButton{padding-left:%1px;}").arg(px); }

  QString canvasTooltipSheet() {
    return QString(
        "#canvasTooltip { background:#222; color:#eee; border:1px solid #555;"
        " border-radius:4px; }"
        " #canvasTooltip QLabel { color:#eee; padding:4px 8px; }");
  }

}  // namespace stencil::support
