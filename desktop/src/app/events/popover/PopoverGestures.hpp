#pragma once
#include <functional>
#include <QPoint>
#include <QRect>
#include <Qt>

class QAction;
class QDialog;
class QToolButton;
class QWidget;

namespace stencil::gui {

  class MainWindow;

  // Browser popover.js altHover: an icon's Alt peek and the glide between peeks, the linger poll
  // that keeps an engaged peek open, and a press judged against the open popover.
  class PopoverGestures {
   public:
    explicit PopoverGestures(MainWindow& w) : w(w) {}

    // Browser popover.js altHover parity. Closes a compact chat the glide moved off first.
    void altPeekOpen(QToolButton* btn, QAction* act);
    // The same once Qt has dispatched the Enter that asked for it, and only while Alt is down.
    void altPeekOpenSoon(QToolButton* btn, QAction* act);
    // A peek opening elsewhere closes a compact chat, unless `opener` sits inside it.
    void closeCompactChatFor(QWidget* opener);
    void startLingerPoll();
    void stopLingerPoll();
    // The popover is this window's child, so its frameGeometry() is not a screen rect.
    QRect popoverRectGlobal() const;
    bool handlePopoverPress(QWidget* target, const QPoint& globalPos,
                            Qt::MouseButton button);
    // Side by side (support::multiWindow, browser modal/shell.js): `dlg` as a non-modal window beside
    // the others while its caller waits; a second open of its kind closes the one up instead.
    int runSideBySide(QDialog& dlg);
    // One window at a time: `dlg` application-modal, in the same loop, so a later switch to side by
    // side can re-show it non-modal (exec's own loop ends on any hide).
    int runModal(QDialog& dlg);
    // Every open window re-shown under the mode's modality; on, the backdrops fade away.
    void setWindowsSideBySide(bool on);
    // A popover gesture re-shapes the side-by-side window of `dlg`'s kind: that window closes.
    void closeSideBySideKind(const QDialog& dlg);
    // Once a popover's loop has unwound: the Alt glide's next peek, or the icon's full window.
    void openAfterPopover();
    // `dlg` hidden and re-shown where it stood, under `modality` (Qt takes one only across a show).
    void remodal(QDialog* dlg, Qt::WindowModality modality);
    // Multiple windows switched off: the focused side-by-side window stays (the settings dialog
    // whose box was unticked), else the newest; the rest close, a popover too (browser registry.js).
    void collapseToOneWindow();

   private:
    int holdOpen(QDialog& dlg);
    MainWindow& w;
  };

}  // namespace stencil::gui
