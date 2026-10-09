#pragma once
// Double-click a selector or a checkbox and it goes back to its default, applied as a pick
// (browser js/ui/control/dblReset.js). A control opts in with setResetDefault: a combo's item
// data (or text, or an int index), a check's bool, a spin's number, a field's text; a colour
// chip's reset is its own function. A logo dropped on any of them resets it the same way.
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QMenu>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPointer>
#include <QSpinBox>
#include <QTimer>
#include <QVariant>

#include <functional>
#include <memory>
#include <utility>

#include "../drag/iconDrag.hpp"
#include "../uiTimings.hpp"

namespace stencil::support {

  inline constexpr const char* RESET_DEFAULT_PROPERTY = "resetDefault";
  // Set by captionToggles: the check a caption stands for, so its double-click resets it too.
  inline constexpr const char* CAPTION_BOX_PROPERTY = "captionBox";

  inline constexpr const char* RESET_HOOK_PROPERTY = "resetHook";

  inline void setResetDefault(QObject* o, const QVariant& v) { if (o) o->setProperty(RESET_DEFAULT_PROPERTY, v); }

  // A control that puts its default back itself (a colour chip); the hook lives as long as it does.
  class ResetHook : public QObject {
   public:
    ResetHook(QObject* owner, std::function<void()> reset) : QObject(owner), reset(std::move(reset)) {}
    std::function<void()> reset;
  };
  inline void setResetHook(QObject* o, std::function<void()> reset) {
    if (o) o->setProperty(RESET_HOOK_PROPERTY, QVariant::fromValue<void*>(new ResetHook(o, std::move(reset))));
  }
  inline ResetHook* resetHookOf(const QObject* o) {
    return o ? static_cast<ResetHook*>(o->property(RESET_HOOK_PROPERTY).value<void*>()) : nullptr;
  }

  inline bool resetCombo(QComboBox* c) {
    const QVariant v = c->property(RESET_DEFAULT_PROPERTY);
    if (!v.isValid() || !c->isEnabled()) return false;
    int i = v.typeId() == QMetaType::Int ? v.toInt() : c->findData(v);
    if (i < 0) i = c->findText(v.toString());
    if (i < 0 || i == c->currentIndex()) return false;
    c->hidePopup();
    c->setCurrentIndex(i);
    emit c->activated(i);   // the route a pick takes: some rows apply on activated alone
    emit c->textActivated(c->itemText(i));
    return true;
  }

  inline bool resetCheck(QAbstractButton* b) {
    const QVariant v = b->property(RESET_DEFAULT_PROPERTY);
    if (!v.isValid() || !b->isEnabled() || b->isChecked() == v.toBool()) return false;
    b->click();   // click(), so the box's own signal chain runs unchanged
    return true;
  }

  template <typename Spin, typename Value>
  bool resetSpin(Spin* s, Value v) {
    if (!s->isEnabled() || s->value() == v) return false;
    s->setValue(v);   // valueChanged is the route a typed value takes
    return true;
  }

  inline bool resetField(QLineEdit* e) {
    const QVariant v = e->property(RESET_DEFAULT_PROPERTY);
    if (!v.isValid() || !e->isEnabled() || e->text() == v.toString()) return false;
    e->setText(v.toString());
    return true;
  }

  // The control a logo dropped on `at` resets: the nearest one with a default, `at` or above it.
  inline QWidget* dropResetTarget(QWidget* at) {
    for (QWidget* w = at; w; w = w->parentWidget()) {
      if (!w->isEnabled()) return nullptr;
      if (resetHookOf(w) || w->property(RESET_DEFAULT_PROPERTY).isValid()) return w;
      if (auto* box = qobject_cast<QAbstractButton*>(w->property(CAPTION_BOX_PROPERTY).value<QObject*>())) return box;
      if (w->isWindow()) return nullptr;
    }
    return nullptr;
  }

  // Puts `w` back to its default through its own change path; false when it already was.
  inline bool resetToDefault(QWidget* w) {
    if (!w || !w->isEnabled()) return false;
    if (ResetHook* hook = resetHookOf(w)) { hook->reset(); return true; }
    const QVariant v = w->property(RESET_DEFAULT_PROPERTY);
    if (auto* c = qobject_cast<QComboBox*>(w)) return resetCombo(c);
    if (auto* b = qobject_cast<QAbstractButton*>(w)) return resetCheck(b);
    if (auto* s = qobject_cast<QSpinBox*>(w)) return v.isValid() && resetSpin(s, v.toInt());
    if (auto* s = qobject_cast<QDoubleSpinBox*>(w)) return v.isValid() && resetSpin(s, v.toDouble());
    if (auto* e = qobject_cast<QLineEdit*>(w)) return resetField(e);
    return false;
  }

  // A checkable menu row (a stay-open menu keeps it up across both clicks).
  inline bool resetAction(QAction* a) {
    const QVariant v = a->property(RESET_DEFAULT_PROPERTY);
    if (!v.isValid() || !a->isEnabled() || !a->isCheckable() || a->isChecked() == v.toBool()) return false;
    a->trigger();
    return true;
  }

  // A combo opens on the press, so two presses on it inside the double-click interval are read as
  // one; a check toggles on each click, so its reset waits for the release after the double-click.
  class DblResetFilter : public QObject {
   public:
    using QObject::QObject;

   protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
      const QEvent::Type t = ev->type();
      if (t == QEvent::MouseButtonPress) {
        QComboBox* c = comboOf(obj);
        if (!c) return false;
        const bool second = c == lastCombo && sinceCombo.isValid() &&
                            sinceCombo.elapsed() < QApplication::doubleClickInterval();
        lastCombo = c;
        sinceCombo.start();
        if (second && resetCombo(c)) { lastCombo.clear(); return true; }
        return false;
      }
      // An ignored double-click climbs to the parents: only a hit is recorded, never a miss.
      if (t == QEvent::MouseButtonDblClick && !pendingCheck && !pendingAction) {
        if (auto* m = qobject_cast<QMenu*>(obj))
          pendingAction = m->actionAt(static_cast<QMouseEvent*>(ev)->position().toPoint());
        else if (auto* b = qobject_cast<QCheckBox*>(obj)) pendingCheck = b;
        else if (auto* cap = qobject_cast<QWidget*>(obj))
          pendingCheck = qobject_cast<QAbstractButton*>(cap->property(CAPTION_BOX_PROPERTY).value<QObject*>());
        return false;
      }
      if (t == QEvent::MouseButtonRelease && pendingAction) {
        QPointer<QAction> a = pendingAction;
        pendingAction.clear();
        QTimer::singleShot(0, a, [a] { if (a) resetAction(a); });
      }
      if (t == QEvent::MouseButtonRelease && pendingCheck) {
        QPointer<QAbstractButton> b = pendingCheck;
        pendingCheck.clear();
        QTimer::singleShot(0, b, [b] { if (b) resetCheck(b); });
      }
      return false;
    }

   private:
    static QComboBox* comboOf(QObject* obj) {
      for (QObject* o = obj; o; o = o->parent()) {
        if (auto* c = qobject_cast<QComboBox*>(o)) return c->property(RESET_DEFAULT_PROPERTY).isValid() ? c : nullptr;
        if (auto* w = qobject_cast<QWidget*>(o); w && w->isWindow()) return nullptr;
      }
      return nullptr;
    }
    QPointer<QComboBox> lastCombo;
    QElapsedTimer sinceCombo;
    QPointer<QAbstractButton> pendingCheck;
    QPointer<QAction> pendingAction;
  };

  // A colour chip opens a modal picker on its release, so a double-click's second press would land
  // outside it: the open waits one double-click interval (POPOVER.doubleClickMs), a second click resets.
  inline void wireColorChip(QAbstractButton* chip, std::function<void()> open, std::function<void()> reset) {
    setResetHook(chip, reset);
    auto* wait = new QTimer(chip);
    wait->setSingleShot(true);
    wait->setInterval(uiTimings().doubleClickMs);
    auto drags = std::make_shared<unsigned>(0);   // a second press that dragged the chip away picks nothing
    QObject::connect(wait, &QTimer::timeout, chip, [drags, open = std::move(open)] {
      if (*drags == dragsStarted()) open();
    });
    QObject::connect(chip, &QAbstractButton::clicked, chip, [wait, drags, reset = std::move(reset)] {
      if (!wait->isActive()) {
        *drags = dragsStarted();
        wait->start();
        return;
      }
      wait->stop();
      reset();
    });
  }

  inline void installDblReset() {
    static QPointer<DblResetFilter> filter;
    if (!filter) qApp->installEventFilter(filter = new DblResetFilter(qApp));
  }

}  // namespace stencil::support
