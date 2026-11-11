#include "menuDoubleClick.hpp"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMouseEvent>

namespace stencil::support {

  MenuDoubleClick::MenuDoubleClick(QMenu* root) : QObject(root) {
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, this, [this] { if (pending) fire(pending, false); });
    watch(root);
  }

  void MenuDoubleClick::watch(QMenu* menu) {
    if (!menu || menus.contains(menu)) return;
    menus.append(menu);
    menu->installEventFilter(this);
  }

  void MenuDoubleClick::add(QAction* action, std::function<void()> single, std::function<void()> dbl) {
    pairs.insert(action, {std::move(single), std::move(dbl)});
    // Only a key reaches `triggered`: the pointer's release is taken by the filter below.
    QObject::connect(action, &QAction::triggered, this, [this, action] { fire(action, false); });
  }

  bool MenuDoubleClick::eventFilter(QObject* obj, QEvent* event) {
    const QEvent::Type t = event->type();
    if (t != QEvent::MouseButtonRelease && t != QEvent::MouseButtonDblClick) return false;
    auto* menu = qobject_cast<QMenu*>(obj);
    auto* mouse = static_cast<QMouseEvent*>(event);
    QAction* action = menu ? menu->actionAt(mouse->position().toPoint()) : nullptr;
    if (!action || !pairs.contains(action)) return false;
    if (t == QEvent::MouseButtonDblClick) return true;
    if (pending == action && timer.isActive()) fire(action, true);
    else {
      pending = action;
      timer.start(QApplication::doubleClickInterval());
    }
    return true;
  }

  void MenuDoubleClick::fire(QAction* action, bool dbl) {
    timer.stop();
    pending = nullptr;
    const auto it = pairs.constFind(action);
    if (it == pairs.constEnd()) return;
    const std::function<void()> run = dbl ? it->dbl : it->single;
    for (auto m = menus.rbegin(); m != menus.rend(); ++m)
      if (*m) (*m)->close();
    // After the menus' own loop has ended: a share sheet anchored on a closing popup misplaces itself.
    QTimer::singleShot(0, qApp, run);
  }

}  // namespace stencil::support
