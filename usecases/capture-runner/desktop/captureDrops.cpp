// The file-drop overlay the window paints while a file is dragged over it: the save/incognito
// split an image or a .stencil gets, and the one zone a .json layout gets. Browser twin:
// usecases/capture-runner/browser/dragSteps.mjs fileDropStep.
#include "captureShared.hpp"

#include "DropZonesOverlay.hpp"

#include <QDragEnterEvent>
#include <QMimeData>
#include <QUrl>
#include <QWindow>

using namespace stencil::gui;

void MainWindowGuiTest::dropShots(MainWindow& win, const ShotSet& shots) {
  // A drag reaches the WINDOW handle, which forwards it as a pointer would; the file need not
  // exist until the drop, which never comes.
  const struct { const char* name; const char* file; double x; } drags[] = {
    {"file-dropzones", "Kitchen plan.stencil", 0.78},
    {"layout-dropzone", "kitchen-layout.json", 0.30},
  };
  for (const auto& d : drags) {
    if (!shots.has(QString::fromUtf8(d.name)) || !win.overlays.dropZones) continue;
    clearToasts(&win);
    pumpFor(400);   // a cleared toast fades out; the overlay would show it through its wash
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(QDir::temp().filePath(QString::fromUtf8(d.file)))});
    QDragEnterEvent enter(QPoint(int(win.width() * d.x), win.height() / 3), Qt::CopyAction, &mime,
                          Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(win.windowHandle(), &enter);
    pumpFor(200);
    save(QString::fromUtf8(d.name), &win);
    win.overlays.dropZones->hideZonesNow();
  }
}
