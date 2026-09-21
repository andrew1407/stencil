#pragma once
// A caption beside a checkbox IS that box's label: clicking the words toggles it, the way
// `<label for=…>` does in the browser. QLabel has no clicked signal of its own, so the
// release is filtered. Used by every modalRow carrying a check, and by the Open Image
// dialog's wrapping caption rows.
#include <QAbstractButton>
#include <QEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QObject>
#include <QPointer>

namespace stencil::support {

  class ClickToToggle : public QObject {
   public:
    ClickToToggle(QAbstractButton* box, QObject* parent) : QObject(parent), box(box) {}

   protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
      if (ev->type() == QEvent::MouseButtonRelease && box && box->isEnabled()) {
        auto* me = static_cast<QMouseEvent*>(ev);
        auto* w = qobject_cast<QWidget*>(obj);
        // Inside the words only: a release that wandered off the label is a cancelled press.
        if (me->button() == Qt::LeftButton && w && w->rect().contains(me->position().toPoint()))
          box->click();   // click(), so the box's own signal chain runs unchanged
      }
      return QObject::eventFilter(obj, ev);
    }

   private:
    QPointer<QAbstractButton> box;
  };

  inline void captionToggles(QLabel* caption, QAbstractButton* box) {
    if (!caption || !box) return;
    caption->setCursor(Qt::PointingHandCursor);
    caption->installEventFilter(new ClickToToggle(box, caption));
  }

  // The same rule for a read-only field that stands in for its button: clicking the Open
  // Image dialog's path box opens the chooser, as the browser's whole file input does.
  inline void clickActivates(QWidget* field, QAbstractButton* button) {
    if (!field || !button) return;
    field->setCursor(Qt::PointingHandCursor);
    field->installEventFilter(new ClickToToggle(button, field));
  }

}  // namespace stencil::support
