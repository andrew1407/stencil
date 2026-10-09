#include "MainWindow.hpp"
#include "SharedState.hpp"
#include "ProjectTitleController.hpp"
#include "CanvasWidget.hpp"
#include "Notifications.hpp"
#include "ServerClient.hpp"
#include "ProjectFlows.hpp"
#include "RemoteSession.hpp"
#include <QToolButton>
#include "menuReveal.hpp"
#include "MenuShimmer.hpp"
#include "iconSet.hpp"
#include "SiblingWindows.hpp"

// Picking a project's colour.

namespace stencil::gui {

  QString ProjectFlows::activeProjectColor() const {
    if (w.activeProjectId.isEmpty()) return {};
    for (const auto& p : w.projectList)
      if (QString::fromStdString(p.meta.id) == w.activeProjectId)
        return QString::fromStdString(p.meta.color);
    return {};
  }

  // The linked server record for a server session, else the active local project; callers apply
  // the incognito gate.
  QString ProjectFlows::currentProjectColor() const {
    return !w.remote.session->getLink().id.isEmpty() ? w.remote.session->getLink().color : activeProjectColor();
  }

  // Not the accent: an uncoloured name is painted in a neutral grey.
  QColor ProjectFlows::projectNameColor() const {
    const QColor own(currentProjectColor());
    return own.isValid() ? own : QColor("#80868f");
  }

  void ProjectFlows::chooseProjectColor() {
    // Direct modal picker: menu/InstantPopup/singleShot variants left a stray mouse grab that
    // closed the dialog. Non-native: the macOS shared NSColorPanel gets dismissed by our event filters.
    const QColor picked =
        support::pickColorAnimated(projectNameColor(), &w, "Project name color", w.nameBar.colorBtn);
    if (!picked.isValid()) return;   // user cancelled
    setActiveProjectColor(picked.name());
  }

  // With a custom colour set there are two real choices, so a menu; without one the picker opens
  // directly.
  void ProjectFlows::showProjectColorMenu() {
    if (currentProjectColor().isEmpty()) {
      chooseProjectColor();
      return;
    }
    QMenu menu(&w);
    menu.setObjectName(QStringLiteral("projectColorMenu"));   // compact rows (theme.cpp)
    const QColor mtxt = w.palette().color(QPalette::WindowText);
    // Browser project-color-menu parity; the glyphs opt the rows into the menu icon motion.
    QAction* pick = menu.addAction(themedIcon("palette", mtxt, 15), "Choose color…");
    QAction* def = menu.addAction(themedIcon("x", mtxt, 15), "Use theme default color");
    // The same dust and shimmer every other popup gets (browser .project-menu-item parity).
    support::MenuShimmer shimmer(&menu);
    support::revealMenuFrom(menu, w.nameBar.colorBtn);
    QAction* chosen =
        menu.exec(w.nameBar.colorBtn->mapToGlobal(QPoint(0, w.nameBar.colorBtn->height())));
    if (chosen == pick) {
      // Defer so the menu's mouse grab is released before the modal picker opens, or it dismisses
      // the dialog.
      QTimer::singleShot(0, &w, [this] { chooseProjectColor(); });
    } else if (chosen == def) {   // guard: dismissed menu yields null, which != def here
      setActiveProjectColor(QString());
    }
  }

  void ProjectFlows::setActiveProjectColor(const QString& color) {
    const auto norm = normalizeProjectColor(color);
    if (!norm) {
      w.notify->error("Invalid color");
      return;
    }
    // A server-linked session has no local id: push straight to the server.
    if (!w.remote.session->getLink().id.isEmpty()) {
      const QString n = *norm;
      QPointer<MainWindow> self(&w);
      setProjectColorById(w.remote.session->getLink().id, w.remote.session->getLink().address, n,
                            [this, self, n](bool ok) {
                              if (!self || !ok) return;
                              w.remote.session->getLink().color = n;
                              w.projectTitle->updateProjectTitle();
                            });
      return;
    }
    if (w.activeProjectId.isEmpty()) {
      w.notify->info("Open or save a project first");
      return;
    }
    QPointer<MainWindow> self(&w);
    setProjectColorById(w.activeProjectId, QString(), *norm,
                          [this, self](bool ok) { if (self && ok) w.projectTitle->updateProjectTitle(); });
  }

  void ProjectFlows::setActiveBlankColor() {
    if (w.docSource.blankColor.isEmpty() || !w.canvas->hasImage()) return;  // blanks only
    QColor init(w.docSource.blankColor);
    if (!init.isValid()) init = QColor("#ffffff");
    // Qt's own dialog, anchored on the Blank swatch button.
    const QColor c =
        support::pickColorAnimated(init, &w, "Blank background color", w.nameBar.blankColorBtn);
    if (!c.isValid()) return;
    applyBlankColor(c);
  }

  // Dialog-free; shared with the assistant's §10 blankColor op.
  void ProjectFlows::applyBlankColor(const QColor& c) {
    if (w.docSource.blankColor.isEmpty() || !w.canvas->hasImage() || !c.isValid()) return;  // blanks only
    // KEEPING the drawn lines (a separate overlay).
    const core::Lines keep = w.canvas->getLines();
    QImage img(w.canvas->imageWidth(), w.canvas->imageHeight(), QImage::Format_RGB32);
    img.fill(c);
    w.canvas->loadFromImage(img, /*keepZoom=*/true);   // same dimensions — nothing to refit
    w.docSource.setBytes({}, {});  // recoloured blank is synthetic → re-encode on bundle
    if (!keep.empty()) w.canvas->setLines(keep);
    w.docSource.blankColor = c.name();
    w.canvas->setBlankPage(true);  // loadFromImage reset the flag; still a blank
    // Persist into the local project's meta + raster; a server-linked session pushes on the next Save.
    if (Project* pr = w.findProject(w.activeProjectId.toStdString())) {
      pr->meta.blankColor = w.docSource.blankColor.toStdString();
      pr->meta.blank = true;
      if (!pr->imagePath.isEmpty()) w.canvas->getOriginalImage().save(pr->imagePath, "PNG");
      SharedState::instance().saveProjects(&w);
    }
    w.refreshActions();
  }

  void ProjectFlows::setProjectColorById(const QString& id, const QString& serverUrl,
                                       const QString& color, std::function<void(bool)> done) {
    const auto norm = normalizeProjectColor(color);
    if (!norm) {
      w.notify->error("Invalid color");
      if (done) done(false);
      return;
    }
    // Version-guarded PUT; refresh our linked version so a later save doesn't 409.
    if (!serverUrl.isEmpty()) {
      stencil::net::ServerClient* c = w.remote.session->requireClient(serverUrl);
      if (!c) { if (done) done(false); return; }
      const QString n = *norm;
      const qint64 before = w.remote.session->getLink().version;
      QPointer<MainWindow> self(&w);
      w.remote.session->putVersionGuardedAsync(
          c, id,
          [c, id, n](qint64 version, std::function<void(bool, qint64, bool)> cb) {
            c->updateProjectColorAsync(id, n, version, cb);
          },
          [this, self, c, id, serverUrl, n, before, done](bool ok, qint64 newVersion) {
            if (!self) return;
            if (!ok) {
              w.notify->error(QString("Color update failed: %1").arg(c->lastError()));
              if (done) done(false);
              return;
            }
            if (w.remote.session->getLink().id == id && w.remote.session->getLink().address == serverUrl)
              adoptOwnFileVersion(w.remote.session->getLink(), before, newVersion);
            w.notify->success(n.isEmpty() ? QStringLiteral("Color reset to theme default")
                                         : QString("Color set to %1").arg(n));
            if (done) done(true);
          });
      return;
    }
    Project* pr = w.findProject(id.toStdString());
    if (!pr) { if (done) done(false); return; }
    pr->meta.color = norm->toStdString();
    SharedState::instance().saveProjects(&w);
    SiblingWindows::refreshDockMenu(w.projectList);
    w.notify->success(norm->isEmpty() ? QStringLiteral("Color reset to theme default")
                                     : QString("Color set to %1").arg(*norm));
    if (done) done(true);
  }

  std::optional<QString> ProjectFlows::normalizeProjectColor(const QString& color) const {
    if (color.isEmpty()) return QString();   // explicit clear → theme default
    const QColor c(color);
    if (!c.isValid()) return std::nullopt;   // reject an unparseable colour
    return c.name().toLower();               // canonical "#rrggbb" lower-case
  }
}  // namespace stencil::gui
