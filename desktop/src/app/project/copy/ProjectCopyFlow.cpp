// The copy's entry points (browser ui/projects/row/copyItem.js, ui/modal/copyProjectModal.js): the
// confirmation every one of them opens, the toolbar Image button's scope menu, and the projects
// window's rows, which "Just copy" leaves up on the new row.
#include "ProjectCopy.hpp"
#include "MainWindow.hpp"
#include "CopyProjectDialog.hpp"
#include "Notifications.hpp"
#include "ProjectsDialog.hpp"
#include "MenuShimmer.hpp"
#include "menuReveal.hpp"
#include "modalReveal.hpp"
#include "guiHelpers.hpp"

#include <QListWidget>
#include <QMenu>
#include <QPalette>
#include <QPointer>
#include <QTimer>
#include <QToolButton>

namespace stencil::gui {

  namespace {
    constexpr CopyOpen OPEN_OF[] = {COPY_OPEN_NONE, COPY_OPEN_HERE, COPY_OPEN_NEW_WINDOW};   // by Outcome

    QListWidgetItem* rowOf(QListWidget* list, const QString& id, const QString& server) {
      for (int i = 0; list && i < list->count(); ++i) {
        QListWidgetItem* it = list->item(i);
        if (it->data(Qt::UserRole).toString() == id && it->data(Qt::UserRole + 1).toString() == server) return it;
      }
      return nullptr;
    }
  }  // namespace

  void ProjectCopy::offer(const CopyRequest& from, const QRect& closeRect,
                          std::function<void(QString, CopyOpen)> done, QWidget* over) {
    QString server;
    const QString name = sourceName(from, &server);
    CopyProjectDialog dlg(over ? over : &w, name, QString::fromUtf8(support::copyScopeLabel(from.what)),
                          copyNameAmong({}, name.toStdString()), server);
    // Out of a projects row it stacks over that window and flies back into the row's chip.
    int answer = 0;
    if (closeRect.isValid()) {
      support::revealDialog(dlg, nullptr, support::gestureAnchorRect(), closeRect);
      answer = dlg.exec();
    } else {
      // From the toolbar or the canvas menu it closes into the toolbar's copy button, or up when folded.
      QPointer<MainWindow> self(&w);
      QPointer<CopyProjectDialog> live(&dlg);
      w.pop.dialogCloseRect = [self, live](bool) {
        QWidget* b = self ? self->buttonForAction(self->acts.copyProject) : nullptr;
        if (b && b->isVisible() && b->width() > 0) return QRect(b->mapToGlobal(QPoint(0, 0)), b->size());
        return live ? support::riseRect(*live) : QRect();
      };
      answer = w.execMaybePopover(dlg, w.acts.copyProject);
    }
    if (answer != QDialog::Accepted) return;
    CopyRequest req = from;
    req.open = OPEN_OF[dlg.getOutcome()];
    req.incognito = dlg.getIncognito();
    req.local = dlg.getLocal();
    QPointer<MainWindow> self(&w);
    run(req, [this, self, req, done](bool ok, QString id, QString) {
      if (!self || !ok) return;
      w.notify->success(req.incognito ? QStringLiteral("Opened an incognito copy") : QStringLiteral("Copy made"));
      if (done) done(id, req.open);
    });
  }

  void ProjectCopy::showToolbarMenu() {
    QWidget* button = w.buttonForAction(w.acts.copyProject);
    QMenu menu(&w);
    support::CopyScope picked = support::COPY_LAYOUT;
    bool chose = false;
    support::addCopyScopes(menu, w.palette().color(QPalette::WindowText), 15,
                           [&picked, &chose](support::CopyScope s) { picked = s; chose = true; });
    support::MenuShimmer shimmer(&menu);
    gui::compactIconMenu(menu);
    const QPoint at = button ? button->mapToGlobal(QPoint(0, button->height())) : QCursor::pos();
    if (button) support::revealMenuFrom(menu, button);
    menu.exec(at);
    if (!chose) return;
    // Deferred so the menu's mouse grab is gone before the modal opens (ProjectFlows::showProjectColorMenu).
    QTimer::singleShot(0, &w, [this, picked] {
      CopyRequest req;
      req.what = picked;
      offer(req);
    });
  }

  void ProjectCopy::wireProjectsList(ProjectsDialog& dlg) {
    QPointer<ProjectsDialog> live(&dlg);
    QObject::connect(&dlg, &ProjectsDialog::copyRequested, &w,
                     [this, live](const QString& id, const QString& server, int scope, const QRect& closeRect) {
      QListWidget* list = live ? live->findChild<QListWidget*>() : nullptr;
      CopyRequest from;
      from.id = id;
      from.serverUrl = server;
      from.what = static_cast<support::CopyScope>(scope);
      if (QListWidgetItem* it = rowOf(list, id, server)) from.name = it->data(Qt::UserRole + 3).toString();
      offer(from, closeRect, [this, live](const QString& newId, CopyOpen open) {
        if (!live) return;
        if (open == COPY_OPEN_HERE) return live->reject();   // the editor moved on to the copy
        live->setProjects(w.projectList);
        QListWidget* rows = live->findChild<QListWidget*>();
        if (QListWidgetItem* it = rowOf(rows, newId, QString())) {
          rows->setCurrentItem(it);
          rows->scrollToItem(it);
        }
      }, live.data());
    });
  }

}  // namespace stencil::gui
