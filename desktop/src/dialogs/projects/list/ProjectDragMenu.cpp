#include "ProjectDragMenu.hpp"
#include "ReorderableListWidget.hpp"
#include "iconDrag.hpp"
#include "iconSet.hpp"
#include "theme.hpp"

#include <QBoxLayout>
#include <QCursor>
#include <QDropEvent>
#include <QMimeData>
#include <QStyle>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>

// The drag-time ⋯ with its release-picked menu, and Close as the open project's drop target.

namespace stencil::gui {

  namespace {
    constexpr int MENU_GAP_PX = 6;   // the browser row menu's gap under its anchor
    constexpr int POLL_MS = 16;      // as ProjectDragZones: a modal's drag loop delivers no moves here

    bool usable(const QAction* a) { return a && !a->isSeparator() && a->isEnabled() && !a->menu(); }

    void restyle(QWidget* w) {
      w->style()->unpolish(w);
      w->style()->polish(w);
    }
  }  // namespace

  ProjectDragMenu::ProjectDragMenu(QLabel* title, QPushButton* closePill, QObject* parent)
      : QObject(parent), closePill(closePill) {
    QWidget* header = title ? title->parentWidget() : nullptr;
    more = new QToolButton(header);
    more->setObjectName(QStringLiteral("projectsDragMore"));
    // The browser's ⋯ is an opaque accent button: the toolbar's own accent chip (toolButtons.qss).
    more->setProperty("nameAffordance", true);
    more->setIconSize(QSize(15, 15));
    more->setFocusPolicy(Qt::NoFocus);   // a target for a held row only, never a tab stop
    more->setAccessibleName(QStringLiteral("More actions for the dragged project"));
    more->hide();
    if (auto* row = header ? qobject_cast<QBoxLayout*>(header->layout()) : nullptr)
      row->insertWidget(row->indexOf(title) + 1, more);
    // Each target takes the drop it owns, so a release there never slides the row back home.
    for (QWidget* target : {static_cast<QWidget*>(more), static_cast<QWidget*>(closePill)})
      if (target) {
        target->setAcceptDrops(true);
        target->installEventFilter(this);
      }
    poll.setInterval(POLL_MS);
    connect(&poll, &QTimer::timeout, this, [this] { track(QCursor::pos()); });
  }

  void ProjectDragMenu::begin(bool openHere) {
    took = closeTaken = overMenu = glowOver = false;
    taken = nullptr;
    armed = openHere && closePill;
    // No taller than Close, so its arrival never moves the header.
    if (closePill && closePill->height() > 0) more->setFixedHeight(closePill->height());
    more->setIcon(themedIcon(QStringLiteral("more"), onAccentInk(more->palette().color(QPalette::Highlight)), 15));
    setOpen(false);
    showMore(true);
    support::markDropTarget(closePill, armed);
    poll.start();
  }

  bool ProjectDragMenu::track(const QPoint& global) {
    if (took || closeTaken) return false;
    const bool overClose = armed && onClose(global);
    if (armed && overClose != glowOver) support::markDropTarget(closePill, true, glowOver = overClose);
    QMenu* level = levelAt(global);
    if (!onMore(global) && !level) {
      fold();
      return overClose;
    }
    if (!rowMenu && openMenu) {
      // Placed under the ⋯'s left edge; its dust gathers out of, and pours back into, the ⋯'s centre.
      rowMenu = openMenu(more->mapToGlobal(QPoint(0, more->height() + MENU_GAP_PX)),
                         more->mapToGlobal(more->rect().center()));
      setOpen(!rowMenu.isNull());
      if (rowMenu) {
        QList<QMenu*> levels = rowMenu->findChildren<QMenu*>(Qt::FindChildrenRecursively);
        levels << rowMenu.data();
        for (QMenu* m : levels) {
          m->setAcceptDrops(true);
          m->installEventFilter(this);
        }
      }
      level = levelAt(global);
    }
    hover(level, level ? level->actionAt(level->mapFromGlobal(global)) : nullptr);
    return true;
  }

  ProjectDragMenu::Release ProjectDragMenu::finish(const QPoint& global) {
    poll.stop();
    if (!took && !closeTaken) {
      QMenu* level = levelAt(global);
      overMenu = level || onMore(global);
      QAction* act = level ? level->actionAt(level->mapFromGlobal(global)) : nullptr;
      if (usable(act)) {
        took = true;
        taken = act;
      } else {
        closeTaken = !overMenu && armed && onClose(global);
      }
    }
    Release out;
    out.taken = taken;
    out.menu = took ? rowMenu : nullptr;
    out.close = closeTaken;
    fold();
    rowMenu = nullptr;   // a taken item's menu is the caller's now
    armed = false;
    support::markDropTarget(closePill, false);
    showMore(false);
    return out;
  }

  QMenu* ProjectDragMenu::levelAt(const QPoint& global) const {
    if (!rowMenu || !rowMenu->isVisible()) return nullptr;
    const auto near = [&global](const QMenu* m) {
      const int s = DRAG_MENU_SLACK_PX;
      return m && m->isVisible() && m->geometry().adjusted(-s, -s, s, s).contains(global);
    };
    // The open flyout first: it is on top where the two meet.
    QAction* opener = rowMenu->activeAction();
    QMenu* sub = opener ? opener->menu() : nullptr;
    if (near(sub)) return sub;
    return near(rowMenu) ? rowMenu.data() : nullptr;
  }

  bool ProjectDragMenu::onMore(const QPoint& global) const {
    if (!more->isVisible()) return false;
    const int s = DRAG_MENU_SLACK_PX;
    return QRect(more->mapToGlobal(QPoint(0, 0)), more->size()).adjusted(-s, -s, s, s).contains(global);
  }

  bool ProjectDragMenu::onClose(const QPoint& global) const {
    return closePill && closePill->isVisible() && closePill->rect().contains(closePill->mapFromGlobal(global));
  }

  // Lit as a real hover lights it; an opener pops its list up. Nothing runs until the release.
  void ProjectDragMenu::hover(QMenu* level, QAction* act) {
    if (level == hoveredLevel && act == hovered) return;
    hoveredLevel = level;
    hovered = act;
    if (level) level->setActiveAction(act);
  }

  // Folds the menu and its flyout through their own exit; a taken item's menu lives until it ran.
  void ProjectDragMenu::fold() {
    hoveredLevel = nullptr;
    hovered = nullptr;
    setOpen(false);
    if (!rowMenu) return;
    for (QMenu* sub : rowMenu->findChildren<QMenu*>()) sub->hide();
    rowMenu->hide();
    if (took) return;
    rowMenu->deleteLater();
    rowMenu = nullptr;
  }

  // The browser's ⋯ wears accent-2 while its menu is up (.projects-drag-more.is-open).
  void ProjectDragMenu::setOpen(bool open) {
    if (more->property("open").toBool() == open) return;
    more->setProperty("open", open);
    restyle(more);
  }

  // A row's drag over the ⋯, its menu, or the Close it armed is accepted, as the browser's
  // dragover accepts it there; what the release does is finish()'s.
  bool ProjectDragMenu::eventFilter(QObject* watched, QEvent* ev) {
    const QEvent::Type t = ev->type();
    if (t != QEvent::DragEnter && t != QEvent::DragMove && t != QEvent::Drop) return QObject::eventFilter(watched, ev);
    auto* drop = static_cast<QDropEvent*>(ev);
    if (!drop->mimeData()->hasFormat(reorderRowMime())) return QObject::eventFilter(watched, ev);
    if (watched == closePill && !armed) drop->ignore();
    else drop->acceptProposedAction();
    return true;
  }

}  // namespace stencil::gui
