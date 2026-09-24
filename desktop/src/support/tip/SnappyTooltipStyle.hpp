#pragma once
// The app style, wrapped so tooltips wake at the browser's pace (controlTooltip.js SHOW_DELAY_MS)
// whichever base style is installed — Fusion at boot, Windows under the webcore skin.
#include <QApplication>
#include <QLatin1String>
#include <QProxyStyle>
#include <QString>
#include <QStyleFactory>

namespace stencil::support {

  class SnappyTooltipStyle : public QProxyStyle {
   public:
    using QProxyStyle::QProxyStyle;
    int styleHint(StyleHint hint, const QStyleOption* opt = nullptr,
                  const QWidget* w = nullptr,
                  QStyleHintReturn* ret = nullptr) const override {
      if (hint == SH_ToolTip_WakeUpDelay) return 200;   // ms
      if (hint == SH_ToolTip_FallAsleepDelay) return 0;
      return QProxyStyle::styleHint(hint, opt, w, ret);
    }
  };

  // The factory key the app wears; a stylesheet hides the style behind its own private class.
  inline QString& installedStyleKey() { static QString key; return key; }

  // False when the factory has no style of that key; the app keeps what it wears.
  inline bool installAppStyle(const char* key) {
    QStyle* base = QStyleFactory::create(QLatin1String(key));
    if (!base) return false;
    QApplication::setStyle(new SnappyTooltipStyle(base));
    installedStyleKey() = QLatin1String(key);
    return true;
  }

}  // namespace stencil::support
