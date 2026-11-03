#pragma once
#include <QHash>
#include <QRect>
#include <QString>

class QWidget;

namespace stencil::gui {

  class MainWindow;

  // The settings windows the menus open: Settings, the assistant's own settings, keyboard
  // shortcuts, the Info window and the Open In targets.
  class SettingsDialogs {
   public:
    explicit SettingsDialogs(MainWindow& w) : w(w) {}

    void openSettings();
    void openAssistantSettings();   // a named pair, NOT an overload: taken by address in connect()s
    void openAssistantSettingsFrom(QWidget* anchor, const QRect& anchorRect = QRect());
    void openInfo();
    void openShortcuts();
    void openInTargets();   // the browser app URL and Telegram bot Open In… hands off to
    void applyHotkeyOverrides(const QHash<QString, QString>& overrides);

   private:
    MainWindow& w;
  };

}  // namespace stencil::gui
