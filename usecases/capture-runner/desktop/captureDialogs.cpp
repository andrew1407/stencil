// The dialog shots over a saved project with lines on the canvas, the console beside the editor,
// the drag-out zones the window paints while a project row is dragged out of the Projects list,
// and a collaboration server: the Servers dialog connected and its projects in the list.
#include "captureShared.hpp"

#include "CanvasWidget.hpp"
#include "ConsoleWindow.hpp"
#include "SharedState.hpp"
#include "ServerClient.hpp"
#include "connectionStore.hpp"
#include "fileStore.hpp"
#include <QDateTime>
#include "OpenImageDialog.hpp"
#include "ProjectDragZones.hpp"
#include "../../../desktop/src/model/ScriptBuffer.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QTabWidget>
#include <QtTest/QTest>

using namespace stencil::gui;

namespace {
  const QString SCRIPT = envOr("STENCIL_DOCS_SCRIPT", "@crop 8% 8% -8% -8%\n@filter sepia");

  // A dialog that exec()s from `act`, handed to `take` once `ready` holds inside its own loop.
  void grabWhen(QAction* act, const std::function<bool(QWidget*)>& ready,
                const std::function<void(QWidget*)>& take) {
    bool done = false;
    QTimer::singleShot(0, [&] {
      QWidget* dlg = nullptr;
      waitUntil([&] { dlg = QApplication::activeModalWidget(); return dlg && dlg->isVisible(); }, 4000);
      if (dlg && waitUntil([&] { return ready(dlg); }, 12000)) take(dlg);
      done = true;
      if (auto* d = qobject_cast<QDialog*>(dlg)) d->reject(); else if (dlg) dlg->close();
    });
    QTimer::singleShot(16000, [] { if (QWidget* stuck = QApplication::activeModalWidget()) stuck->close(); });
    if (act) act->trigger();
    waitUntil([&] { return done; }, 17000);
    pumpFor(200);
  }
}  // namespace

// The URL tab with Stencil's icon typed in (setText alone never fires textEdited, so Preview would
// stay off) and previewed, then opened here with Crop unticked, so the whole picture lands.
void MainWindowGuiTest::openImageUrlShot(MainWindow& win, const ShotSet& shots) {
  auto* canvas = win.findChild<CanvasWidget*>();
  bool done = false;
  QTimer::singleShot(0, [&] {
    auto* dlg = qobject_cast<OpenImageDialog*>(QApplication::activeModalWidget());
    if (!dlg) return;
    dlg->tabs->setCurrentIndex(1);
    QTest::keyClicks(dlg->url, iconUrl());
    waitUntil([dlg] { return dlg->previewBtn->isEnabled(); }, 2000);
    // A cold first fetch can outlast one wait: Preview is pressed again once before giving up.
    bool previewed = false;
    for (int attempt = 0; attempt < 2 && !previewed; ++attempt) {
      dlg->previewBtn->click();
      previewed = waitUntil([dlg] { return !dlg->previewedImage().isNull(); }, 20000);
    }
    if (!previewed) std::printf("  open-image-dialog: the URL did not preview\n");
    pumpFor(500);
    if (shots.has("open-image-dialog")) saveOver("open-image-dialog", &win, dlg);
    done = true;
    if (!previewed) { dlg->reject(); return; }
    if (dlg->cropPage) dlg->cropPage->setChecked(false);
    dlg->act.here->click();
  });
  QTimer::singleShot(50000, [] { if (QWidget* stuck = QApplication::activeModalWidget()) stuck->close(); });
  win.parts.sourceOpener.openImage();
  waitUntil([&] { return done; }, 52000);
  if (shots.has("open-from-url")) {
    if (waitUntil([canvas] { return canvas->hasImage() && canvas->getImage().width() == 512; }, 12000)) {
      fit(win);
      save("open-from-url", &win);
    } else {
      std::printf("  open-from-url SKIPPED (the URL did not load whole)\n");
    }
  }
  pumpFor(200);
}

void MainWindowGuiTest::dialogShots(MainWindow& win, const ShotSet& shots) {
  auto* canvas = win.findChild<CanvasWidget*>();
  win.parts.sourceOpener.createBlankImage(Qt::white, 960, 640);
  waitUntil([canvas] { return canvas->hasImage(); });
  // The meta windows are gated on a SAVED project: "Kitchen plan", with its keywords and a
  // description, and the lines on the canvas — every window dims and blurs what it covers.
  const stencil::core::Lines plan = {line({{140, 150}, {520, 260}, {330, 520}}, "#c81e1e", 4, "solid"),
                                     line({{610, 170}, {860, 540}}, "#1e63c8", 3, "dashed"),
                                     line({{200, 560}, {780, 600}}, "#2e9e4f", 3, "dotted")};
  canvas->setLines(plan);
  {
    // Only the seeded projects and this one: the blank pages this run made are not the story.
    win.projectHost.projectList.erase(std::remove_if(win.projectHost.projectList.begin(), win.projectHost.projectList.end(),
                                         [](const Project& p) { return p.meta.id != "p1" && p.meta.id != "p2"; }),
                          win.projectHost.projectList.end());
    Project pr;
    pr.meta.id = "usecases-doc";
    pr.meta.name = "Kitchen plan";
    pr.meta.keywords = {"kitchen", "floor plan", "survey", "draft", "north wall"};
    pr.meta.description = "Ground-floor kitchen, measured on site. Red marks the new worktop, blue the "
                          "window wall, green the skirting to replace.";
    pr.meta.source = "https://example.com/survey/kitchen-plan.png";
    pr.meta.resource = "https://example.com/survey/";
    pr.meta.createdAt = pr.meta.updatedAt = QDateTime::currentMSecsSinceEpoch();
    // Its own picture and lines on disk, so the list draws its row even while another project is open.
    pr.imagePath = QDir(fileStore::stateDir()).filePath(QStringLiteral("kitchen-plan.png"));
    canvas->getImage().save(pr.imagePath, "PNG");
    pr.lines = plan;
    win.projectHost.projectList.push_back(pr);
    SharedState::instance().saveProjects(&win);
    win.projectHost.activeProjectId = "usecases-doc";
    fileStore::markProjectOpened(win.projectHost.activeProjectId, QDateTime::currentMSecsSinceEpoch());
    win.refreshActions();
  }
  fit(win);

  const struct { const char* action; QString name; } dialogs[] = {
    {"Visuals & Settings…", QStringLiteral("visuals-dialog")},
    {"Projects…", QStringLiteral("projects-dialog")},
    {"Keyboard Shortcuts…", QStringLiteral("shortcuts-dialog")},
    {"Stencil Script…", QStringLiteral("script-dialog")},
    {"AI Assistant Settings…", QStringLiteral("assistant-settings-dialog")},
    {"Controls & Shortcuts Info", QStringLiteral("help-dialog")},
    {"Project Description…", QStringLiteral("description-dialog")},
    {"Project Keywords…", QStringLiteral("keywords-dialog")},
    {"Image Links…", QStringLiteral("links-dialog")},
    {"Open In…", QStringLiteral("open-in-dialog")},
  };
  for (const auto& dialog : dialogs) {
    if (!shots.has(dialog.name)) continue;
    // The editor reads the session buffer as it is built, so the window opens on a real script
    // and its Run / Copy / Download gate open with it.
    if (dialog.name == QLatin1String("script-dialog")) stencil::model::ScriptBuffer::instance().setText(SCRIPT);
    grabModal(win, actionNamed(win, QString::fromUtf8(dialog.action)), dialog.name);
  }

  // The console is non-modal: moved off the picture, it answers a typed /help with the editor live
  // and undimmed behind it, the listing shown from its first group once it has typed out.
  if (shots.has("console-dialog")) {
    win.parts.console.openConsole();
    ConsoleWindow* console = win.parts.console.consoleWindow();
    waitUntil([&] { return console && console->isVisible(); }, 4000);
    console->move(win.geometry().topLeft() + QPoint(win.width() - console->width() - 24,
                                                    win.height() - console->height() - 56));
    pumpFor(300);
    console->submit(QStringLiteral("/help"));
    for (QString was; was != console->outputText(); pumpFor(400)) was = console->outputText();
    if (auto* out = console->findChild<QPlainTextEdit*>()) out->verticalScrollBar()->setValue(0);
    pumpFor(200);
    saveTops(QStringLiteral("console-dialog"), &win, {console});
    win.parts.console.closeConsole();
    pumpFor(400);
  }

  // The drag-out zones (dialogs/projects/list/ProjectDragZones.hpp) poll QCursor::pos(): the pointer is
  // parked in "Open here" and given the poll's ticks, so that zone shows hovered.
  if (shots.has("projects-dropzones")) {
    grabWhen(actionNamed(win, QStringLiteral("Projects…")), [](QWidget*) { return true; }, [&](QWidget* dlg) {
      if (!win.overlays.projectZones) return;
      pumpFor(400);
      win.overlays.projectZones->begin(dlg);
      // Top-left quadrant of the canvas, clear of the centred dialog: the "Open here" zone.
      const QRect area = win.scroll->viewport()->rect();
      QCursor::setPos(win.scroll->viewport()->mapToGlobal(QPoint(area.width() / 6, area.height() / 5)));
      pumpFor(120);
      saveDragOver(QStringLiteral("projects-dropzones"), &win, win.overlays.projectZones, dlg);
      win.overlays.projectZones->end();
    });
  }
}

void MainWindowGuiTest::serverShots(MainWindow& win, const ShotSet& shots) {
  if (!shots.hasAny({QStringLiteral("connect-dialog"), QStringLiteral("projects-server")})) return;
  // Dialled straight from what desktop.mjs handed over, as Servers… does with a typed URL and token.
  const QString url = qEnvironmentVariable("STENCIL_DOCS_SERVER_URL");
  const QString token = qEnvironmentVariable("STENCIL_DOCS_SERVER_TOKEN");
  if (url.isEmpty() || token.isEmpty()) { std::printf("  server shots SKIPPED (no server handed over)\n"); return; }
  stencil::net::connectionStore::setAutoConnect(true);
  stencil::net::ConnectionManager* mgr = win.ensureConnections();
  if (!mgr->find(url))
    mgr->connectToAsync(url, token, [](bool, QString) {}, stencil::net::ServerClient::kindFromTag(QStringLiteral("session")));
  if (!waitUntil([&] {
        const stencil::net::ServerClient* c = mgr->find(url);
        return c && c->getStatus() == stencil::net::ServerClient::Status::CONNECTED;
      }, 15000)) {
    std::printf("  server shots SKIPPED (%s did not connect)\n", qPrintable(url));
    return;
  }
  pumpFor(400);
  clearToasts(&win);
  if (shots.has("connect-dialog")) grabModal(win, actionNamed(win, QStringLiteral("Servers…")), "connect-dialog");
  // The server's copy of "Floor survey" beside the local one, outlined in gold: the list is searched by
  // name for it, so whatever else the shared server holds stays out of the picture.
  if (shots.has("projects-server")) {
    const QString seeded = envOr("STENCIL_DOCS_SERVER_PROJECT", "Floor survey");
    const auto serverRow = [seeded](QWidget* dlg) -> QListWidgetItem* {
      auto* list = dlg->findChild<QListWidget*>();
      for (int i = 0; list && i < list->count(); ++i) {
        QListWidgetItem* it = list->item(i);
        if (!it->data(Qt::UserRole + 1).toString().isEmpty() && it->data(Qt::UserRole + 3).toString() == seeded) return it;
      }
      return nullptr;
    };
    grabWhen(actionNamed(win, QStringLiteral("Projects…")), [&](QWidget* dlg) { return serverRow(dlg) != nullptr; },
             [&](QWidget* dlg) {
      for (auto* mode : dlg->findChildren<QComboBox*>())
        if (const int names = mode->findData(QStringLiteral("names")); names >= 0) mode->setCurrentIndex(names);
      for (auto* field : dlg->findChildren<QLineEdit*>())
        if (field->placeholderText().startsWith(QStringLiteral("Search"))) QTest::keyClicks(field, seeded);
      waitUntil([&] { QListWidgetItem* it = serverRow(dlg); return it && !it->data(Qt::UserRole + 2).isNull(); }, 8000);
      pumpFor(800);
      saveOver(QStringLiteral("projects-server"), &win, dlg);
    });
  }
}
