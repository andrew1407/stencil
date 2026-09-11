#include "mainWindow.hpp"
#include "mainWindow.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "dockZonesOverlay.hpp"
#include "openImageDialog.hpp"
#include "guiHelpers.hpp"
#include "menuReveal.hpp"
#include "linksDialog.hpp"
#include "descriptionDialog.hpp"
#include "keywordsDialog.hpp"
#include "mediaLoader.hpp"
#include "projectsDialog.hpp"
#include "remoteSyncController.hpp"
#include "liveFeed.hpp"
#include "../support/controlReveal.hpp"
#include "../support/shimmerOverlay.hpp"

#include <QAction>
#include <QMenu>
#include <QTimer>
#include <algorithm>

// Spawning sibling windows, and the macOS Dock menu they share.

namespace stencil::gui {

  // Each opens a fresh, self-owned top-level window so the action is independent
  // of whichever window (or app-lifetime menu) triggered it.
  void MainWindow::openIncognitoWindow() {
    // restoreLast=false → a brand-new EMPTY editor, not the last session.
    auto* win = new MainWindow(nullptr, /*restoreLast=*/false);
    win->setAttribute(Qt::WA_DeleteOnClose);
    if (win->actIncognito_->isEnabled())
      win->actIncognito_->setChecked(true);  // no image yet → toggle allowed
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

  // Rebuild the macOS Dock menu: New Incognito Editor · Open Projects · the most
  // recently updated projects. Connected to qApp so the lambdas outlive the window
  // that built the menu. A no-op off macOS (QMenu::setAsDockMenu is macOS-only).
  void MainWindow::refreshDockMenu() {
#ifdef Q_OS_MACOS
    if (!sDockMenu_) {
      sDockMenu_ = new QMenu();  // app-lifetime; owned by neither window
      sDockMenu_->setAsDockMenu();
    }
    sDockMenu_->clear();
    connect(sDockMenu_->addAction("New Incognito Editor"), &QAction::triggered,
            qApp, [] { MainWindow::openIncognitoWindow(); });
    connect(sDockMenu_->addAction("Open Projects…"), &QAction::triggered, qApp,
            [] { MainWindow::openProjectsWindow(); });

    // Recent projects: the most recently updated, newest first (proxy for
    // "recently opened"). Each opens in its own window, leaving others untouched.
    std::vector<Project> recents = projectList_;
    std::sort(recents.begin(), recents.end(), [](const Project& a, const Project& b) {
      return a.meta.updatedAt > b.meta.updatedAt;
    });
    constexpr std::size_t kMaxRecents = 8;
    if (recents.size() > kMaxRecents) recents.resize(kMaxRecents);
    if (!recents.empty()) {
      sDockMenu_->addSeparator();
      for (const auto& pr : recents) {
        const QString id = QString::fromStdString(pr.meta.id);
        const QString name = QString::fromStdString(pr.meta.name);
        connect(sDockMenu_->addAction(name), &QAction::triggered, qApp,
                [id] { MainWindow::openProjectWindowById(id); });
      }
    }
#endif
  }

}  // namespace stencil::gui
