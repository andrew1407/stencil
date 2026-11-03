// The desktop-only switches the browser's Visuals has no row for, each placed after its neighbour in
// the menus buildMenus() made: File ▸ Autosave, View ▸ Use System Menu Bar, Project ▸ Open In Targets….
#include "MainWindow.hpp"
#include "MenuBuilder.hpp"
#include "Notifications.hpp"

#include <QMenu>
#include <QSignalBlocker>

namespace stencil::gui {

  namespace {
    QMenu* menuOf(QAction* a) {
      for (QObject* o : a->associatedObjects())
        if (auto* menu = qobject_cast<QMenu*>(o)) return menu;
      return nullptr;
    }

    void placeAfter(QAction* anchor, QAction* a) {
      QMenu* menu = menuOf(anchor);
      if (!menu) return;
      const QList<QAction*> rows = menu->actions();
      const int at = rows.indexOf(anchor);
      menu->insertAction(at + 1 < rows.size() ? rows[at + 1] : nullptr, a);
    }
  }  // namespace

  void MenuBuilder::addDesktopSwitches() {
    ActionsBuilder& build = w.parts.actionsBuilder;
    w.acts.autosave = build.newAction("Autosave", QString());
    w.acts.nativeMenuBar = build.newAction("Use System Menu Bar", QString());
    w.acts.openInTargets = build.newAction("Open In Targets…", QString());
    w.setActionTip(w.acts.autosave, "Automatically save the session as you edit");
    w.setActionTip(w.acts.nativeMenuBar,
        "On: the menu bar goes where your desktop puts it (the macOS menu bar, or a GNOME/Unity app menu). "
        "Off: it stays inside the window. Takes effect on restart.");
    w.setActionTip(w.acts.openInTargets, "Where Open In… hands a project off: the browser app URL and the Telegram bot");

    // Each persists at once, as Show Points does; a menu re-reads them as it opens, so a change
    // another window made is what it shows.
    const auto sync = [this] {
      for (auto [act, on] : {std::pair{w.acts.autosave, w.settings.autosave},
                             std::pair{w.acts.nativeMenuBar, w.settings.nativeMenuBar}}) {
        const QSignalBlocker quiet(act);
        act->setChecked(on);
      }
    };
    for (QAction* a : {w.acts.autosave, w.acts.nativeMenuBar}) a->setCheckable(true);
    sync();
    QObject::connect(w.acts.autosave, &QAction::toggled, &w, [this](bool on) {
      w.settings.autosave = on;
      w.persistSettings();
      if (on) w.scheduleAutosave();
    });
    QObject::connect(w.acts.nativeMenuBar, &QAction::toggled, &w, [this](bool on) {
      w.settings.nativeMenuBar = on;
      w.persistSettings();
      if (w.notify) w.notify->info(MainWindow::tr("The menu bar moves the next time Stencil starts"));
    });
    QObject::connect(w.acts.openInTargets, &QAction::triggered, &w, [this] { w.parts.dialogs.openInTargets(); });

    placeAfter(w.acts.saveSession, w.acts.autosave);
#ifndef Q_OS_WIN
    placeAfter(w.acts.settings, w.acts.nativeMenuBar);   // Windows has no global menu bar
#endif
    placeAfter(w.acts.openIn, w.acts.openInTargets);
    for (QAction* anchor : {w.acts.saveSession, w.acts.settings})
      if (QMenu* menu = menuOf(anchor)) QObject::connect(menu, &QMenu::aboutToShow, &w, sync);
  }

}  // namespace stencil::gui
