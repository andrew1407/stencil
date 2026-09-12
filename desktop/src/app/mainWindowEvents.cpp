#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "canvasWidget.hpp"
#include "canvasTooltip.hpp"
#include "chatDock.hpp"
#include "chatMenuPanel.hpp"
#include "../support/dockGrip.hpp"
#include "iconSet.hpp"
#include "modalReveal.hpp"
#include "notifications.hpp"
#include "selectionPanel.hpp"
#include "theme.hpp"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWheelEvent>
#include <optional>

// MainWindow::eventFilter and its chain; the other handlers are the mainWindowEvents*.cpp siblings.

namespace stencil::gui {

  static bool isTextEntry(QObject* obj) {
    return qobject_cast<QLineEdit*>(obj) || qobject_cast<QPlainTextEdit*>(obj) ||
           qobject_cast<QTextEdit*>(obj);
  }

  // A chain of handlers in THIS order: void ones observe, an optional-returning one that answers ends the chain.
  // tests/mainWindow.composition.gui.cpp pins the verdicts.
  bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    filterPointerChrome(obj, event);
    filterDockChrome(obj, event);
    if (const auto r = filterKeyClaims(obj, event)) return *r;
    if (const auto r = filterPopoverGestures(obj, event)) return *r;
    if (const auto r = filterPopoverButton(obj, event)) return *r;
    if (const auto r = filterZoomAndLogo(obj, event)) return *r;
    if (const auto r = filterCanvasViewport(obj, event)) return *r;
    if (const auto r = filterProjectNameBar(obj, event)) return *r;
    return QMainWindow::eventFilter(obj, event);
  }

  std::optional<bool> MainWindow::filterKeyClaims(QObject* obj, QEvent* event) {
    // While a TEXT BOX has focus the editing chords belong to it (⌥⌫ deleted the selected LINE mid-typing); claiming ShortcutOverride hands the key back.
    if (event->type() == QEvent::ShortcutOverride && isTextEntry(obj)) {
      auto* ke = static_cast<QKeyEvent*>(event);
      static const QKeySequence::StandardKey kEditing[] = {
          QKeySequence::DeleteStartOfWord, QKeySequence::DeleteEndOfWord,
          QKeySequence::DeleteCompleteLine, QKeySequence::MoveToPreviousWord,
          QKeySequence::MoveToNextWord,     QKeySequence::SelectPreviousWord,
          QKeySequence::SelectNextWord,     QKeySequence::MoveToStartOfLine,
          QKeySequence::MoveToEndOfLine,    QKeySequence::SelectStartOfLine,
          QKeySequence::SelectEndOfLine,    QKeySequence::Undo,
          QKeySequence::Redo,               QKeySequence::SelectAll};
      for (const auto key : kEditing) {
        if (ke->matches(key)) {
          event->accept();
          return true;
        }
      }
    }
    // Escape, app-wide (KeyPress AND ShortcutOverride): closes a popover first, then leaves fullscreen (fs_.active — isFullScreen() is unreliable on macOS).
    if (pop_.active &&
        (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
      auto* w = qobject_cast<QWidget*>(obj);
      if (w && (w->window() == this || w == pop_.active.data() ||
                pop_.active->isAncestorOf(w))) {
        pop_.peekAction.clear();   // Escape is deliberate: it ends a peek for good
        dismissPopover();
        return true;
      }
    }
    if ((event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape && chatCompactShowing()) {
      if (event->type() == QEvent::KeyPress && actChat_) actChat_->setChecked(false);
      return true;   // the ShortcutOverride claim keeps focused widgets from eating it
    }
    if ((event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape && fs_.active) {
      toggleFullscreen();
      return true;
    }
    return {};   // nothing here answered — the chain goes on
  }


}  // namespace stencil::gui
