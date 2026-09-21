#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "MainWindow.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "planExecutor.hpp"
#include "OpenImageDialog.hpp"
#include "CanvasWidget.hpp"
#include "guiHelpers.hpp"
#include "modalReveal.hpp"
#include "LinksDialog.hpp"
#include "DescriptionDialog.hpp"
#include "KeywordsDialog.hpp"
#include "MediaLoader.hpp"
#include "Notifications.hpp"
#include "ProjectsDialog.hpp"
#include "ServerClient.hpp"
#include "../../support/modal/modalChrome.hpp"

#include <QImage>

// The project metadata dialogs: links, description, keywords.

namespace stencil::gui {

  // Link edits persist to the active project and the in-memory provenance; a URL load routes through loadImageByUrl().
  void MainWindow::openLinks() {
    // Seed from the active project's stored provenance, else the live current* provenance.
    QString src = currentSource, res = currentResource;
    if (!activeProjectId.isEmpty()) {
      Project* pr = findProject(activeProjectId.toStdString());
      if (pr) {
        src = QString::fromStdString(pr->meta.source);
        res = QString::fromStdString(pr->meta.resource);
      }
    }

    // The Name seed the browser's linksModal shows: the bound project's name, else the image's base name.

    LinksDialog dlg(src, res, canvas->hasImage(), settings.pageSize,
                    settings.units, this);
    execMaybePopover(dlg);

    if (dlg.getLoadRequested()) {
      // Quick pre-load edits, consumed once by onLaunchImageLoaded.
      if (dlg.cropToPage())
        pendingCrop = {QuickCropOpts::Mode::PAGE, dlg.getCropAlbum(), dlg.getCropPageSize()};
      else
        pendingCrop = {QuickCropOpts::Mode::NONE, false, QString()};
      // Adopt the pixels the preview already decoded (no second download/seek).
      const QImage previewed = dlg.previewedImage();
      if (!previewed.isNull()) {
        pendingProvSource = dlg.urlSource();
        pendingProvResource = dlg.urlResource();
        onLaunchImageLoaded(previewed, QString());
      } else {
        loadImageByUrl(dlg.urlSource(), dlg.urlResource(), dlg.urlFrame());
      }
      return;
    }

    // No image → add-by-URL mode just closed; nothing to save.
    if (!canvas->hasImage()) return;

    // Browser parity: no Cancel/Save — apply whatever changed.
    if (dlg.source() == src && dlg.resource() == res) return;   // links untouched
    currentSource = dlg.source();
    currentResource = dlg.resource();
    if (!activeProjectId.isEmpty()) {
      Project* pr = findProject(activeProjectId.toStdString());
      if (pr) {
        pr->meta.source = currentSource.toStdString();
        pr->meta.resource = currentResource.toStdString();
        pr->meta.updatedAt = nowMs();
        fileStore::saveProjects(projectList);
        notify->success("Links saved");
        return;
      }
    }
    notify->info("Links updated — save to a project to keep them");
  }

  // Written back through the Projects window's store path (DescriptionDialog::apply ≙ commitRowEdit).
  void MainWindow::openDescription() {
    Project* pr = incognito ? nullptr : findProject(activeProjectId.toStdString());
    if (!pr) { notify->info("Save the project first to add a description"); return; }
    const QString current = QString::fromStdString(pr->meta.description);
    DescriptionDialog dlg(current, this);
    if (execMaybePopover(dlg, actDescription) != QDialog::Accepted) return;
    const QString text = dlg.text();
    if (text == current) return;
    if (DescriptionDialog::apply(projectList, activeProjectId, text, nowMs()))
      notify->success(text.isEmpty() ? "Description cleared" : "Description saved");
  }

  // Same way (KeywordsDialog::apply ≙ commitRowEdit).
  void MainWindow::openKeywords() {
    Project* pr = incognito ? nullptr : findProject(activeProjectId.toStdString());
    if (!pr) { notify->info("Save the project first to add keywords"); return; }
    QStringList current;
    for (const auto& k : pr->meta.keywords) current << QString::fromStdString(k);
    KeywordsDialog dlg(current, this);
    if (execMaybePopover(dlg, actKeywords) != QDialog::Accepted) return;
    const QStringList next = dlg.keywords();
    if (next == current) return;
    if (KeywordsDialog::apply(projectList, activeProjectId, next, nowMs()))
      notify->success(next.isEmpty() ? "Keywords cleared" : "Keywords saved");
  }

  // Remembers the URL + resource as provenance for the next project save.
  void MainWindow::loadImageByUrl(const QString& source, const QString& resource,
                                  int frame) {
    if (source.isEmpty()) return;
    pendingProvSource = source;
    pendingProvResource = resource;
    openImageSource(source, frame);
  }

}  // namespace stencil::gui
