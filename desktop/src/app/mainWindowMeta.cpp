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

  // Link edits persist to the active project and the in-memory provenance; a URL load routes through loadImageByUrl().
  void MainWindow::openLinks() {
    // Seed from the active project's stored provenance, else the live current* provenance.
    QString src = currentSource_, res = currentResource_;
    if (!activeProjectId_.isEmpty()) {
      Project* pr = findProject(activeProjectId_.toStdString());
      if (pr) {
        src = QString::fromStdString(pr->meta.source);
        res = QString::fromStdString(pr->meta.resource);
      }
    }

    // The Name seed the browser's linksModal shows: the bound project's name, else the image's base name.

    LinksDialog dlg(src, res, canvas_->hasImage(), settings_.pageSize,
                    settings_.units, this);
    execMaybePopover(dlg);

    if (dlg.loadRequested()) {
      // Quick pre-load edits, consumed once by onLaunchImageLoaded.
      if (dlg.cropToPage())
        pendingCrop_ = {QuickCropOpts::Mode::PAGE, dlg.cropAlbum(), dlg.cropPageSize()};
      else
        pendingCrop_ = {QuickCropOpts::Mode::NONE, false, QString()};
      // Adopt the pixels the preview already decoded (no second download/seek).
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

    // No image → add-by-URL mode just closed; nothing to save.
    if (!canvas_->hasImage()) return;

    // Browser parity: no Cancel/Save — apply whatever changed.
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

  // Written back through the Projects window's store path (DescriptionDialog::apply ≙ commitRowEdit).
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

  // Same way (KeywordsDialog::apply ≙ commitRowEdit).
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

  // Remembers the URL + resource as provenance for the next project save.
  void MainWindow::loadImageByUrl(const QString& source, const QString& resource,
                                  int frame) {
    if (source.isEmpty()) return;
    pendingProvSource_ = source;
    pendingProvResource_ = resource;
    openImageSource(source, frame);
  }

}  // namespace stencil::gui
