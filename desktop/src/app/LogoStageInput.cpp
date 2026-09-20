// The logo stage's input half: the hold that opens a show, the typed words, and the lock that
// makes the stage the only thing the editor hears. Behaviour and painting are its siblings.
#include "LogoStage.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QWindow>

namespace stencil::gui {

  namespace {
    constexpr int PRESS_SLOP_PX = 10;   // browser ui/popover.js

    bool isTextEntry(QObject* o) {
      return qobject_cast<QLineEdit*>(o) || qobject_cast<QPlainTextEdit*>(o) ||
             qobject_cast<QTextEdit*>(o);
    }
    bool inputEvent(QEvent::Type t) {
      return t == QEvent::KeyPress || t == QEvent::KeyRelease || t == QEvent::ShortcutOverride ||
             t == QEvent::Shortcut || t == QEvent::MouseButtonPress || t == QEvent::MouseButtonRelease ||
             t == QEvent::MouseButtonDblClick || t == QEvent::Wheel || t == QEvent::ContextMenu ||
             t == QEvent::NativeGesture;
    }
  }  // namespace

  void LogoStage::mouseMoveEvent(QMouseEvent* e) {
    cursor_ = e->position();
    syncCursor();
  }

  // The mark is the one thing on the stage a press acts on, so it wears the hand — and a roaming
  // mark runs under a still pointer, so the frame re-asks (tick) as well as the move.
  void LogoStage::syncCursor() {
    const bool hot = onMark(cursor_.toPoint());
    if (hot == handCursor_) return;
    handCursor_ = hot;
    setCursor(hot ? Qt::PointingHandCursor : Qt::ArrowCursor);
  }

  void LogoStage::mousePressEvent(QMouseEvent* e) { pressed(e->position().toPoint()); }

  void LogoStage::mouseReleaseEvent(QMouseEvent*) { held_ = false; }

  void LogoStage::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) dismiss();
  }

  // A printable key typed into the bare window, outside any text box, spells a show's name.
  bool LogoStage::typedKey(const QString& text) {
    if (text.size() != 1 || !text.at(0).isPrint()) return false;
    typed_ += text.toLower();
    const QStringList words = support::typedWords();
    int longest = 0;
    for (const QString& word : words) longest = std::max(longest, int(word.size()));
    if (typed_.size() > longest) typed_ = typed_.right(longest);
    for (int i = 0; i < words.size(); ++i) {
      if (!typed_.endsWith(words.at(i))) continue;
      typed_.clear();
      activateByName(support::logoStageConfig().shows.at(i).name);
      return true;
    }
    return false;
  }

  // While a stage is up it is the only thing that hears anything; Escape is the way out.
  bool LogoStage::lockEvent(QObject* o, QEvent* e) {
    const QEvent::Type t = e->type();
    if (!inputEvent(t)) return false;
    auto* w = qobject_cast<QWidget*>(o);
    if (w == this) return false;
    // An UNaccepted override lets the key fire an editor shortcut instead of arriving as a
    // press, so every key is accepted here and swallowed as a press below.
    if (t == QEvent::ShortcutOverride) {
      e->accept();
      return true;
    }
    if (t == QEvent::KeyPress && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
      dismiss();
      return true;
    }
    // A press that landed anywhere else is still the stage's: map it into stage space. A REAL
    // one arrives on the window HANDLE before any widget sees it, and the lock swallows it
    // there, so the widget cast must not be what decides whether the stage hears it.
    if (t == QEvent::MouseButtonPress) {
      const bool ours = w ? w->window() == window_ : (window_ && o == window_->windowHandle());
      if (ours) pressed(mapFromGlobal(static_cast<QMouseEvent*>(e)->globalPosition().toPoint()));
      return true;
    }
    if (t == QEvent::MouseButtonRelease) held_ = false;
    return true;
  }

  bool LogoStage::eventFilter(QObject* o, QEvent* e) {
    if (open_ && lockEvent(o, e)) return true;
    const QEvent::Type t = e->type();
    // Nothing resizes the stage for it: it is a bare child of the window, in no layout.
    if (t == QEvent::Resize && o == window_) relayout();
    if (o == logo_ && logo_) {
      if (t == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(e);
        if (me->button() == Qt::LeftButton && me->modifiers() == Qt::NoModifier) {
          fired_ = false;
          pressAt_ = me->globalPosition().toPoint();
          hold_->start(support::logoStageConfig().holdMs);
        }
      } else if (t == QEvent::MouseMove) {
        const QPoint at = static_cast<QMouseEvent*>(e)->globalPosition().toPoint();
        if (hold_->isActive() && (at - pressAt_).manhattanLength() > PRESS_SLOP_PX) hold_->stop();
      } else if (t == QEvent::MouseButtonRelease || t == QEvent::Leave) {
        hold_->stop();
        // The release after a hold must not reach clicked(), which would cycle the accent.
        if (fired_ && t == QEvent::MouseButtonRelease) {
          fired_ = false;
          logo_->setDown(false);
          return true;
        }
      }
      return false;
    }
    // An unhandled key walks up the widget chain, and the app filter sees EVERY step of it, so
    // only the first delivery counts — the rest would spell the letter five times over.
    if (t == QEvent::KeyPress && !open_) {
      QWidget* focus = QApplication::focusWidget();
      QWidget* first = focus ? focus : window_;
      if (o == first && first->window() == window_ && !isTextEntry(focus) &&
          !(static_cast<QKeyEvent*>(e)->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
          typedKey(static_cast<QKeyEvent*>(e)->text()))
        return true;
    }
    return QWidget::eventFilter(o, e);
  }

}  // namespace stencil::gui
