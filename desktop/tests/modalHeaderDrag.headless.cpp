// Headless check of the modal shell's header drag (support/modalChromeInstall HeaderDrag).
// The header moves the dialog while the dialog is its own window — and must NOT once
// MainWindow::execMaybePopover has reparented that same dialog into the popover overlay as a
// plain Qt::Widget child, where the drag's GLOBAL points would be read as parent-relative and
// throw the compact "mini window" clean out of the overlay it is anchored to.
#include "modalChrome.hpp"

#include <QApplication>
#include <QDialog>
#include <QMouseEvent>
#include <QWidget>

#include "support/check.hpp"

namespace {

  QWidget* headerOf(QDialog& dlg) { return dlg.findChild<QWidget*>(QStringLiteral("modalHeader")); }

  // The press/move/release a drag of the header is made of, in GLOBAL coordinates.
  void dragHeader(QWidget* header, QPoint from, QPoint by) {
    const QPointF local(5, 5);
    QMouseEvent press(QEvent::MouseButtonPress, local, QPointF(from), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(header, &press);
    QMouseEvent move(QEvent::MouseMove, local, QPointF(from + by), Qt::NoButton, Qt::LeftButton,
                     Qt::NoModifier);
    QApplication::sendEvent(header, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, local, QPointF(from + by), Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(header, &release);
  }

  Qt::CursorShape cursorAfterHover(QWidget* header) {
    QEvent enter(QEvent::Enter);
    QApplication::sendEvent(header, &enter);
    return header->cursor().shape();
  }

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  // ── A window: the header is the only drag handle a frameless dialog has.
  {
    QDialog dlg;
    stencil::gui::installModalChrome(&dlg, QStringLiteral("settings"), QStringLiteral("Settings"));
    dlg.resize(300, 200);
    dlg.move(100, 100);
    QWidget* header = headerOf(dlg);
    check(header != nullptr, "the shell installs a named header");
    check(dlg.isWindow(), "a plain modal-chrome dialog is its own window");

    const QPoint before = dlg.pos();
    dragHeader(header, dlg.frameGeometry().topLeft() + QPoint(20, 10), QPoint(40, 25));
    check(dlg.pos() != before, "dragging its header moves the window");
    check(cursorAfterHover(header) == Qt::OpenHandCursor, "and the header offers the open hand");
  }

  // ── A popover: the same dialog, reparented the way execMaybePopover reparents it.
  {
    QWidget overlay;
    overlay.resize(480, 600);
    QDialog dlg;
    stencil::gui::installModalChrome(&dlg, QStringLiteral("settings"), QStringLiteral("Settings"));
    dlg.setParent(&overlay);
    dlg.setWindowFlags(Qt::Widget);
    dlg.setGeometry(QRect(0, 0, 300, 200));
    QWidget* header = headerOf(dlg);
    check(!dlg.isWindow(), "reparented into the overlay it is no longer a window");

    const QPoint before = dlg.pos();
    dragHeader(header, QPoint(600, 400), QPoint(40, 25));
    check(dlg.pos() == before, "dragging its header does NOT move the popover");
    check(cursorAfterHover(header) == Qt::ArrowCursor, "and the header does not offer to");

    // A drag that starts as a window must not carry over once it becomes a popover.
    dlg.setParent(nullptr);
    dlg.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dlg.move(50, 50);
    const QPoint asWindow = dlg.pos();
    dragHeader(header, dlg.frameGeometry().topLeft() + QPoint(20, 10), QPoint(30, 30));
    check(dlg.pos() != asWindow, "and handing it back its window makes the header work again");
  }

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}
