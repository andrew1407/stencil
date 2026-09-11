#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "mainWindow.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "planExecutor.hpp"
#include "openImageDialog.hpp"
#include "canvasWidget.hpp"
#include "guiHelpers.hpp"
#include "modalReveal.hpp"
#include "linksDialog.hpp"
#include "descriptionDialog.hpp"
#include "keywordsDialog.hpp"
#include "mediaLoader.hpp"
#include "notifications.hpp"
#include "projectsDialog.hpp"
#include "serverClient.hpp"
#include "../support/modalChrome.hpp"

#include <QImage>

// The project metadata dialogs: links, description, keywords.

namespace stencil::gui {

  // View/edit/open/remove the current image's source & resource links, or add a
  // new image by URL. Edits to the links persist to the active project (and the
  // in-memory current* provenance); a URL load routes through loadImageByUrl().
  void MainWindow::openLinks() {
    // Image Links only edits a loaded image's provenance; the action is disabled
    // without one (refreshActions), so this is only reached with an image.
    // Seed from the active project's stored provenance when available, else from
    // the live current* provenance (e.g. an image just loaded by URL, not yet saved).
    QString src = currentSource_, res = currentResource_;
    if (!activeProjectId_.isEmpty()) {
      Project* pr = findProject(activeProjectId_.toStdString());
      if (pr) {
        src = QString::fromStdString(pr->meta.source);
        res = QString::fromStdString(pr->meta.resource);
      }
    }

    // The Name field's seed — the same the browser's linksModal shows: the bound
    // project's stored name, else the image's base name.

    LinksDialog dlg(src, res, canvas_->hasImage(), settings_.pageSize,
                    settings_.units, this);
    execMaybePopover(dlg);

    if (dlg.loadRequested()) {
      // Quick pre-load edits: crop to the chosen page aspect/orientation, or load
      // the full frame uncropped — consumed once by onLaunchImageLoaded.
      if (dlg.cropToPage())
        pendingCrop_ = {QuickCropOpts::Mode::Page, dlg.cropAlbum(), dlg.cropPageSize()};
      else
        pendingCrop_ = {QuickCropOpts::Mode::None, false, QString()};
      // The dialog already fetched and decoded the exact image/frame for its
      // preview — adopt those pixels directly (no second download/seek) so what
      // was previewed is exactly what loads. Fall back to a fresh resolve only if
      // the preview image is somehow absent.
      const QImage previewed = dlg.previewedImage();
      if (!previewed.isNull()) {
        pendingProvSource_ = dlg.urlSource();
        pendingProvResource_ = dlg.urlResource();
        onLaunchImageLoaded(previewed, QString());
      } else {
        loadImageByUrl(dlg.urlSource(), dlg.urlResource(), dlg.urlFrame());
      }
      return;
    }

    // No image → the dialog was in add-by-URL mode and just closed; nothing to save.
    if (!canvas_->hasImage()) return;

    // Browser parity: the modal has no Cancel/Save — whatever way it was dismissed,
    // apply what changed.
    if (dlg.source() == src && dlg.resource() == res) return;   // links untouched
    currentSource_ = dlg.source();
    currentResource_ = dlg.resource();
    if (!activeProjectId_.isEmpty()) {
      Project* pr = findProject(activeProjectId_.toStdString());
      if (pr) {
        pr->meta.source = currentSource_.toStdString();
        pr->meta.resource = currentResource_.toStdString();
        pr->meta.updatedAt = nowMs();
        fileStore::saveProjects(projectList_);
        notify_->success("Links saved");
        return;
      }
    }
    notify_->info("Links updated — save to a project to keep them");
  }

  // The saved project's description: pre-filled from the store, written back through the
  // Projects window's own store path (DescriptionDialog::apply ≙ commitRowEdit).
  void MainWindow::openDescription() {
    Project* pr = incognito_ ? nullptr : findProject(activeProjectId_.toStdString());
    if (!pr) { notify_->info("Save the project first to add a description"); return; }
    const QString current = QString::fromStdString(pr->meta.description);
    DescriptionDialog dlg(current, this);
    if (execMaybePopover(dlg, actDescription_) != QDialog::Accepted) return;
    const QString text = dlg.text();
    if (text == current) return;
    if (DescriptionDialog::apply(projectList_, activeProjectId_, text, nowMs()))
      notify_->success(text.isEmpty() ? "Description cleared" : "Description saved");
  }

  // …and its search keywords, the same way (KeywordsDialog::apply ≙ commitRowEdit).
  void MainWindow::openKeywords() {
    Project* pr = incognito_ ? nullptr : findProject(activeProjectId_.toStdString());
    if (!pr) { notify_->info("Save the project first to add keywords"); return; }
    QStringList current;
    for (const auto& k : pr->meta.keywords) current << QString::fromStdString(k);
    KeywordsDialog dlg(current, this);
    if (execMaybePopover(dlg, actKeywords_) != QDialog::Accepted) return;
    const QStringList next = dlg.keywords();
    if (next == current) return;
    if (KeywordsDialog::apply(projectList_, activeProjectId_, next, nowMs()))
      notify_->success(next.isEmpty() ? "Keywords cleared" : "Keywords saved");
  }

  // Load an image/video by URL (reusing the --src resolver), remembering the URL +
  // optional resource as provenance so the next project save records them.
  void MainWindow::loadImageByUrl(const QString& source, const QString& resource,
                                  int frame) {
    if (source.isEmpty()) return;
    pendingProvSource_ = source;
    pendingProvResource_ = resource;
    openImageSource(source, frame);
  }

}  // namespace stencil::gui
