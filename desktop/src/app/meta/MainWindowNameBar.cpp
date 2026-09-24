#include "MainWindow.hpp"
#include "../../support/skinPrefs.hpp"
#include "RemoteSession.hpp"
#include <QToolButton>
#include "MainWindow.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "DockZonesOverlay.hpp"
#include "OpenImageDialog.hpp"
#include "CanvasWidget.hpp"
#include "guiHelpers.hpp"
#include "SearchCombo.hpp"
#include "InfoDialog.hpp"
#include "LinksDialog.hpp"
#include "DescriptionDialog.hpp"
#include "KeywordsDialog.hpp"
#include "MediaLoader.hpp"
#include "ProjectsDialog.hpp"
#include "ConnectDialog.hpp"
#include "RemoteSyncController.hpp"
#include "LiveFeed.hpp"
#include "ServerClient.hpp"
#include "SettingsDialog.hpp"
#include "ShortcutsDialog.hpp"
#include "theme.hpp"
#include "../../support/control/reveal/controlReveal.hpp"
#include "../../support/modal/modalChrome.hpp"
#include "../../support/motion/ShimmerOverlay.hpp"

#include <QLineEdit>
#include <QTimer>

// The name row's ✎/🎨/✓/✗ chips and the field's two styles.

namespace stencil::gui {

  // ✓/✗ only IN edit mode, ✎ only OUT of it; ✓ enabled only for a changed, valid name.
  void MainWindow::refreshProjectNameButtons() {
    if (!nameBar.field || !nameBar.accept || !nameBar.cancel) return;
    // Toggle the QWidgetActions (not the widgets) so the toolbar re-lays-out. revealControls is a no-op when the state matches.
    const auto chips = nameBar.chips(nameBar.field->isEnabled(), !blankColor.isEmpty());
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
      QPointer<MainWindow> self(this);
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
        const QColor c(blankColor);
        updateColorSwatch(nameBar.blankColorBtn, c.isValid() ? c : QColor("#ffffff"));
      }
    }
    if (!nameBar.editing) return;
    const QString v = nameBar.field->text().trimmed();
    // The CURRENT name: the server link's for a server-linked session, else the local one.
    const QString current = !remoteSession->getLink().id.isEmpty() ? remoteSession->getLink().name : activeProjectName();
    const bool changed = v != current;
    bool ok = changed;
    // No rest-state tooltip on ✓ (browser parity); only a REJECTED name explains itself.
    QString reason;
    if (changed && remoteSession->getLink().id.isEmpty()) {
      const auto check = checkProjectName(v, activeProjectId);
      ok = check.ok;
      if (!ok) reason = QString::fromStdString(check.reason);
    } else if (changed) {  // server project: uniqueness is the server's job
      ok = !v.isEmpty();
      if (!ok) reason = QStringLiteral("Enter a name");
    }
    nameBar.accept->setEnabled(ok);
    nameBar.accept->setCursor(ok ? Qt::PointingHandCursor : Qt::ForbiddenCursor);
    nameBar.accept->setToolTip(reason);
  }

  // Editing → accent-outlined input; read-only → a plain title with NO border (browser title look).
  void MainWindow::applyProjectNameStyle(bool editing) {
    if (!nameBar.field) return;
    const QString color = incognito ? QString() : currentProjectColor();
    const QColor c(color);
    // Bold + a lighter grey than the browser's #80868f: Qt can't give a QLineEdit the legibility text-shadow.
    const QString fg = (!color.isEmpty() && c.isValid()) ? c.name()
                       : support::isWebcore()
                             ? themePalette(paintedDark, settings.accentColor).textMain.name()
                             : QStringLiteral("#9aa0a8");
    if (editing) {
      const QColor accent = accentPrimary(settings.accentColor);
      nameBar.field->setStyleSheet(
          QString("QLineEdit{color:%1;font-weight:600;border:1px solid %2;border-radius:6px;"
                  "background:palette(base);padding:2px 6px;}"
                  "QLineEdit:focus{border:1px solid %2;}")
              .arg(fg, accent.name()));
    } else {
      // The hover ring has to live HERE: this per-widget sheet outranks the app-wide one, so the themed
      // `QLineEdit#projectNameField:hover` never applied. Same ring strength as the browser's two stacked 45% layers.
      const QColor accent = accentPrimary(settings.accentColor);
      const QString ring = QString("rgba(%1,%2,%3,0.45)")
                               .arg(accent.red())
                               .arg(accent.green())
                               .arg(accent.blue());
      nameBar.field->setStyleSheet(
          QString("QLineEdit{color:%1;font-weight:600;border:1px solid transparent;"
                  "border-radius:6px;background:transparent;padding:3px 8px;}"
                  "QLineEdit:hover{border:2px solid %2;padding:2px 7px;}"
                  "QLineEdit:focus{border:1px solid transparent;}")
              .arg(fg, ring));
    }
  }

}  // namespace stencil::gui
