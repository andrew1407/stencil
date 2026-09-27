#include "MainWindow.hpp"
#include "DocumentPersistence.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "guiHelpers.hpp"  // showSaveDialog
#include "lineUnion.hpp"
#include "../../support/modal/modalChrome.hpp"  // confirmModal — the browser-styled question
#include "Notifications.hpp"
#include "StencilFileSync.hpp"

#include <QFileInfo>

// The project a .stencil file holds: serialize it, Save-As / delete the file, and load an external
// version (applied or merged); StencilFileSync keeps the link in step.

namespace stencil::gui {

  void DocumentPersistence::saveProjectFileAs() {
    if (!w.canvas->hasImage()) {
      w.notify->error("Load an image first");
      return;
    }
    const QString suggested = w.projectBaseName() + ".stencil";
    const QString path = showSaveDialog(&w, "Save project", suggested,
                                        "Stencil project (*.stencil)");
    if (path.isEmpty()) return;
    const QByteArray out = w.buildStencilBytes();
    if (!writeFileBytes(path, out)) {
      w.notify->error("Could not write the project file");
      return;
    }
    w.stencilSync->link(path, out);   // this file becomes the project's live-sync target
    w.notify->success("Project saved");
  }

  // After a confirm; the project stays open, only the file goes. Mirrors the browser
  // ExportService.deleteProjectFile.
  void DocumentPersistence::deleteProjectFile() {
    if (w.stencilSync->linkedPath().isEmpty()) {
      w.notify->error("No linked .stencil file to delete");
      return;
    }
    const QString path = w.stencilSync->linkedPath();
    const QString shown = QFileInfo(path).fileName();
    // The browser's styled confirm (export/service.js confirmIcon): a bin, red.
    ConfirmSpec spec;
    spec.title = MainWindow::tr("Delete project file");
    spec.message = MainWindow::tr("Delete “%1” from disk? This can’t be undone. The project stays open here.").arg(shown);
    spec.confirmLabel = MainWindow::tr("Delete");
    spec.confirmIcon = QStringLiteral("trash");
    spec.danger = true;
    if (!confirmModal(&w, spec)) { w.notify->info("Delete canceled"); return; }

    if (!QFile::remove(path)) {
      w.notify->error("Could not delete the project file");   // keep the link so live-sync survives
      return;
    }
    w.stencilSync->unlink();   // gone from disk → nothing to sync to
    w.notify->success(MainWindow::tr("Deleted “%1”").arg(shown));
  }

  void DocumentPersistence::applyStencilExternal(const QByteArray& text, bool merge) {
    fileStore::ProjectFileData pf;
    QString err;
    if (!fileStore::parseProjectFile(text, pf, &err)) {
      w.notify->error("Could not read the changed project file");
      return;
    }
    // Decoded on the pool; a link that moved on meanwhile drops the result.
    const QString link = w.stencilSync->linkedPath();
    w.decodeForCanvas(
        [image = pf.imageBytes] { return QImage::fromData(image); },
        [this, link, text, merge, pf](const QImage& img) {
          if (img.isNull() || w.stencilSync->linkedPath() != link) return;
          QJsonObject layout = pf.layout;
          if (merge) {
            int width = 0, height = 0;
            const core::Lines fileLines = fileStore::parseLayoutJson(pf.layout, width, height);
            layout["lines"] = fileStore::linesToJson(model::unionLines(fileLines, w.canvas->allLines()).lines);
          }
          w.stencilSync->setApplying(true);
          w.loadImageWithLayout(img, layout, pf.imageBytes, pf.imageExt);
          w.stencilSync->setApplying(false);
          if (merge) {
            w.stencilSync->writeNow();   // push the merged result back to the file
            w.notify->success("Merged with file");
          } else {
            w.stencilSync->adoptBaseline(text);
            w.notify->success("Reloaded from file");
          }
          w.refreshActions();
        });
  }
}  // namespace stencil::gui
