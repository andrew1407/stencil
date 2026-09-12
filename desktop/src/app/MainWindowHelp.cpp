#include "MainWindow.hpp"
#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "ChatPlanTarget.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "InfoDialog.hpp"
#include "MediaLoader.hpp"
#include "Notifications.hpp"
#include "DataExportController.hpp"
#include "RemoteSession.hpp"
#include "ProjectTransferController.hpp"
#include "ShortcutsDialog.hpp"

#include <QKeySequence>

// The info and shortcuts windows, hotkey overrides and the idle status line.

namespace stencil::gui {

  void MainWindow::openInfo() {
    InfoDialog dlg(this);
    execMaybePopover(dlg, actInfo_);   // the controls/shortcuts window grows out of its icon too
  }

  void MainWindow::openShortcuts() {
    // Config order (the browser walks HOTKEY_DEFS).
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
    // Live-apply (browser hotkeys.save parity); Close is the only way out.
    connect(&dlg, &ShortcutsDialog::overridesChanged, this,
            [this, &dlg] { applyHotkeyOverrides(dlg.overrides()); });
    connect(&dlg, &ShortcutsDialog::allReset, this,
            [this] { notify_->success(QStringLiteral("Hotkeys reset to defaults")); });
    connect(&dlg, &ShortcutsDialog::conflict, this,
            [this](const QString& msg) { notify_->error(msg); });
    execMaybePopover(dlg, actShortcuts_);   // grows out of its icon too
  }

  void MainWindow::applyHotkeyOverrides(const QHash<QString, QString>& overrides) {
    hotkeys_ = hotkeyDefaults_;
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
      hotkeys_.insert(it.key(), it.value());
    if (!incognito_) fileStore::saveHotkeys(overrides);  // incognito suppresses

    // Warn (still apply) on a duplicate binding, as the browser does; PortableText so equivalent spellings collide.
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
        // Error is the strongest cue Notifications has; only the shown seq is native.
        const QString shown = QKeySequence(seq).toString(QKeySequence::NativeText);
        notify_->error(QString("Duplicate shortcut: '%1' is bound to %2 and %3")
                           .arg(shown, label(prior.value()), label(it.key())));
        break;  // one warning is enough; still applies below
      }
      seen.insert(seq, it.key());
    }

    // platformizeSeq keeps the delete combos on the macOS Backspace key.
    for (auto it = hotkeyActions_.begin(); it != hotkeyActions_.end(); ++it) {
      const QString seq = hotkeys_.value(it.key(), hotkeyDefaults_.value(it.key()));
      it.value()->setShortcut(QKeySequence(platformizeSeq(seq)));
    }
  }

  void MainWindow::updateStatusIdle() {
    // Empty with the pointer off the canvas or no image (browser parity).
    status_->setText(QString());
  }

}  // namespace stencil::gui
