// The window states behind usecases/docs/desktop/img, in the order one window passes through
// them: the empty editor, a picture from a link, a selected line and the context menu here, then
// the file drops, the assistant, the dialogs, the copy flyouts, the toolbar lists, the video, the
// logo's accents, the filmed edits and a server last (capture{Drops,Assistant,Dialogs,CopyScript,
// Toolbar,Video,Gestures,Editing}.cpp); and the theme-swap frames.
#include "captureStrip.hpp"

#include "CanvasWidget.hpp"

#include <QMenu>
#include <QScrollArea>
#include <QToolButton>
#include <QtTest/QTest>

using namespace stencil::gui;

namespace {
  constexpr QSize WINDOW(1280, 860);
}  // namespace

void MainWindowGuiTest::fit(MainWindow& win) {
  pumpFor(150);
  win.acts.fit->trigger();
  pumpFor(150);
  clearToasts(&win);
}

void MainWindowGuiTest::windowStates(const QString& theme, const ShotSet& shots) {
  MainWindow win(nullptr, /*restoreLast=*/false);
  win.resize(WINDOW);
  win.show();
  if (!QTest::qWaitForWindowExposed(&win)) return;
  pumpFor(250);
  auto* canvas = win.findChild<CanvasWidget*>();
  const QString editorEmpty = suffixed("editor-empty", theme);
  if (shots.has(editorEmpty)) save(editorEmpty, &win);
  // The Open Image dialog on a link, then the picture it opens, whole, in a window that held nothing.
  if (shots.hasAny({QStringLiteral("open-image-dialog"), QStringLiteral("open-from-url")}))
    openImageUrlShot(win, shots);

  win.parts.sourceOpener.createBlankImage(Qt::white, 960, 640);
  waitUntil([canvas] { return canvas->hasImage(); });
  fit(win);

  const QString linesSelection = suffixed("lines-selection", theme);
  if (shots.has(linesSelection)) {
    canvas->setLines({line({{120, 110}, {520, 260}, {300, 520}}, "#c81e1e", 4, "solid"),
                      line({{620, 140}, {840, 560}}, "#1e63c8", 3, "dashed")});
    canvas->selectLineByIndex(0);
    pumpFor(150);
    save(linesSelection, &win);
    // The selected-line bar grows the window; every later shot is taken at the same size.
    canvas->deselect();
    canvas->setLines({});
    pumpFor(150);
    win.resize(WINDOW);
    pumpFor(150);
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

  dropShots(win, shots);
  assistantShots(win, shots);
  dialogShots(win, shots);
  copyScriptShots(win, shots);
  toolbarShots(win, shots);
  videoShots(win, shots);

  // The logo's right-click list of accents: a popover INSIDE the window, under the logo, in its own
  // loop — found as the window's child and cut, with the logo, out of the window's grab.
  if (shots.has("accent-picker")) {
    const auto popover = [&win] { return win.findChild<QWidget*>(QStringLiteral("popoverOverlay")); };
    const auto dismiss = [popover] {
      if (QWidget* pop = popover()) if (auto* dlg = pop->findChild<QDialog*>()) dlg->reject();
    };
    bool done = false;
    QTimer::singleShot(0, [&] {
      if (waitUntil([&] { return popover() && popover()->isVisible(); }, 4000)) {
        pumpFor(500);
        const QRect logo(win.tools.logoBtn->mapTo(&win, QPoint()), win.tools.logoBtn->size());
        const int pad = MODAL_MARGIN_PX / 2;
        const QRect box = popover()->geometry().united(logo).adjusted(-pad, -pad, pad, pad) & win.rect();
        const QImage full = grabLive(&win).toImage();
        const qreal dpr = full.devicePixelRatio();
        QImage out = full.copy(QRect(box.topLeft() * dpr, box.size() * dpr));
        out.setDevicePixelRatio(dpr);
        save(QStringLiteral("accent-picker"), out);
      }
      done = true;
      dismiss();
    });
    QTimer::singleShot(8000, dismiss);
    emit win.tools.logoBtn->customContextMenuRequested(QPoint(4, 4));
    waitUntil([&] { return done; }, 9000);
    pumpFor(300);
  }
  gestureShots(win, shots);
  editingShots(win, shots);
  // Last: once a server is connected, every Projects list carries its projects.
  serverShots(win, shots);
}

// The theme wipe and a hold on the new palette at ~30 fps, in its own pass at 1x with motion on (a 2x
// grab outlasts a frame); the lengths are config/desktop.json's clip, handed over by desktop.mjs.
void MainWindowGuiTest::themeClip() {
  MainWindow win(nullptr, /*restoreLast=*/false);
  win.resize(WINDOW);
  win.show();
  if (!QTest::qWaitForWindowExposed(&win)) return;
  pumpFor(250);
  auto* canvas = win.findChild<CanvasWidget*>();
  win.parts.sourceOpener.createBlankImage(Qt::white, 960, 640);
  waitUntil([canvas] { return canvas->hasImage(); });
  // The page lands on a dust gather that ends when the canvas drops its veil: filming starts after it.
  waitUntil([canvas] { return canvas->graphicsEffect() == nullptr; }, 8000);
  pumpFor(150);
  win.acts.fit->trigger();
  canvas->setLines({line({{120, 110}, {520, 260}, {300, 520}}, "#c81e1e", 4, "solid"),
                    line({{620, 140}, {840, 560}}, "#1e63c8", 3, "dashed")});
  pumpFor(300);
  clearToasts(&win);
  // The switch is pressed where it sits: the pointer rests on it, then the press swaps the palette.
  QPoint at;
  for (QToolButton* b : win.findChildren<QToolButton*>())
    if (b->defaultAction() == win.acts.theme && b->isVisible()) at = b->mapToGlobal(b->rect().center());
  film("theme-swap", &win, envOr("STENCIL_DOCS_CLIP_MS", "1100").toInt(),
       envOr("STENCIL_DOCS_CLIP_EVERY_MS", "33").toInt(), at, [&win] { win.parts.theme.toggleTheme(); });
}
