#include "mainWindow.hpp"
#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "chatPlanTarget.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "infoDialog.hpp"
#include "mediaLoader.hpp"
#include "notifications.hpp"
#include "dataExportController.hpp"
#include "remoteSession.hpp"
#include "projectTransferController.hpp"
#include "shortcutsDialog.hpp"

#include <QKeySequence>

// The info and shortcuts windows, hotkey overrides and the idle status line.

namespace stencil::gui {

  void MainWindow::openInfo() {
    InfoDialog dlg(this);
    execMaybePopover(dlg, actInfo_);   // the controls/shortcuts window grows out of its icon too
  }

  // Open the rebind dialog, then persist overrides and re-apply them to the
  // live QActions without a restart.
  void MainWindow::openShortcuts() {
    // In config order (the browser walks HOTKEY_DEFS), not the hash's.
    QVector<ShortcutsDialog::Entry> entries;
    for (const QString& id : hotkeyOrder_) {
      ShortcutsDialog::Entry e;
      e.id = id;
      e.label = hotkeyLabels_.value(id);
      e.defaultSeq = hotkeyDefaults_.value(id);
      e.currentSeq = hotkeys_.value(id, e.defaultSeq);
      entries.push_back(e);
    }
    ShortcutsDialog dlg(entries, this);
    // Live-apply (browser parity: hotkeys.save on every set) — Close is the only way out.
    connect(&dlg, &ShortcutsDialog::overridesChanged, this,
            [this, &dlg] { applyHotkeyOverrides(dlg.overrides()); });
    connect(&dlg, &ShortcutsDialog::allReset, this,
            [this] { notify_->success(QStringLiteral("Hotkeys reset to defaults")); });
    connect(&dlg, &ShortcutsDialog::conflict, this,
            [this](const QString& msg) { notify_->error(msg); });
    execMaybePopover(dlg, actShortcuts_);   // grows out of its icon too
  }

  // Persist `overrides` and re-apply them to the live QActions without a restart.
  void MainWindow::applyHotkeyOverrides(const QHash<QString, QString>& overrides) {
    // Rebuild the effective map: defaults, then overrides on top.
    hotkeys_ = hotkeyDefaults_;
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
      hotkeys_.insert(it.key(), it.value());
    if (!incognito_) fileStore::saveHotkeys(overrides);  // incognito suppresses

    // Warn (but still apply) if two distinct ids now resolve to the same
    // non-empty sequence — mirrors the browser's duplicate-binding caution.
    // Sequences are normalized to PortableText so equivalent spellings collide.
    QHash<QString, QString> seen;  // normalized seq -> first id using it
    for (auto it = hotkeys_.begin(); it != hotkeys_.end(); ++it) {
      const QString seq =
          QKeySequence(it.value()).toString(QKeySequence::PortableText);
      if (seq.isEmpty()) continue;  // unset bindings are never duplicates
      const auto prior = seen.constFind(seq);
      if (prior != seen.constEnd()) {
        auto label = [this](const QString& id) {
          const QString l = hotkeyLabels_.value(id);
          return l.isEmpty() ? id : l;
        };
        // Notifications has Info/Success/Error levels; Error is the strongest
        // visual cue for this caution. Still applies below (warn, don't block).
        // Comparison stays PortableText (above); only the shown seq is native.
        const QString shown = QKeySequence(seq).toString(QKeySequence::NativeText);
        notify_->error(QString("Duplicate shortcut: '%1' is bound to %2 and %3")
                           .arg(shown, label(prior.value()), label(it.key())));
        break;  // one warning is enough; still applies below
      }
      seen.insert(seq, it.key());
    }

    // Re-apply to the live actions. platformizeSeq keeps the delete combos on the
    // macOS Backspace key (no-op for everything else / off macOS).
    for (auto it = hotkeyActions_.begin(); it != hotkeyActions_.end(); ++it) {
      const QString seq = hotkeys_.value(it.key(), hotkeyDefaults_.value(it.key()));
      it.value()->setShortcut(QKeySequence(platformizeSeq(seq)));
    }
  }

  void MainWindow::updateStatusIdle() {
    // The coordinate bar reads out the cursor and nothing else: empty with the pointer off
    // the canvas, and empty when there is no image at all (the canvas shows its own "Open
    // an image…" invitation, and the browser's bar is blank there too).
    status_->setText(QString());
  }

}  // namespace stencil::gui
