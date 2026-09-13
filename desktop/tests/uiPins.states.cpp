// The rendered appearance pins — see uiPins.headless.cpp for what they cover and how
// the baselines are rewritten.
#include "MainWindow.hpp"
#include "CanvasWidget.hpp"
#include "ChatDock.hpp"
#include "ConnectDialog.hpp"
#include "connectionStore.hpp"
#include "CropDialog.hpp"
#include "fileStore.hpp"
#include "ProjectsDialog.hpp"
#include "SelectionPanel.hpp"
#include "SettingsDialog.hpp"
#include "uiPins.states.hpp"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QImage>
#include <QMenu>
#include <QScrollArea>
#include <QTimer>
#include <QtTest/QTest>
#include <string>
#include <utility>
#include <vector>

using namespace stencil::gui;

#include "support/uiPin.hpp"

// One MainWindow serves every window state; the dialogs are built standalone below
// (their real callers exec() them, which would block this loop).
void pinWindowStates(const QString& png) {
  MainWindow win(nullptr, /*restoreLast=*/false);
  win.resize(1280, 860);
  win.show();
  if (!QTest::qWaitForWindowExposed(&win)) {
    check(false, "the window came up");
    return;
  }
  pumpFor(120);

  pin("toolbar-default", &win);
  pin("idle-card-empty-canvas", win.centralWidget());

  // A loaded image plus one selected line: the selection panel's populated state.
  win.openPathFromOS(png);
  auto* canvas = win.findChild<CanvasWidget*>();
  check(canvas && waitUntil([canvas] { return canvas && canvas->hasImage(); }), "the image loaded");
  if (canvas) {
    stencil::core::Line line;
    line.points = {{40, 30}, {180, 110}, {200, 40}};
    canvas->setLines({line});
    canvas->selectLineByIndex(0);
    pumpFor(120);
  }
  clearToasts(&win);
  pin("selection-panel-with-line", win.findChild<SelectionPanel*>());

  // The canvas context menu — popped from a right-click, grabbed and closed from a
  // timer inside its own popup loop.
  {
    auto* area = win.findChild<QScrollArea*>();
    QWidget* viewport = area ? area->viewport() : nullptr;
    clearToasts(&win);
    bool opened = false;
    QTimer::singleShot(0, [&opened] {
      QMenu* menu = nullptr;
      waitUntil([&menu] { return (menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())); },
                2000);
      if (!menu) return;
      opened = true;
      pin("context-menu-open", menu);
      menu->close();
    });
    // exec()s its own loop: never leave it standing if the grab above missed it.
    QTimer::singleShot(3000, [] {
      if (QWidget* stuck = QApplication::activePopupWidget()) stuck->close();
    });
    if (viewport) QTest::mouseClick(viewport, Qt::RightButton, {}, QPoint(6, 6));
    waitUntil([&opened] { return opened; }, 2500);
    check(opened, "the context menu opened");
    if (!opened) ++shotCount;   // keep the count honest when the menu never came
  }

  // The chat dock, docked right and floating.
  auto* chat = win.findChild<ChatDock*>();
  check(chat != nullptr, "the window has a chat dock");
  if (chat) {
    win.addDockWidget(Qt::RightDockWidgetArea, chat, Qt::Horizontal);
    chat->setFloating(false);
    chat->show();
    waitUntil([chat] { return chat->isVisible(); });
    pumpFor(150);
    clearToasts(&win);
    pin("chat-dock-right", chat);

    chat->setFloating(true);
    chat->resize(420, 560);
    waitUntil([chat] { return chat->isFloating() && chat->isVisible(); });
    pumpFor(150);
    pin("chat-dock-floating", chat);
    chat->setFloating(false);
    chat->hide();
    pumpFor(80);
  }

  // The logo's accent-preset popover (openAccentPicker) — an exec()'d QDialog, so it is
  // grabbed and closed from a timer running inside its own modal loop.
  {
    QAction* accent = win.findChild<QAction*>(QStringLiteral("actAccent"));
    check(accent != nullptr, "the window has the Theme Color action");
    bool opened = false;
    if (accent) {
      QTimer::singleShot(0, [&opened] {
        QDialog* pop = nullptr;
        waitUntil([&pop] {
          pop = qobject_cast<QDialog*>(QApplication::activeModalWidget());
          return pop && pop->objectName() == QLatin1String("accentPopover");
        }, 2000);
        if (!pop) return;
        opened = true;
        pin("accent-picker", pop);
        pop->reject();
      });
      QTimer::singleShot(3000, [] {
        if (QDialog* stuck = qobject_cast<QDialog*>(QApplication::activeModalWidget())) stuck->reject();
      });
      accent->trigger();
    }
    check(opened, "the accent picker opened");
    if (!opened) ++shotCount;
    pumpFor(80);
  }

  // Fullscreen: the toolbars retract and the canvas takes the window.
  QAction* fullscreen = nullptr;
  for (QAction* a : win.findChildren<QAction*>())
    if (a->text().contains(QLatin1String("Fullscreen"))) fullscreen = a;
  check(fullscreen != nullptr, "the window has a Fullscreen action");
  if (fullscreen) {
    fullscreen->trigger();
    pumpFor(400);
    clearToasts(&win);
    pin("fullscreen-bars-hidden", &win);
    fullscreen->trigger();
    pumpFor(200);
  } else {
    ++shotCount;
  }
}

void pinDialogs(const QString& png) {
  {
    std::vector<Project> projects;
    const std::pair<const char*, long long> rows[] = {{"alpha", 3000}, {"beta", 2000}, {"gamma", 1000}};
    int n = 0;
    for (const auto& [name, updatedAt] : rows) {
      Project pr;
      pr.meta.id = "p" + std::to_string(++n);
      pr.meta.name = name;
      pr.meta.updatedAt = updatedAt;
      projects.push_back(pr);
    }
    ProjectsDialog dlg(projects, /*now=*/5000);
    dlg.resize(880, 620);
    dlg.show();
    waitUntil([&dlg] { return dlg.isVisible(); });
    pumpFor(150);
    pin("projects-dialog", &dlg);
    dlg.reject();
  }

  {
    Settings s;
    s.themeMode = QStringLiteral("light");
    SettingsDialog dlg(s);
    dlg.resize(760, 700);
    dlg.show();
    waitUntil([&dlg] { return dlg.isVisible(); });
    pumpFor(150);
    pin("settings-dialog", &dlg);
    dlg.reject();
  }

  {
    QImage original(png);
    CropDialog dlg(original, 21.0, 29.7, /*album=*/true, {});
    dlg.resize(720, 620);
    dlg.show();
    waitUntil([&dlg] { return dlg.isVisible(); });
    pumpFor(150);
    pin("crop-dialog", &dlg);
    dlg.reject();
  }

  {
    stencil::net::ConnectionManager mgr;
    ConnectDialog dlg(&mgr);
    dlg.resize(680, 560);
    dlg.show();
    waitUntil([&dlg] { return dlg.isVisible(); });
    pumpFor(150);
    pin("connect-dialog", &dlg);
    dlg.reject();
  }
}
