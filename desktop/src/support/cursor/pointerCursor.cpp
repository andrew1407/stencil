#include "pointerCursor.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QMenu>
#include <QMouseEvent>

namespace stencil::gui {

  namespace {
    class DisabledCursorFilter : public QObject {
     public:
      using QObject::QObject;

     protected:
      bool eventFilter(QObject*, QEvent* e) override {
        const QEvent::Type t = e->type();
        if (t != QEvent::MouseMove && t != QEvent::Enter && t != QEvent::Leave &&
            t != QEvent::HoverMove)
          return false;
        QWidget* under = QApplication::widgetAt(QCursor::pos());
        const bool blocked = under && !under->isEnabled();
        if (blocked == showing) return false;
        showing = blocked;
        if (blocked) QApplication::setOverrideCursor(Qt::ForbiddenCursor);
        else QApplication::restoreOverrideCursor();
        return false;
      }

     private:
      bool showing = false;
    };

    // Set at polish, so a widget that chose its own cursor (WA_SetCursor) keeps it.
    class PointerCursorFilter : public QObject {
     public:
      using QObject::QObject;

     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        const QEvent::Type t = e->type();
        if (t == QEvent::Polish && o->isWidgetType()) {
          auto* w = static_cast<QWidget*>(o);
          if (!w->testAttribute(Qt::WA_SetCursor) && (qobject_cast<QAbstractButton*>(w) || qobject_cast<QComboBox*>(w)))
            w->setCursor(Qt::PointingHandCursor);
        } else if (t == QEvent::MouseMove) {
          // A menu's items are not widgets: the pointer follows the action under it (.ctx-item).
          if (auto* menu = qobject_cast<QMenu*>(o)) {
            const QAction* a = menu->actionAt(static_cast<QMouseEvent*>(e)->position().toPoint());
            const bool item = a && !a->isSeparator() && a->isEnabled();
            // The shape, not WA_SetCursor: unsetCursor() leaves that flag on a window, a menu included.
            if (item == (menu->cursor().shape() == Qt::PointingHandCursor)) return false;
            if (item) menu->setCursor(Qt::PointingHandCursor);
            else menu->unsetCursor();
          }
        }
        return false;
      }
    };
  }  // namespace

  void installPointerCursor(QApplication* app) {
    if (app) app->installEventFilter(new PointerCursorFilter(app));
  }

  void installDisabledCursor(QApplication* app) {
    if (app) app->installEventFilter(new DisabledCursorFilter(app));
  }
}
