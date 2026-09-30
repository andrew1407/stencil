// The "Make a copy" and script hand-off shots: the canvas menu's copy and script flyouts, the
// projects row menu's copy flyout, the copy confirmation for a local and a server project, the
// Lines tab's swatch picker, the settings' VS Code path and the confirm a linked script asks.
#include "captureShared.hpp"

#include "CanvasWidget.hpp"
#include "CopyProjectDialog.hpp"
#include "SelectionPanel.hpp"
#include "copyProjectMenu.hpp"
#include "../../../desktop/src/model/ScriptBuffer.hpp"

#include <QElapsedTimer>
#include <QListWidget>
#include <QMenu>
#include <QScrollArea>
#include <QTabBar>
#include <QtTest/QTest>

using namespace stencil::gui;

namespace {
  const QString SCRIPT = envOr("STENCIL_DOCS_SCRIPT", "@crop 8% 8% -8% -8%\n@filter sepia");
  const QString COPY_SERVER = envOr("STENCIL_DOCS_COPY_SERVER", "http://127.0.0.1:8090");
  const QString COPY_SOURCE = QStringLiteral("Kitchen plan");

  QMenu* popupMenu() { return qobject_cast<QMenu*>(QApplication::activePopupWidget()); }

  // The row whose text is `label` (mnemonics stripped), opened as a keyboard → does.
  QMenu* openSubmenu(QMenu* menu, const QString& label) {
    for (QAction* a : menu->actions()) {
      QString t = a->text();
      t.remove(QLatin1Char('&'));
      if (!a->menu() || !t.startsWith(label)) continue;
      menu->setActiveAction(a);
      QTest::keyClick(menu, Qt::Key_Right);
      if (!waitUntil([a] { return a->menu()->isVisible(); }, 1500))
        a->menu()->popup(menu->mapToGlobal(menu->actionGeometry(a).topRight()));
      waitUntil([a] { return a->menu()->isVisible(); }, 1500);
      pumpFor(250);
      return a->menu();
    }
    return nullptr;
  }

  // Right-click the canvas and hand its menu to `take` from inside the popup's own loop.
  void withCanvasMenu(MainWindow& win, const std::function<void(QMenu*)>& take) {
    auto* area = win.findChild<QScrollArea*>();
    bool done = false;
    QTimer::singleShot(0, [&] {
      QMenu* menu = nullptr;
      waitUntil([&menu] { return (menu = popupMenu()); }, 2000);
      if (menu) {
        pumpFor(200);
        take(menu);
      }
      done = true;
      while (QWidget* open = QApplication::activePopupWidget()) open->close();
    });
    QTimer::singleShot(4000, [] { while (QWidget* stuck = QApplication::activePopupWidget()) stuck->close(); });
    if (area) QTest::mouseClick(area->viewport(), Qt::RightButton, {}, QPoint(40, 40));
    waitUntil([&] { return done; }, 4500);
    pumpFor(150);
  }

  void canvasFlyout(MainWindow& win, const QString& name, const QString& row) {
    withCanvasMenu(win, [&](QMenu* menu) {
      QMenu* sub = openSubmenu(menu, row);
      if (!sub) std::printf("  %s SKIPPED (no \"%s\" row)\n", qPrintable(name), qPrintable(row));
      else saveLayers(name, &win, {menu, sub}, false);
    });
  }

  void copyDialog(MainWindow& win, const QString& name, const QString& server) {
    CopyProjectDialog dlg(&win, COPY_SOURCE,
                          QString::fromUtf8(stencil::support::copyScopeLabel(stencil::support::COPY_LAYOUT)),
                          COPY_SOURCE + QStringLiteral("-copy"), server);
    dlg.show();
    waitUntil([&dlg] { return dlg.isVisible(); }, 2000);
    pumpFor(400);
    saveOver(name, &win, &dlg);
    dlg.close();
    pumpFor(100);
  }

  // Every modal the step leaves behind (a declined confirm opens the Script window), closed.
  void closeModalsFor(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
      if (QWidget* open = QApplication::activeModalWidget()) open->close();
      pumpFor(50);
    }
  }
}  // namespace

void MainWindowGuiTest::copyScriptShots(MainWindow& win, const ShotSet& shots) {
  if (shots.has("context-menu-copy")) canvasFlyout(win, "context-menu-copy", QStringLiteral("Make a copy"));
  if (shots.has("context-menu-script")) {
    stencil::model::ScriptBuffer::instance().setText(SCRIPT);
    canvasFlyout(win, "context-menu-script", QStringLiteral("Stencil Script"));
  }

  // The projects window's row menu, opened on its first row the way a right-click opens it.
  if (shots.has("projects-row-copy")) {
    QAction* act = actionNamed(win, QStringLiteral("Projects…"));
    bool done = false;
    QTimer::singleShot(0, [&] {
      QWidget* dlg = nullptr;
      waitUntil([&] { dlg = QApplication::activeModalWidget(); return dlg && dlg->isVisible(); }, 4000);
      auto* list = dlg ? dlg->findChild<QListWidget*>() : nullptr;
      if (list && list->count()) {
        pumpFor(400);
        QTimer::singleShot(0, [&] {
          QMenu* menu = nullptr;
          waitUntil([&menu] { return (menu = popupMenu()); }, 2000);
          if (QMenu* sub = menu ? openSubmenu(menu, QStringLiteral("Make a copy")) : nullptr)
            saveLayers("projects-row-copy", &win, {dlg, menu, sub}, true);
          while (QWidget* open = QApplication::activePopupWidget()) open->close();
        });
        emit list->customContextMenuRequested(list->visualItemRect(list->item(0)).center());
      }
      done = true;
      if (auto* d = qobject_cast<QDialog*>(dlg)) d->reject(); else if (dlg) dlg->close();
    });
    QTimer::singleShot(9000, [] { if (QWidget* stuck = QApplication::activeModalWidget()) stuck->close(); });
    if (act) act->trigger();
    waitUntil([&] { return done; }, 9500);
    pumpFor(200);
  }

  if (shots.has("copy-dialog")) copyDialog(win, "copy-dialog", QString());
  if (shots.has("copy-dialog-server")) copyDialog(win, "copy-dialog-server", COPY_SERVER);

  // The Lines tab with its colour picker up, grown from the first line's swatch.
  if (shots.has("lines-swatch-picker") && win.selPanel) {
    if (auto* tabs = win.selPanel->findChild<QTabBar*>(QStringLiteral("selectionTabBar"))) tabs->setCurrentIndex(1);
    pumpFor(250);
    bool done = false;
    QTimer::singleShot(0, [&] {
      QWidget* dlg = nullptr;
      waitUntil([&] { dlg = QApplication::activeModalWidget(); return dlg && dlg->isVisible(); }, 4000);
      if (dlg) {
        pumpFor(500);
        saveLayers("lines-swatch-picker", &win, {win.selPanel, dlg}, false);
        if (auto* d = qobject_cast<QDialog*>(dlg)) d->reject(); else dlg->close();
      }
      done = true;
    });
    QTimer::singleShot(8000, [] { if (QWidget* stuck = QApplication::activeModalWidget()) stuck->close(); });
    emit win.selPanel->lineSwatchPick(0);
    waitUntil([&] { return done; }, 8500);
    if (auto* tabs = win.selPanel->findChild<QTabBar*>(QStringLiteral("selectionTabBar"))) tabs->setCurrentIndex(0);
    pumpFor(200);
  }

  // The question a script arriving by a stencil:// link asks before it runs. It exec()s from a
  // queued call, so a timer polls for it from inside that loop, then closes what declining opens.
  if (shots.has("script-link-confirm")) {
    bool grabbed = false;
    QTimer poll;
    poll.setInterval(50);
    QObject::connect(&poll, &QTimer::timeout, [&] {
      QWidget* dlg = QApplication::activeModalWidget();
      if (!dlg || !dlg->isVisible()) return;
      if (grabbed) { dlg->close(); return; }
      grabbed = true;
      pumpFor(400);
      saveOver("script-link-confirm", &win, dlg);
      if (auto* d = qobject_cast<QDialog*>(dlg)) d->reject(); else dlg->close();
    });
    poll.start();
    win.parts.scriptHost.adoptLinkedScript(SCRIPT, /*openOnly=*/false, std::nullopt);
    waitUntil([&] { return grabbed; }, 6000);
    pumpFor(1500);
    poll.stop();
    closeModalsFor(500);
  }
}
