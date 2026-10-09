#include "SharedState.hpp"

namespace stencil::gui {

  SharedState& SharedState::instance() {
    static SharedState state;
    return state;
  }

  void SharedState::load() {
    projects = fileStore::loadProjects();
    settings = fileStore::loadSettings();
  }

  void SharedState::saveProjects(QObject* source) {
    fileStore::saveProjects(projects);
    emit projectsChanged(source);
  }

  void SharedState::saveSettings(const Settings& s, QObject* source) {
    settings = s;
    fileStore::saveSettings(settings);
    emit settingsChanged(settings, source);
  }

}  // namespace stencil::gui
