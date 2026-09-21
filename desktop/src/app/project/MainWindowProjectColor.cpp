#include "MainWindow.hpp"
#include "RemoteSession.hpp"
#include <QToolButton>
#include "MainWindow.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "DockZonesOverlay.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "CanvasWidget.hpp"
#include "guiHelpers.hpp"
#include "menuReveal.hpp"
#include "MenuShimmer.hpp"
#include "modalReveal.hpp"
#include "iconSet.hpp"
#include "LinksDialog.hpp"
#include "DescriptionDialog.hpp"
#include "KeywordsDialog.hpp"
#include "MediaLoader.hpp"
#include "ProjectsDialog.hpp"
#include "RemoteSyncController.hpp"
#include "LiveFeed.hpp"
#include "../../support/control/reveal/controlReveal.hpp"
#include "../../support/motion/ShimmerOverlay.hpp"

#include <QAction>
#include <QMenu>
#include <QPalette>
#include <QTimer>

// Picking a project's colour.

namespace stencil::gui {


  QString MainWindow::activeProjectColor() const {
    if (activeProjectId.isEmpty()) return {};
    for (const auto& p : projectList)
      if (QString::fromStdString(p.meta.id) == activeProjectId)
        return QString::fromStdString(p.meta.color);
    return {};
  }

  // The linked server record for a server session, else the active local project; callers apply
  // the incognito gate.
  QString MainWindow::currentProjectColor() const {
    return !remoteSession->getLink().id.isEmpty() ? remoteSession->getLink().color : activeProjectColor();
  }

  std::optional<QString> MainWindow::normalizeProjectColor(const QString& color) const {
    if (color.isEmpty()) return QString();   // explicit clear → theme default
    const QColor c(color);
    if (!c.isValid()) return std::nullopt;   // reject an unparseable colour
    return c.name().toLower();               // canonical "#rrggbb" lower-case
  }

  void MainWindow::chooseProjectColor() {
    // Direct modal picker: menu/InstantPopup/singleShot variants left a stray mouse grab that
    // closed the dialog.
    const QString cur = currentProjectColor();
    // Seed with the neutral grey the name is painted in, not the accent.
    const QColor seed = (!cur.isEmpty() && QColor(cur).isValid())
                            ? QColor(cur)
                            : QColor("#80868f");
    // Non-native: the macOS shared NSColorPanel gets dismissed by our event filters.
    const QColor picked =
        support::pickColorAnimated(seed, this, "Project name color", nameBar.colorBtn);
    if (!picked.isValid()) return;   // user cancelled
    setActiveProjectColor(picked.name());
  }

  // With a custom colour set there are two real choices, so a menu; without one the picker opens
  // directly.
  void MainWindow::showProjectColorMenu() {
    if (currentProjectColor().isEmpty()) {
      chooseProjectColor();
      return;
    }
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("projectColorMenu"));   // compact rows (theme.cpp)
    const QColor mtxt = palette().color(QPalette::WindowText);
    // Browser project-color-menu parity; the glyphs opt the rows into the menu icon motion.
    QAction* pick = menu.addAction(themedIcon("palette", mtxt, 15), "Choose color…");
    QAction* def = menu.addAction(themedIcon("x", mtxt, 15), "Use theme default color");
    // The same dust and shimmer every other popup gets (browser .project-menu-item parity).
    support::MenuShimmer shimmer(&menu);
    support::revealMenuFrom(menu, nameBar.colorBtn);
    QAction* chosen =
        menu.exec(nameBar.colorBtn->mapToGlobal(QPoint(0, nameBar.colorBtn->height())));
    if (chosen == pick) {
      // Defer so the menu's mouse grab is released before the modal picker opens, or it dismisses
      // the dialog.
      QTimer::singleShot(0, this, [this] { chooseProjectColor(); });
    } else if (chosen == def) {   // guard: dismissed menu yields null, which != def here
      setActiveProjectColor(QString());
    }
  }

}  // namespace stencil::gui
