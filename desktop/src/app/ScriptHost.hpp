#pragma once
#include <QString>

namespace stencil::gui {

  class MainWindow;

  // The .stc script's two hosts, the Data section's Script window and the context menu's flyout,
  // and a script file run from a drop or the OS: both run through the SAME PlanTarget.
  class ScriptHost {
   public:
    explicit ScriptHost(MainWindow& w) : w(w) {}

    void openScript();
    // The QWidgetAction owns the panel, so the typed script survives the menu closing.
    void ensureScriptMenuPanel();
    void refreshAfterScript();
    void runScriptFromFile(const QString& path);

   private:
    MainWindow& w;
  };

}  // namespace stencil::gui
