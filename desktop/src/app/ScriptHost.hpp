#pragma once
#include <QString>

#include <functional>
#include <optional>

namespace stencil::gui {

  class MainWindow;
  struct ScriptRunResult;

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

    /* A script a stencil:// link carried, once the picture it named has landed (`pictureBefore`
     * is the canvas original's cacheKey before that load; none = no picture was asked for): it
     * opens in the Script window, whatever `scriptMode` the link names — a link never runs it. */
    void adoptLinkedScript(const QString& text, std::optional<qint64> pictureBefore);

   private:
    void reportRun(const ScriptRunResult& result) const;

    MainWindow& w;
  };

}  // namespace stencil::gui
