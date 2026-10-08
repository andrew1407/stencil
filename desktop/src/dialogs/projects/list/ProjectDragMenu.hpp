#pragma once
// The Projects window's header while a row is held: a "⋯" beside the title pops that row's own
// menu up, lit under the pointer as a hover lights it, and the item the row is released on is
// taken; Close, armed by the project this window holds, takes the drop that closes it. Browser
// twins: ui/projects/list/dragMenu.js and ui/projects/list/dragClose.js.
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QTimer>
#include <functional>

class QAction;
class QLabel;
class QMenu;
class QPushButton;
class QToolButton;
class QWidget;

namespace stencil::gui {

  // The held row's "⋯" chip in global coords, carried by a held menu to the item it runs.
  inline constexpr const char* HELD_MENU_KEBAB_PROP = "stencilHeldKebab";
  // px of slack around the ⋯ and each open list: the gap between them is not a way out.
  inline constexpr int DRAG_MENU_SLACK_PX = 8;

  class ProjectDragMenu : public QObject {
   public:
    ProjectDragMenu(QLabel* title, QPushButton* closePill, QObject* parent);

    // Pops the held row's own menu up at `at` (global), its dust out of `from`, and hands it over.
    std::function<QMenu*(const QPoint& at, const QPoint& from)> openMenu;

    // A row was picked up; only the project this window holds arms Close.
    void begin(bool openHere);
    // The poll's step: true while `global` is on the ⋯, its menu or the armed Close.
    bool track(const QPoint& global);

    // `taken` runs once the drag loop is gone — a modal raised inside that loop never sees the
    // release — and `menu` is deleted after it.
    struct Release {
      QPointer<QAction> taken;
      QPointer<QMenu> menu;
      bool close = false;   // dropped on the armed Close
    };
    // The drag let go at `global`: the menu folds, the ⋯ leaves, Close is plain again.
    Release finish(const QPoint& global);
    // The release belonged to the header: no reorder, no zone.
    bool claimed() const { return took || closeTaken || overMenu; }

    QToolButton* button() const { return more; }
    bool eventFilter(QObject* watched, QEvent* ev) override;
    QMenu* menu() const { return rowMenu; }
    bool closeArmed() const { return armed; }
    // The ⋯'s dust clocks, ms (common/config/motion.json).
    static int moreInMs();
    static int moreOutMs();

   private:
    QMenu* levelAt(const QPoint& global) const;
    bool onMore(const QPoint& global) const;
    bool onClose(const QPoint& global) const;
    void hover(QMenu* level, QAction* act);
    void fold();
    void setOpen(bool open);
    void showMore(bool show);

    QPointer<QPushButton> closePill;
    QToolButton* more = nullptr;
    QPointer<QMenu> rowMenu;
    QPointer<QMenu> hoveredLevel;
    QPointer<QAction> hovered;
    QPointer<QAction> taken;
    QTimer poll;
    QPointer<QObject> entrance;   // the ⋯'s veil fade while its dust gathers
    bool armed = false;
    bool took = false;
    bool closeTaken = false;
    bool overMenu = false;
    bool glowOver = false;
  };

}  // namespace stencil::gui
