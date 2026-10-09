#pragma once
#include "fileStore.hpp"
#include <QObject>
#include <vector>

namespace stencil::gui {

  // The one project registry and Settings every window of the process reads: loaded once, written
  // only from here, so a window never serialises a sibling's stale copy. Browser twin: the storage
  // every tab shares (js/core/storage/storage.js); a sibling window re-applies on `changed`.
  class SharedState : public QObject {
    Q_OBJECT
   public:
    static SharedState& instance();

    // Reads both files; the first window of a process calls it, a sibling never does.
    void load();
    std::vector<Project>& getProjects() { return projects; }
    const Settings& getSettings() const { return settings; }

    // The registry as it is now; `source` is the window that changed it, which skips its own echo.
    void saveProjects(QObject* source);
    void saveSettings(const Settings& s, QObject* source);

   signals:
    void projectsChanged(QObject* source);
    void settingsChanged(const Settings& s, QObject* source);

   private:
    std::vector<Project> projects;
    Settings settings;
  };

}  // namespace stencil::gui
