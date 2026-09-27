#include "SiblingWindows.hpp"
#include "MainWindow.hpp"

#include <QMenu>

// Spawning sibling windows, and the macOS Dock menu they share.

namespace stencil::gui {

  // Each opens a self-owned top-level window, independent of the window that triggered it.
  void SiblingWindows::openIncognitoWindow() {
    // restoreLast=false → a brand-new empty editor.
    auto* win = new MainWindow(nullptr, /*restoreLast=*/false);
    win->setAttribute(Qt::WA_DeleteOnClose);
    if (win->acts.incognito->isEnabled())
      win->acts.incognito->setChecked(true);  // no image yet → toggle allowed
    win->show();
  }

  void SiblingWindows::openProjectsWindow() {
    auto* win = new MainWindow();
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->show();
    QTimer::singleShot(0, win, [win] { win->parts.projects.openProjects(); });
  }

  void SiblingWindows::openProjectWindowById(const QString& id) {
    auto* win = new MainWindow();
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->show();
    if (!win->loadProjectIntoCanvas(id)) win->close();
  }

  // Connected to qApp so the lambdas outlive the window that built the menu. A no-op off macOS.
  void SiblingWindows::refreshDockMenu([[maybe_unused]] const std::vector<Project>& projects) {
#ifdef Q_OS_MACOS
    // Last setAsDockMenu wins; owned by the app, not any window, so a closing window never dangles it.
    static QMenu* sDockMenu = nullptr;
    if (!sDockMenu) {
      sDockMenu = new QMenu();  // app-lifetime; owned by neither window
      sDockMenu->setAsDockMenu();
    }
    sDockMenu->clear();
    QObject::connect(sDockMenu->addAction("New Incognito Editor"), &QAction::triggered,
            qApp, [] { openIncognitoWindow(); });
    QObject::connect(sDockMenu->addAction("Open Projects…"), &QAction::triggered, qApp,
            [] { openProjectsWindow(); });

    // Most recently updated first; each opens in its own window.
    std::vector<Project> recents = projects;
    std::sort(recents.begin(), recents.end(), [](const Project& a, const Project& b) {
      return a.meta.updatedAt > b.meta.updatedAt;
    });
    constexpr std::size_t MAX_RECENTS = 8;
    if (recents.size() > MAX_RECENTS) recents.resize(MAX_RECENTS);
    if (!recents.empty()) {
      sDockMenu->addSeparator();
      for (const auto& pr : recents) {
        const QString id = QString::fromStdString(pr.meta.id);
        const QString name = QString::fromStdString(pr.meta.name);
        QObject::connect(sDockMenu->addAction(name), &QAction::triggered, qApp,
                [id] { openProjectWindowById(id); });
      }
    }
#endif
  }

}  // namespace stencil::gui
