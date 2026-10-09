// The dialog shots over a saved project with lines on the canvas, and the drag-out zones the
// window paints while a project row is dragged out of the Projects list.
#include "captureShared.hpp"

#include "CanvasWidget.hpp"
#include "OpenImageDialog.hpp"
#include "ProjectDragZones.hpp"
#include "../../../desktop/src/model/ScriptBuffer.hpp"
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QtTest/QTest>

using namespace stencil::gui;

namespace {
  const QString SCRIPT = envOr("STENCIL_DOCS_SCRIPT", "@crop 8% 8% -8% -8%\n@filter sepia");
}  // namespace

// The tab the "from a link" scenario is about. Typed, not set: setText alone never fires
// textEdited, so Preview would stay off.
void MainWindowGuiTest::openImageUrlShot(MainWindow& win, const QString& name) {
  bool done = false;
  QTimer::singleShot(0, [&] {
    auto* dlg = qobject_cast<OpenImageDialog*>(QApplication::activeModalWidget());
    if (!dlg) return;
    dlg->tabs->setCurrentIndex(1);
    QTest::keyClicks(dlg->url, faviconUrl());
    waitUntil([dlg] { return dlg->previewBtn->isEnabled(); }, 2000);
    dlg->previewBtn->click();
    if (!waitUntil([dlg] { return !dlg->previewedImage().isNull(); }, 12000))
      std::printf("  %s: the URL did not preview\n", qPrintable(name));
    pumpFor(500);
    saveOver(name, &win, dlg);
    done = true;
    dlg->reject();
  });
  QTimer::singleShot(16000, [] { if (QWidget* stuck = QApplication::activeModalWidget()) stuck->close(); });
  win.parts.sourceOpener.openImage();
  waitUntil([&] { return done; }, 17000);
  pumpFor(200);
}

void MainWindowGuiTest::dialogShots(MainWindow& win, const QString& theme, const ShotSet& shots) {
  auto* canvas = win.findChild<CanvasWidget*>();
  // The meta windows are gated on a SAVED project: give it keywords and no description, and keep
  // the lines on the canvas — every window dims and blurs what it covers (support/ModalBackdrop).
  canvas->setLines({line({{140, 150}, {520, 260}, {330, 520}}, "#c81e1e", 4, "solid"),
                    line({{610, 170}, {860, 540}}, "#1e63c8", 3, "dashed"),
                    line({{200, 560}, {780, 600}}, "#2e9e4f", 3, "dotted")});
  {
    win.projectList.erase(std::remove_if(win.projectList.begin(), win.projectList.end(),
                                          [](const stencil::gui::Project& p) {
                                            return p.meta.id == "usecases-doc";
                                          }),
                           win.projectList.end());
    stencil::gui::Project pr;
    pr.meta.id = "usecases-doc";
    pr.meta.name = "Kitchen plan";
    // Keywords, but NO description: the description window is photographed empty, showing
    // the prompt that says what to type there.
    pr.meta.keywords = {"kitchen", "floor plan", "survey", "draft", "north wall"};
    win.projectList.push_back(pr);
    win.activeProjectId = "usecases-doc";
    win.refreshActions();
  }
  fit(win);

  const struct { const char* action; QString name; } dialogs[] = {
    {"Visuals & Settings…", suffixed("settings-dialog", theme)},
    {"Projects…", suffixed("projects-dialog", theme)},
    {"Crop Image…", QStringLiteral("crop-dialog")},
    {"Servers…", QStringLiteral("connect-dialog")},
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
  if (shots.has("open-image-dialog")) openImageUrlShot(win, QStringLiteral("open-image-dialog"));

  // The drag-out zones (dialogs/projects/list/ProjectDragZones.hpp): the overlay the WINDOW paints while a
  // project row is dragged out of the list. They live only for the drag, and the dialog's own
  // blocking loop delivers no drag-move events — the widget polls QCursor::pos() at 16ms — so the
  // pointer is parked in a zone and the poll given ticks to read it.
  const QString zonesShot = suffixed("projects-dropzones", theme);
  if (shots.has(zonesShot)) {
    QAction* act = actionNamed(win, QStringLiteral("Projects…"));
    if (!act) std::printf("  %s SKIPPED (no action)\n", qPrintable(zonesShot));
    else {
      bool done = false;
      QTimer::singleShot(0, [&] {
        QWidget* dlg = nullptr;
        waitUntil([&] { dlg = QApplication::activeModalWidget(); return dlg && dlg->isVisible(); }, 4000);
        if (!dlg || !win.overlays.projectZones) return;
        pumpFor(400);
        win.overlays.projectZones->begin(dlg);
        // Top-left quadrant of the canvas, clear of the centred dialog: the "Open here" zone.
        const QRect canvas = win.scroll->viewport()->rect();
        const QPoint here = win.scroll->viewport()->mapToGlobal(
            QPoint(canvas.width() / 6, canvas.height() / 5));
        QCursor::setPos(here);
        pumpFor(120);   // ~7 poll ticks, so hover has read the parked cursor
        saveDragOver(zonesShot, &win, win.overlays.projectZones, dlg);
        win.overlays.projectZones->end();
        done = true;
        if (auto* d = qobject_cast<QDialog*>(dlg)) d->reject(); else dlg->close();
      });
      QTimer::singleShot(8000, [] { if (QWidget* stuck = QApplication::activeModalWidget()) stuck->close(); });
      act->trigger();
      waitUntil([&] { return done; }, 9000);
      pumpFor(200);
    }
  }
}
