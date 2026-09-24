#pragma once
// Double-click a selector or a checkbox and it goes back to its default, applied as a pick
// (browser js/ui/control/dblReset.js). A control opts in with setResetDefault: a combo's item
// data (or text, or an int index), a check's bool.
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QTimer>
#include <QVariant>

namespace stencil::support {

  inline constexpr const char* RESET_DEFAULT_PROPERTY = "resetDefault";
  // Set by captionToggles: the check a caption stands for, so its double-click resets it too.
  inline constexpr const char* CAPTION_BOX_PROPERTY = "captionBox";

  inline void setResetDefault(QObject* o, const QVariant& v) { if (o) o->setProperty(RESET_DEFAULT_PROPERTY, v); }

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

  inline void installDblReset() {
    static QPointer<DblResetFilter> filter;
    if (!filter) qApp->installEventFilter(filter = new DblResetFilter(qApp));
  }

}  // namespace stencil::support
