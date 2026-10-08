#include "ProjectTitleController.hpp"
#include "windowSheets.hpp"
#include "CanvasWidget.hpp"
#include "ProjectNameBar.hpp"
#include "RemoteSession.hpp"
#include "RemoteState.hpp"
#include "WindowActions.hpp"
#include "fileStore.hpp"
#include "theme.hpp"
#include "../../support/control/reveal/controlReveal.hpp"
#include "../../support/skinPrefs.hpp"

#include <QAction>
#include <QLineEdit>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>

// The title, the name field and its chips, and the field's two styles. Renaming in place is in
// ProjectTitleControllerEdit.cpp.

namespace stencil::gui {

  ProjectTitleController::ProjectTitleController(QWidget* host, CanvasWidget* canvas, QScrollArea* scroll,
                                                 const bool& incognito, const QString& activeProjectId,
                                                 ProjectNameBar& nameBar, WindowActions& acts,
                                                 RemoteState& remote, const QPointer<Notifications>& notify,
                                                 const Settings& settings, Hooks hooks)
      : QObject(host), host(host), canvas(canvas), scroll(scroll), incognito(incognito),
        activeProjectId(activeProjectId), nameBar(nameBar), acts(acts), remote(remote), notify(notify),
        settings(settings), h(std::move(hooks)) {}

  void ProjectTitleController::updateProjectTitle() {
    QString name;
    bool editable = false;
    const bool remote = !this->remote.session->getLink().id.isEmpty();
    if (incognito) {
      name = "Incognito";
    } else if (!activeProjectId.isEmpty()) {
      name = h.activeProjectName();
      editable = true;   // an active LOCAL project is always renameable/colourable (even if the
                         // registry name lookup momentarily returns empty and we fall back to the id)
    } else if (remote) {
      name = this->remote.session->getLink().name;   // server-linked session (no local project id)
      editable = true;   // server projects are renameable/colourable too (pushed via commitProjectName)
    }
    if (name.isEmpty() && canvas && canvas->hasImage())
      name = canvas->imageBaseName();   // show the image name until it's a saved project
    host->setWindowTitle(name.isEmpty() ? QStringLiteral("Stencil")
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
    if (acts.projectColor) acts.projectColor->setEnabled(hasProject);
    // With no custom colour there is nothing to clear, so the row greys out.
    if (acts.projectColorClear)
      acts.projectColorClear->setEnabled(hasProject && !h.projectColor().isEmpty());
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
    for (QAction* a : {acts.description, acts.keywords, acts.links})
      if (a) a->setEnabled(savedProject);
    h.imageInfoChanged();
  }

  // ✓/✗ only IN edit mode, ✎ only OUT of it; ✓ enabled only for a changed, valid name.
  void ProjectTitleController::refreshProjectNameButtons() {
    if (!nameBar.field || !nameBar.accept || !nameBar.cancel) return;
    // Toggle the QWidgetActions (not the widgets) so the toolbar re-lays-out. revealControls is a no-op when the state matches.
    const auto chips = nameBar.chips(nameBar.field->isEnabled(), !h.blankColor().isEmpty());
    revealControls(nameBar.accept, chips.marks);
    revealControls(nameBar.cancel, chips.marks);
    // ✎/🎨 keep their slots and are painted out (the browser's `visibility: hidden`), or the "?" shifts sideways.
    const bool affordable = chips.affordances;
    const auto placeAffordances = [this](bool on) {
      if (nameBar.edit) nameBar.edit->setVisible(on);
      if (nameBar.colorBtn) nameBar.colorBtn->setVisible(on);
      setPaintedOut(nameBar.edit, on && !nameBar.hover);
      setPaintedOut(nameBar.colorBtn, on && !nameBar.hover);
    };
    if (!affordable) {
      placeAffordances(false);
    } else if (nameBar.edit && nameBar.edit->isVisible()) {
      placeAffordances(true);   // already there: nothing is coming or going
    } else {
      // Leaving edit mode: wait for the ✓/✗ slots to close before ✎/🎨 take them, or all four sit in the row at once.
      QPointer<ProjectTitleController> self(this);
      QTimer::singleShot(CONTROL_REVEAL_OUT_MS, this, [self, placeAffordances] {
        if (!self) return;
        placeAffordances(self->nameBar.field && self->nameBar.field->isEnabled()
                         && !self->nameBar.editing);
      });
    }
    // Shown only for a blank image (recolourable), saved or not; the icon is a live swatch of the fill.
    if (nameBar.blankColorBtn) {
      nameBar.blankColorBtn->setVisible(chips.blankSwatch);   // a plain layout widget, gated directly
      if (chips.blankSwatch) {
        // Same chip recipe as the line-style colour button.
        const QColor c(h.blankColor());
        h.paintSwatch(nameBar.blankColorBtn, c.isValid() ? c : QColor("#ffffff"));
      }
    }
    if (!nameBar.editing) return;
    const QString v = nameBar.field->text().trimmed();
    // The CURRENT name: the server link's for a server-linked session, else the local one.
    const QString current = !remote.session->getLink().id.isEmpty() ? remote.session->getLink().name : h.activeProjectName();
    const bool changed = v != current;
    bool ok = changed;
    // No rest-state tooltip on ✓ (browser parity); only a REJECTED name explains itself.
    QString reason;
    if (changed && remote.session->getLink().id.isEmpty()) {
      const auto check = h.checkName(v, activeProjectId);
      ok = check.ok;
      if (!ok) reason = check.reason;
    } else if (changed) {  // server project: uniqueness is the server's job
      ok = !v.isEmpty();
      if (!ok) reason = QStringLiteral("Enter a name");
    }
    nameBar.accept->setEnabled(ok);
    nameBar.accept->setCursor(ok ? Qt::PointingHandCursor : Qt::ForbiddenCursor);
    nameBar.accept->setToolTip(reason);
  }

  // Editing → accent-outlined input; read-only → a plain title with NO border (browser title look).
  void ProjectTitleController::applyProjectNameStyle(bool editing) {
    if (!nameBar.field) return;
    const QString color = incognito ? QString() : h.projectColor();
    const QColor c(color);
    // Bold + a lighter grey than the browser's #80868f: Qt can't give a QLineEdit the legibility text-shadow.
    const QString fg = (!color.isEmpty() && c.isValid()) ? c.name()
                       : support::isWebcore()
                             ? themePalette(h.dark(), settings.accentColor).textMain.name()
                             : QStringLiteral("#9aa0a8");
    if (editing) {
      const QColor accent = accentPrimary(settings.accentColor);
      nameBar.field->setStyleSheet(support::projectNameEditingSheet(fg, accent));
    } else {
      const QColor accent = accentPrimary(settings.accentColor);
      nameBar.field->setStyleSheet(support::projectNameRestingSheet(fg, accent));
    }
  }

}  // namespace stencil::gui
