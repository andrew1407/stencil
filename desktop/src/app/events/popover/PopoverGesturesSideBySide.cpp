// Side-by-side windows (support::multiWindow; browser ui/modal/shell.js): a dialog runs as a
// non-modal window beside the others, its caller waiting in a nested loop as it does on exec(),
// and the popover's hand-offs once its own loop has unwound.
#include "MainWindow.hpp"
#include "PopoverGestures.hpp"
#include "modalReveal.hpp"
#include "hoverResync.hpp"
#include "ModalBackdrop.hpp"

#include <QApplication>
#include <QDialog>
#include <QEventLoop>
#include <QShortcut>
#include <QTimer>
#include <QToolButton>
#include <cstring>

namespace stencil::gui {

  namespace {
    // ms a closing window's flight takes (modalRevealParts.hpp CLOSE_MS 360) and a beat past it.
    constexpr int OTHERS_GONE_MS = 420;

    // One window per kind: the dialog class is what an opener's icon stands for.
    bool sameKind(const QDialog& a, const QDialog& b) {
      return std::strcmp(a.metaObject()->className(), b.metaObject()->className()) == 0;
    }
  }  // namespace

  void PopoverGestures::closeSideBySideKind(const QDialog& dlg) {
    for (const QPointer<QDialog>& open : w.pop.windows)
      if (open && open->isVisible() && sameKind(*open, dlg)) open->reject();
  }

  int PopoverGestures::runSideBySide(QDialog& dlg) {
    std::function<QRect(bool)> closeRectFor;
    closeRectFor.swap(w.pop.dialogCloseRect);
    for (const QPointer<QDialog>& open : w.pop.windows)
      if (open && open->isVisible() && sameKind(*open, dlg)) {
        open->reject();
        return QDialog::Rejected;
      }
    // A non-modal window keeps its own focus, so it carries the window chords; each one opens or
    // closes its window through the same toggle as the icon.
    for (QAction* a : w.pop.dialogActions) {
      if (!a || a->shortcut().isEmpty()) continue;
      auto* sc = new QShortcut(a->shortcut(), &dlg);
      sc->setContext(Qt::WidgetWithChildrenShortcut);
      QObject::connect(sc, &QShortcut::activated, a, [a] { QTimer::singleShot(0, a, &QAction::trigger); });
    }
    dlg.setWindowModality(Qt::NonModal);
    support::revealDialog(dlg, w.pop.dialogAnchor.data(), w.pop.dialogAnchorRect, QRect(),
                          std::move(closeRectFor));
    return holdOpen(dlg);
  }

  int PopoverGestures::runModal(QDialog& dlg) {
    dlg.setWindowModality(Qt::ApplicationModal);
    return holdOpen(dlg);
  }

  // exec() without exec's own loop: a hide does not end this one, so a mode switch can re-show the
  // window under its new modality while its caller keeps waiting.
  int PopoverGestures::holdOpen(QDialog& dlg) {
    w.pop.windows.append(&dlg);
    QPointer<QDialog> alive(&dlg);
    QEventLoop loop;
    const PopoverHost::LoopScope loopScope(w.pop, loop);
    // An accepted window acts now: the windows opened after it wait inside its loop, so they close first.
    QObject::connect(&dlg, &QDialog::finished, &loop, [this, alive, &loop](int result) {
      if (result == QDialog::Accepted) {
        const qsizetype at = w.pop.windows.indexOf(alive);
        for (qsizetype i = w.pop.windows.size() - 1; at >= 0 && i > at; --i)
          if (QDialog* later = w.pop.windows.at(i).data(); later && later->isVisible()) later->reject();
      }
      loop.quit();
    });
    QObject::connect(&dlg, &QObject::destroyed, &loop, &QEventLoop::quit);
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, &loop, &QEventLoop::quit);
    dlg.show();
    dlg.raise();
    dlg.activateWindow();
    loop.exec();
    w.pop.windows.removeAll(alive);
    w.pop.windows.removeAll(QPointer<QDialog>());
    support::resyncHover(&w);   // the icon under the pointer, not the one hovered when it opened
    return alive ? alive->result() : int(QDialog::Rejected);
  }

  void PopoverGestures::setWindowsSideBySide(bool on) {
    for (const QPointer<QDialog>& d : w.pop.windows)
      if (d && d->isVisible()) remodal(d, on ? Qt::NonModal : Qt::ApplicationModal);
    if (on) support::ModalBackdrop::clearAll();
  }

  void PopoverGestures::remodal(QDialog* dlg, Qt::WindowModality modality) {
    if (!dlg || dlg->windowModality() == modality) return;
    dlg->setProperty(support::REMODAL_PROPERTY, true);
    const QRect at = dlg->geometry();
    dlg->hide();
    if (modality == Qt::NonModal) {   // the macOS outside-click catcher goes with the modality
      for (QWidget* c : dlg->findChildren<QWidget*>(QString::fromLatin1(support::MODAL_CATCHER_NAME),
                                                     Qt::FindDirectChildrenOnly))
        c->deleteLater();
      dlg->setProperty(support::MODAL_CATCHER_ATTACHED_PROPERTY, QVariant());
    }
    dlg->setWindowModality(modality);
    dlg->show();
    dlg->setGeometry(at);
    dlg->raise();
    dlg->activateWindow();
    dlg->setProperty(support::REMODAL_PROPERTY, QVariant());
  }

  void PopoverGestures::collapseToOneWindow() {
    QList<QPointer<QDialog>> up;
    for (const QPointer<QDialog>& d : w.pop.windows)
      if (d && d->isVisible()) up.append(d);
    QDialog* keep = nullptr;
    for (const QPointer<QDialog>& d : up)
      if (QApplication::activeWindow() == d.data()) keep = d.data();
    if (!keep && !up.isEmpty()) keep = up.last().data();
    for (const QPointer<QDialog>& d : up)
      if (d && d.data() != keep) d->reject();
    if (w.pop.active) w.dismissPopover();
    // The one kept takes its dim and blur once the others have flown home, never under their flight.
    if (keep)
      QTimer::singleShot(OTHERS_GONE_MS, keep, [this, keep = QPointer<QDialog>(keep)] {
        if (!keep || !keep->isVisible() || support::multiWindow()) return;
        remodal(keep, Qt::ApplicationModal);   // one window at a time blocks the app again
        support::ModalBackdrop::behindAll(keep);
      });
  }

  void PopoverGestures::openAfterPopover() {
    if (!w.pop.peekNextAction && !w.pop.fullNextAction) return;
    QTimer::singleShot(0, &w, [this] {
      if (QAction* full = w.pop.fullNextAction.data()) {
        w.pop.fullNextAction.clear();
        full->trigger();
        return;
      }
      QToolButton* b = w.pop.peekNextButton.data();
      QAction* a = w.pop.peekNextAction.data();
      w.pop.peekNextButton.clear();
      w.pop.peekNextAction.clear();
      altPeekOpen(b, a);
    });
  }

}  // namespace stencil::gui
