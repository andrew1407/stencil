// The window's settings windows: the assistant settings the dock's gear raises, app settings, the
// info and shortcuts windows, and applying the hotkey overrides.
#include "MainWindow.hpp"
#include "SettingsDialogs.hpp"
#include "mainWindowShellParts.hpp"
#include "modalReveal.hpp"
#include "AssistantSettingsDialog.hpp"
#include "SettingsDialog.hpp"
#include "mainWindowHelpers.hpp"
#include "InfoDialog.hpp"
#include "Notifications.hpp"
#include "ShortcutsDialog.hpp"
#include "ChatDock.hpp"

#include <QKeySequence>
#include <functional>

namespace stencil::gui {

  // llm-contract.md §5. The gear sits in the dock's "…" menu, closed by now — so the "…" anchors the flight.
  void SettingsDialogs::openAssistantSettings() {
    openAssistantSettingsFrom(w.chatDock ? w.chatDock->moreButton() : nullptr);
  }

  void SettingsDialogs::openAssistantSettingsFrom(QWidget* anchor, const QRect& anchorRect) {
    // The WINDOW, whichever door opened it: exec() centres a dialog on its parent's window, and the
    // outside-press catcher covers only that window, so a floating chat is never the parent.
    AssistantSettingsDialog dlg(w.settings, &w);
    wireWindowSwitching(dlg, w.pop.dialogActions, w.acts.assistantSettings);
    support::revealDialog(dlg, anchor, anchorRect);
    if (dlg.exec() == QDialog::Accepted) w.applySettings(dlg.result(), true);
  }

  void SettingsDialogs::openSettings() {
    SettingsDialog dlg(w.settings, &w);
    // Live-apply: every row persists itself as it changes; no Save/Cancel.
    dlg.setOnChange([this](const Settings& s) { w.applySettings(s, true); });
    QObject::connect(&dlg, &SettingsDialog::visualsReset, &w,
                     [this] { w.notify->success(QStringLiteral("Visual defaults reset")); });
    w.execMaybePopover(dlg, w.acts.settings);
    // Settle-up catches a field left mid-edit; skipped when nothing changed.
    if (fileStore::settingsToJson(dlg.result()) != fileStore::settingsToJson(w.settings))
      w.applySettings(dlg.result(), true);
  }

  void SettingsDialogs::openInfo() {
    InfoDialog dlg(&w);
    w.execMaybePopover(dlg, w.acts.info);   // the controls/shortcuts window grows out of its icon too
  }

  void SettingsDialogs::openShortcuts() {
    // Config order (the browser walks HOTKEY_DEFS).
    QVector<ShortcutsDialog::Entry> entries;
    for (const QString& id : w.keys.order) {
      ShortcutsDialog::Entry e;
      e.id = id;
      e.label = w.keys.labels.value(id);
      e.defaultSeq = w.keys.defaults.value(id);
      e.currentSeq = w.keys.bound.value(id, e.defaultSeq);
      entries.push_back(e);
    }
    ShortcutsDialog dlg(entries, &w);
    // Live-apply (browser hotkeys.save parity); Close is the only way out.
    QObject::connect(&dlg, &ShortcutsDialog::overridesChanged, &w,
                     [this, &dlg] { applyHotkeyOverrides(dlg.overrides()); });
    QObject::connect(&dlg, &ShortcutsDialog::allReset, &w,
                     [this] { w.notify->success(QStringLiteral("Hotkeys reset to defaults")); });
    QObject::connect(&dlg, &ShortcutsDialog::conflict, &w,
                     [this](const QString& msg) { w.notify->error(msg); });
    w.execMaybePopover(dlg, w.acts.shortcuts);   // grows out of its icon too
  }

  void SettingsDialogs::applyHotkeyOverrides(const QHash<QString, QString>& overrides) {
    w.keys.bound = w.keys.defaults;
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
      w.keys.bound.insert(it.key(), it.value());
    if (!w.incognito) fileStore::saveHotkeys(overrides);  // incognito suppresses

    // Warn (still apply) on a duplicate binding, as the browser does; PortableText so equivalent spellings collide.
    QHash<QString, QString> seen;  // normalized seq -> first id using it
    for (auto it = w.keys.bound.begin(); it != w.keys.bound.end(); ++it) {
      const QString seq =
          QKeySequence(it.value()).toString(QKeySequence::PortableText);
      if (seq.isEmpty()) continue;  // unset bindings are never duplicates
      const auto prior = seen.constFind(seq);
      if (prior != seen.constEnd()) {
        auto label = [this](const QString& id) {
          const QString l = w.keys.labels.value(id);
          return l.isEmpty() ? id : l;
        };
        // Error is the strongest cue Notifications has; only the shown seq is native.
        const QString shown = QKeySequence(seq).toString(QKeySequence::NativeText);
        w.notify->error(QString("Duplicate shortcut: '%1' is bound to %2 and %3")
                           .arg(shown, label(prior.value()), label(it.key())));
        break;  // one warning is enough; still applies below
      }
      seen.insert(seq, it.key());
    }

    // platformizeSeq keeps the delete combos on the macOS Backspace key.
    for (auto it = w.keys.actions.begin(); it != w.keys.actions.end(); ++it) {
      const QString seq = w.keys.bound.value(it.key(), w.keys.defaults.value(it.key()));
      it.value()->setShortcut(QKeySequence(platformizeSeq(qtKeySeq(seq))));
    }
    w.refreshActions();   // the drawing toggle's key belongs to whichever of Start / Stop is live
  }

}  // namespace stencil::gui
