#include "MainWindow.hpp"
#include "SharedState.hpp"
#include "SourceOpener.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "LinksDialog.hpp"
#include "Notifications.hpp"

// The project metadata dialogs: links, description, keywords.

namespace stencil::gui {

  // Link edits persist to the active project and the in-memory provenance; a URL load routes through loadImageByUrl().
  void SourceOpener::openLinks() {
    // Seed from the active project's stored provenance, else the live current* provenance.
    QString src = w.docSource.currentSource, res = w.docSource.currentResource;
    if (!w.activeProjectId.isEmpty()) {
      Project* pr = w.findProject(w.activeProjectId.toStdString());
      if (pr) {
        src = QString::fromStdString(pr->meta.source);
        res = QString::fromStdString(pr->meta.resource);
      }
    }

    // The Name seed the browser's linksModal shows: the bound project's name, else the image's base name.

    LinksDialog dlg(src, res, w.canvas->hasImage(), w.settings.pageSize,
                    w.settings.units, &w);
    w.execMaybePopover(dlg);

    if (dlg.getLoadRequested()) {
      // Quick pre-load edits, consumed once by onLaunchImageLoaded.
      if (dlg.cropToPage())
        w.docSource.pendingCrop = {QuickCropOpts::Mode::PAGE, dlg.getCropAlbum(), dlg.getCropPageSize(), {}};
      else
        w.docSource.pendingCrop = QuickCropOpts::none();
      // Adopt the pixels the preview already decoded (no second download/seek).
      const QImage previewed = dlg.previewedImage();
      if (!previewed.isNull()) {
        w.docSource.pendingProvSource = dlg.urlSource();
        w.docSource.pendingProvResource = dlg.urlResource();
        onLaunchImageLoaded(previewed, QString());
      } else {
        loadImageByUrl(dlg.urlSource(), dlg.urlResource(), dlg.urlFrame());
      }
      return;
    }

    // No image → add-by-URL mode just closed; nothing to save.
    if (!w.canvas->hasImage()) return;

    // Browser parity: no Cancel/Save — apply whatever changed.
    if (dlg.source() == src && dlg.resource() == res) return;   // links untouched
    w.docSource.currentSource = dlg.source();
    w.docSource.currentResource = dlg.resource();
    if (!w.activeProjectId.isEmpty()) {
      Project* pr = w.findProject(w.activeProjectId.toStdString());
      if (pr) {
        pr->meta.source = w.docSource.currentSource.toStdString();
        pr->meta.resource = w.docSource.currentResource.toStdString();
        pr->meta.updatedAt = nowMs();
        SharedState::instance().saveProjects(&w);
        w.notify->success("Links saved");
        return;
      }
    }
    w.notify->info("Links updated — save to a project to keep them");
  }

  // Remembers the URL + resource as provenance for the next project save.
  void SourceOpener::loadImageByUrl(const QString& source, const QString& resource,
                                  int frame) {
    if (source.isEmpty()) return;
    w.docSource.pendingProvSource = source;
    w.docSource.pendingProvResource = resource;
    openImageSource(source, frame);
  }
}  // namespace stencil::gui
