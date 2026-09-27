#pragma once
// What the modal openers among the shell TUs share: the chord forwarding a dialog carries while its
// own event loop runs, and the popover's motion. Private to the MainWindow shell TUs.
#include <QAction>
#include <QDialog>
#include <QObject>
#include <QShortcut>
#include <QTimer>

namespace stencil::gui {

  namespace {
    // A modal dialog runs its own event loop, so the main window's QActions never fire there: it carries copies of those
    // chords while showing and the originals are parked (a live twin with the same chord would be ambiguous).
    template <typename Actions>
    void wireWindowSwitching(QDialog& dlg, const Actions& actions, QAction* opener) {
      for (QAction* a : actions) {
        if (!a || a->shortcut().isEmpty()) continue;
        auto* sc = new QShortcut(a->shortcut(), &dlg);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        QObject::connect(sc, &QShortcut::activated, &dlg, [&dlg, a, opener] {
          // A different window's chord: close, then open that one once this dialog's loop has unwound.
          if (a != opener) QTimer::singleShot(0, a, &QAction::trigger);
          dlg.reject();
        });
      }
    }
    // The popover's motion: the dialog reveal (modalReveal.cpp) ×1.5.
    constexpr int POPOVER_OPEN_MS = 450, POPOVER_CLOSE_MS = 360;
  }  // namespace
}  // namespace stencil::gui
