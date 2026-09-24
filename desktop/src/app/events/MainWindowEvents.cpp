#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "CanvasTooltip.hpp"
#include "ChatDock.hpp"
#include "ChatMenuPanel.hpp"
#include "../../support/dockGrip.hpp"
#include "iconSet.hpp"
#include "modalReveal.hpp"
#include "Notifications.hpp"
#include "SelectionPanel.hpp"
#include "theme.hpp"
#include "../../support/control/textFocus.hpp"

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

// MainWindow::eventFilter and its chain; the other handlers are the MainWindowEvents*.cpp siblings.

namespace stencil::gui {

  namespace {
    bool isTextEntry(QObject* obj) {
      return qobject_cast<QLineEdit*>(obj) || qobject_cast<QPlainTextEdit*>(obj) ||
             qobject_cast<QTextEdit*>(obj);
    }
    // A press on something that takes no focus, or Escape, blurs the field (browser parity).
    void blurFieldOn(QWidget* win, QObject* obj, QEvent* event) {
      QWidget* focus = QApplication::focusWidget();
      if (!focus || focus->window() != win || !support::isTextEntry(focus)) return;
      auto* w = qobject_cast<QWidget*>(obj);
      if (!w || w->window() != win) return;
      if (event->type() == QEvent::KeyPress) {
        const bool isOwn = w == focus || focus->isAncestorOf(w);
        if (isOwn && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape)
          QTimer::singleShot(0, focus, [focus] { focus->clearFocus(); });
        return;
      }
      for (QWidget* p = w; p; p = p->isWindow() ? nullptr : p->parentWidget())
        if (p->isEnabled() && (p->focusPolicy() & Qt::ClickFocus)) return;
      focus->clearFocus();
    }
  }  // namespace

  // A chain of handlers in THIS order: void ones observe, an optional-returning one that answers ends the chain.
  // tests/MainWindow.composition.gui.cpp pins the verdicts.
  bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    filterPointerChrome(obj, event);
    filterDockChrome(obj, event);
    if (const auto r = filterKeyClaims(obj, event)) return *r;
    if ((event->type() == QEvent::MouseButtonPress || event->type() == QEvent::KeyPress) && !pop.active)
      blurFieldOn(this, obj, event);
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
      static const QKeySequence::StandardKey EDITING[] = {
          QKeySequence::DeleteStartOfWord, QKeySequence::DeleteEndOfWord,
          QKeySequence::DeleteCompleteLine, QKeySequence::MoveToPreviousWord,
          QKeySequence::MoveToNextWord,     QKeySequence::SelectPreviousWord,
          QKeySequence::SelectNextWord,     QKeySequence::MoveToStartOfLine,
          QKeySequence::MoveToEndOfLine,    QKeySequence::SelectStartOfLine,
          QKeySequence::SelectEndOfLine,    QKeySequence::Undo,
          QKeySequence::Redo,               QKeySequence::SelectAll};
      for (const auto key : EDITING) {
        if (ke->matches(key)) {
          event->accept();
          return true;
        }
      }
    }
    // Escape, app-wide (KeyPress AND ShortcutOverride): closes a popover first, then leaves fullscreen (fs.active — isFullScreen() is unreliable on macOS).
    if (pop.active &&
        (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
      auto* w = qobject_cast<QWidget*>(obj);
      if (w && (w->window() == this || w == pop.active.data() ||
                pop.active->isAncestorOf(w))) {
        pop.peekAction.clear();   // Escape is deliberate: it ends a peek for good
        dismissPopover();
        return true;
      }
    }
    if ((event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape && chatCompactShowing()) {
      if (event->type() == QEvent::KeyPress && actChat) actChat->setChecked(false);
      return true;   // the ShortcutOverride claim keeps focused widgets from eating it
    }
    if ((event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape && fs.active) {
      toggleFullscreen();
      return true;
    }
    return {};   // nothing here answered — the chain goes on
  }


}  // namespace stencil::gui
