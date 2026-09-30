#include "ScriptEditorWidget.hpp"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QKeyEvent>
#include <QPalette>
#include <QPlainTextEdit>
#include <QStyle>

// The editor's keys and its hover / focus frame: Tab and the run chord.
namespace stencil::gui {

  /* Hover lights the halo in the accent, focus thickens the border — the browser's
   * .script-editor-wrap :hover / :focus-within, which QSS has no box-shadow for. Under a popup
   * grab the editor is the only child the menu synthesises an Enter for, so the flyout reads
   * the hover off it instead of off the halo. */
  bool ScriptEditorWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == edit && handleKey(event)) return true;
    if (watched == edit && (event->type() == QEvent::PaletteChange
                            || event->type() == QEvent::ApplicationPaletteChange))
      fadePlaceholder();
    if (style.hoverOnFrame) {
      if (watched == glow) {
        if (event->type() == QEvent::Enter) setFrameState("hovered", true);
        else if (event->type() == QEvent::Leave) setFrameState("hovered", false);
      } else if (watched == edit) {
        if (event->type() == QEvent::FocusIn) setFrameState("focused", true);
        else if (event->type() == QEvent::FocusOut) setFrameState("focused", false);
      }
      return QWidget::eventFilter(watched, event);
    }
    if (watched != edit) return QWidget::eventFilter(watched, event);
    switch (event->type()) {
      case QEvent::Enter: setFrameState("hovered", true); break;
      case QEvent::Leave: setFrameState("hovered", false); break;
      case QEvent::FocusIn: setFrameState("focused", true); break;
      case QEvent::FocusOut: setFrameState("focused", false); break;
      default: break;
    }
    return QWidget::eventFilter(watched, event);
  }

  // Half the theme's placeholder ink (browser .script-input::placeholder opacity 0.5), so the
  // example never reads as a script already typed. Read off the app palette, so a theme flip follows.
  void ScriptEditorWidget::fadePlaceholder() {
    QColor ink = QApplication::palette(edit).color(QPalette::PlaceholderText);
    ink.setAlphaF(ink.alphaF() * 0.5);
    QPalette pal = edit->palette();
    if (pal.color(QPalette::PlaceholderText) == ink) return;   // the change this set, echoed back
    pal.setColor(QPalette::PlaceholderText, ink);
    edit->setPalette(pal);
  }

  // Both hosts' keys, whichever way the host reads its hover: true = the editor took the key.
  bool ScriptEditorWidget::handleKey(QEvent* event) {
    if (event->type() != QEvent::KeyPress) return false;
    auto* ke = static_cast<QKeyEvent*>(event);
    if (style.codeKeys && ke->key() == Qt::Key_Tab && !(ke->modifiers() & Qt::ShiftModifier)) {
      edit->insertPlainText(QString(style.indent, QLatin1Char(' ')));
      return true;
    }
    if (style.runKeys && (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) &&
        (ke->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))) {
      emit runRequested();
      return true;
    }
    return false;
  }

  void ScriptEditorWidget::setFrameState(const char* key, bool on) {
    for (QFrame* f : {glow, wrap}) {
      if (!f) continue;
      f->setProperty(key, on);
      f->style()->unpolish(f);
      f->style()->polish(f);
    }
  }

}  // namespace stencil::gui
