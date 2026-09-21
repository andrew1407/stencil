#include "MainWindow.hpp"
#include "RemoteSession.hpp"
#include <QLineEdit>
#include <QStyle>
#include <QToolButton>
#include "MainWindow.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "ChatMenuPanel.hpp"
#include "planExecutor.hpp"
#include "displayName.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "CanvasWidget.hpp"
#include "guiHelpers.hpp"
#include "menuReveal.hpp"
#include "DescriptionDialog.hpp"
#include "KeywordsDialog.hpp"
#include "MediaLoader.hpp"
#include "Notifications.hpp"
#include "ShortcutsDialog.hpp"
#include "theme.hpp"
#include "../support/modalChrome.hpp"

#include <QAction>
#include <QScrollArea>
#include <algorithm>

// The project's name: what it reads, what a rename accepts, and the window title.

namespace stencil::gui {


  QString MainWindow::activeProjectName() const {
    if (activeProjectId.isEmpty()) return {};
    for (const auto& p : projectList)
      if (QString::fromStdString(p.meta.id) == activeProjectId)
        return QString::fromStdString(p.meta.name);
    return {};
  }

  QString MainWindow::projectBaseName() const {
    const QString n = activeProjectName();
    if (!n.isEmpty()) return n;   // the project name IS the download name
    return canvas ? canvas->imageBaseName() : QStringLiteral("image");
  }

  core::ProjectsStore::NameCheck MainWindow::checkProjectName(
      const QString& name, const QString& exceptId) const {
    std::vector<core::ProjectMeta> metas;
    for (const auto& p : projectList) metas.push_back(p.meta);
    core::ProjectsStore store;   // local; never disturbs projectsStore
    store.load(metas);
    return store.validateName(name.toStdString(), exceptId.toStdString());
  }

  bool MainWindow::renameProjectById(const QString& id, const QString& rawName) {
    const QString name = rawName.trimmed();
    Project* pr = findProject(id.toStdString());
    if (!pr) return false;
    const auto check = checkProjectName(name, id);
    if (!check.ok) {
      notify->error(QString::fromStdString(check.reason));
      return false;
    }
    pr->meta.name = name.toStdString();
    // Downloads use projectBaseName(), so there is no separate image name.
    fileStore::saveProjects(projectList);
    refreshDockMenu();
    if (activeProjectId == id) updateProjectTitle();
    notify->success(QString("Renamed to \"%1\"").arg(support::shortName(name)));
    return true;
  }

  // Mirrors the browser's #image-info bar. The incognito half is the toolbar's own glyph (an emoji
  // took the font's colour); divider and tag are one unit, so no dangling separator.
  QString MainWindow::incognitoTagHtml() const {
    const Palette pal = themePalette(resolveDark(settings.themeMode), settings.accentColor);
    const int glyphPx = std::max(12, QFontMetrics(imageSizeInfo->font()).height() - 2);
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
    const bool remote = !remoteSession->getLink().id.isEmpty();
    if (incognito) {
      name = "Incognito";
    } else if (!activeProjectId.isEmpty()) {
      name = activeProjectName();
      editable = true;   // an active LOCAL project is always renameable/colourable (even if the
                         // registry name lookup momentarily returns empty and we fall back to the id)
    } else if (remote) {
      name = remoteSession->getLink().name;   // server-linked session (no local project id)
      editable = true;   // server projects are renameable/colourable too (pushed via commitProjectName)
    }
    if (name.isEmpty() && canvas && canvas->hasImage())
      name = canvas->imageBaseName();   // show the image name until it's a saved project
    setWindowTitle(name.isEmpty() ? QStringLiteral("Stencil")
                                  : QString("%1 — Stencil").arg(name));
    // Golden frame for a server-backed session (browser badge/outline); a dynamic property
    // (theme.cpp [remoteEditing="true"]) so it layers on the themed border.
    if (scroll) {
      scroll->setProperty("remoteEditing", remote);
      scroll->style()->unpolish(scroll);
      scroll->style()->polish(scroll);
    }
    // Only the field is tinted — the title is OS-drawn (browser: coloured #project-name-input).
    const bool hasProject = !incognito && (!activeProjectId.isEmpty() || remote);
    if (nameBar.field && !nameBar.field->hasFocus()) {
      nameBar.field->setText(name);
      nameBar.field->setEnabled(editable);
      nameBar.field->setReadOnly(true);  // back to read-only after any edit (enter edit via ✎/dbl-click)
      nameBar.field->setPlaceholderText(
          incognito ? QStringLiteral("Incognito (unsaved)") : QStringLiteral("No project"));
      // Custom colour, else the shared neutral #80868f (browser --project-name-fg); no border in
      // read-only mode.
      applyProjectNameStyle(false);
      refreshProjectNameButtons();
    }
    if (actProjectColor) actProjectColor->setEnabled(hasProject);
    // With no custom colour there is nothing to clear, so the row greys out.
    if (actProjectColorClear)
      actProjectColorClear->setEnabled(hasProject && !currentProjectColor().isEmpty());
    // buildToolbars bakes each cursor from the enabled state at construction and nothing else re-
    // reads it.
    if (nameBar.colorBtn) {
      nameBar.colorBtn->setEnabled(hasProject);
      nameBar.colorBtn->setCursor(hasProject ? Qt::PointingHandCursor : Qt::ForbiddenCursor);
    }
    if (nameBar.edit) {
      nameBar.edit->setEnabled(editable);
      nameBar.edit->setCursor(editable ? Qt::PointingHandCursor : Qt::ForbiddenCursor);
    }
    // browser: activeProjectId && !incognito; the tooltips carry the reason while greyed out.
    const bool savedProject = !incognito && !activeProjectId.isEmpty();
    for (QAction* a : {actDescription, actKeywords, actLinks})
      if (a) a->setEnabled(savedProject);
    updateImageSizeInfo();
  }

}  // namespace stencil::gui
