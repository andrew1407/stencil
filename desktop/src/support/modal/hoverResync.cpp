#include "hoverResync.hpp"
#include "modalReveal.hpp"

#include <QApplication>
#include <QCursor>
#include <QDialog>
#include <QEnterEvent>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <QWindow>

namespace stencil::support {

  namespace {
    constexpr const char* FILTER_NAME = "stencilHoverResync";
    constexpr const char* PENDING_PROPERTY = "stencilHoverResyncPending";
    constexpr int BUTTON_WAIT_MS = 50, BUTTON_WAIT_TRIES = 40;   // a press that closed it: 2s to lift

    // Through a different shape first: Qt skips a cursor it believes the window already wears,
    // while the screen may still show the closed window's.
    void reapplyCursor(QWindow* handle, const QCursor& cursor) {
      handle->setCursor(cursor.shape() == Qt::ArrowCursor ? Qt::IBeamCursor : Qt::ArrowCursor);
      handle->setCursor(cursor);
    }

    void resyncSoon(QWidget* window, int tries) {
      QPointer<QWidget> guard(window);
      QTimer::singleShot(tries ? BUTTON_WAIT_MS : 0, window, [guard, tries] {
        if (!guard) return;
        if (QGuiApplication::mouseButtons() != Qt::NoButton) {
          if (tries < BUTTON_WAIT_TRIES) resyncSoon(guard, tries + 1);
          return;
        }
        resyncHover(guard);
      });
    }

    class HoverResyncFilter : public QObject {
     public:
      explicit HoverResyncFilter(QObject* parent) : QObject(parent) {
        setObjectName(QString::fromLatin1(FILTER_NAME));
      }

     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        const QEvent::Type t = e->type();
        if (t != QEvent::Hide && t != QEvent::WindowActivate) return QObject::eventFilter(o, e);
        auto* w = qobject_cast<QWidget*>(o);
        if (!w || !w->isWindow()) return QObject::eventFilter(o, e);
        if (t == QEvent::WindowActivate) {
          // macOS hands the window its key status a beat after the modal session ends.
          if (w->property(PENDING_PROPERTY).toBool()) {
            w->setProperty(PENDING_PROPERTY, false);
            resyncSoon(w, 0);
          }
          return QObject::eventFilter(o, e);
        }
        auto* dlg = qobject_cast<QDialog*>(w);
        if (!dlg || e->spontaneous() || dlg->windowModality() == Qt::NonModal) return QObject::eventFilter(o, e);
        QWidget* host = dialogHost(dlg);
        if (!host) host = QApplication::topLevelAt(QCursor::pos());
        if (!host || host == dlg) return QObject::eventFilter(o, e);
        if (!host->isActiveWindow()) host->setProperty(PENDING_PROPERTY, true);
        resyncSoon(host, 0);
        return QObject::eventFilter(o, e);
      }
    };
  }  // namespace

  void resyncHover(QWidget* window) {
    QWidget* top = window ? window->window() : nullptr;
    if (!top || !top->isVisible() || QApplication::activeModalWidget() ||
        QApplication::activePopupWidget() || QGuiApplication::overrideCursor())
      return;
    QWindow* handle = top->windowHandle();
    const QPoint global = QCursor::pos();
    QWidget* under = QApplication::widgetAt(global);
    if (!handle || !under || under->window() != top) return;
    if (!under->underMouse()) {
      // The window's own Leave then Enter: Qt leaves what it last recorded and enters the widget
      // really under the pointer, whose cursor it applies on the way in.
      QEvent leave(QEvent::Leave);
      QCoreApplication::sendEvent(handle, &leave);
      const QPointF local = handle->mapFromGlobal(QPointF(global));
      QEnterEvent enter(local, local, QPointF(global));
      QCoreApplication::sendEvent(handle, &enter);
    }
    QWidget* native = under->internalWinId() ? under : under->nativeParentWidget();
    if (QWindow* shown = native ? native->windowHandle() : nullptr) reapplyCursor(shown, under->cursor());
  }

  void installHoverResync() {
    QCoreApplication* app = QCoreApplication::instance();
    if (!app || app->findChild<QObject*>(QString::fromLatin1(FILTER_NAME), Qt::FindDirectChildrenOnly))
      return;
    app->installEventFilter(new HoverResyncFilter(app));
  }

}  // namespace stencil::support
