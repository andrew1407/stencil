// The window states behind usecases/docs/desktop/img, in the order one window passes through
// them: the blank fills, an image from a URL and the context menu here, then the assistant, the
// dialogs and the video (capture{Assistant,Dialogs,Video}.cpp); and the theme-swap frames.
#include "captureShared.hpp"

#include "CanvasWidget.hpp"

#include <QMenu>
#include <QScrollArea>
#include <QtTest/QTest>

using namespace stencil::gui;

namespace {
  const QString FAVICON_URL = envOr("STENCIL_DOCS_FAVICON_URL",
      "https://raw.githubusercontent.com/andrew1407/stencil/main/browser/favicon.svg");
}  // namespace

void MainWindowGuiTest::fit(MainWindow& win) {
  pumpFor(150);
  win.acts.fit->trigger();
  pumpFor(150);
  clearToasts(&win);
}

void MainWindowGuiTest::windowStates(const QString& theme, const ShotSet& shots) {
  MainWindow win(nullptr, /*restoreLast=*/false);
  win.resize(1280, 860);
  win.show();
  if (!QTest::qWaitForWindowExposed(&win)) return;
  pumpFor(250);
  auto* canvas = win.findChild<CanvasWidget*>();
  const QString editorEmpty = suffixed("editor-empty", theme);
  if (shots.has(editorEmpty)) save(editorEmpty, &win);

  win.parts.sourceOpener.createBlankImage(Qt::white, 960, 640);
  waitUntil([canvas] { return canvas->hasImage(); });
  fit(win);
  if (shots.has("blank-white")) save("blank-white", &win);

  const QString linesSelection = suffixed("lines-selection", theme);
  if (shots.has(linesSelection)) {
    canvas->setLines({line({{120, 110}, {520, 260}, {300, 520}}, "#c81e1e", 4, "solid"),
                      line({{620, 140}, {840, 560}}, "#1e63c8", 3, "dashed")});
    canvas->selectLineByIndex(0);
    pumpFor(150);
    save(linesSelection, &win);
    canvas->setLines({});
  }

  if (shots.has("blank-black")) {
    win.parts.sourceOpener.createBlankImage(Qt::black, 960, 640);
    fit(win);
    save("blank-black", &win);
    win.parts.sourceOpener.createBlankImage(Qt::white, 960, 640);
    fit(win);
  }

  if (shots.has("open-from-url")) {
    const QImage before = canvas->getImage();
    win.parts.sourceOpener.openSourceHere(FAVICON_URL, 0, false);
    if (waitUntil([&] { return canvas->hasImage() && canvas->getImage().size() != before.size(); }, 12000)) {
      fit(win);
      save("open-from-url", &win);
    } else {
      std::printf("  open-from-url SKIPPED (the URL did not load)\n");
    }
    win.parts.sourceOpener.createBlankImage(Qt::white, 960, 640);
    fit(win);
  }

  // The canvas context menu, grabbed from inside its own popup loop.
  if (shots.has("context-menu")) {
    auto* area = win.findChild<QScrollArea*>();
    bool opened = false;
    QTimer::singleShot(0, [&] {
      QMenu* menu = nullptr;
      waitUntil([&menu] { return (menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())); }, 2000);
      if (!menu) return;
      opened = true;
      pumpFor(200);
      saveOver("context-menu", &win, menu);
      menu->close();
    });
    QTimer::singleShot(3000, [] { if (QWidget* stuck = QApplication::activePopupWidget()) stuck->close(); });
    if (area) QTest::mouseClick(area->viewport(), Qt::RightButton, {}, QPoint(40, 40));
    waitUntil([&] { return opened; }, 2500);
    pumpFor(100);
  }

  assistantShots(win, theme, shots);
  dialogShots(win, theme, shots);
  videoShots(win, shots);

  if (shots.has("accent-picker"))
    grabModal(win, win.findChild<QAction*>(QStringLiteral("actAccent")), "accent-picker");
}

// The theme wipe (SWAP_MS + the dust's life) at ~30 fps: its own pass, at 1x and with
// motion on, because a 2x grab outlasts a frame.
void MainWindowGuiTest::themeClip() {
  MainWindow win(nullptr, /*restoreLast=*/false);
  win.resize(1280, 860);
  win.show();
  if (!QTest::qWaitForWindowExposed(&win)) return;
  pumpFor(250);
  auto* canvas = win.findChild<CanvasWidget*>();
  win.parts.sourceOpener.createBlankImage(Qt::white, 960, 640);
  waitUntil([canvas] { return canvas->hasImage(); });
  pumpFor(150);
  win.acts.fit->trigger();
  canvas->setLines({line({{120, 110}, {520, 260}, {300, 520}}, "#c81e1e", 4, "solid"),
                    line({{620, 140}, {840, 560}}, "#1e63c8", 3, "dashed")});
  pumpFor(300);
  clearToasts(&win);
  win.parts.theme.toggleTheme();
  film("theme-swap", &win, 1100, 33);
}
