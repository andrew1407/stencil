#include "comboAltPeek.hpp"
#include "SearchCombo.hpp"
#include "altPeek.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QCursor>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QTimer>

namespace stencil::support {

  namespace {
    // Seen on the Key_Alt events cocoa sends on flagsChanged: a fallback for a modifier query
    // that has not caught up yet. Another key or a press without Alt clears it.
    bool keyAltDown = false;

    template <typename Pick>
    QWidget* climb(QWidget* w, Pick pick) {
      for (; w; w = w->parentWidget()) {
        if (pick(w)) return w;
        if (w->isWindow()) break;
      }
      return nullptr;
    }

    QWidget* underCursor() { return QApplication::widgetAt(QCursor::pos()); }
    // A popup list is a top level, so its geometry is global.
    bool pointerOver(const QWidget* popup) { return popup && popup->geometry().contains(QCursor::pos()); }

    // The selector `w` belongs to, if any, within its own window.
    QComboBox* comboAt(QWidget* w) {
      return static_cast<QComboBox*>(climb(w, [](QWidget* x) { return qobject_cast<QComboBox*>(x); }));
    }

    // The list opens as a Qt::Popup that grabs the pointer, so a poll watches the cursor
    // (enter/leave of the list, Alt dropping, a glide onto another opener) while one is peeked.
    class ComboAltPeekFilter : public QObject {
     public:
      explicit ComboAltPeekFilter(QObject* parent) : QObject(parent) {
        poll.setInterval(80);
        connect(&poll, &QTimer::timeout, this, [this] { tick(); });
      }

     protected:
      bool eventFilter(QObject* obj, QEvent* ev) override {
        switch (ev->type()) {
          case QEvent::Enter:
            if (auto* c = qobject_cast<QComboBox*>(obj); c && altKeyHeld()) peek(c);
            break;
          case QEvent::KeyPress:
          case QEvent::KeyRelease: {
            auto* ke = static_cast<QKeyEvent*>(ev);
            if (ke->key() != Qt::Key_Alt) {
              if (ev->type() == QEvent::KeyPress) keyAltDown = ke->modifiers() & Qt::AltModifier;
              break;
            }
            if (ke->isAutoRepeat()) break;
            // One press climbs the parents and meets this filter at every step: act once.
            if (ev->type() == QEvent::KeyPress && ev == lastPress && ke->timestamp() == lastPressAt)
              break;
            if (ev->type() == QEvent::KeyPress) {
              lastPress = ev;
              lastPressAt = ke->timestamp();
            }
            keyAltDown = ev->type() == QEvent::KeyPress;
            // The pointer resting on the selector is the intent, so a focused text field does
            // not defer it; the key itself is never swallowed.
            if (keyAltDown) peekUnderCursor();
            else release();
            break;
          }
          case QEvent::MouseButtonPress:
            if (ev->spontaneous())
              keyAltDown = static_cast<QMouseEvent*>(ev)->modifiers() & Qt::AltModifier;
            break;
          // Alt+Tab switches away without delivering the keyup.
          case QEvent::ApplicationDeactivate:
            dropAlt();
            break;
          case QEvent::ApplicationStateChange:
            if (static_cast<QApplicationStateChangeEvent*>(ev)->applicationState() !=
                Qt::ApplicationActive)
              dropAlt();
            break;
          default:
            break;
        }
        return false;
      }

     private:
      AltPeekGesture* gestureFor(QComboBox* c) {
        if (AltPeekGesture* g = gestures.value(c)) return g;
        auto* g = new AltPeekGesture(
            {[c] { if (!comboPopup(c)) c->showPopup(); },
             [c] { c->hidePopup(); },
             [c] { return comboPopup(c) != nullptr; },
             [c] { return pointerOver(comboPopup(c)); }},
            c);
        gestures.insert(c, g);
        connect(c, &QObject::destroyed, this, [this, c] { gestures.remove(c); });
        return g;
      }

      void peek(QComboBox* c) {
        if (!c->isEnabled() || !c->isVisible()) return;
        // A list opened by click, or a menu, owns the pointer: nothing peeks over it.
        QWidget* owner = QApplication::activePopupWidget();
        if (owner && !(live && owner == comboPopup(live))) return;
        AltPeekGesture* g = gestureFor(c);
        g->altHover(c);
        if (g->mode() != AltPeekGesture::Mode::PEEK) return;
        live = c;
        inside = false;
        poll.start();
      }

      void peekUnderCursor() {
        if (QComboBox* c = comboAt(underCursor())) peek(c);
      }

      void release() {
        if (AltPeekGesture* g = live ? gestures.value(live) : nullptr) g->altRelease();
      }

      void dropAlt() {
        keyAltDown = false;
        release();
      }

      void tick() {
        AltPeekGesture* g = live ? gestures.value(live) : nullptr;
        if (g && !live->isVisible()) live->hidePopup();
        QWidget* popup = live ? comboPopup(live) : nullptr;
        if (!g || !popup || g->mode() == AltPeekGesture::Mode::NONE) {
          if (g) g->notifyClosed();
          live.clear();
          poll.stop();
          return;
        }
        const bool now = pointerOver(popup);
        if (now != inside) {
          inside = now;
          now ? g->boxEnter() : g->boxLeave();
        }
        if (g->mode() != AltPeekGesture::Mode::PEEK) return;
        if (!altKeyHeld()) return g->altRelease();
        if (!now) glide(popup);
      }

      void glide(QWidget* popup) {
        QWidget* w = underCursor();
        if (!w || w->window() == popup) return;
        QComboBox* c = comboAt(w);
        if (c && c != live) return peek(c);
        QWidget* icon =
            climb(w, [](QWidget* x) { return x->property(ALT_PEEK_TARGET_PROPERTY).toBool(); });
        // The grab kept the icon's own Enter away, so its owner's handle opens its peek.
        if (icon && icon->isEnabled()) glideFrom(nullptr, icon);
      }

      QHash<QComboBox*, AltPeekGesture*> gestures;
      QPointer<QComboBox> live;
      const QEvent* lastPress = nullptr;
      quint64 lastPressAt = 0;
      bool inside = false;
      QTimer poll;
    };
  }  // namespace

  QWidget* comboPopup(QComboBox* combo) {
    if (!combo) return nullptr;
    QWidget* p = nullptr;
    if (auto* s = dynamic_cast<gui::SearchComboBox*>(combo)) p = s->popupWindow();
    else if (combo->view()) p = combo->view()->window();
    return p && p->isVisible() && p != combo->window() ? p : nullptr;
  }

  bool altKeyHeld() {
    return keyAltDown || QGuiApplication::keyboardModifiers().testFlag(Qt::AltModifier) ||
           QGuiApplication::queryKeyboardModifiers().testFlag(Qt::AltModifier);
  }

  QWidget* popupOf(const QWidget* host) {
    QWidget* p = QApplication::activePopupWidget();
    QWidget* owner = p ? p->parentWidget() : nullptr;
    return host && owner && (owner == host || host->isAncestorOf(owner)) ? p : nullptr;
  }

  bool pointerInPopupOf(const QWidget* host) { return pointerOver(popupOf(host)); }

  // Re-installed on every call so it runs ahead of a window's filter installed since: that one
  // may swallow the Alt press this one tracks.
  void installComboAltPeek() {
    static QPointer<ComboAltPeekFilter> filter;
    if (!filter) filter = new ComboAltPeekFilter(qApp);
    qApp->removeEventFilter(filter);
    qApp->installEventFilter(filter);
  }

}  // namespace stencil::support
