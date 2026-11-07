#include "SharedState.hpp"
#include "../../support/aqua/look.hpp"

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
    // A session Blue is never written; any other accent saved meanwhile is the user's pick and stands.
    std::optional<QString>& own = support::setAsideAccent();
    if (own && settings.accentColor != QLatin1String(support::AQUA_ACCENT)) own.reset();
    if (own) settings.accentColor = *own;
    fileStore::saveSettings(settings);
    emit settingsChanged(getSettings(), source);
  }

  Settings SharedState::getSettings() const {
    Settings worn = settings;
    if (support::setAsideAccent()) worn.accentColor = QString::fromLatin1(support::AQUA_ACCENT);
    return worn;
  }

}  // namespace stencil::gui
