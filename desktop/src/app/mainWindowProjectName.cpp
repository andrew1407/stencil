#include "mainWindow.hpp"
#include "remoteSession.hpp"
#include <QLineEdit>
#include <QStyle>
#include <QToolButton>
#include "mainWindow.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "chatMenuPanel.hpp"
#include "planExecutor.hpp"
#include "displayName.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "canvasWidget.hpp"
#include "guiHelpers.hpp"
#include "menuReveal.hpp"
#include "descriptionDialog.hpp"
#include "keywordsDialog.hpp"
#include "mediaLoader.hpp"
#include "notifications.hpp"
#include "shortcutsDialog.hpp"
#include "theme.hpp"
#include "../support/modalChrome.hpp"

#include <QAction>
#include <QScrollArea>
#include <algorithm>

// The project's name: what it reads, what a rename accepts, and the window title.

namespace stencil::gui {


  QString MainWindow::activeProjectName() const {
    if (activeProjectId_.isEmpty()) return {};
    for (const auto& p : projectList_)
      if (QString::fromStdString(p.meta.id) == activeProjectId_)
        return QString::fromStdString(p.meta.name);
    return {};
  }

  QString MainWindow::projectBaseName() const {
    const QString n = activeProjectName();
    if (!n.isEmpty()) return n;   // the project name IS the download name
    return canvas_ ? canvas_->imageBaseName() : QStringLiteral("image");
  }

  core::ProjectsStore::NameCheck MainWindow::checkProjectName(
      const QString& name, const QString& exceptId) const {
    std::vector<core::ProjectMeta> metas;
    for (const auto& p : projectList_) metas.push_back(p.meta);
    core::ProjectsStore store;   // local; never disturbs projectsStore_
    store.load(metas);
    return store.validateName(name.toStdString(), exceptId.toStdString());
  }

  bool MainWindow::renameProjectById(const QString& id, const QString& rawName) {
    const QString name = rawName.trimmed();
    Project* pr = findProject(id.toStdString());
    if (!pr) return false;
    const auto check = checkProjectName(name, id);
    if (!check.ok) {
      notify_->error(QString::fromStdString(check.reason));
      return false;
    }
    pr->meta.name = name.toStdString();
    // The project name is THE name: downloads use projectBaseName(), so there is no
    // separate image name to keep in sync.
    fileStore::saveProjects(projectList_);
    refreshDockMenu();
    if (activeProjectId_ == id) updateProjectTitle();
    notify_->success(QString("Renamed to \"%1\"").arg(support::shortName(name)));
    return true;
  }

  // Header-row "Image Size: W × H px" (+ "· blank"), or a neutral hint when no image is loaded.
  // Always visible — the header row never collapses — mirroring the browser's #image-info bar.
  // The incognito half of the image-info line: a muted "|" divider, then the app's OWN
  // incognito glyph (the one the toolbar toggle wears — never an emoji, which rendered
  // in the font's colour and style) tinted like the accent tag beside it. Divider and
  // tag are one unit: nothing here is ever emitted without the rest, so a plain line
  // can't end in a dangling separator.
  QString MainWindow::incognitoTagHtml() const {
    const Palette pal = themePalette(resolveDark(settings_.themeMode), settings_.accentColor);
    const int glyphPx = std::max(12, QFontMetrics(imageSizeInfo_->font()).height() - 2);
    return QStringLiteral("&nbsp;&nbsp;<span style=\"color:%1;\">|</span>&nbsp;&nbsp;"
                          "%2<span style=\"color:%3;font-weight:700;vertical-align:middle;\">"
                          "&nbsp;Incognito &mdash; not saved</span>")
        .arg(pal.textMuted.name(),
             inlineIconHtml("incognito", pal.accent, glyphPx,
                            QStringLiteral("vertical-align:middle")),
             pal.accent.name());
  }

  void MainWindow::updateProjectTitle() {
    QString name;
    bool editable = false;
    const bool remote = !remoteSession_->link().id.isEmpty();
    if (incognito_) {
      name = "Incognito";
    } else if (!activeProjectId_.isEmpty()) {
      name = activeProjectName();
      editable = true;   // an active LOCAL project is always renameable/colourable (even if the
                         // registry name lookup momentarily returns empty and we fall back to the id)
    } else if (remote) {
      name = remoteSession_->link().name;   // server-linked session (no local project id)
      editable = true;   // server projects are renameable/colourable too (pushed via commitProjectName)
    }
    if (name.isEmpty() && canvas_ && canvas_->hasImage())
      name = canvas_->imageBaseName();   // show the image name until it's a saved project
    setWindowTitle(name.isEmpty() ? QStringLiteral("Stencil")
                                  : QString("%1 — Stencil").arg(name));
    // Server-editing indicator: a golden frame around the canvas (mirrors the browser
    // badge/outline), so a server-backed session is unmistakable. A dynamic property
    // (theme.cpp: QScrollArea#canvasViewport[remoteEditing="true"]) rather than a local
    // stylesheet override, so it layers on top of the viewport's normal themed border
    // instead of replacing the whole rule.
    if (scroll_) {
      scroll_->setProperty("remoteEditing", remote);
      scroll_->style()->unpolish(scroll_);
      scroll_->style()->polish(scroll_);
    }
    // Per-project accent: the toolbar name field is painted in the project's colour by
    // applyProjectNameStyle below (empty => theme default). The window title is OS-drawn,
    // so only the field is tinted — mirroring the browser's coloured #project-name-input.
    const bool hasProject = !incognito_ && (!activeProjectId_.isEmpty() || remote);
    // Don't clobber the field while the user is typing in it.
    if (nameBar_.field && !nameBar_.field->hasFocus()) {
      nameBar_.field->setText(name);
      nameBar_.field->setEnabled(editable);
      nameBar_.field->setReadOnly(true);  // back to read-only after any edit (enter edit via ✎/dbl-click)
      nameBar_.field->setPlaceholderText(
          incognito_ ? QStringLiteral("Incognito (unsaved)") : QStringLiteral("No project"));
      // Custom colour when set; otherwise the shared neutral grey (#80868f), readable on
      // light and dark — mirrors the browser's --project-name-fg (Qt has no text-shadow). The
      // read-only look carries NO border/focus ring (applyProjectNameStyle); the bordered input
      // appears only in edit mode.
      applyProjectNameStyle(false);
      refreshProjectNameButtons();
    }
    // Project-colour menu actions + the toolbar 🎨 icon enable with an active project; the ✎
    // rename pencil only when the name is editable (a saved, non-incognito project).
    if (actProjectColor_) actProjectColor_->setEnabled(hasProject);
    // "…default colour" clears a CUSTOM colour — with none set there is nothing to
    // clear, so the menubar row greys out (setActiveProjectColor refreshes this).
    if (actProjectColorClear_)
      actProjectColorClear_->setEnabled(hasProject && !currentProjectColor().isEmpty());
    // Cursor refreshed WITH the state: buildToolbars bakes every button's cursor from
    // its enabled state at construction (these two start disabled — no project yet),
    // and nothing else re-reads it, so an enabled ✎/🎨 kept the forbidden cursor
    // forever.
    if (nameBar_.colorBtn) {
      nameBar_.colorBtn->setEnabled(hasProject);
      nameBar_.colorBtn->setCursor(hasProject ? Qt::PointingHandCursor : Qt::ForbiddenCursor);
    }
    if (nameBar_.edit) {
      nameBar_.edit->setEnabled(editable);
      nameBar_.edit->setCursor(editable ? Qt::PointingHandCursor : Qt::ForbiddenCursor);
    }
    // DESCRIPTION & ATTRIBUTES edits a saved LOCAL project's metadata (browser:
    // activeProjectId && !incognito); the tooltips carry the reason while greyed out.
    const bool savedProject = !incognito_ && !activeProjectId_.isEmpty();
    for (QAction* a : {actDescription_, actKeywords_, actLinks_})
      if (a) a->setEnabled(savedProject);
    updateImageSizeInfo();
  }

}  // namespace stencil::gui
