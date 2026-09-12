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

  // ✓/✗ only IN edit mode, ✎ only OUT of it; ✓ enabled only for a changed, valid name.
  void MainWindow::refreshProjectNameButtons() {
    if (!nameBar_.field || !nameBar_.accept || !nameBar_.cancel) return;
    // Toggle the QWidgetActions (not the widgets) so the toolbar re-lays-out. revealControls is a no-op when the state matches.
    const auto chips = nameBar_.chips(nameBar_.field->isEnabled(), !blankColor_.isEmpty());
    revealControls(nameBar_.accept, chips.marks);
    revealControls(nameBar_.cancel, chips.marks);
    // ✎/🎨 keep their slots and are painted out (the browser's `visibility: hidden`), or the "?" shifts sideways.
    const bool affordable = chips.affordances;
    const auto placeAffordances = [this](bool on) {
      if (nameBar_.edit) nameBar_.edit->setVisible(on);
      if (nameBar_.colorBtn) nameBar_.colorBtn->setVisible(on);
      setPaintedOut(nameBar_.edit, on && !nameBar_.hover);
      setPaintedOut(nameBar_.colorBtn, on && !nameBar_.hover);
    };
    if (!affordable) {
      placeAffordances(false);
    } else if (nameBar_.edit && nameBar_.edit->isVisible()) {
      placeAffordances(true);   // already there: nothing is coming or going
    } else {
      // Leaving edit mode: wait for the ✓/✗ slots to close before ✎/🎨 take them, or all four sit in the row at once.
      QPointer<MainWindow> self(this);
      QTimer::singleShot(CONTROL_REVEAL_OUT_MS, this, [self, placeAffordances] {
        if (!self) return;
        placeAffordances(self->nameBar_.field && self->nameBar_.field->isEnabled()
                         && !self->nameBar_.editing);
      });
    }
    // Shown only for a blank image (recolourable), saved or not; the icon is a live swatch of the fill.
    if (nameBar_.blankColorBtn) {
      nameBar_.blankColorBtn->setVisible(chips.blankSwatch);   // a plain layout widget, gated directly
      if (chips.blankSwatch) {
        // Same chip recipe as the line-style colour button.
        const QColor c(blankColor_);
        updateColorSwatch(nameBar_.blankColorBtn, c.isValid() ? c : QColor("#ffffff"));
      }
    }
    if (!nameBar_.editing) return;
    const QString v = nameBar_.field->text().trimmed();
    // The CURRENT name: the server link's for a server-linked session, else the local one.
    const QString current = !remoteSession_->link().id.isEmpty() ? remoteSession_->link().name : activeProjectName();
    const bool changed = v != current;
    bool ok = changed;
    // No rest-state tooltip on ✓ (browser parity); only a REJECTED name explains itself.
    QString reason;
    if (changed && remoteSession_->link().id.isEmpty()) {
      const auto check = checkProjectName(v, activeProjectId_);
      ok = check.ok;
      if (!ok) reason = QString::fromStdString(check.reason);
    } else if (changed) {  // server project: uniqueness is the server's job
      ok = !v.isEmpty();
      if (!ok) reason = QStringLiteral("Enter a name");
    }
    nameBar_.accept->setEnabled(ok);
    nameBar_.accept->setCursor(ok ? Qt::PointingHandCursor : Qt::ForbiddenCursor);
    nameBar_.accept->setToolTip(reason);
  }

  // Editing → accent-outlined input; read-only → a plain title with NO border (browser title look).
  void MainWindow::applyProjectNameStyle(bool editing) {
    if (!nameBar_.field) return;
    const QString color = incognito_ ? QString() : currentProjectColor();
    const QColor c(color);
    // Bold + a lighter grey than the browser's #80868f: Qt can't give a QLineEdit the legibility text-shadow.
    const QString fg =
        (!color.isEmpty() && c.isValid()) ? c.name() : QStringLiteral("#9aa0a8");
    if (editing) {
      const QColor accent = accentPrimary(settings_.accentColor);
      nameBar_.field->setStyleSheet(
          QString("QLineEdit{color:%1;font-weight:600;border:1px solid %2;border-radius:6px;"
                  "background:palette(base);padding:2px 6px;}"
                  "QLineEdit:focus{border:1px solid %2;}")
              .arg(fg, accent.name()));
    } else {
      // The hover ring has to live HERE: this per-widget sheet outranks the app-wide one, so the themed
      // `QLineEdit#projectNameField:hover` never applied. Same ring strength as the browser's two stacked 45% layers.
      const QColor accent = accentPrimary(settings_.accentColor);
      const QString ring = QString("rgba(%1,%2,%3,0.45)")
                               .arg(accent.red())
                               .arg(accent.green())
                               .arg(accent.blue());
      nameBar_.field->setStyleSheet(
          QString("QLineEdit{color:%1;font-weight:600;border:1px solid transparent;"
                  "border-radius:6px;background:transparent;padding:3px 8px;}"
                  "QLineEdit:hover{border:2px solid %2;padding:2px 7px;}"
                  "QLineEdit:focus{border:1px solid transparent;}")
              .arg(fg, ring));
    }
  }

}  // namespace stencil::gui
