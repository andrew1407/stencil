#include "mainWindow.hpp"
#include "remoteSession.hpp"
#include <QToolButton>
#include "mainWindow.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "dockZonesOverlay.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "canvasWidget.hpp"
#include "guiHelpers.hpp"
#include "menuReveal.hpp"
#include "menuShimmer.hpp"
#include "modalReveal.hpp"
#include "iconSet.hpp"
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
#include <QPalette>
#include <QTimer>

// Picking a project's colour.

namespace stencil::gui {


  QString MainWindow::activeProjectColor() const {
    if (activeProjectId_.isEmpty()) return {};
    for (const auto& p : projectList_)
      if (QString::fromStdString(p.meta.id) == activeProjectId_)
        return QString::fromStdString(p.meta.color);
    return {};
  }

  // The colour of the project this editor is bound to: the linked server record for a
  // server session (no local id), else the active local project. (Does not consider
  // incognito — callers that paint apply that gate themselves.)
  QString MainWindow::currentProjectColor() const {
    return !remoteSession_->link().id.isEmpty() ? remoteSession_->link().color : activeProjectColor();
  }

  std::optional<QString> MainWindow::normalizeProjectColor(const QString& color) const {
    if (color.isEmpty()) return QString();   // explicit clear → theme default
    const QColor c(color);
    if (!c.isValid()) return std::nullopt;   // reject an unparseable colour
    return c.name().toLower();               // canonical "#rrggbb" lower-case
  }

  void MainWindow::chooseProjectColor() {
    // Direct modal picker — identical to the line-colour button, which works cleanly. (Earlier
    // menu/InstantPopup/singleShot variants left a stray mouse grab that closed the dialog.)
    const QString cur = currentProjectColor();
    // No custom colour → seed with the neutral grey the name is actually painted in (the unset
    // default), not the theme accent, so the picker reflects the real current state.
    const QColor seed = (!cur.isEmpty() && QColor(cur).isValid())
                            ? QColor(cur)
                            : QColor("#80868f");
    // Non-native (helper) — the macOS shared NSColorPanel gets dismissed by our event
    // filters; Qt's own modal dialog stays put. Anchored on the 🎨 button that opened it.
    const QColor picked =
        support::pickColorAnimated(seed, this, "Project name color", nameBar_.colorBtn);
    if (!picked.isValid()) return;   // user cancelled
    setActiveProjectColor(picked.name());
  }

  // Browser-style 🎨 popup: with a custom colour set there are two real choices, so a tiny
  // menu offers "Choose colour…" and "Use theme default colour". Without one there is
  // nothing to clear — a one-row menu was a detour — so the picker opens directly.
  void MainWindow::showProjectColorMenu() {
    if (currentProjectColor().isEmpty()) {
      chooseProjectColor();
      return;
    }
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("projectColorMenu"));   // compact rows (theme.cpp)
    const QColor mtxt = palette().color(QPalette::WindowText);
    // Browser project-color-menu parity: each row carries its glyph (palette / ✕),
    // which also opts them into the shared menu icon motion.
    QAction* pick = menu.addAction(themedIcon("palette", mtxt, 15), "Choose color…");
    QAction* def = menu.addAction(themedIcon("x", mtxt, 15), "Use theme default color");
    // Same dust every other popup flies — this menu exec'd bare and just popped (user
    // report) — and the same glass shimmer the context menu's rows sweep (browser
    // .project-menu-item parity; icon motion rides the app-wide filter).
    support::MenuShimmer shimmer(&menu);
    support::revealMenuFrom(menu, nameBar_.colorBtn);
    QAction* chosen =
        menu.exec(nameBar_.colorBtn->mapToGlobal(QPoint(0, nameBar_.colorBtn->height())));
    if (chosen == pick) {
      // Defer so the menu's mouse grab is fully released before the modal picker opens — a live
      // grab is exactly what dismissed the dialog in the earlier direct-popup attempts.
      QTimer::singleShot(0, this, [this] { chooseProjectColor(); });
    } else if (chosen == def) {   // guard: dismissed menu yields null, which != def here
      setActiveProjectColor(QString());
    }
  }

}  // namespace stencil::gui
