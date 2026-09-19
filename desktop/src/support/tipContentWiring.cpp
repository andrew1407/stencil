// Keeping a control's tooltip composed as its enabled state or chord changes: the base text, the
// chord it borrows from an action, and the reason it shows while grey. One event filter per control.
#include "tipContentParts.hpp"
#include <QAction>
#include <QEvent>
#include <QKeySequence>
#include <QPointer>
#include <QRegularExpression>
#include <QWidget>

namespace stencil::gui {

  using tipdetail::keysHtml;

  namespace {

    // Or, for a plain-tooltip control, that tooltip stripped of "(combo)" and "— reason" (browser data-title).
    QString tipBaseOf(QObject* t) {
      const QVariant v = t->property(TIP_BASE_PROPERTY);
      if (v.isValid()) return v.toString();
      QString base = t->property(PLAIN_TIP_PROPERTY).toString();
      if (base.isEmpty()) {
        if (auto* a = qobject_cast<QAction*>(t)) base = a->toolTip();
        else if (auto* w = qobject_cast<QWidget*>(t)) base = w->toolTip();
      }
      if (base.trimmed().startsWith('<')) base.clear();   // someone's own HTML: no base to read
      static const QRegularExpression note("\\n[\\s\\S]*$");
      base.remove(note);
      static const QRegularExpression paren("\\s*\\(([^()]*)\\)\\s*$");
      const auto m = paren.match(base);
      if (m.hasMatch() && isKeyCombo(m.captured(1))) base = base.left(m.capturedStart()).trimmed();
      t->setProperty(TIP_BASE_PROPERTY, base);
      return base;
    }

    QString tipComboOf(QObject* t) {
      QAction* a = qobject_cast<QAction*>(t);
      if (!a) a = qobject_cast<QAction*>(t->property(TIP_HOTKEY_PROPERTY).value<QObject*>());
      return a ? a->shortcut().toString(QKeySequence::NativeText) : QString();
    }

    // A widget's enabled state has no signal; this rides its EnabledChange event.
    class TipStateWatch : public QObject {
     public:
      explicit TipStateWatch(QWidget* w) : QObject(w) {}
     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::EnabledChange) syncControlTip(o);
        return QObject::eventFilter(o, e);
      }
    };

    void wireControlTip(QObject* t) {
      static constexpr const char* WIRED = "stencilTipWired";
      if (t->property(WIRED).toBool()) return;
      t->setProperty(WIRED, true);
      if (auto* a = qobject_cast<QAction*>(t))
        QObject::connect(a, &QAction::changed, a, [a] { syncControlTip(a); });
      else if (auto* w = qobject_cast<QWidget*>(t))
        w->installEventFilter(new TipStateWatch(w));
    }

  }  // namespace

  void setTipBase(QObject* target, const QString& base) {
    if (!target) return;
    target->setProperty(TIP_BASE_PROPERTY, base);
    wireControlTip(target);
    syncControlTip(target);
  }

  void setTipReason(QObject* target, const QString& reason) {
    if (!target) return;
    tipBaseOf(target);   // pin the heading before the note can land on the tooltip
    target->setProperty(TIP_REASON_PROPERTY, reason);
    wireControlTip(target);
    syncControlTip(target);
  }

  void setTipHotkey(QWidget* target, QAction* hotkey) {
    if (!target) return;
    tipBaseOf(target);
    target->setProperty(TIP_HOTKEY_PROPERTY, QVariant::fromValue<QObject*>(hotkey));
    wireControlTip(target);
    if (hotkey) {
      QPointer<QWidget> w(target);
      QObject::connect(hotkey, &QAction::changed, target, [w] { if (w) syncControlTip(w); });
    }
    syncControlTip(target);
  }

  void syncControlTip(QObject* target) {
    if (!target) return;
    if (!target->property(TIP_BASE_PROPERTY).isValid()) return;
    const QString reason = target->property(TIP_REASON_PROPERTY).toString();
    QString tip;
    if (auto* a = qobject_cast<QAction*>(target)) {
      tip = composeControlTitle(tipBaseOf(a), tipComboOf(a), !a->isEnabled(), reason);
      a->setToolTip(tip);   // a no-op when unchanged, so the `changed` it emits cannot loop
      return;
    }
    auto* w = qobject_cast<QWidget*>(target);
    if (!w) return;
    tip = composeControlTitle(tipBaseOf(w), tipComboOf(w), !w->isEnabled(), reason);
    // The live tooltip may already be the RENDERED form of this text; compare against the plain.
    if (w->property(PLAIN_TIP_PROPERTY).toString() == tip || w->toolTip() == tip) return;
    w->setToolTip(tip);
  }
  QString comboKeycapsHtml(const QString& combo, const Palette& pal, bool mac, qreal scale) {
    return keysHtml(combo, pal, mac, scale);
  }

}  // namespace stencil::gui
