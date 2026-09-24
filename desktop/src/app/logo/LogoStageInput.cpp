// The logo stage's input half: the hold and the typed words that open a showWord, and the lock that
// makes the stage the only thing the editor hears. Behaviour and painting are its siblings.
#include "LogoStage.hpp"
#include "textFocus.hpp"
#include "typedLetter.hpp"
#include "../../support/webcore/rules.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QToolButton>
#include <QWindow>

namespace stencil::gui {

  namespace {
    constexpr int PRESS_SLOP_PX = 10;   // browser ui/popover.js
    constexpr int WAY_MS = 30, WAY_TRIES = 40;   // a closing modal or fullscreen gets 1.2s to clear

    // The window's own keys: its body, or the modal dialog it has open.
    bool isHostKeyTarget(QWidget* host, QWidget* first) {
      QWidget* top = first->window();
      return top == host || (top == QApplication::activeModalWidget() && top->parentWidget() &&
                             top->parentWidget()->window() == host);
    }

    bool inputEvent(QEvent::Type t) {
      return t == QEvent::KeyPress || t == QEvent::KeyRelease || t == QEvent::ShortcutOverride ||
             t == QEvent::Shortcut || t == QEvent::MouseButtonPress || t == QEvent::MouseButtonRelease ||
             t == QEvent::MouseButtonDblClick || t == QEvent::Wheel || t == QEvent::ContextMenu ||
             t == QEvent::NativeGesture;
    }
  }  // namespace

  void LogoStage::mouseMoveEvent(QMouseEvent* e) {
    cursorPos = e->position();
    syncCursor();
  }

  // The mark is the one thing on the stage a press acts on, so it wears the hand — and a roaming
  // mark runs under a still pointer, so the frame re-asks (tick) as well as the move.
  void LogoStage::syncCursor() {
    const bool hot = onMark(cursorPos.toPoint());
    if (hot == handCursor) return;
    handCursor = hot;
    setCursor(hot ? Qt::PointingHandCursor : Qt::ArrowCursor);
  }

  void LogoStage::mousePressEvent(QMouseEvent* e) { pressed(e->position().toPoint()); }

  void LogoStage::mouseReleaseEvent(QMouseEvent*) { held = false; }

  void LogoStage::keyPressEvent(QKeyEvent* e) { stageKey(*e); }

  bool LogoStage::activateByName(const QString& name, bool clearWay) {
    using support::StageEffect;
    const support::StageShow* spec = support::showByName(name);
    if (!spec) return false;
    const QString& toast = support::logoStageConfig().toast;
    if (spec->effect == StageEffect::WEBCORE) {   // a skin toggle beside any show, never against one
      const bool on = hooks.webcore ? hooks.webcore() : false;
      if (hooks.toast) hooks.toast(on ? toast : support::webcoreConfig().offToast);
      return true;
    }
    if (open && name == showWord) return false;
    if (hooks.bareWindow && !hooks.bareWindow()) {
      if (!clearWay || !hooks.clearWay) return false;
      awaitBareWindow(name);
      return true;
    }
    if (spec->effect == StageEffect::PINK) {   // an edit on the canvas under any stage
      dismiss();
      if (hooks.pinkVibe) hooks.pinkVibe();
    } else {
      start(name);
    }
    if (hooks.toast) hooks.toast(toast);
    return true;
  }

  // A dialog's exec() is still unwinding when its reject() returns, so the show waits a turn.
  void LogoStage::awaitBareWindow(const QString& name) {
    if (!way) {
      way = new QTimer(this);
      way->setSingleShot(true);
      connect(way, &QTimer::timeout, this, [this] {
        if (!hooks.bareWindow || hooks.bareWindow()) {
          activateByName(std::exchange(pending, QString()));
        } else if (++wayTries < WAY_TRIES) {
          hooks.clearWay();
          way->start(WAY_MS);
        } else {
          pending.clear();
        }
      });
    }
    pending = name;
    wayTries = 0;
    hooks.clearWay();
    way->start(0);
  }

  // Escape ends the show; any other word typed at it opens its own.
  bool LogoStage::stageKey(const QKeyEvent& e) {
    if (e.key() == Qt::Key_Escape) { dismiss(); return true; }
    if (e.modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) return false;
    return typedKey(e);
  }

  // A printable key typed into the bare hostWindow, outside any text box, spells a showWord's name.
  // A letter more than typeGapMs after the last starts a new word (browser typedWords.js).
  bool LogoStage::typedKey(const QKeyEvent& e) {
    const QChar letter = support::typedLetter(e);
    if (letter.isNull()) return false;
    if (!sinceLetter.isValid() || sinceLetter.elapsed() > support::logoStageConfig().typeGapMs)
      typed.clear();
    sinceLetter.restart();
    typed += letter;
    const QStringList words = support::typedWords();
    int longest = 0;
    for (const QString& word : words) longest = std::max(longest, int(word.size()));
    if (typed.size() > longest) typed = typed.right(longest);
    for (int i = 0; i < words.size(); ++i) {
      if (!typed.endsWith(words.at(i))) continue;
      typed.clear();
      activateByName(support::logoStageConfig().shows.at(i).name, /*clearWay=*/true);
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
    if (t == QEvent::KeyPress) {
      stageKey(*static_cast<QKeyEvent*>(e));
      return true;
    }
    // A press that landed anywhere else is still the stage's: map it into stage space. A REAL
    // one arrives on the hostWindow HANDLE before any widget sees it, and the lock swallows it
    // there, so the widget cast must not be what decides whether the stage hears it.
    if (t == QEvent::MouseButtonPress) {
      const bool ours = w ? w->window() == hostWindow : (hostWindow && o == hostWindow->windowHandle());
      if (ours) pressed(mapFromGlobal(static_cast<QMouseEvent*>(e)->globalPosition().toPoint()));
      return true;
    }
    if (t == QEvent::MouseButtonRelease) held = false;
    return true;
  }

  bool LogoStage::eventFilter(QObject* o, QEvent* e) {
    if (open && lockEvent(o, e)) return true;
    const QEvent::Type t = e->type();
    // Nothing resizes the stage for it: it is a bare child of the hostWindow, in no layout.
    if (t == QEvent::Resize && o == hostWindow) relayout();
    if (o == logo && logo) {
      if (t == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(e);
        if (me->button() == Qt::LeftButton && me->modifiers() == Qt::NoModifier) {
          fired = false;
          pressAt = me->globalPosition().toPoint();
          hold->start(support::logoStageConfig().holdMs);
        }
      } else if (t == QEvent::MouseMove) {
        const QPoint at = static_cast<QMouseEvent*>(e)->globalPosition().toPoint();
        if (hold->isActive() && (at - pressAt).manhattanLength() > PRESS_SLOP_PX) hold->stop();
      } else if (t == QEvent::MouseButtonRelease || t == QEvent::Leave) {
        hold->stop();
        // The release after a hold must not reach clicked(), which would cycle the accent.
        if (fired && t == QEvent::MouseButtonRelease) {
          fired = false;
          logo->setDown(false);
          return true;
        }
      }
      return false;
    }
    // An unhandled key walks up the widget chain, and the app filter sees EVERY step of it, so
    // only the first delivery counts — the rest would spell the letter five times over.
    if (t == QEvent::KeyPress && !open) {
      QWidget* focus = QApplication::focusWidget();
      QWidget* first = focus ? focus : hostWindow;
      const auto& key = *static_cast<QKeyEvent*>(e);
      if (o == first && isHostKeyTarget(hostWindow, first) && !support::isTextEntry(focus) &&
          key.key() != Qt::Key_Escape && stageKey(key))
        return true;
    }
    return QWidget::eventFilter(o, e);
  }

}  // namespace stencil::gui
