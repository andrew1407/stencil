#include "MainWindow.hpp"
#include "MainWindow.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "DockZonesOverlay.hpp"
#include "OpenImageDialog.hpp"
#include "guiHelpers.hpp"
#include "menuReveal.hpp"
#include "LinksDialog.hpp"
#include "DescriptionDialog.hpp"
#include "KeywordsDialog.hpp"
#include "MediaLoader.hpp"
#include "ProjectsDialog.hpp"
#include "RemoteSyncController.hpp"
#include "LiveFeed.hpp"
#include "../../support/control/controlReveal.hpp"
#include "../../support/motion/ShimmerOverlay.hpp"

#include <QAction>
#include <QMenu>
#include <QTimer>
#include <algorithm>

// Spawning sibling windows, and the macOS Dock menu they share.

namespace stencil::gui {

  // Each opens a self-owned top-level window, independent of the window that triggered it.
  void MainWindow::openIncognitoWindow() {
    // restoreLast=false → a brand-new empty editor.
    auto* win = new MainWindow(nullptr, /*restoreLast=*/false);
    win->setAttribute(Qt::WA_DeleteOnClose);
    if (win->actIncognito->isEnabled())
      win->actIncognito->setChecked(true);  // no image yet → toggle allowed
    win->show();
  }

  void MainWindow::openProjectsWindow() {
    auto* win = new MainWindow();
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->show();
    QTimer::singleShot(0, win, &MainWindow::openProjects);
  }

  void MainWindow::openProjectWindowById(const QString& id) {
    auto* win = new MainWindow();
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->show();
    if (!win->loadProjectIntoCanvas(id)) win->close();
  }

  // Connected to qApp so the lambdas outlive the window that built the menu. A no-op off macOS.
  void MainWindow::refreshDockMenu() {
#ifdef Q_OS_MACOS
    if (!sDockMenu) {
      sDockMenu = new QMenu();  // app-lifetime; owned by neither window
      sDockMenu->setAsDockMenu();
    }
    sDockMenu->clear();
    connect(sDockMenu->addAction("New Incognito Editor"), &QAction::triggered,
            qApp, [] { MainWindow::openIncognitoWindow(); });
    connect(sDockMenu->addAction("Open Projects…"), &QAction::triggered, qApp,
            [] { MainWindow::openProjectsWindow(); });

    // Most recently updated first; each opens in its own window.
    std::vector<Project> recents = projectList;
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
        connect(sDockMenu->addAction(name), &QAction::triggered, qApp,
                [id] { MainWindow::openProjectWindowById(id); });
      }
    }
#endif
  }

}  // namespace stencil::gui
