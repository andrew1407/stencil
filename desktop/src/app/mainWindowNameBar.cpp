#include "mainWindow.hpp"
#include "remoteSession.hpp"
#include <QToolButton>
#include "mainWindow.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "dockZonesOverlay.hpp"
#include "openImageDialog.hpp"
#include "canvasWidget.hpp"
#include "guiHelpers.hpp"
#include "searchCombo.hpp"
#include "infoDialog.hpp"
#include "linksDialog.hpp"
#include "descriptionDialog.hpp"
#include "keywordsDialog.hpp"
#include "mediaLoader.hpp"
#include "projectsDialog.hpp"
#include "connectDialog.hpp"
#include "remoteSyncController.hpp"
#include "liveFeed.hpp"
#include "serverClient.hpp"
#include "settingsDialog.hpp"
#include "shortcutsDialog.hpp"
#include "theme.hpp"
#include "../support/controlReveal.hpp"
#include "../support/modalChrome.hpp"
#include "../support/shimmerOverlay.hpp"

#include <QLineEdit>
#include <QTimer>

// The name row's ✎/🎨/✓/✗ chips and the field's two styles.

namespace stencil::gui {

  // Browser-like: the ✓/✗ buttons show only IN edit mode; the ✎ pencil shows only OUT of it.
  // ✓ is enabled only for a changed, valid name (its tooltip carries the reason when disabled).
  void MainWindow::refreshProjectNameButtons() {
    if (!projectName_ || !projectNameAccept_ || !projectNameCancel_) return;
    const bool editable = projectName_->isEnabled();
    // Toggle the QWidgetActions (not the widgets) so the toolbar actually re-lays-out. In edit
    // mode only ✓/✗ show; out of it only ✎ + 🎨 show — exactly like the browser topbar.
    // They arrive and leave as SAND, like the browser's ✓/✗ (markIn / markOut) — a bare
    // setVisible blinked them in and out. revealControls is a no-op when the
    // state already matches, so refreshActions may call this as often as it likes.
    revealControls(projectNameAccept_, nameEditing_);
    revealControls(projectNameCancel_, nameEditing_);
    // ✎/🎨 reveal only on name-group hover (✓/✗ replace them while editing) and
    // must not MOVE anything: they keep their slots and are merely painted out —
    // the browser's `visibility: hidden`. Removing slots shoved the "?" sideways.
    const bool affordable = editable && !nameEditing_;
    const auto placeAffordances = [this](bool on) {
      if (projectNameEdit_) projectNameEdit_->setVisible(on);
      if (projectColorBtn_) projectColorBtn_->setVisible(on);
      setPaintedOut(projectNameEdit_, on && !nameHover_);
      setPaintedOut(projectColorBtn_, on && !nameHover_);
    };
    if (!affordable) {
      placeAffordances(false);
    } else if (projectNameEdit_ && projectNameEdit_->isVisible()) {
      placeAffordances(true);   // already there: nothing is coming or going
    } else {
      // Leaving edit mode: the ✓/✗ are still sliding out, and giving ✎/🎨 their slots now
      // put all four in the row at once — it widened and the pair appeared BESIDE the marks
      // still flying instead of in their place. Wait for the
      // slots to close, then take them; re-checked on arrival, since anything may have
      // changed in the meantime.
      QPointer<MainWindow> self(this);
      QTimer::singleShot(kControlRevealOutMs, this, [self, placeAffordances] {
        if (!self) return;
        placeAffordances(self->projectName_ && self->projectName_->isEnabled()
                         && !self->nameEditing_);
      });
    }
    // Blank-colour button: shown only when this session is a blank image (recolourable), regardless
    // of whether it's a saved/editable project (in-memory recolour works for unsaved blanks too).
    // Paint its icon as a live swatch of the current fill colour.
    if (blankColorBtn_) {
      const bool showBlank = !blankColor_.isEmpty() && !nameEditing_;
      blankColorBtn_->setVisible(showBlank);   // now a plain layout widget, gated directly
      if (showBlank) {
        // Same input-palette chip recipe as the line-style colour button
        // (inset swatch rect + luminance-tuned outline, theme/accent tracked).
        const QColor c(blankColor_);
        updateColorSwatch(blankColorBtn_, c.isValid() ? c : QColor("#ffffff"));
      }
    }
    if (!nameEditing_) return;
    const QString v = projectName_->text().trimmed();
    // Compare against the CURRENT name — remoteSession_->link().name for a server-linked session (no local id),
    // else the local name.
    const QString current = !remoteSession_->link().id.isEmpty() ? remoteSession_->link().name : activeProjectName();
    const bool changed = v != current;
    bool ok = changed;
    // No rest-state tooltip on the ✓ (user decision — the browser chips carry none);
    // only a REJECTED name explains itself.
    QString reason;
    if (changed && remoteSession_->link().id.isEmpty()) {
      const auto check = checkProjectName(v, activeProjectId_);
      ok = check.ok;
      if (!ok) reason = QString::fromStdString(check.reason);
    } else if (changed) {  // server project: uniqueness is the server's job
      ok = !v.isEmpty();
      if (!ok) reason = QStringLiteral("Enter a name");
    }
    projectNameAccept_->setEnabled(ok);
    projectNameAccept_->setCursor(ok ? Qt::PointingHandCursor : Qt::ForbiddenCursor);
    projectNameAccept_->setToolTip(reason);
  }

  // Paint the name field for its mode. Editing → accent-outlined input (focus ring visible);
  // read-only → a plain title with NO border/focus ring (matches the browser's title look), so a
  // stray single-click focus never shows an editable-looking box. Project colour is kept in both.
  void MainWindow::applyProjectNameStyle(bool editing) {
    if (!projectName_) return;
    const QString color = incognito_ ? QString() : currentProjectColor();
    const QColor c(color);
    // Default (no custom colour): a brighter grey than the browser's #80868f + bold, since Qt can't
    // give a QLineEdit the browser's legibility text-shadow — bold + a lighter grey matches the
    // perceived brightness. A custom colour is used as-is (also bold).
    const QString fg =
        (!color.isEmpty() && c.isValid()) ? c.name() : QStringLiteral("#9aa0a8");
    if (editing) {
      const QColor accent = accentPrimary(settings_.accentColor);
      projectName_->setStyleSheet(
          QString("QLineEdit{color:%1;font-weight:600;border:1px solid %2;border-radius:6px;"
                  "background:palette(base);padding:2px 6px;}"
                  "QLineEdit:focus{border:1px solid %2;}")
              .arg(fg, accent.name()));
    } else {
      // A TITLE at rest — no box — that rings under the pointer, exactly as the browser's
      // read-only #project-name-input does (transparent border, the field ring on hover).
      // The ring has to live HERE: this per-widget sheet outranks the app-wide one for
      // every property it names, so the themed `QLineEdit#projectNameField:hover` never got
      // a look in and the title stayed inert — the rule was in the stylesheet, just not
      // the stylesheet that wins.
      // The ring is the shared one: the accent at the ring's own strength, which is what the
      // browser's two stacked 45% layers come to on screen — the solid accent read far
      // brighter than the browser's beside it.
      const QColor accent = accentPrimary(settings_.accentColor);
      const QString ring = QString("rgba(%1,%2,%3,0.45)")
                               .arg(accent.red())
                               .arg(accent.green())
                               .arg(accent.blue());
      projectName_->setStyleSheet(
          QString("QLineEdit{color:%1;font-weight:600;border:1px solid transparent;"
                  "border-radius:6px;background:transparent;padding:3px 8px;}"
                  "QLineEdit:hover{border:2px solid %2;padding:2px 7px;}"
                  "QLineEdit:focus{border:1px solid transparent;}")
              .arg(fg, ring));
    }
  }

}  // namespace stencil::gui
